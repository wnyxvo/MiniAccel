# AMD XDNA Driver 源码研究

## 1. 研究定位

AMD XDNA 是研究现代 PCIe NPU Driver 的重要对象，重点关注 Client、Hardware Context、GEM Buffer、Job 提交、并发窗口、Firmware Mailbox，以及 KMQ/UMQ 两种队列管理方式。

仓库包含 Linux DRM_ACCEL Kernel Driver 和 XRT SHIM 用户态适配层。固定版本和源码入口见 [SOURCE_INDEX.md](../SOURCE_INDEX.md)。

## 2. 核心模块

| 文件 | 关键符号/内容 | 职责 |
|---|---|---|
| `amdxdna_pci_drv.c` | `amdxdna_probe()` | PCI 设备初始化 |
| `amdxdna_drm.c` | `amdxdna_drm_open()` / close | Client 生命周期 |
| `amdxdna_ctx.c` | create/destroy HW Context IOCTL | Hardware Context 生命周期 |
| `amdxdna_ctx.c` | `amdxdna_cmd_submit()` / `amdxdna_cmd_wait()` | 通用命令提交与等待 |
| `amdxdna_gem.c` | GEM 创建与设备地址处理 | Buffer 管理 |
| `aie2_ctx.c` | `aie2_hwctx_init()`、`aie2_cmd_submit()`、`aie2_job_run()` | AIE2 Context 和任务执行 |
| `aie2_message.c` | `aie2_create_context()`、`aie2_execbuf()` | Firmware Context / 执行消息 |
| `src/shim/hwq.cpp` | `submit_command()` / `wait_command()` | 用户态 Queue 提交和等待 |

## 3. 核心对象关系

```text
Device
  |
  +-- Client A
  |     +-- HW Context A1
  |     +-- HW Context A2
  |     +-- GEM Buffer Objects
  |
  +-- Client B
        +-- HW Context B1

HW Context
  +-- Firmware Context ID
  +-- QoS / AIE Resource State
  +-- Command / Wait State
  +-- Job Semaphore
  +-- Sequence Number

Sched Job
  +-- Hardware Context reference
  +-- Command Buffer
  +-- Sequence
  +-- Fence / Completion State
```

`amdxdna_client` 是打开设备的客户端状态；`amdxdna_hwctx` 是硬件执行上下文；`amdxdna_sched_job` 是单次提交任务。Client 与 Hardware Context 是一对多关系。

## 4. Client 与 Context 生命周期

Client 使用 XArray 管理 Hardware Context，并通过 SRCU 等机制处理并发查找和销毁。其设计要点是：资源释放不仅要考虑引用计数，也要考虑并发读者是否仍可能访问对象。

创建 Hardware Context 的通用入口为 `amdxdna_drm_create_hwctx_ioctl()`。该路径解析用户配置、创建 Context，并通过设备操作表调用代际相关的 `hwctx_init()`。AIE2 的具体初始化由 `aie2_hwctx_init()` 完成。

这体现了通用 DRM 层与设备代际实现分离的思路：

```text
Generic Driver
      |
      v
Hardware Backend Ops
      |
      +-- init_context()
      +-- submit_command()
      +-- wait_command()
      +-- destroy_context()
```

## 5. AIE2 KMQ 提交与完成链路

```text
XRT SHIM hwq::submit_command()
      |
      v
hwq::issue_command()
      |
      v
DRM_IOCTL_AMDXDNA_EXEC_CMD
      |
      v
amdxdna_drm_submit_cmd_ioctl()
      |
      v
amdxdna_cmd_submit()
      |  查找 Command Buffer / 参数 Buffer / HW Context
      |  构建 Job
      v
aie2_cmd_submit()
      |  获取 job_sem 执行额度
      |  分配 Context 内 Sequence
      |  建立 Fence / Sync 状态
      v
aie2_job_run()
      |
      v
aie2_execbuf()
      |
      v
Firmware Mailbox
      |
      v
Response Handler
      |
      v
aie2_sched_notify()
      |
      v
Fence / Syncobj completion
      |
      v
Userspace wait
```

注意：AIE2 的这条执行路径不能简单描述为 Ethos-U 那种 DRM GPU Scheduler 流程。AIE2 提交路径使用 Context 级 `job_sem` 控制并发、分配序号并进入设备代际执行路径，之后通过 Mailbox/Firmware 响应驱动完成状态更新。

## 6. 关键机制

### 6.1 Job Semaphore：有界 In-flight 窗口

提交时获取 `job_sem`，任务完成时归还额度；Context 初始化时设置信号量容量。它限制单个 Context 的在途任务数量，提供背压和资源上限，但本身不是多 Context 公平调度算法。

MiniAccel 可以借鉴为 Context 级 `max_inflight`。

### 6.2 Sequence Number

任务序号在 Context 内分配。序号不能脱离 Context 身份单独作为全局任务身份。MiniAccel 可采用：

```cpp
struct TaskId {
    uint64_t context_id;
    uint64_t sequence;
    uint32_t generation;
};
```

其中 generation 用于避免 Context 销毁重建后旧任务与新 Context 的序号冲突。

### 6.3 Fence Chain 与 Syncobj

XDNA 将任务序号与完成 Fence 关联，使用户态能够等待特定任务进度，而不是依赖一个全局状态变量。MiniAccel MVP 可先实现 Task Token、Completion Record 和等待队列；DRM Syncobj 可作为后续增强研究。

### 6.4 Firmware Mailbox

`aie2_execbuf()` 组装执行请求并通过 Mailbox 发送。请求消息中的关联句柄和通知回调将异步 Firmware 响应与原始 Job 对应起来。MiniAccel 没有 XDNA Firmware，不应复制其协议；可借鉴的是异步请求与 Job 的关联、响应处理和状态更新机制。

## 7. KMQ 与 UMQ

| 维度 | KMQ | UMQ |
|---|---|---|
| 队列管理主体 | Kernel Driver | User-space SHIM |
| 典型提交 | IOCTL 进入内核 | 用户态更新 Ring，再通知设备 |
| 关键机制 | Kernel Job、并发额度、Fence | Queue Buffer、Packet Ring、Doorbell |
| 主要收益 | 内核集中校验和管理 | 减少逐任务进入内核的开销 |
| 复杂度 | 相对较低 | 更高，涉及映射、内存序和队列协议 |

UMQ 路径涉及 Queue Buffer、Ring Header、write index、Doorbell Offset 和 mmap 映射。其基本过程包括分配队列 Buffer、初始化 Packet Ring、映射 Doorbell、填充 Packet、发布 write index 并通知设备。写入队列内容、发布索引和 Doorbell 通知之间必须有正确的内存可见性顺序。

MiniAccel MVP 建议先完成 KMQ 风格的 IOCTL 提交闭环；UMQ 留作后续低开销提交优化，不作为首期功能。

## 8. 对 MiniAccel 的映射

| AMD XDNA 机制 | MiniAccel 对应 | 研究建议 |
|---|---|---|
| `amdxdna_client` | ClientSession | ADAPT：每个打开文件独立管理资源 |
| `amdxdna_hwctx` | Driver Context | ADOPT：一个 Client 管理多个 Context |
| `amdxdna_sched_job` | Kernel Job | ADOPT：Job 显式持有 Context/Buffer 引用 |
| `job_sem` | Context In-flight Limit | ADAPT：有界准入和背压 |
| Context Sequence | Task Token | ADAPT：Context ID + Generation + Sequence |
| Fence / Syncobj | Completion Record / Runtime Event | ADAPT：MVP 使用轻量完成机制 |
| Firmware Mailbox | Backend completion callback | ADAPT：借鉴异步关联，不复制协议 |
| UMQ Ring / Doorbell | User Managed Queue | DEFER：MVP 不实现 |
| XArray + SRCU | Context lookup/lifetime | DEFER：首期使用锁与引用计数 |

## 9. 开发检查点

- Runtime Context 与 Driver Context 的关系必须明确。
- Client 可以拥有多个 Context，资源查找必须检查 Client 归属。
- Context 销毁时阻止新任务进入，并处理未完成 Job。
- In-flight 额度必须在所有终态路径中准确归还。
- Task Token 必须能区分 Context 和 Context generation。
- Workqueue completion 与真实 Firmware response 必须明确区分。

## 10. 建议阅读顺序

1. `amdxdna_drm.c`：Client 创建和销毁。
2. `amdxdna_ctx.c`：Context 创建、提交、等待。
3. `amdxdna_gem.c`：Buffer 管理。
4. `aie2_ctx.c`：Context 初始化、Job Semaphore、Sequence 和执行。
5. `aie2_message.c`：Mailbox 请求与响应。
6. `src/shim/hwq.cpp`：用户态 KMQ 提交。
7. `src/shim/umq/`：再研究 UMQ Ring 和 Doorbell。
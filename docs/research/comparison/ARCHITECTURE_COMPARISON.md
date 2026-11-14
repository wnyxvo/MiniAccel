# Ethos-U、AMD XDNA 与 XRT 架构横向对照

## 1. 统一观察模型

```text
Application / User API
        |
        v
Runtime / Device Abstraction
        |
        v
User-space Driver / Backend / SHIM
        |
        v
Linux Kernel Driver
        |
        v
Hardware / Firmware / Virtual Accelerator
```

三个项目覆盖链路的范围不同：
- Ethos-U：重点覆盖 Linux Kernel Driver 的 Buffer、Job、Scheduler、Fence 和 IRQ。
- AMD XDNA：覆盖 Linux NPU Driver、SHIM、Hardware Context、KMQ/UMQ 和 Firmware Mailbox。
- XRT：重点覆盖用户态 Runtime API、SHIM 抽象、Buffer 和 Hardware Queue；仓库还包含 XOCL FPGA Driver/KDS 参考实现。

## 2. 核心对象对照

| 逻辑对象 | Ethos-U | AMD XDNA | XRT |
|---|---|---|---|
| Device | `ethosu_device` / DRM Device | AMD XDNA Device | `xrt::device` / Core Device |
| Client | `ethosu_file_priv` | `amdxdna_client` | Runtime/SHIM 设备会话与 DRM Client |
| Context | File Scheduler Entity 及相关状态 | `amdxdna_hwctx` | `hw_context_impl` / `hwctx_handle` |
| Buffer | GEM BO | GEM BO | `bo_impl` 及具体 Buffer 实现 |
| Task/Job | `ethosu_job` | `amdxdna_sched_job` | `kernel_command` / Command |
| Queue | DRM Scheduler Entity/Queue | KMQ 或 UMQ | `hw_queue_impl`、QDS/KDS |
| Completion | DMA Fence + IRQ | Fence/Syncobj + Firmware Response | 平台 Wait/Poll 与 Runtime Command Manager |

该表是概念映射，不表示对象语义完全相同。例如 Runtime Event 不是 DMA Fence，XRT Command 也不必然等于 Kernel Job。

## 3. 任务提交链路

### 3.1 Ethos-U

```text
ioctl(SUBMIT)
 -> ethosu_ioctl_submit()
 -> ethosu_ioctl_submit_job()
 -> ethosu_job_push()
 -> drm_sched_entity_push_job()
 -> DRM Scheduler
 -> ethosu_job_run()
 -> ethosu_job_hw_submit()
 -> Hardware IRQ
 -> dma_fence_signal()
 -> Job cleanup
```

特点：Job 进入 DRM Scheduler，真正执行由 Scheduler 调用 Job Run；完成由硬件 IRQ 驱动。

### 3.2 AMD XDNA AIE2 KMQ

```text
XRT SHIM submit
 -> EXEC_CMD IOCTL
 -> amdxdna_cmd_submit()
 -> aie2_cmd_submit()
 -> job_sem / Sequence / Fence
 -> aie2_job_run()
 -> aie2_execbuf()
 -> Firmware Mailbox
 -> Response Handler
 -> Completion
```

特点：AIE2 路径通过 Context 级任务额度和 Firmware 消息管理执行，不应简单描述成与 Ethos-U 相同的 DRM Scheduler 流程。

### 3.3 XRT Runtime

```text
xrt::run::start()
 -> kernel_command::run()
 -> managed/unmanaged queue start
 -> hw_queue_impl
 -> qds_device / kds_device
 -> Platform SHIM
 -> platform-specific driver path
```

特点：XRT 在用户态将 Command 和执行队列抽象起来，平台 SHIM 负责连接具体设备。提交和完成机制依赖平台后端。

## 4. 调度与队列机制差异

| 机制 | 解决的问题 | 不能误解为 |
|---|---|---|
| DRM Scheduler | Kernel Job 排队、调度和执行回调 | Runtime Stream 的完整语义 |
| XDNA `job_sem` | Context 级最大在途任务数/提交额度 | 多 Context 公平调度算法 |
| XRT Hardware Queue | Runtime 对平台执行队列的统一抽象 | 某一种固定 Kernel Scheduler |
| XRT Command Manager | 用户态 Managed Execution 与完成管理 | 硬件任务调度器 |
| XDNA UMQ | 用户态维护 Ring 并通知设备 | 无需同步/校验的普通内存队列 |

KMQ 与 UMQ 是不同的队列管理策略。KMQ 由内核处理任务提交；UMQ 由用户态维护 Ring 并通过 Doorbell 通知设备。UMQ 可以减少逐任务进入内核的成本，但增加共享队列、内存序和映射协议复杂度。

## 5. 资源生命周期共同原则

三个项目共同体现：异步任务执行期间，任务所访问的 Buffer、Context 和关联对象必须保持有效。

- Ethos-U：Job 持有 GEM BO 和 Fence 等资源引用，完成后进入清理路径。
- AMD XDNA：Job 关联 Hardware Context、Command Buffer、Sequence 和同步状态；Context 查找/销毁还要处理并发访问。
- XRT：Runtime 对象通过共享所有权管理生命周期，Command 执行期间需要保持关联 Buffer 有效。

MiniAccel 必须明确 Application Handle、Runtime Object、Backend Handle 和 Kernel Object 的关系，不能把用户态对象地址直接作为内核资源身份。

## 6. 三个项目的互补关系

| 项目 | 最适合回答的问题 | MiniAccel 研究优先级 |
|---|---|---|
| Ethos-U | Kernel Driver 如何创建 Job、引用 Buffer、调度执行并通过 IRQ/Fence 完成？ | 第一 |
| AMD XDNA | NPU Driver 如何管理 Client、多个 Hardware Context、在途任务和 Firmware 执行？ | 第二 |
| XRT | Runtime 如何统一 Device/Context/Buffer/Queue，并通过 SHIM 对接不同平台？ | 第三 |

## 7. 研究边界与待核实项

1. 函数和数据结构可能随上游提交变化，复核时以 `SOURCE_INDEX.md` 的 SHA 为准。
2. XRT 的 XOCL KDS 与 AMD XDNA AIE2 KMQ 属于不同设备路径，不能混为一谈。
3. 本文总结的是软件栈机制，不代表 MiniAccel 必须采用 DRM_ACCEL、GEM、DRM Scheduler 或 Firmware Mailbox。
4. 表中的函数名是源码阅读入口；实际调用顺序应在固定版本源码中逐项复核。
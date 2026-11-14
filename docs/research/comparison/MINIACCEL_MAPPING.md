# 参考项目到 MiniAccel 的设计映射

## 1. 目的与标签

本文把 Ethos-U、AMD XDNA 和 XRT 的研究结果转化为 MiniAccel 的候选设计决策。本文是 Spec 编写输入，不是 MiniAccel 的权威规范；正式决定必须写入 `docs/specs/`。

| 标签 | 含义 |
|---|---|
| ADOPT | 机制与 MiniAccel 目标一致，可采用其核心思想 |
| ADAPT | 借鉴机制，但需按 MiniAccel 平台和范围调整 |
| DEFER | 有价值，但超出 MVP 范围，留待后续阶段 |
| REJECT | 与项目目标不符，明确不采用 |

标签针对机制思想，不表示复制上游源码。

## 2. 候选设计决策

| 来源机制 | 建议 | MiniAccel 映射 | 原因与边界 |
|---|---|---|---|
| XRT `ishim` | ADOPT | `IBackend` | Runtime 不直接依赖 Linux IOCTL 或具体后端 |
| XRT Device/Context/Buffer API 分层 | ADOPT | Runtime SDK 对象模型 | 建立稳定用户态抽象 |
| Ethos-U Job 持有 GEM Buffer 引用 | ADAPT | Kernel Job 持有 Buffer 引用 | MVP 可先用自定义 Handle/Object，不强制 GEM |
| Ethos-U Job→Scheduler→Run→Completion | ADAPT | Kernel Job 生命周期 | MVP 使用有界 FIFO 与 Workqueue 模拟 |
| Ethos-U Reservation/Fence 隐式依赖 | DEFER | Buffer hazard/隐式同步 | MVP 先由应用显式建立 Event/Stream 依赖 |
| AMD XDNA Client 管理多个 HW Context | ADOPT | ClientSession→Driver Context Table | 适合多 Context 资源隔离 |
| AMD XDNA `job_sem` | ADAPT | Context In-flight Limit | 用于背压和资源上限，不等于公平调度 |
| AMD XDNA Sequence + Fence/Syncobj | ADAPT | TaskToken + Completion Record | 首期使用 Context ID、Generation、Sequence 和等待队列 |
| AMD XDNA Firmware Mailbox | REJECT（首期） | 不实现厂商 Firmware 协议 | MiniAccel 没有对应设备固件 |
| AMD XDNA UMQ Ring + Doorbell | DEFER | 用户态队列提交 | 先完成 IOCTL/KMQ 风格链路 |
| AMD XDNA SRCU Context Lookup | DEFER | Context 并发生命周期 | 首期用锁、引用计数和销毁协议 |
| XRT Command Manager | ADAPT | Completion Dispatcher | 先登记任务再提交，防止快速完成竞态 |
| XRT QDS/KDS 多路径选择 | ADAPT | Backend/Queue 适配 | CPU 与 Kernel Backend 共享逻辑接口，内部实现不同 |
| XOCL KDS / ERT Packet | DEFER | FPGA 专用命令调度 | 不作为通用 NPU Runtime 的 MVP 必需组件 |
| DRM_ACCEL / GEM / DRM Scheduler | DEFER | 后续 Driver Backend 增强 | MVP 先用 Character Driver 降低原型复杂度 |

## 3. 建议的 MiniAccel 逻辑架构

```text
Application
    |
    v
MiniAccel Runtime SDK
    +-- Runtime / Device Manager
    +-- Context Manager
    +-- Stream Manager
    +-- Event Manager
    +-- Memory Manager
    +-- Runtime Scheduler
    +-- Completion Dispatcher
    |
    v
IBackend
    +-------------------------+
    |                         |
    v                         v
CpuBackend          KernelDriverBackend
    |                         |
ThreadPool              ioctl/mmap/poll
    |                         |
CPU Task Execution   Linux Character Driver
                              |
                         Client Manager
                         Context Manager
                         Buffer Manager
                         Job Manager
                         Kernel Scheduler
                         Completion Manager
                              |
                          Workqueue
                              |
                      Virtual Accelerator
```

这是研究映射建议。最终模块边界、对象所有权和接口以正式 Architecture/API/Driver Spec 为准。

## 4. 必须在 Spec 中明确的边界

### 4.1 Runtime Scheduler 与 Kernel Scheduler

| Runtime Scheduler | Kernel Scheduler |
|---|---|
| Stream 顺序 | Kernel Job 准入 |
| Event 依赖解析 | Driver 队列容量 |
| Runtime Task 状态 | Kernel Job 状态 |
| 用户态背压 | Context/Device In-flight 限制 |
| Backend 分发 | Workqueue 派发 |

两级 Scheduler 的职责不能重复。Runtime Scheduler 不直接管理 Kernel Job 内部对象，Kernel Scheduler 不实现 Runtime Event 语义。

### 4.2 Stream、Queue、Task、Fence 与 Event

- Stream：用户态有序提交序列，定义同一 Stream 内的执行顺序。
- Queue：Backend 或 Driver 中承载待执行命令的队列。
- Task：一次执行请求及其状态和资源引用。
- Fence/Completion：底层任务完成或依赖同步原语。
- Event：Runtime 对应用暴露的同步点，可以记录某个 Stream 的任务完成位置。

这些对象可以关联，但不能简单合并为一个类或一个整数状态。

### 4.3 Runtime Context 与 Driver Context

建议保留两个层次：
- Runtime Context 管理 Stream、Event、Buffer Registry 和 Runtime Task。
- Driver Context 管理内核资源、Kernel Job 准入和 Buffer Handle 归属。

MVP 可以一对一映射，但不应让 Runtime 公共 API 直接暴露内核结构体。

### 4.4 Buffer 所有权

Application Handle、Runtime Buffer、Backend Buffer Handle 和 Kernel Buffer Object 是不同层次的身份。Task 在异步执行期间必须持有足够引用，确保 Buffer 不会被提前释放。禁止把用户态裸指针直接当作设备物理地址或可信内核对象地址。

### 4.5 提交、完成与超时

- 任务提交成功只代表 Backend 接受请求，不代表任务已完成。
- Wait Timeout 只代表等待调用超时，不自动取消任务。
- 每个被接受的任务必须且只能进入一个终态。
- 完成处理和资源回收必须幂等。
- Runtime 应先登记待完成 Task，再触发可能快速完成的 Backend submit，避免完成竞态。

## 5. MVP 与后续增强边界

| 能力 | MVP | 后续增强 |
|---|---|---|
| Runtime API | Device / Context / Stream / Event / Buffer / Task | 更丰富设备能力和多设备语义 |
| Backend | CPU Simulator + Kernel Driver Backend | CUDA Backend 或真实设备 Backend |
| Kernel Driver | Character Driver、IOCTL、mmap、poll | DRM_ACCEL、GEM |
| Kernel 执行 | 有界队列 + Workqueue 完成模拟 | 真实硬件提交、DMA、IRQ |
| 同步 | Task Token、Completion Queue、Wait Queue | DMA Fence、DRM Syncobj |
| 调度 | FIFO、Stream 顺序、Context In-flight 上限 | 多 Context 公平调度、优先级 |
| 用户态队列 | IOCTL 提交 | UMQ Ring / Doorbell |

Workqueue 模拟只能证明软件任务管理和异步完成路径，不得描述为真实 NPU 执行、DMA 或 IRQ。

## 6. Spec 编写时的追踪要求

每个关键设计至少记录：
1. 设计问题是什么。
2. 参考了哪个上游项目、文件和函数。
3. 借鉴的是机制还是接口形式。
4. MiniAccel 采用了哪些简化。
5. 哪些行为与上游实现不同。
6. 如何通过测试验证该设计。

Research 结论不能替代 Spec 中的接口定义、状态机、不变量和验收标准。

## 7. 尚待裁决的问题

- Runtime Context 与 Driver Context 是否严格一对一，以及何时允许一对多。
- Kernel Backend 完成事件采用 `poll/read`、`eventfd` 还是其他接口。
- Buffer 采用内核分配并 mmap 的内存，还是后续接入 GEM/DMA API。
- Runtime Scheduler 的跨 Stream Ready Queue 和背压策略。
- Context 销毁时，尚未开始与正在执行的任务分别采用何种处理协议。

以上问题应在正式 Spec 阶段裁决，不能在研究资料中提前固化为实现要求。
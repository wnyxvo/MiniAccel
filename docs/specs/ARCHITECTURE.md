# MiniAccel 总体架构设计规范

> 文档编号：SPEC-ARCH-001　版本：v1.0　状态：**正式基线**　语言：中文  
> 关联：[RUNTIME_SPEC.md](./RUNTIME_SPEC.md) · [DRIVER_SPEC.md](./DRIVER_SPEC.md) · [SCHEDULER_SPEC.md](./SCHEDULER_SPEC.md) · [TEST_AND_BENCHMARK.md](./TEST_AND_BENCHMARK.md)  
> 来源：[Research 总索引](../research/README.md) · [设计映射](../research/comparison/MINIACCEL_MAPPING.md)

## 1. 目标、适用范围与非目标

MiniAccel 是面向 **2027 春招 AI 芯片 Runtime / Linux 驱动框架**的可运行异构计算软件栈原型。目标是验证应用、C++17 Runtime、可替换 Backend、Linux Kernel Driver、软件模拟设备之间的完整任务和资源管理闭环。

**必须实现**：Device / Context / Stream / Event / Buffer / Task 的独立对象模型；CPU 和 Kernel 双 Backend；IOCTL、mmap、poll/read；显式依赖、有界队列、异步完成；多线程及多 Context 生命周期测试；可复现实验。

**不实现**：真实 NPU 指令、PCIe BAR/MMIO、真实 DMA/IRQ、设备 Firmware、生产级隔离、多机分布式 Runtime、DRM_ACCEL（仅保留升级边界）、真实 AI 模型执行。Kernel Backend 的 NOP/DELAY 仅表示软件驱动路径，并非 NPU 计算；CPU Backend 的 FILL/COPY/VECTOR_ADD 是软件计算。

默认环境：Ubuntu 24.04 x86_64 虚拟机，匹配运行内核的 headers；C++17/C11；CMake 编译用户态，Kbuild 编译外部驱动。实际 kernel 版本、编译器、CPU/VM 配置须记录于测试产物。

## 2. 权威性、变更与上游来源

- 本目录五份 Spec 共同构成规范性基线；Research 仅作为事实和设计来源。
- 规范优先级：**跨文档公共契约以本文件 §4～§8 为准；接口细节以 RUNTIME_SPEC 为准；UAPI/内核细节以 DRIVER_SPEC 为准；调度策略以 SCHEDULER_SPEC 为准；验收口径以 TEST_AND_BENCHMARK 为准。** 发现矛盾时停止受影响功能的开发，提出版本化变更，不得自行选择解释。
- 所有正式变更须写明：变更 ID、动机、影响模块、向后兼容性、测试影响、文档修订号。
- 源码借鉴：XRT `ishim` → `IBackend`；Ethos-U Job/BO/Fence → Job 引用与完成；XDNA Client/HW Context/job_sem → Context 和在途额度。这里是 **MiniAccel 自主设计选择**，不是上游源码的强制规范。

## 3. 逻辑层次及依赖方向

```text
Application / examples / tests
           |
           v
Public C++17 API (miniaccel::Runtime, Device, Context, Stream, Event, Buffer, Task)
           |
           v
Runtime Core
  ├─ DeviceRegistry / ContextRegistry / BufferRegistry
  ├─ Stream + Event dependency tracker
  ├─ RuntimeScheduler + bounded pending accounting
  ├─ TaskRegistry / CompletionDispatcher
  └─ MemoryPool (Context scoped)
           |
           v
IBackend  <---- capability negotiation / opaque backend handles
  ├─ CpuBackend: bounded worker queue -> CPU operation -> completion queue
  └─ KernelDriverBackend: ioctl + mmap + poll/read
                                      |
                                      v
                        /dev/mini_accel (Character Driver)
                         ├─ Device / per-open Client
                         ├─ Context / Buffer handle tables
                         ├─ validated Job + bounded FIFO
                         ├─ ordered delayed_work execution
                         └─ completion FIFO / waitqueue / poll
```

**依赖禁止事项**：
1. Public API 与 Runtime Core 不得包含 Linux Kernel/UAPI 私有头；只有 KernelDriverBackend 可引用共享 UAPI C 头。
2. RuntimeScheduler 不调用 `ioctl`、`mmap` 或 Kernel 数据结构；Driver 不包含任何 Runtime C++ 类型。
3. Application 不直接使用 Driver IOCTL；测试 UAPI 时可单独调用（Driver 集成测试例外）。
4. CPU Backend 不依赖内核模块；Buffer Pool 不获取 Kernel 内部指针。
5. 上游代码不得直接复制作为 MiniAccel 核心实现。

## 4. 跨文档唯一对象模型

| 概念 | 位置 | 创建方/所有者 | 关键约束 |
|---|---|---|---|
| Device | Runtime Core | Runtime/DeviceRegistry | 一个 Backend 实例代表一个逻辑 Device |
| Client | Kernel | 每次独立 `open()` 形成的 open file description | `dup()` 共享同一个 Client；不同 `open()` 隔离 |
| Runtime Context | Runtime | Device 创建 | 与 Backend Context 一一对应，拥有 Stream、Event、Buffer、Task 注册表 |
| Backend Context | Backend | Context 创建时 | 对 Kernel Backend 映射到 Driver Context Handle |
| Driver Context | Kernel | Client 下 CREATE_CONTEXT | 归属单一 Client；销毁必须排空资源 |
| Stream | Runtime | Context 创建 | 严格按提交先后完成（包括失败传播） |
| Event | Runtime | Context 创建 | record 生成不可变的代际快照；wait 使用快照 |
| Buffer | Runtime/Backend/Kernel | Context 内申请 | Context 内归属，Task 持有引用，释放句柄不等于立即释放内存 |
| Command | Runtime | 调用方构造值对象 | 描述操作码、参数、Buffer 引用，**不是**执行状态 |
| Task | Runtime | Stream::submit 成功时 | Context 唯一 TaskToken、依赖、状态、结果 |
| BackendTaskId | Backend | Backend 接受提交时 | 不暴露为 Runtime TaskToken |
| Kernel Job | Kernel | SUBMIT_TASK | Client/Context 引用、命令参数、状态、序列号 |
| Completion | Backend→Runtime | Backend 执行结束产生 | 必须与一个被接受的任务关联，至多消费一次 |

ID 规则：`TaskToken = {context_id:u64, generation:u32, sequence:u64}`；Runtime 创建 Context 时分配单调不复用的 context_id（溢出报错），generation 由对象创建代际设置；sequence 从 1 递增，溢出拒绝提交。Kernel 使用独立的单 Client Context Handle 和 Kernel 任务序号；两者通过 Backend 映射，绝不将用户态裸指针作为 ID。首版 Kernel Handle 单调分配且不复用，耗尽报错。

## 5. 命令能力矩阵

| 操作 | CpuBackend | KernelDriverBackend | 语义 |
|---|---|---|---|
| NOP | 必须 | 必须 | 不读写 Buffer，成功完成 |
| DELAY | 必须 | 必须 | 模拟软件设备处理时间；0～1000ms |
| FILL | 必须 | 不支持 | 用单字节填充 Buffer 指定范围 |
| COPY | 必须 | 不支持 | 两个 Buffer 之间复制指定字节区间 |
| VECTOR_ADD | 必须 | 不支持 | 对等长 float32 数组执行 A+B→C |

Backend 返回能力位图；对不支持的操作 **同步拒绝** 为 `Unsupported`，不得悄悄降级到 CPU。所有命令先进行操作码、参数、归属、offset/length、对齐和溢出检查。

## 6. 任务与同步的跨层约束

### 6.1 Runtime Task 状态

```text
CREATED -> WAITING_DEPENDENCIES -> READY -> SUBMITTED -> COMPLETED
                          |          |          +-----> FAILED
                          |          +----------------> FAILED
                          +---------------------------> FAILED
                          |          |
                          +----------+----------------> CANCELED (仅未启动任务)
```

`CREATED` 仅用于构造过程，提交被接受后必须快速进入后续状态；`WAITING_DEPENDENCIES` 包括等待同 Stream 前序任务及 Event。终态只有 COMPLETED、FAILED、CANCELED。执行中不能无能力地宣称已取消。等待超时仅是等待 API 返回 `Timeout`，不是任务终态。

### 6.2 顺序与依赖

- **同一 Stream：** 按提交序列建立前驱依赖；任务严格按序终结，前驱失败/取消则后继不执行并以依赖失败进入 FAILED。
- **不同 Stream：** 默认无执行先后；可通过 `Event::record(stream)` 与 `Stream::waitEvent(event)` 建立跨 Stream 依赖。
- Event `record` 是 Stream 上的零 Backend 工作标记，绑定其前驱任务的完成位置；每次 record 生成独立 generation，后续 re-record 不修改已绑定 wait 的版本。
- 同 Context 内支持跨 Stream Event；跨 Context Event/Buffer 共享不支持。
- 显式依赖：MVP **不做 GEM Reservation 风格的隐式 Buffer hazard 检测**；跨 Stream 共享可写 Buffer 时，调用者必须建立依赖。

### 6.3 生命周期和资源保活

- Task 在 **被 Runtime 接受前**检查 Buffer/Context 归属；被接受后持有所有访问对象的强引用；最终终态时归还引用。
- Runtime 先登记 TaskToken→TaskRecord，再触发 Backend 提交，防止 NOP 快速完成早于登记。
- Context::close() **同步排空**：拒绝新提交，尚未下发的任务进入 CANCELED；已被 Backend 接受的任务等待自然终态；资源排空后才销毁 Backend Context。MVP DELAY≤1000ms 保证有界可排空；若 Backend 失联并且无法确认 DMA/工作已停止，**不得释放仍可能被访问的对象**，显式报告错误并保留资源直到安全停止。
- Stream::close() 对该 Stream 同样执行拒绝新提交和排空，不允许离开被引用的悬空对象。
- Context 关闭是幂等操作；关闭进行中并发提交返回 `Closed`。
- Runtime::shutdown() 先关闭 Context，最后关闭 Backend。Kernel Client `release()` 阻止新任务、取消排队工作、等待运行工作及映射安全退出。

## 7. 统一提交与完成流程

```text
Application
 -> Stream::submit(Command)
 -> 验证能力/归属/参数/队列配额
 -> TaskRegistry 登记 Task + 持有 Buffer
 -> RuntimeScheduler 建立同 Stream / Event 依赖
 -> Ready Queue（按 Stream Round-Robin）
 -> IBackend::submit()
    -> CpuBackend::worker
    OR
    -> KernelBackend::ioctl(MA_IOCTL_SUBMIT_TASK)
       -> validate + enqueue Kernel Job
       -> ordered Workqueue / delayed_work
       -> completion FIFO + wake_up
       -> read/poll
 -> CompletionDispatcher 按 BackendTaskId 找 Runtime Task
 -> 原子/互斥锁保护的唯一终态转换
 -> 归还配额、释放 Job/Buffer 引用、唤醒 wait/Event
```

IOCTL 返回成功仅意味着 Kernel Job 已被接受排队；只有完成记录才意味着执行结果确定。Backend 提交失败使 **已经被 Runtime 接受** 的 Task 进入 FAILED 并发送一次终态通知，不再保留对 Job 的假引用。

## 8. 锁、线程模型、容量与故障边界

- Runtime 可以用 `std::mutex`/`std::condition_variable`；禁止在持有 Registry/Stream 锁时阻塞调用 Backend、等待任务或执行回调。
- 每个 Device 一个 RuntimeScheduler 线程和一个 CompletionDispatcher 线程；CpuBackend 有独立 Worker 线程池；Kernel Backend 独立阻塞 poll/read，不能卡住提交线程。
- 锁序建议：Device State → Context State → Stream/Task State；原则上通过复制必要句柄再释放上层锁，避免跨层锁嵌套。
- 默认资源上限（可配置但必须验证非零、受控）：每 Context `max_pending_tasks=256`（WAITING/READY/SUBMITTED 总量）；每 Backend Context `max_inflight=16`；Kernel Device 排队容量 `256`；CPU Worker 数默认 `min(4, hardware_concurrency)` 且至少 1。
- Runtime 提交队列满：`Stream::submit` **非阻塞** 返回 `QueueFull`，不创建 Task，不占用 sequence，不插入 Stream 顺序；Runtime 不能在背压失败后丢失既有任务。
- Kernel 容量满：IOCTL 返回 `-EAGAIN`，Kernel 不创建可见任务，不消耗 sequence；KernelBackend 映射为 `QueueFull`；Runtime 已接受的任务重试时须保持身份和资源引用，使用事件驱动/有限等待，不允许忙自旋。若设备终止或超过显式派发截止期限，完成 FAILED。
- Device/Backend 失效时，禁止假报完成；已运行任务在安全停止确认前不得回收内核内存。
- `mmap` 的 VMA 生命周期可能长于用户态 Buffer Handle；Kernel Buffer 对象应由 VMA 引用保护。
- 所有错误传播为稳定 Status；不得以 `printf` 或日志代替错误码。

## 9. 目录与模块目标

```text
MiniAccel/
├── docs/
│   ├── research/
│   └── specs/
├── include/miniaccel/         # Public C++ headers
├── src/core/                 # Runtime, Context, Stream, Event, Scheduler, Task
├── src/backends/cpu/         # CpuBackend
├── src/backends/kernel/      # KernelDriverBackend
├── driver/                   # Kernel C/Kbuild
├── uapi/                     # 用户态/内核态共享 C ABI
├── tests/unit/
├── tests/integration/
├── tests/stress/
├── benchmarks/
├── examples/
└── CMakeLists.txt
```

目录是开发目标，不代表当前源码已存在。只有 Codex 开发阶段才应创建代码目录。

## 10. 可观测性

每个 Task 记录单调时钟时间点：`accepted_ns`、`ready_ns`、`backend_submit_ns`、`backend_complete_ns`、`terminal_ns`；状态转换、TaskToken、BackendTaskId、Context/Stream、队列深度、操作码、错误码可用于日志或 trace；不得泄漏未初始化 Kernel 内存地址。所有性能数据标记 Backend 类型与计算/模拟属性。

## 11. 阶段交付与约束

| 阶段 | 交付 | 必须通过的门禁 |
|---|---|---|
| M0 | 公共 Status/Task、BoundedQueue、CpuBackend 雏形 | 编译、单元测试和 TSan |
| M1 | Character Driver + 原生 UAPI 示例 | VM 中模块 load/open/ioctl/poll/read/unload |
| M2 | Driver Context/Buffer/Job/mmap | 越权句柄、并发销毁、映射生命周期测试 |
| M3 | 统一 C++ API + 双 Backend | Stream/Event/Buffer/Task E2E |
| M4 | 跨 Stream 调度、背压、关闭与错误注入 | 压测、无死锁、单次终态 |
| M5 | Benchmark、文档、结果归档 | 可复现报告与边界说明 |

**尚不能称为完成：** 仅有文档、编译通过、单一 NOP 演示，或缺少 Kernel Driver VM 实测。

## 12. 实施禁止事项与审查要点

- 不得把 Character Driver Workqueue 的数据当真实硬件吞吐或 DMA 延迟。
- 不得跨 Client/Context 使用 Handle；不信任 UAPI 中的地址。
- 不得用 `std::future` 或裸 `std::thread` 临时代替正式任务状态和完成队列而跳过资源生命周期。
- 不得允许单个任务有两个终态、发生重复完成通知、重复归还信号量或重复释放 Buffer。
- 改动公共语义必须同步修订五份 Spec 和关联测试。
- 每个开发 PR 应有：Spec 条款、改动文件、风险、编译/测试命令及真实结果。

## 13. 验收引用

具体公共 API 签名与 Event 语义见 [RUNTIME_SPEC.md](./RUNTIME_SPEC.md)；IOCTL 与 mmap 见 [DRIVER_SPEC.md](./DRIVER_SPEC.md)；配额、两级调度和异常路径见 [SCHEDULER_SPEC.md](./SCHEDULER_SPEC.md)；测试编号与交付门禁见 [TEST_AND_BENCHMARK.md](./TEST_AND_BENCHMARK.md)。
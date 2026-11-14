# MiniAccel 异步任务与调度规范

> 文档编号：SPEC-SCH-001　版本：v1.0　状态：**正式基线**  
> 依赖：[ARCHITECTURE.md](./ARCHITECTURE.md) · [RUNTIME_SPEC.md](./RUNTIME_SPEC.md) · [DRIVER_SPEC.md](./DRIVER_SPEC.md)

## 1. 调度范围与双层职责

调度分为 **Runtime Scheduler** 与 **Kernel Scheduler** 两个层次，不能合并。

| 维度 | Runtime Scheduler（用户态） | Kernel Scheduler（内核态） |
|---|---|---|
| 任务单位 | Runtime Task / Command / Event barrier | Kernel Job |
| 资源 | Context pending quota、Stream-ready queue | Context inflight quota、Device capacity |
| 顺序 | Stream FIFO、Event DAG、失败传播 | 已接收 Job 的 FIFO 和完成 |
| 策略 | Ready Stream Round-Robin | MVP Device FIFO + ordered Workqueue |
| 提交 | `IBackend::submit` | `MA_IOCTL_SUBMIT_TASK` |
| 完成 | BackendCompletion→RuntimeTask | Job terminal→completion record |
| 不负责 | ioctl/Kernel Job/硬件执行 | Stream/Event DAG/跨 Stream 顺序 |

MVP 采用非抢占式调度，CPU Worker 数可配置；Kernel 单执行通道以便追踪。多 Context 公平调度作为 M4 增强（须有测试证明），**不能将一个 FIFO 算法写成具有公平性保证的算法**。

## 2. 确定性参数和资源边界

| 参数 | 默认 | 含义 |
|---|---|---|
| `max_pending_tasks` | 256 / Context | WAITING+READY+SUBMITTED 的所有非终态 Runtime Task，含 Marker/Barrier |
| `max_inflight` | 16 / Backend Context | Kernel 已接受未终态 Job/CPU 已派发未终态 Task |
| `kernel_device_capacity` | 256 | 全设备已接受未终态 Kernel Job |
| `cpu_worker_count` | min(4,hardware_concurrency)，至少1 | CPU 并行执行通道 |
| `kernel_max_delay_ms` | 1000 | 单个 DELAY 的模拟执行上限 |
| `completion_fifo_records` | 1024 / Client | 完成 FIFO 现存记录 + 已预留槽位之和；每个成功提交 Kernel Job 先预留 1 个、read 消费归还 |
| `dispatch_retry_wait_ms` | 10 | `QueueFull` 重试等待上限/次；不得忙轮询 |

可通过 ContextConfig/driver module 参数改变经允许的配置；全部整数范围必须校验，零值除专门定义的非阻塞 timeout 外一律拒绝。Benchmark 必须记录实际参数。

## 3. Runtime Task 状态机

```text
CREATED
   |
   v
WAITING_DEPENDENCIES
   |           \
   |            -> FAILED(DependencyFailed)
   v
READY
   |  \ 
   |   -> CANCELED(尚未派发的 Close)
   v
SUBMITTED
   |  \
   |   -> FAILED(Execution/Backend failure)
   v
COMPLETED

任意非终态可以在关闭中未派发时进入 CANCELED；
已派发任务只有在 Backend 确认取消完成后才能 CANCELED。
```

状态迁移必须由 Task 的原子锁/互斥锁保护，已进入终态的 Task 不可再修改结果/释放引用。FAILED 不得被“重试成功”覆盖；Retry 必须在尚未成功提交 Backend、没有生成 Backend TaskId 的 Ready Task 上进行。

**不变量 SCH-I01**：每个已被 Runtime 接受的 Task 最终有且仅有一个终态（无法安全停止的 Backend 失联场景应报告未完成状态，不得捏造终态）。  
**SCH-I02**：同 Stream 第 N+1 个 Command 在第 N 个未成功结束前不能被 Backend 执行。  
**SCH-I03**：任务所有 Buffer/Context 引用在终态并且 Backend 确认不再访问后才能释放。  
**SCH-I04**：`TaskToken` 不复用；Runtime/Backend/Kernel 的 ID 不混用。  
**SCH-I05**：每个配额在且仅在相应状态转换点获取/归还一次。  
**SCH-I06**：对依赖失败传播的任务，不得执行 Backend 命令。

## 4. Stream FIFO 与 Event 依赖图

每个 Stream 保存 `tail_task`、按成功提交顺序分配的本地序列，以及 Pending 节点。

新增 Command 的依赖集合包括：
1. 同 Stream 前一个 Task 或 Marker/Barrier。
2. 调用 `waitEvent()` 插入的屏障节点对应 Event generation。

Event:
- record 生成 `EventGeneration`，在指定 Stream 后插入零 Backend 工作 Marker。Marker 成功意味着该 record 位置之前 Stream 所有 Task 成功结束。
- waitEvent 捕获并持有 `EventGeneration` 的不可变引用，插入本 Stream 依赖屏障。
- Marker 与 Barrier 仍占 Context pending 配额，但不消耗 Backend inflight。
- 先 record 后 wait；Unrecorded Event 不可等待。
- Re-record 不修改旧 Event generation 的快照；Event.wait 按调用时最新 generation。
- 插入依赖边前验证 Context 相同、无自依赖、无依赖环；如形成环，返回 InvalidArgument 且不产生节点/不占配额。
- 依赖边只能指向已经存在且已被接受的 Task/Marker，严禁指向未来不可辨识的裸序号。
- 当前 Runtime **不做隐式 Buffer hazard**：跨 Stream 共享写 Buffer 的应用必须显式 Event 同步；测试应演示正确/错误用法的差异。

## 5. Runtime Scheduler 算法

Runtime Scheduler 是一个 Device 级工作线程，内部维护 Ready Stream 环形表，确保每次只从一个 Stream 的头部取一个已满足依赖的 Task。该算法保证的是**就绪 Stream 的轮询机会**，不是严格延迟公平或跨 Context 权重公平。

伪代码：

```text
on_submit(stream, command):
  validate(command, ownership, capability)
  lock(context)
    if closing: return Closed
    if pending_count == max_pending: return QueueFull
    token = allocate_monotonic_token()
    record = create_and_register_task(token, strong_references)
    pending_count++
    append_dependency_edges(record, stream.tail)
    stream.tail = record
  unlock(context)
  wake_scheduler()
  return Task(record)

scheduler_loop:
  wait_until(ready_stream_exists || stopping)
  choose_next_ready_stream_round_robin()
  record = peek_stream_head()
  if marker_or_barrier:
    resolve_without_backend_once_dependencies_success()
  else:
    mark_ready(record)
    result = backend.submit(record)  // 不持有全局 Registry 锁
    if accepted:
      mark_submitted(record)
    else if QueueFull:
      wait_for_completion_or_capacity_notification_bounded(10ms)
      requeue_same_record_without_new_token()
    else:
      finish_failed(record, result)
  publish_newly_ready_dependents()
```

**竞态约束**：提交时登记 TaskRecord **先于 Backend 可能的快速完成**；提交返回和 CompletionDispatcher 之间存在可控 ID 映射/暂存。Scheduler 只处理同一个 Stream 头节点，不允许一个 Stream 的 N+1 越过未完成的 N。

任务 Ready 后如果 Backend 反复 QueueFull，使用 Completion 通知/条件变量唤醒；可以有限时间等待，但不消耗额外 TaskToken，不在忙循环中重复 IOCTL。停机时唤醒所有等待者。

## 6. Backend 接受与完成

**接受点划分**：
- Runtime 接受：TaskToken 创建，pending_count++，调用方获得 Task。
- Backend 接受：Backend 为任务创建 BackendTaskId 并承担完成报告义务，进入 SUBMITTED。
- Kernel 接受：`MA_IOCTL_SUBMIT_TASK` 成功返回 `task_sequence`，Job 入队；IOCTL 的 `-EAGAIN` 未接受，不产生序列号/完成。准入时还必须取得一个 Completion FIFO 预留槽位，完成后转换为记录，read 消费时释放，确保不静默丢失通知。
- Completion：Backend Completion 返回终态，Runtime 以 BackendTaskId 定位 Task，再应用唯一终态转换。

不变量：
1. Backend 完成信息不能在 TaskRegistry 尚未建立时被丢弃。
2. Runtime 可以暂存早到的 BackendCompletion，待 BackendTaskId 映射建立后立即应用；未知 ID 必须记录并使 Backend 进入故障流程，不能忽略。
3. 不得把 Kernel Job 的队列/运行状态映射为 Runtime Task 已完成。Kernel 终态查询记录通过 `MA_IOCTL_REAP_TASK` 显式回收；KernelDriverBackend 消费完成并安全写入 Runtime TaskRegistry 后负责 REAP；原生 UAPI 用户自行决定回收时机。
4. 完成必须最终唤醒 Task.wait、Event.wait、Stream/Context.synchronize 以及 Scheduler Capacity Waiter。
5. CompletionDispatcher 不得持有全局锁时调用用户回调；MVP 不提供自定义完成回调 API。

## 7. Kernel Scheduler 模型

- 单设备级 bounded FIFO，按 IOCTL **成功接受的时间**排序；一个 ordered worker 执行（无真实硬件并发）。
- `device_capacity=256` 是所有 Context 已接受未终态 Job 数上限；`Context max_inflight≤16` 是该 Context 的已接受未终态 Job 上限。两个额度必须在同一受锁保护的准入事务中获取。
- 未获得所有额度时：原子返回 `-EAGAIN`，不能占某个额度后遗失另一个。
- Job 接受后单调分配 Context 内 `task_sequence`，登记 Job 后入队；排队工作和已运行工作分别维护。
- NOP 迅速完成；DELAY 仅模拟软件执行耗时，0～1000ms。
- 完成处理：唯一终态转换 → 记录时间戳和状态 → 释放额度 → 唤醒等待者 → 投递 Completion Record → 最后按引用计数清理。
- 两个 Context 交替提交时，MVP FIFO 可能不公平；M4 可使用 per-Context 队列 + Round-Robin，但必须同步更新 Spec 和性能基线，不得无审查变更。

## 8. 背压、超时与取消

### 8.1 三层容量区别

- Runtime pending：防止用户态保存无界未完成 Task。
- Backend in-flight：防止设备层接受太多任务。
- Kernel device capacity：防止内核排队无界增长。

**容量不能混淆**。提交被 Runtime 接受后，Kernel `-EAGAIN` 不能返回给已经拿到 Task 的调用者并删除 Task，应在 Scheduler 保留同一 TaskRecord 后重试（只有尚未被 Backend 接受时才可重试）。

### 8.2 超时

- `Task.wait(t)`、`Event.wait(t)`、`Stream/Context.synchronize(t)` 的 Timeout 是等待超时，任务保持原状态。
- Kernel `WAIT_TASK` 超时也只返回 `-ETIMEDOUT` 或非阻塞 `-EAGAIN`；不得自动转为 CANCELED。
- 任务执行超时属于独立设备故障诊断，MVP 不用人为超时机制强行释放正在执行的 DELAY 工作。
- Stop/Close 必须唤醒容量等待、Scheduler wait 和 Completion wait，防止无限挂起。

### 8.3 取消与关闭

- 未派发 Ready/Waiting Task：Close 将其 CANCELED，并向其依赖后继传播 DependencyFailed；Task 持有的 Buffer 引用安全释放。
- 已成功 Backend 接受的 Task：MVP 不支持任意即时硬取消；必须等待 Backend 安全终态。
- Kernel Context Destroy 是非强制接口：存在 in-flight Job 或活跃 Buffer/VMA 时 `-EBUSY`；正常 Runtime Context.close 会先 drain/free，再调用 Destroy。
- Client release 强制进入 Closing，可取消未执行 Job；正在执行工作必须安全 drain 后释放。
- 错误/取消由独立组件以幂等终态操作处理，最多释放一次额度、最多投递一次 Completion。

## 9. 错误传播与依赖失败

| 原因 | 任务状态 | Status |
|---|---|---|
| 前置参数/能力不合法 | 不创建 Task | InvalidArgument / Unsupported |
| Runtime 队列满 | 不创建 Task | QueueFull |
| Backend 提交失败（非临时 QueueFull） | FAILED | IoError / BackendUnavailable |
| 执行完成成功 | COMPLETED | Ok |
| 依赖任务 Failed/Canceled | FAILED | DependencyFailed |
| Close 取消未派发任务 | CANCELED | Canceled |
| wait 超时 | Task 不变 | Timeout |

依赖错误传播必须记录上游 Token 与根因；不能只记录“任务消失”。一个 Task 的最终结果一旦发布必须对所有 wait/query 可见，不允许不同调用得到相互矛盾终态。

## 10. 可观测性与评价

记录：ContextID、StreamID、TaskToken、BackendTaskId、Opcode、队列深度、inflight、前驱 Token、任务状态、提交/完成时间、状态错误码、重试次数。

指标定义：
- 接受等待：ready_ns - accepted_ns
- 后端派发等待：backend_submit_ns - ready_ns
- 后端执行/完成：backend_complete_ns - backend_submit_ns（包含 Backend 排队）
- Runtime 收尾开销：terminal_ns - backend_complete_ns
- End-to-end Task Latency：terminal_ns - accepted_ns
- Throughput：成功完成 Task 数 / 从第一任务接受到最后任务终态的 wall time

所有统计以单调时钟为准，不混用不同进程/Kernel 时钟域的裸 timestamp；跨层统计需明确对齐方法。End-to-end 延迟包含排队，不能与纯计算耗时互相替代。

## 11. 调度验收矩阵

| ID | 场景 | 必须成立 |
|---|---|---|
| SCH-01 | 单 Stream NOP/DELAY | 成功任务严格依序终结 |
| SCH-02 | 双 Stream，无显式依赖 | 无隐式跨 Stream 顺序要求 |
| SCH-03 | Record/WaitEvent | 后继只在 Event generation 成功后执行 |
| SCH-04 | Event 重复 Record | 旧 wait 绑定旧 generation |
| SCH-05 | DAG 环、自依赖 | 提交前拒绝，无资源泄漏 |
| SCH-06 | Runtime 满额 | QueueFull、不消耗 Token |
| SCH-07 | Kernel Context 满额 | -EAGAIN、不生成 Job/Seq |
| SCH-08 | Backend QueueFull | 同一 Task 延后重试，不生成第二 Task |
| SCH-09 | 快速 NOP 完成与映射 | Completion 不丢、不重复 |
| SCH-10 | Close/失败传播 | 所有已接受 Task 处于安全终态或显式故障保活 |
| SCH-11 | 多 Context 长压测 | 无饥饿导致的无限资源增长、无死锁；公平保证需独立证明 |

## 12. Codex 实施分批

S0：TaskRecord/StateMachine；S1：Stream FIFO 与 pending 配额；S2：Event generation + DAG；S3：Ready Stream RR；S4：Backend QueueFull 重试 + Completion Dispatcher；S5：Close/Shutdown + 故障注入。每次提交须附对应 SCH 测试编号、结果和资源清理验证。
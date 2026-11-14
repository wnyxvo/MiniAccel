# MiniAccel C++17 Runtime 设计规范

> 文档编号：SPEC-RT-001　版本：v1.0　状态：**正式基线**  
> 上游研究：[XRT](../research/xrt/README.md)、[AMD XDNA](../research/amd-xdna/README.md)。总约束见 [ARCHITECTURE.md](./ARCHITECTURE.md)。

## 1. 职责和边界

Runtime 提供稳定的 C++17 API，管理 Device、Context、Stream、Event、Buffer、Command 和 Task；Backend 只提供资源与异步执行能力。Runtime 不直接执行 Kernel Driver IOCTL；KernelDriverBackend 独占 UAPI 封装。CPU/Kernel Backend 的操作能力不同，必须通过 Capability 查询，绝不能在操作不支持时隐式切换后端。

线程安全：Runtime/Device/Context/Stream/Event/Buffer 的明确 API 可多线程调用；一个 Stream 对并发调用线性化，序号按成功接收的提交顺序。对同一 Buffer 的并发读写需要应用自行同步。

## 2. 公共 API 规范性草案

以下代码是**接口签名契约**，并非当前仓库已实现代码。Codex 可以拆分头文件或改用 PImpl，但不得改变公共可见语义；若必须修改签名，先提出 Spec 变更。

```cpp
namespace miniaccel {
enum class StatusCode : uint32_t {
    Ok=0, InvalidArgument, InvalidHandle, WrongContext,
    Unsupported, QueueFull, Timeout, Closed, Busy,
    DependencyFailed, Canceled, BackendUnavailable,
    IoError, InternalError, OutOfMemory
};
struct Status {
    StatusCode code{StatusCode::Ok};
    std::string message;
    bool ok() const noexcept { return code == StatusCode::Ok; }
};
template<class T> class Result; // 提供 ok(), status(), value()；错误时禁止访问 value()
enum class BackendKind : uint32_t { Cpu=1, Kernel=2 };
enum class Opcode : uint32_t { Nop=1, Delay=2, Fill=3, Copy=4, VectorAdd=5 };
enum class TaskState : uint32_t {
    Created, WaitingDependencies, Ready, Submitted,
    Completed, Failed, Canceled
};
struct TaskToken {
    uint64_t context_id{};
    uint32_t generation{};
    uint64_t sequence{};
    bool operator==(const TaskToken&) const noexcept = default; // C++17 实现时显式定义
};
struct DeviceInfo {
    uint32_t ordinal{};
    BackendKind backend{};
    std::string name;
    uint64_t capability_bits{};
};
struct ContextConfig {
    uint32_t max_pending_tasks{256};
    uint32_t max_inflight{16};
};
struct Command {
    Opcode opcode{Opcode::Nop};
    std::vector<std::shared_ptr<class Buffer>> buffers;
    uint64_t src_offset{}, dst_offset{}, bytes{};
    uint32_t delay_ms{}, fill_byte{};
};
struct TaskResult {
    TaskToken token{};
    TaskState state{};
    Status status{};
};
class Task {
public:
    TaskToken token() const;
    Result<TaskResult> wait(uint32_t timeout_ms);
    Result<TaskResult> query() const;
};
class Buffer {
public:
    size_t size() const noexcept;
    Result<void*> map();
    Status unmap();
    Status syncToDevice();   // Kernel 软件共享内存中为验证性 no-op
    Status syncFromDevice(); // 不宣称 DMA/缓存一致性动作
};
class Event {
public:
    Status record(class Stream& stream);
    Result<TaskResult> wait(uint32_t timeout_ms);
};
class Stream {
public:
    Result<std::shared_ptr<Task>> submit(const Command& command);
    Status waitEvent(const Event& event);
    Status synchronize(uint32_t timeout_ms);
    Status close();
};
class Context {
public:
    Result<std::shared_ptr<Stream>> createStream();
    Result<std::shared_ptr<Event>> createEvent();
    Result<std::shared_ptr<Buffer>> allocateBuffer(size_t size,
                                                    size_t alignment=64);
    Status synchronize(uint32_t timeout_ms);
    Status close();
};
class Device {
public:
    DeviceInfo info() const;
    Result<std::shared_ptr<Context>> createContext(const ContextConfig& config={});
};
class Runtime {
public:
    Status initialize();
    std::vector<DeviceInfo> enumerateDevices() const;
    Result<std::shared_ptr<Device>> openDevice(BackendKind kind,
                                               uint32_t ordinal=0);
    Status shutdown();
};
} // namespace miniaccel
```

**C++17 注意**：示例 `operator== = default` 是意图展示，C++17 实现必须显式定义 `operator==`（不可引入 C++20 特性）。`Result<T>`、`Result<void*>` 实现应统一错误传递；`Status` 为显式错误值，不以异常作为正常控制流。对象析构函数必须 noexcept。

## 3. 创建、所有权与关闭

- Runtime::initialize() 可幂等调用；shutdown() 与 initialize() 并发时返回 Busy/Closed 而非产生半初始化对象。shutdown 后再次 initialize 属于新 Runtime 生命周期，旧 Handle 不得恢复有效。
- Device 所属 Backend 由 Runtime 唯一管理。MVP 每种 Backend 暴露一个逻辑 Device；CPU 总能发现，Kernel Driver 缺失时不返回对应 Device。
- Context 只属于一个 Device，创建时获得新的全局 Context ID；对应一个 Backend Context；一个 Context 可拥有多个 Stream/Event/Buffer。
- `shared_ptr` 对象句柄不等于资源所有权：Context 内维护 Resource Registry，关闭后旧 Stream/Event/Buffer 方法必须返回 Closed；Task 保活 Backend 资源直至终态。
- Context::close() 同步：标记 Closing → 禁新建/新提交 → 取消未派发任务 → 等待所有已接受任务安全终态 → 删除 Backend Context → Closed。正常 DELAY 最大 1000ms；Backend 永久失联时不允许以假完成释放资源，返回 BackendUnavailable，保留保护引用。
- Stream::close() 同理，只作用该 Stream，不能破坏同 Context 其他 Stream。
- `Context::synchronize(timeout)` 仅等待调用瞬间已经提交的任务快照；运行期间后续提交任务不属于本次等待集。
- `Stream::synchronize(timeout)` 等待调用瞬间尾任务终态；若此前任务失败，返回其可追踪的首个错误而非成功。
- `Task::wait(timeout)` 0 表示非阻塞观察；`UINT32_MAX` 表示无限等待（测试只在可控场景使用）。超时返回 `Timeout`，不改变 Task 状态。

## 4. TaskToken 和结果身份

`TaskToken={context_id,generation,sequence}`；Context ID 按 Runtime 生存期单调分配，generation 标识创建代际；sequence 从 1 递增，只对成功接受的 Runtime Task 分配。0 为无效值。TaskToken 的身份只在 Context 内序列有意义，三元组在本 Runtime 生存期必须唯一。

**任务入队原子性**：
1. 验证 Context/Stream、Command、Buffer ownership、Opcode capability 和参数。
2. 原子检查本 Context `max_pending_tasks` 配额。
3. 创建 Task + 分配 Sequence + 加强引用 + 登记 TaskRegistry。
4. 提交到 Scheduler。任意前置步骤失败，不得消耗 sequence 或队列额度。
5. 后续 Backend 返回拒绝或故障时，Task 已存在，应进入 FAILED，而非从 Registry 悄悄消失。

`query()` 在 Task 生命周期内返回已知状态；状态读取必须线程安全。Event 记录使用独立逻辑标记任务；其任务也计入 Context pending 配额。

## 5. Command 合法性及能力

| Opcode | Buffer 顺序/参数 | CPU | Kernel |
|---|---|---|---|
| Nop | buffers 为空，bytes=0, delay=0 | 支持 | 支持 |
| Delay | buffers 为空；delay_ms∈[0,1000] | 支持 | 支持 |
| Fill | buffers=[dst]；dst_offset/bytes 在边界内，fill_byte≤255 | 支持 | 不支持 |
| Copy | buffers=[src,dst]；源/目标区间均有效，重叠时采用 memmove 语义 | 支持 | 不支持 |
| VectorAdd | buffers=[a,b,c]；bytes 是元素数量×4；所有 offset/bytes 4 字节对齐 | 支持 | 不支持 |

- `bytes=0` 对 Fill/Copy/VectorAdd 合法，视为成功 no-op，但所有 Handle 和 offset 仍需验证。
- 验证 offset ≤ size 且 bytes ≤ size-offset，禁止 `offset+bytes` 溢出。
- 不支持的操作同步返回 `Unsupported`，不创建 Task。
- VectorAdd 仅支持 float32，可用标准 C++ 循环实现，输出逐元素误差按 TEST 文档容差验证；别名与部分重叠范围不支持，必须明确拒绝。
- CpuBackend 的 Buffer 在 host 可直接映射；KernelBackend mmap 仅支持其 Buffer，数据操作由应用访问映射完成，不意味着 Kernel Backend 执行 FILL/COPY/VECTOR_ADD。

## 6. Stream 顺序与 Event 语义

- Stream 每个成功接受的 Command 在其前一个已接受任务之后才可执行；前驱失败则后继以 `DependencyFailed` 终结且不下发。
- Event 初始为 Unrecorded，wait 或 waitEvent 返回 InvalidArgument。
- `Event::record(stream)` 必须与 Event 和 Stream 属同 Context；在该 Stream 当前尾部插入零 Backend 工作的标记 Task，并创建新的 `event_generation` 快照。标记本身在前驱终态成功后立即完成；前驱失败则失败。
- `Stream::waitEvent(event)` 在调用时捕获 Event 当前 generation 和对应标记 Task，在目标 Stream 插入 **依赖屏障**；后继任务隐式依赖该屏障。重复 record 不改变已创建屏障。
- Event 重复 record 创建新 generation；`Event::wait()` 等待调用时最新记录的快照，不追随之后的 record。
- 同 Stream `waitEvent` 若引用自己未来才能完成的未就绪任务，或者形成跨 Stream 依赖环，必须在依赖图插入前检测并拒绝 `InvalidArgument`。MVP 使用显式 DAG 验证，拒绝依赖环。
- 屏障任务与 Event 标记均不调用 Backend，但完整参与 TaskRegistry、配额、终态和依赖失败传播。
- Event 销毁不使已被其他 Stream 捕获的快照失效（Task/Marker 持有必要引用），Context 关闭除外。

## 7. Backend 内部契约

```cpp
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual Status initialize() = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual Result<BackendContextHandle> createContext(const ContextConfig&) = 0;
    virtual Status destroyContext(BackendContextHandle) = 0;
    virtual Result<BackendBuffer> allocateBuffer(BackendContextHandle, size_t, size_t) = 0;
    virtual Status releaseBuffer(BackendBufferHandle) = 0;
    virtual Status submit(BackendContextHandle, const BackendCommand&,
                          BackendTaskId* out_id) = 0;
    virtual Status pollCompletions(std::vector<BackendCompletion>&, int timeout_ms) = 0;
    virtual void shutdown() noexcept = 0;
};
```

**关键并发协议**：
- BackendTaskId 必须在 Backend 接受提交时确定，且在 Backend 生存期内唯一；完成消费时查找映射。
- 解决“完成先于映射登记”竞态：`submit` 内部必须先在 Backend 中登记可见 Task ID 和完成槽，再触发可完成的工作；Runtime 在 `submit` 返回后登记 Runtime/Backend 映射，并由 CompletionDispatcher 在必要时暂存早到的完成事件，直到映射可见；对未登记 ID 不能默默丢弃。
- Completion 记录包含 BackendTaskId、终态、错误码及时间戳；同一 ID 最多产生一次终态。
- Backend 能力查询不可失真；Kernel Backend 不得模拟 CPU 专属命令。
- `pollCompletions` 必须可被 shutdown 安全唤醒/退出，不得无限阻塞造成析构死锁。
- Backend 缺失返回 `BackendUnavailable`，不得自动切换到其他 Device。

## 8. Buffer、MemoryPool 与映射

- Buffer 对象记录 size、alignment、所属 Context、Backend Handle、映射引用计数、任务引用；不要公开所谓“device physical address”。
- `map()` 返回 Host 可访问范围；同一 Buffer 重复 map 计数；`unmap()` 配对；不允许 Buffer 越界。
- KernelBackend 从 Driver ALLOC_BUFFER 获取 `mmap_offset`，用 `mmap(fd,...,offset)` 创建映射；`munmap` 仅解除映射，是否释放内核内存取决于 Handle/Job/VMA 引用。
- Context MemoryPool 先按 size/alignment 分桶复用已不被任务访问的 Buffer；MVP 默认关闭池化，M4 开启并提供 `trim()`，测试需对照池化前后分配延迟。
- `syncToDevice/syncFromDevice`：CPU Backend 为无动作；Kernel 软件共享内存路径为 no-op，不得宣称进行 DMA 传输或设备缓存同步。

## 9. 线程模型与统计

- RuntimeScheduler：每逻辑 Device 一个线程，选择 Ready Stream，提交 Backend。
- CompletionDispatcher：每 Device 一个线程，消费 Backend Completion，推进 Task 状态和依赖。
- CpuBackend Worker 默认 `min(4, hardware_concurrency)` 且至少 1；Kernel Driver 通过 `poll/read` 消费完成。
- 任务统计保留 `accepted_ns`、`ready_ns`、`backend_submit_ns`、`backend_complete_ns`、`terminal_ns`，所有时间采用 `steady_clock`。
- 对外日志至少含 TaskToken、stream_id、context_id、opcode、state、result、Backend 类型；禁止把私有内核指针输出到用户日志。

## 10. 错误码映射与关闭返回值

| 场景 | StatusCode |
|---|---|
| 无效参数、offset 越界 | InvalidArgument |
| 无效/过期 Handle | InvalidHandle |
| 对象属于其他 Context | WrongContext |
| Backend 不支持 Opcode | Unsupported |
| Runtime 队列满 | QueueFull |
| poll/wait 超时 | Timeout |
| Context/Stream Closing/Closed | Closed |
| 同时关闭/初始化冲突 | Busy |
| 前驱失败 | DependencyFailed |
| 尚未派发时被关闭取消 | Canceled |
| 驱动不存在/连接断开 | BackendUnavailable |
| IOCTL/映射系统调用失败 | IoError（附 errno） |
| Buffer 分配失败 | OutOfMemory |

关闭协议：对象一旦 Closing 就不再接受新操作；正常 Close 等待并清理后返回 Ok；如果 Backend 无法确认工作停止，返回 BackendUnavailable，并保持安全所需的资源引用，不得返回 Ok 伪装完全回收。

## 11. 实现阶段与验收

| ID | 验收条目 | 预期 |
|---|---|---|
| RT-01 | 两类 Device 发现与 Capability | CPU 必有，Kernel 缺失时可解释 |
| RT-02 | 多 Context / Stream / Event 创建和关闭 | 无跨 Context 句柄泄漏 |
| RT-03 | NOP/DELAY 双 Backend | submit 非等待，wait 最终成功 |
| RT-04 | CPU FILL/COPY/VECTOR_ADD | 结果正确、边界检查正确 |
| RT-05 | Stream 顺序与跨 Stream Event | 依赖 DAG 可重现，失败传播 |
| RT-06 | Event 重复 record / 早到完成 | 无丢失/错代际 |
| RT-07 | QueueFull、Timeout、Close 竞态 | 无任务丢失、无双终态 |
| RT-08 | Buffer Map/Unmap 与异步引用 | 无提前释放 |
| RT-09 | 线程和错误边界 | TSan/ASan 检查通过（用户态） |

测试详细命令、工作负载和结果格式见 [TEST_AND_BENCHMARK.md](./TEST_AND_BENCHMARK.md)。
# MiniAccel 测试与性能评估规范

> 文档编号：SPEC-TEST-001　版本：v1.0.1　状态：**正式基线**  
> 依赖：[ARCHITECTURE.md](./ARCHITECTURE.md) · [RUNTIME_SPEC.md](./RUNTIME_SPEC.md) · [DRIVER_SPEC.md](./DRIVER_SPEC.md) · [SCHEDULER_SPEC.md](./SCHEDULER_SPEC.md)  
> 说明：本文件定义**必须实现的测试与验收口径**；文中命令是 Codex 完成构建系统后应能执行的接口，不表示目前已有代码或测试结果。

## 1. 评价原则

- 先验证正确性与生命周期，再评估性能。只有满足测试门禁，Benchmark 数据才有意义。
- CPU Backend 的 FILL/COPY/VECTOR_ADD 是**真实软件计算**；Kernel Backend 的 NOP/DELAY 是**软件模拟设备任务**。不得声称验证了真实 NPU 性能、DMA 带宽或硬件 IRQ。
- 每项结果必须包含：代码 Git Commit、Spec 版本、OS/kernel、编译器、CPU 核数、VM 配置、Backend、配置参数、工作负载、预热次数、采样次数、原始日志。
- 不允许将预期结果当作实测值；不允许凭推测填写吞吐、P95/P99、泄漏统计。
- 用户态可用 ASan/UBSan/TSan；内核态在 VM 中使用 dmesg、KASAN/LOCKDEP（如可用）、kmemleak（如可用）及并发压力测试。TSan 不用于内核模块。
- 性能比较必须显式记录操作语义、任务容量和并发度，避免混合不同 Benchmark 口径。

## 2. 推荐测试布局

```text
tests/
├── unit/
│   ├── test_status.cpp
│   ├── test_task_state.cpp
│   ├── test_bounded_queue.cpp
│   ├── test_stream_event.cpp
│   ├── test_buffer.cpp
│   └── test_backend_contract.cpp
├── integration/
│   ├── test_cpu_backend.cpp
│   ├── test_kernel_uapi.cpp
│   ├── test_kernel_mmap.cpp
│   └── test_runtime_kernel.cpp
├── stress/
│   ├── stress_multithread.cpp
│   ├── stress_context_lifecycle.cpp
│   ├── stress_completion.cpp
│   └── stress_fault_injection.cpp
└── fixtures/
benchmarks/
├── bench_submission.cpp
├── bench_throughput.cpp
├── bench_memory_pool.cpp
└── README.md
scripts/
├── build_userspace.sh
├── build_driver.sh
├── run_unit.sh
├── run_integration.sh
├── run_stress.sh
└── run_benchmarks.sh
```

命名可按工程统一，但 **CMake/CTest 必须提供稳定可发现的测试入口**。Kernel 测试必须与 CPU 单元测试隔离，防止没有 root 权限或设备节点时污染纯用户态测试。

## 3. 环境与基线记录

每轮测试创建 `results/<UTC-date>/<commit>/environment.json` 和 `summary.md`。至少包括：

```json
{
  "git_commit": "actual_commit_sha",
  "spec_version": "v1.0",
  "os": "Ubuntu 24.04",
  "kernel": "uname -r",
  "compiler": "actual gcc/clang version",
  "cmake": "actual version",
  "cpu": "actual CPU model",
  "logical_cpus": 0,
  "vm_memory_gib": 0,
  "backend": "cpu|kernel",
  "max_pending_tasks": 256,
  "max_inflight": 16,
  "kernel_device_capacity": 256,
  "worker_count": 0,
  "build_type": "Release|Debug",
  "asan": false,
  "tsan": false
}
```

上面的字符串是**模板占位符**，不得作为实际结果入库。测试应自动采集环境真实值，缺失字段标记 `unknown` 并解释原因。

推荐：
- 用户态 Debug + sanitizers 跑正确性；Release -O2 跑性能。
- Kernel 只在单独 VM 快照中加载，卸载后检查 dmesg。
- 编译用户态 `C++17`，编译驱动匹配当前运行内核 `/lib/modules/$(uname -r)/build`。

## 4. M0：纯用户态基础测试

| ID | 内容 | 必须断言 |
|---|---|---|
| UT-001 | Status/Result | 错误码可传播、错误时不可访问 value |
| UT-002 | TaskToken 唯一性 | 同 Context 序号递增，Context/代际区分，溢出拒绝 |
| UT-003 | Task 状态机 | 所有非法迁移拒绝；终态不可再次进入 |
| UT-004 | BoundedQueue | 多生产者多消费者，容量不超限，关闭正确唤醒 |
| UT-005 | 线程池异常 | 任意任务异常不会杀死 worker，任务 FAILED |
| UT-006 | CPU NOP/DELAY | 正确状态、0/1000ms 边界、超界同步拒绝 |
| UT-007 | CPU FILL | 各字节等于填充值，边界检查 |
| UT-008 | CPU COPY | 正确复制与指定重叠时 memmove 语义 |
| UT-009 | CPU VECTOR_ADD | 结果与参考数组比较，误差符合 §10 |
| UT-010 | Backend 无效提交与准入拒绝 | 不支持 Opcode、非法/跨 Context Handle、越界和容量不足均同步拒绝；不接受 Backend Task、不分配 BackendTaskId、不消耗 Backend inflight/Completion 槽位；输出 ID 不被改写，后续合法提交仍可成功 |

**DEC-G0-001 阶段裁决（2026-11-14）**：UT-002 仅验证给定 Context ID、generation 的 `TokenAllocator` 身份构造、零值拒绝、唯一性及溢出；UT-010 仅验证 Backend 层的同步拒绝和资源/ID 不被消耗。二者**不得代替**完整 Runtime 全局 Context ID 分配、`Stream::submit` 的 Sequence/TaskRegistry/pending 配额原子准入。这些原 UT-010 Runtime 级义务完整转移到 M3 的强制测试 RT-13，并在 M4 由 ST-004 加压复核；此裁决不改变 `RUNTIME_SPEC.md` §4 或 `SCHEDULER_SPEC.md` 的不变量。

M0 要求 `ctest` 全通过。TSan/ASan/UBSan 按工具支持分别构建，不能同时开启互不兼容的 Sanitizer。G0 的 PASS 只代表 M0 基础能力，必须报告适用测试的真实运行结果和环境限制，不得声称 M3 Runtime E2E 已完成。

## 5. M1～M2：Kernel Driver 功能和安全测试

测试环境先检查：
- `/dev/mini_accel` 存在且权限正确。
- `MA_UAPI_VERSION=1`、能力位为 NOP/DELAY。
- 驱动可卸载，测试进程正常 close fd。

| ID | 内容 | 验收 |
|---|---|---|
| KT-001 | insmod/open/DEVICE_QUERY/close/rmmod | 无 oops/WARN/泄漏 |
| KT-002 | CREATE/DESTROY_CONTEXT | 归属正确；无效 Handle → -ENOENT |
| KT-003 | ALLOC_BUFFER/mmap/munmap/FREE | 正确共享映射、缓冲区内容一致 |
| KT-004 | 映射保活 | FREE_BUFFER 后 VMA 仍有效直至 munmap |
| KT-005 | 非法 mmap offset/length/MAP_PRIVATE | 拒绝，Kernel 不越界 |
| KT-006 | 两次独立 open 跨 Client Handle | 交叉访问失败，无越权 |
| KT-007 | dup(fd) | 共享同一 Client 的资源表 |
| KT-008 | SUBMIT NOP/DELAY/WAIT/QUERY | 序号、状态、完成记录正确 |
| KT-009 | poll/read | 没记录阻塞/非阻塞语义，记录格式完整 |
| KT-010 | 错误码/保留字段 | -EINVAL/-EOPNOTSUPP/-ENOENT 等正确 |
| KT-011 | Context 有 Job 时 Destroy | -EBUSY，引用不会提前释放 |
| KT-012 | Context 有映射时 Destroy | -EBUSY，VMA 生命周期有效 |
| KT-013 | 队列/Context In-flight 满 | -EAGAIN，不占用序号 |
| KT-014 | fd close 与任务执行竞争 | worker 安全 drain，无 UAF |
| KT-015 | Completion FIFO 高压 | 无任务终态丢失，容量受控 |
| KT-016 | 100 次 load/unload 循环 | 无残留节点、内核告警 |
| KT-017 | IOCTL 模糊输入 | 任意 magic/size/reserved/非法字段，内核稳定 |

**M2 强制门禁**：至少 1000 次 Context 创建/销毁、10000 次 Buffer 创建/映射/释放；若被 VM 资源或时间限制，必须将未完成项标记为未通过而不是缩小口径后宣称完成。

## 6. M3：双 Backend E2E 测试

| ID | 内容 | 验收 |
|---|---|---|
| RT-01 | Device 枚举与 Capability | Kernel 缺失不影响 CPU Backend |
| RT-02 | Context/Stream/Event 创建与关闭 | 所有权/关闭状态正确 |
| RT-03 | 双 Backend NOP/DELAY | 异步 submit 与 wait 正确 |
| RT-04 | CPU FILL/COPY/VECTOR_ADD | 与独立参考实现一致 |
| RT-05 | 同 Stream 顺序 | N+1 不早于 N 成功结束 |
| RT-06 | 不同 Stream 无隐式顺序 | 不做跨 Stream 顺序断言 |
| RT-07 | Event record/waitEvent | 跨 Stream 依赖正确 |
| RT-08 | Event re-record | 旧 wait 仍绑定旧 generation |
| RT-09 | Buffer 异步保活 | 删除调用方 Handle 不造成 UAF |
| RT-10 | 早到 Completion | 10000 次快速 NOP 无 lost/duplicate |
| RT-11 | 错 Context Buffer/Event | WrongContext 同步拒绝 |
| RT-12 | Kernel 不支持计算 Opcode | Unsupported，禁止静默 CPU fallback |
| RT-13 | Runtime 全局身份与提交准入原子性 | Runtime 生命周期内 Context ID 单调、唯一且不复用；合法接受 TaskToken 的 generation/sequence 非零、sequence 单调且无因同步拒绝造成的缺口；非法参数、无效/跨 Context 对象、不支持 Opcode 或 Runtime pending QueueFull 均同步拒绝，不创建 Task/TaskRegistry 条目、不消耗 Sequence/配额；成功接受只登记一次；后续 Backend QueueFull 重试沿用原 TaskToken，最终终态和配额准确 |

**RT-13 强制场景**：至少包含多 Context 创建/关闭与身份非复用、同一 Stream 和不同 Stream 并发提交、非法命令/错误归属/越界/不支持命令、`max_pending_tasks` 满、同步拒绝后合法提交的 Sequence 连续性，以及已接受任务遭遇 Backend `QueueFull` 后保持原 Token/Registry 记录。检查失败路径无隐藏 Task、配额泄漏、重复分配或双终态；M4 的 ST-004 再执行高压矩阵。RT-13 仅在 M3 的完整 Runtime API、TaskRegistry 和 Scheduler 实现后验收，M0 的 BackendTaskId 测试不计作其通过。

E2E 不应因为 Kernel 没有 VECTOR_ADD 而跳过 Capabilities 验证；必须明确证明 Kernel 返回 Unsupported 且 CPU 计算正确。

## 7. M4：并发、背压与故障注入

### 7.1 压力矩阵

| ID | 配置 | 验收 |
|---|---|---|
| ST-001 | 1/2/4/8 Producer × 1/2/4/8 Stream，CPU | 无数据竞争/丢任务 |
| ST-002 | 1/2/4/8 Producer × 1/2/4/8 Stream，Kernel | 无内核告警、双终态 |
| ST-003 | 1/2/4/8 Context 并发 Submit | 资源按 Context 隔离 |
| ST-004 | max_pending=1/16/256 | QueueFull 可靠且无 Sequence gap |
| ST-005 | Kernel max_inflight=1/4/16 | EAGAIN 与配额归还准确 |
| ST-006 | 执行中 Stream.close/Context.close | 无悬空 Job/Buffer、按协议 drain |
| ST-007 | 大量 Event DAG/非法环 | 依赖正确，无死锁；有环同步拒绝 |
| ST-008 | 故障注入 Backend submit errno | FAILED 一次、依赖传播 |
| ST-009 | read/poll 中断/EINTR | 可恢复，完成不丢失 |
| ST-010 | Completion FIFO 溢出前后 | 无静默覆盖/重复投递 |
| ST-011 | 10000 次 NOP 与 1000 次 DELAY | 恰有对应可追踪终态 |
| ST-012 | 连续运行 30 分钟 | 内存和队列资源有界、无永久饥饿于有限负载 |

一次 Task 已被 Runtime 接受、但 Backend 暂时 QueueFull 时，不得创建第二个 Runtime TaskToken；要按同一 ID 安全重试。

### 7.2 负面/故障测试

必须覆盖：
- 非法 opcode、错误 Client/Context Handle、Buffer Offset 溢出、空 Buffer、边界长度零。
- Event 未 record 即 wait、重复 record、依赖环、不同 Context Event。
- Backend 不存在、fd 被关闭/驱动失效的错误传递。
- Kernel Driver 满队列 `-EAGAIN`、Runtime 队列满 `QueueFull`、等待超时但 Task 后续成功完成。
- Context Close 与 submit/wait/query/map 并发，资源释放、完成回调/通知次序。
- 用户态 Map/Unmap 和 FREE_BUFFER 并发，Driver VMA 引用不 UAF。
- Kernel Completion FIFO/Registry 在高压下容量受控且不会丢终态。
- 模拟 Worker 延迟和注入失败：使用测试构建中的显式 fault injection hook，默认 Release 构建禁止随意注入。

## 8. Benchmark 场景与定义

### 8.1 负载集合

| 负载 ID | Backend | Opcode | 目的 |
|---|---|---|---|
| BM-01 | CPU | NOP | Runtime 最小提交与完成开销 |
| BM-02 | Kernel | NOP | IOCTL→Workqueue→poll/read 开销 |
| BM-03 | CPU、Kernel | DELAY(1/10/100ms) | 背压和排队模拟 |
| BM-04 | CPU | COPY(4KiB/1MiB/8MiB) | 内存操作正确性/CPU 基线 |
| BM-05 | CPU | VECTOR_ADD(1Ki/1Mi elements) | 软件计算基线 |
| BM-06 | CPU、Kernel | 多 Producer/Stream/Context | 竞争与扩展性 |
| BM-07 | CPU、Kernel | Buffer 分配与释放 | MemoryPool 复用影响 |

所有负载明确 CPU/Kernel 不具有相同计算能力；BM-01 vs BM-02 比较的是**软件提交与完成路径**，不是 NPU 与 CPU 算力。

### 8.2 统一计时点

```text
accepted_ns  -> ready_ns -> backend_submit_ns
                                       |
                               backend_complete_ns
                                       |
                                 terminal_ns
```

定义：
- **排队前等待**：ready_ns - accepted_ns。
- **Scheduler 派发等待**：backend_submit_ns - ready_ns。
- **Backend 等待/执行时间**：backend_complete_ns - backend_submit_ns（含 Backend 排队）。
- **Runtime 完成收尾**：terminal_ns - backend_complete_ns。
- **端到端延迟**：terminal_ns - accepted_ns（包含所有排队和模拟处理）。
- **提交调用延迟**：submit 返回时间 - submit 入口时间（不等于端到端）。
- **吞吐**：成功终态任务数 / （最后任务终态时间 - 第一任务接受时间），单位 task/s。
- **失败率**：FAILED/CANCELED/总接受任务数；必须独立报告 QueueFull 的同步拒绝次数。

跨用户态与内核态时钟不同，不得直接相减未校准的原始时戳。主要 Benchmark 以用户态 `steady_clock` 为准，Kernel `ktime_get_ns` 作为独立的驱动内部 trace，不直接参与端到端公式。

### 8.3 预热、采样、统计

- 每组至少 **5 次独立运行**；每次至少 1000 个完成任务（DELAY(100ms) 单独可降为 100 次，但必须记录例外和原因）。
- Warmup 200 任务或持续 1s，独立于正式采样；预热前后不得复用未终态任务干扰采样。
- 延迟报告 Mean、Median、P95、P99、Min、Max；计算百分位必须固定排序/插值方法并在工具中公布。
- 吞吐报告 5 次运行的平均及标准差；保留每轮原始结果，不只保存最佳值。
- 队列容量和 Worker 数固定对照，CPU 使用同一机器条件，Kernel 使用同一 VM 内核和模块。
- 功能错误/资源泄漏或缺失采样时不可仅展示性能，须标记 Invalid Run。

## 9. 精度与正确性标准

- CPU FILL/COPY：逐字节一致；含边界和重叠案例。
- CPU VECTOR_ADD：输入 float32，参考计算每元素 A+B；有限正常输入允许 `abs(actual-ref) <= 1e-6 + 1e-5 * abs(ref)`。NaN/Inf 另设边界测试，不纳入普通浮点误差比较。
- NOP：不修改任何 Buffer；DELAY：不修改 Buffer，验证耗时有序且完成终态正确；不设置虚假微秒级精度承诺（VM 调度抖动可能较大）。
- 任务依赖：从 trace 核实 Event 后继的 `backend_submit_ns` 不早于依赖项 `terminal_ns`；失败路径不执行后继 Backend 命令。

## 10. 必须落盘的产物

```text
results/<date>/<commit>/
├── environment.json
├── unit-tests.xml
├── integration-tests.xml
├── stress-tests.json
├── benchmark-raw.csv
├── benchmark-summary.json
├── dmesg-after.txt
├── kernel-module-info.txt
└── summary.md
```

CSV 至少包含：run_id、backend、opcode、context_count、stream_count、producer_count、queue_capacity、worker_count、accepted_ns、ready_ns、backend_submit_ns、backend_complete_ns、terminal_ns、terminal_state、error_code、task_token。

不允许写入内核指针、用户敏感数据或未初始化内存。生成日志要给出固定列头、单位、数据源和采集方法。

## 11. 阶段验收门禁与 Codex 回报格式

| Gate | 前提 | 通过标准 |
|---|---|---|
| G0 | M0 用户态基础 | C++17 编译、UT-001～UT-010 全通过、Sanitizer 可运行；UT-010 限定为 Backend 准入，须附实际测试证据 |
| G1 | M1 Kernel 基础 | 驱动 load/open/query/close/unload，KT-001/010 |
| G2 | M2 资源与任务 | KT-002～017，驱动高压/资源回收 |
| G3 | M3 双 Backend | RT-01～13 全通过，能力差异透明；其中 RT-13 强制验证 Runtime 全局身份与原子准入 |
| G4 | M4 调度可靠性 | ST-001～12，正确关闭/背压/依赖 |
| G5 | M5 性能与材料 | BM-01～07 数据、日志、分析可复现 |

**每次 Codex 交付必须报告**：
1. 读取的 Spec 版本和实现条款。
2. 修改的源码与测试文件。
3. 逐项对应的测试 ID。
4. 真实执行的编译/测试命令与退出码。
5. 已通过、失败、未运行、受环境限制的测试分别列明。
6. 潜在 UAF、死锁、double free、ABI 变更、兼容性风险。
7. 尚未解决的问题及下一步，不得把“代码已生成”表述为“功能已验证”。

## 12. 首轮可执行命令约定

Codex 建立脚本/构建系统后，应使以下命令在相应环境可工作：

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j
ctest --test-dir build/debug --output-on-failure

# 内核模块：在 Ubuntu VM，匹配内核 headers
make -C /lib/modules/$(uname -r)/build M=$PWD/driver modules
sudo insmod driver/mini_accel.ko
ls -l /dev/mini_accel
sudo dmesg --level=err,warn | tail -n 100
sudo rmmod mini_accel
```

也应提供 `scripts/run_integration.sh`、`scripts/run_stress.sh` 和 `scripts/run_benchmarks.sh`，能自动收集真实结果且明确报告缺失驱动或权限不足的原因。当前仓库尚未有这些脚本；本节是后续实现验收目标。

## 13. 最终验收定义

MiniAccel v1.0 通过验收，必须同时满足：
- 双 Backend 公共接口及能力查询正确。
- CPU 计算正确；Kernel 完整 IOCTL→Job→Workqueue→Completion 端到端成功。
- 无跨 Client/Context Handle 越权；异步资源不提前释放。
- Event 快照、依赖失败、快速完成竞态、超时与关闭符合 Spec。
- CPU 用户态 Sanitizer 检查及 VM Driver 压测有实际执行记录。
- 无未经解释的内核 oops/WARN、用户态 UAF、死锁、双终态或未处理资源泄漏。
- Benchmark 和环境可复现，论文/简历材料明确称为**软件模拟 Runtime/Driver**而非真实 NPU 性能。
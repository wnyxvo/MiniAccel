# MiniAccel M0：用户态基础设施

本仓库的正式设计基线是 [Spec v1.0](docs/specs/README.md)。当前代码仅覆盖 M0：状态与结果、任务状态基础、有界队列、线程池和 CPU Backend；尚无完整 Runtime、Scheduler 或内核驱动。

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j
ctest --test-dir build/debug --output-on-failure

cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DMINIACCEL_SANITIZER=address-undefined
cmake --build build/asan -j
ctest --test-dir build/asan --output-on-failure

cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DMINIACCEL_SANITIZER=thread
cmake --build build/tsan -j
ctest --test-dir build/tsan --output-on-failure
```

部分容器的内存布局与 TSan 不兼容，支持时可尝试 `setarch "$(uname -m)" -R ctest --test-dir build/tsan --output-on-failure`；若 LeakSanitizer 被容器的 ptrace 策略限制，需在支持的环境重新运行完整 ASan 检测。仅关闭泄漏检测的 ASan 运行不能证明无泄漏。

内部 Backend 接口位于 `src/backends/backend.hpp`，并非公开 Runtime API。成功提交之前验证所有参数、Context、Buffer 归属和额度；每次接受预留一个完成槽，槽的总数有界，记录被 `pollCompletions` 消费时归还。任务完成后归还 Context inflight 配额；任务持有 Buffer 强引用直到完成，不要求调用方保留句柄。`shutdown()` 拒绝新工作并排空已接受任务，完成记录仍可消费，排空后 poll 返回 Closed；后续 M3 需要登记 Runtime Task 并处理 submit 返回前的早到完成。

内部队列 Close 拒绝新入队并唤醒等待者，已接受元素继续排空。线程池执行异常时使用入队前准备的错误状态调用失败处理函数。若处理函数抛异常，Worker 捕获该异常、增加可通过 `failureCallbackErrors()` 观察的计数并继续处理已接受任务；线程池不重试该回调，也不假定受影响任务已进入终态，调用方必须检查该故障并负责恢复或报告。CPU Backend 的处理函数为 `noexcept`，将预留槽转换为 FAILED Completion；其失败路径不依赖执行时分配诊断字符串。不能从 Worker 调用同步 `shutdown()`。CPU 映射需配对 unmap，Backend 对象及映射期间必须由调用者保活；并发访问同一 Buffer 的数据由调用方同步。Debug 构建提供内部单次故障注入以测试失败 Completion，Release 不提供。

内部能力位图使用 `Opcode` 数值减一作为位索引：Nop、Delay、Fill、Copy、VectorAdd 分别对应 bit 0～4。`opcodeCapabilityBit()` 和 `BackendCapabilities::supports()` 对未知值返回 0/false；提交也使用相同能力检查。该约定仅规定内部 Backend 表示，不改变 Driver UAPI。

TaskToken 分配器是 Context 范围内的内部基础类型；Runtime 全局 Context ID 分配、提交原子性、Task Registry 和 Event/Stream 依赖均属于后续阶段，不能从 M0 测试推断已实现。

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

内部队列 Close 拒绝新入队并唤醒等待者，已接受元素继续排空。线程池失败处理函数由内部调用方提供，必须不抛异常，且不能从其 Worker 调用同步 `shutdown()`。CPU 映射需配对 unmap，Backend 对象及映射期间必须由调用者保活；并发访问同一 Buffer 的数据由调用方同步。Debug 构建提供内部单次故障注入以测试失败 Completion，Release 不提供。

TaskToken 分配器是 Context 范围内的内部基础类型；Runtime 全局 Context ID 分配、提交原子性、Task Registry 和 Event/Stream 依赖均属于后续阶段，不能从 M0 测试推断已实现。

# PR #1 第一轮修复验证记录

- 测试源码提交：`a97cc9612237d64940182b0dc6ff7e7429be8217`。本目录在后续仅含验证记录的提交中加入；日志对应上述源码树，不表示独立机器或 CI 已复现。
- Spec：v1.0；未更改正式 Spec。实际环境及配置见 [environment.json](environment.json)。VM 身份和内存无法独立确认，标为 unknown。
- CPU 后端是软件执行；M0 不包含 Kernel Driver，Kernel VM 测试未运行，不适用本轮。

| 配置 | 配置/构建/CTest 退出码 | CTest | 原始记录 |
|---|---|---|---|
| Debug | 0/0/0 | 9/9 PASS | [log](debug.log), [JUnit](debug.xml) |
| Release | 0/0/0 | 9/9 PASS | [log](release.log), [JUnit](release.xml) |
| ASan + UBSan + LSan | 0/0/0 | 9/9 PASS | [log](asan-ubsan.log), [JUnit](asan-ubsan.xml) |
| TSan | 0/0/0 | 9/9 PASS | [log](tsan.log), [JUnit](tsan.xml) |

每份 log 都保留真实命令、编译输出、CTest 输出和退出码，仅去除了行尾空白和文件末尾多余空行。ASan 与 UBSan 合并构建，运行时未关闭 LSan；因默认沙箱限制，在沙箱外运行。默认沙箱单独运行 ASan 测试时，LSan 返回退出码 1，并报告 ptrace 不兼容，见 [原始输出](asan-sandbox-default.log)。TSan 默认地址布局单独运行时退出码 66，报告 `unexpected memory mapping`，见 [原始输出](tsan-default.log)；完整 TSan 使用 `setarch x86_64 -R`，以日志中的实际命令运行。通过的结果不等于已在默认受限环境通过，也不能证明所有竞态和泄漏场景均已覆盖。

## UT 与 G0

| ID | 入口 | 本轮结果与覆盖范围 |
|---|---|---|
| UT-001 | `status` | PASS：错误传播和错误时不可访问值 |
| UT-002 | `task_state` | 局部 PASS：给定 Context/代际的序号、唯一性和溢出；全局 Context ID 分配未实现 |
| UT-003 | `task_state` | PASS：状态对迁移、并发唯一终态与超时 |
| UT-004 | `bounded_queue` | PASS：MPMC、容量、关闭排空 |
| UT-005 | `thread_pool` | PASS：工作异常、失败回调异常、计数可观察和 Worker 后续运行；CPU 执行失败完成由 Debug UT-010 覆盖 |
| UT-006 | `UT-006` | PASS：NOP/DELAY 与全部能力位、未知 Opcode |
| UT-007 | `UT-007` | PASS：FILL |
| UT-008 | `UT-008` | PASS：COPY 和重叠 |
| UT-009 | `UT-009` | PASS：VECTOR_ADD |
| UT-010 | `UT-010` | Backend 部分 PASS：非法命令不消耗 BackendTaskId、配额和 Completion；Runtime Sequence 原子准入未实现，端到端测试 NOT_RUN |

**G0：BLOCKED。** 仅有的 TokenAllocator 要求调用者提供 Context ID，局部测试不能证明 Runtime 生命周期内全局 Context ID 唯一；BackendTaskId 属于 Backend 身份，不是 Runtime TaskToken。当前没有 Stream::submit、Runtime Task Registry 及其完整准入事务，因此不能验证 Runtime 无效提交、配额与 Sequence 的原子性。不得以局部测试替代上述端到端要求，也未自行修改 Spec 或 G0。

线程池失败回调抛异常时，只递增可查询的 `failureCallbackErrors()` 并继续处理队列，不推断该任务已 FAILED，也不重试可能产生部分副作用的回调。通用调用方需要监测此计数并处理任务结果未知的故障；CPU Backend 使用 `noexcept` 回调和入队前准备的错误状态，错误执行会产生 FAILED Completion。Runtime Scheduler、CompletionDispatcher、身份映射和完整故障恢复仍属于后续阶段。

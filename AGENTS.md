# MiniAccel Codex 协作说明

本文规定在本仓库开发、修复、测试或审查代码时需要遵守的文档驱动流程。

## 1. 权威依据

**开发前必须阅读**：
- `docs/specs/README.md`
- `docs/specs/ARCHITECTURE.md`
- `docs/specs/RUNTIME_SPEC.md`
- `docs/specs/DRIVER_SPEC.md`
- `docs/specs/SCHEDULER_SPEC.md`
- `docs/specs/TEST_AND_BENCHMARK.md`

研究资料保存在 `docs/research/`，可帮助理解 Ethos-U、AMD XDNA、XRT 的上游机制；不允许以研究结论覆盖正式 Spec。代码注释、变量名、公开 API 可保留英文；文档、审查报告和提交说明优先中文。

## 2. 开发边界

- 用户态使用 C++17；Kernel Driver 使用 C + Kbuild。
- MVP 为 CPU Simulator + Character Driver 双 Backend，Kernel Workqueue 仅模拟 NOP/DELAY。
- 不得无授权引入 DRM_ACCEL、GEM、真实 DMA、MMIO、Firmware、复杂用户态 Doorbell。
- Public Runtime API 不暴露 UAPI 私有结构，Kernel Driver 不包含 Runtime C++ 类型。
- 保证 Stream 顺序、Event 代际、任务唯一终态、异步 Buffer 引用、Context 关闭安全、Completion 不丢失。
- **不得擅自修改正式 Spec 来让现有代码“通过”**。发现 Spec 不一致或无法实现时，明确报告并提出设计变更方案。

## 3. 代码提交规范

每次任务先列出：
1. 本次目标与实施阶段（M0～M5）。
2. 受影响 Spec 条款和测试 ID。
3. 计划修改文件与关键边界。
4. 潜在并发/资源/ABI 风险。

提交时附：
1. 实际修改文件和核心逻辑。
2. 编译/测试命令及完整成功/失败结果摘要。
3. 未运行、环境受限、已知问题。
4. 用户态 Sanitizer 与 Kernel VM 测试情况（适用时）。
5. 不能验证时不得声称已验证。

建议按照独立可回滚模块拆分提交，不一次性实现所有阶段。

## 4. 安全与质量

- 不把用户态指针或物理地址当作可信 Kernel Handle。
- 严格校验 ioctl 参数、reserved 字段、长度、归属和状态。
- 完成只能入终态一次；失败路径不得泄漏额度或重复释放资源。
- 不在持有自旋锁时睡眠；避免持有全局锁调用 Backend、等待任务或执行回调。
- Driver 在 VM 中测试；未验证的内核操作不得描述为安全。
- Benchmark 数据必须真实测量并注明是模拟环境，不得杜撰性能数字。

## 5. 文档职责

`docs/specs/` 规定应当怎样实现；`docs/research/` 记录借鉴来源与分析。实现与正式规范冲突时先停止受影响功能的开发并要求审查裁决，而不是无声更改测试或接口。
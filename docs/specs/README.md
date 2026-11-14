# MiniAccel 正式设计文档索引

版本：v1.0.1（门禁归属裁决补丁；架构/Runtime/Driver/Scheduler 正文仍为 v1.0）｜状态：**正式基线**｜语言：中文

本目录是 MiniAccel 运行时与 Linux Driver 原型的规范性设计依据。开发、重构、测试和代码审查均应按本文档索引阅读五份规范。上游源码研究保存在 [docs/research](../research/README.md)，不直接构成实现要求。

## 1. 五份规范

| 文档 | 主题 | 开发对应 |
|---|---|---|
| [ARCHITECTURE.md](./ARCHITECTURE.md) | 系统边界、模块职责、所有权、跨层契约 | 项目通用前提 |
| [RUNTIME_SPEC.md](./RUNTIME_SPEC.md) | C++17 Runtime API、Context/Stream/Event/Buffer | Runtime SDK |
| [DRIVER_SPEC.md](./DRIVER_SPEC.md) | Character Driver、UAPI、mmap、Job、poll/read | Kernel Driver |
| [SCHEDULER_SPEC.md](./SCHEDULER_SPEC.md) | 双层调度、任务状态、Event DAG、背压与完成 | Scheduler / Completion |
| [TEST_AND_BENCHMARK.md](./TEST_AND_BENCHMARK.md) | 测试矩阵、门禁、性能统计与真实结果 | 测试与评测 |

**文档阅读顺序：** Architecture → Runtime → Driver → Scheduler → Test。执行具体任务时可重点重读目标模块文档，但不得忽略跨文档公共不变量。

## 2. 本次设计裁决摘要

- 用户态 C++17 Runtime，CPU Simulator 与 KernelDriverBackend 双后端。
- Kernel 采用 Character Device，内核 Workqueue 模拟 NOP/DELAY；DRM_ACCEL 是后续方向。
- Runtime Stream 内按序；跨 Stream 仅通过 Event 显式建立依赖。
- TaskToken 为 `{context_id,generation,sequence}`；Kernel 使用独立 Handle/Sequence。
- Runtime 先登记 Task 再 Backend 提交；完成最多生效一次。
- Context/Buffer/Job 具有明确的引用和关闭协议，等待超时不等于任务取消。
- Kernel 用有界 FIFO + Client Completion FIFO 预留槽位，避免静默丢完成。
- 性能指标必须标注 **软件模拟** 和真实硬件不存在的边界。

## 3. Codex 工作流程

1. 明确本次任务 ID 与所覆盖的 Spec 条款、测试编号。
2. 阅读本目录五份 Spec、`AGENTS.md` 和相关 Research 文档。
3. 提出代码修改计划，标明受影响接口、生命周期、并发和异常路径。
4. 只实现本次明确指定的里程碑/模块，配套编写测试。
5. 本地执行可运行的构建/测试命令，报告真实输出与退出码。
6. 不得将未运行测试写成通过；环境不可用时明确标记未验证。
7. 遇到 Spec 冲突或必须修改契约时，停止受影响实现并报告变更需求。
8. 提交源码和测试后，由 ChatGPT 按对应条款审查，审查发现问题由 Codex 修复并重新验证。

## 4. 分阶段实施

| 阶段 | Codex 目标 | Gate |
|---|---|---|
| M0 | User-space Status/Result/TaskState/BoundedQueue/CpuBackend 基础 | G0 |
| M1 | Character Device + Query/UAPI + Workqueue 基础 | G1 |
| M2 | Client/Context/Buffer/mmap/Job/Completion | G2 |
| M3 | 统一 Runtime API、双 Backend、Stream/Event | G3 |
| M4 | 两级调度、背压、生命周期、故障注入 | G4 |
| M5 | Benchmark、结果归档、文档和面试素材 | G5 |

M0 之前不应先实现完整 DRM_ACCEL 或 Firmware。每个 Gate 的测试编号和验收标准见 TEST_AND_BENCHMARK。

### 4.1 门禁裁决 DEC-G0-001（2026-11-14）

**冲突来源**：原 `TEST_AND_BENCHMARK.md` §4 的 UT-010 要求 Runtime 无效提交“不创建 Task、不消耗 Sequence / 配额”，但本索引 §4 将 `Stream::submit`、Runtime TaskRegistry 与完整准入事务归入 M3。M0 的 `TokenAllocator` 与 `CpuBackend` 无法独立证明 Runtime 级事务正确。

**正式裁决（随本变更 PR 合并后生效）**：

| 范围 | 测试归属 | 不可删减的验收义务 |
|---|---|---|
| M0 / G0 | UT-001～UT-010；UT-010 明确为 Backend 无效提交的同步拒绝、BackendTaskId/额度/Completion 保全 | C++17 构建、M0 基础测试及适用的 Sanitizer 验证；不得宣称 Runtime 全局身份或 `Stream::submit` 已实现 |
| M3 / G3 | 新增强制 RT-13：Runtime 全局身份与提交准入原子性；原 RT-01～RT-12 保留 | 全局 Context ID 单调唯一、TaskToken 正确分配、Runtime 拒绝不创建任务且不耗 Sequence/pending 额度、已接受任务在 Backend 背压后身份不变 |
| M4 / G4 | 保留 ST-004 和其余并发/背压测试 | 对 M3 原子性保证进行高并发、容量与故障路径压力验证 |

**边界与证据规则**：原 UT-010 的 Runtime 级正确性要求是**移交到 RT-13，不是删除或降级为可选项**；`RUNTIME_SPEC.md` §4 与 `SCHEDULER_SPEC.md` 的身份、提交、配额和唯一终态不变量保持不变。BackendTaskId 不等于 Runtime TaskToken；M0 通过不能作为 M3 完成证明。阶段门禁只有在本裁决合并成为正式基线且相应代码/测试证据核实后才能标记 PASS；修改文档本身不意味着 G0 自动通过。

**版本与影响**：本索引和 `TEST_AND_BENCHMARK.md` 更新为 v1.0.1；`ARCHITECTURE.md`、`RUNTIME_SPEC.md`、`DRIVER_SPEC.md`、`SCHEDULER_SPEC.md` 保持 v1.0。未更改公共 C++ API、Driver UAPI、执行协议、资源所有权或最终测试总要求。

## 5. 变更治理

本目录当前版本为正式基线，但允许审查后调整。所有改动都要在 PR/Commit 中记录原因、影响范围、受影响测试与版本。Research 中的 ADOPT/ADAPT/DEFER/REJECT 只是研究建议，必须经过 Spec 裁决才构成正式要求。

**文档和源码状态须区分：本目录的实现描述是目标规范，不代表仓库中的 C++/Driver 代码已经存在或测试通过。**
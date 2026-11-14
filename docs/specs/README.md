# MiniAccel 正式设计文档索引

版本：v1.0｜状态：**正式基线**｜语言：中文

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

## 5. 变更治理

本目录当前版本为正式基线，但允许审查后调整。所有改动都要在 PR/Commit 中记录原因、影响范围、受影响测试与版本。Research 中的 ADOPT/ADAPT/DEFER/REJECT 只是研究建议，必须经过 Spec 裁决才构成正式要求。

**文档和源码状态须区分：本目录的实现描述是目标规范，不代表仓库中的 C++/Driver 代码已经存在或测试通过。**
# MiniAccel 源码研究

本目录保存 MiniAccel Runtime / Linux Driver 设计所依赖的上游源码研究、执行链路分析和架构对照。

## 1. 研究目标

围绕以下完整异构计算软件链路建立可复核的源码认知：

```text
Application
    |
    v
User-space Runtime
    |
    v
Backend / SHIM
    |
    v
Linux Kernel Driver
    |
    v
Hardware / Firmware / Virtual Accelerator
```

重点研究 Device、Client、Context、Buffer、Command、Task/Job、Queue、Scheduler、Fence、Event 和 Completion 的职责、所有权、生命周期及跨用户态/内核态的交互关系。

## 2. 核心研究对象与阅读顺序

| 顺序 | 项目 | 主要研究层次 | 重点问题 |
|---|---|---|---|
| 1 | [Arm Ethos-U Linux Driver](./ethos-u/README.md) | Linux Kernel Driver | IOCTL、GEM Buffer、Job、DRM Scheduler、Fence、IRQ |
| 2 | [AMD XDNA Driver](./amd-xdna/README.md) | Linux Kernel Driver + User-space SHIM | Client、Hardware Context、KMQ/UMQ、Job Semaphore、Sequence、Mailbox |
| 3 | [Xilinx XRT](./xrt/README.md) | User-space Runtime | Device、Context、Buffer、SHIM、Hardware Queue、Command Manager |

三个项目覆盖的软件层次不同，不能把它们理解为同层次竞品：
- Ethos-U 用于建立 NPU Kernel Driver 的基础认知。
- AMD XDNA 用于研究现代 NPU 的 Context、任务准入、Firmware 通信及 KMQ/UMQ。
- XRT 用于研究完整 Runtime API 如何通过 SHIM 对接不同设备后端。

## 3. Linux DRM_ACCEL 背景

[DRM_ACCEL 框架研究](./DRM_ACCEL_FRAMEWORK.md) 解释 Linux Compute Acceleration Framework 的基础、相关上游驱动，以及 MiniAccel 从 Character Driver MVP 过渡到 DRM_ACCEL 的建议路径。

## 4. 目录结构

```text
docs/research/
├── README.md
├── SOURCE_INDEX.md
├── DRM_ACCEL_FRAMEWORK.md
├── ethos-u/
│   └── README.md
├── amd-xdna/
│   └── README.md
├── xrt/
│   └── README.md
└── comparison/
    ├── ARCHITECTURE_COMPARISON.md
    └── MINIACCEL_MAPPING.md
```

## 5. 研究文档的证据等级

每项研究结论应区分：

1. **源码事实**：能够定位到固定版本、具体文件、结构体或函数。
2. **架构解释**：根据源码调用关系、状态变化和数据流给出的解释。
3. **MiniAccel 设计建议**：结合 MiniAccel 的目标和 MVP 约束作出的设计判断。

不得把设计建议写成上游项目已经实现的事实；不得把不同项目中名称相似但语义不同的对象直接等同。对尚未核实的细节标记为“待核实”。

## 6. 与 MiniAccel Spec 的关系

`docs/research/` 是设计参考资料，不是规范性要求。

- `docs/specs/` 定义 MiniAccel 必须实现的行为、接口、约束和验收标准。
- `docs/research/` 解释上游项目如何实现相关机制，以及这些机制对 MiniAccel 有何启发。
- Research 与 Spec 不一致时，以当前 Spec 作为开发依据；若发现 Spec 有问题，应先提出设计变更，不得由 Codex 静默改写 Spec。
- ADOPT、ADAPT、DEFER、REJECT 仅表示研究阶段的建议。经设计裁决并写入 Spec 后，才成为项目正式约束。

## 7. 研究维护规则

1. 记录上游仓库、Commit SHA、文件路径和关键符号。
2. 代码链接尽量使用 Commit SHA 固定链接，不只使用会移动的 main/master。
3. 上游版本变化后，先复核调用链和结构体，再更新结论。
4. 不大段复制上游源码；以对象模型、调用链、状态机和机制解释为主，并遵守上游许可证。
5. 每个设计建议都应说明借鉴来源、MiniAccel 的简化以及与上游实现的差异。
6. 新增研究对象时，同时更新本目录索引和对照文档。

## 8. 当前研究范围边界

当前核心对照聚焦 Ethos-U、AMD XDNA 和 XRT。CANN、Intel NPU、ROCm/ROCR 是后续扩展阅读候选，不属于本轮三仓库源码对照的既定基线。

MiniAccel 首期以可控的软件原型为目标。Workqueue 模拟执行只能验证接口、调度、并发和完成通知，不代表实现了真实硬件 DMA、MMIO、IRQ 或 NPU 计算。
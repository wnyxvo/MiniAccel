# Linux DRM_ACCEL：Compute Acceleration Framework 研究

## 1. 研究定位

Linux Kernel 已为计算加速设备提供 DRM Compute Acceleration Framework，通常称为 DRM_ACCEL。它建立在 DRM 子系统之上，为计算加速器驱动提供设备模型和用户态交互基础。

官方文档：[Documentation/accel/introduction.rst](https://github.com/torvalds/linux/blob/97d73c8251226cf27cc6656b4c542869bb8c69fa/Documentation/accel/introduction.rst)。

本文件用于解释 MiniAccel 为什么需要研究 DRM_ACCEL，以及 Character Driver MVP 与 DRM_ACCEL 后续增强之间的取舍。这里的研究不意味着 MiniAccel 当前已经采用 DRM_ACCEL。

## 2. DRM_ACCEL 提供的基础

根据当前研究材料，重点关注：

- 使用 `DRIVER_COMPUTE_ACCEL` 标识计算加速设备。
- 使用 DRM GEM 管理 Buffer Object。
- 通过 IOCTL 暴露设备查询、Buffer 和任务提交等 UAPI。
- 计算加速设备节点使用 `/dev/accel/accel*`。
- 可结合 DRM 的 Buffer、同步和调度基础设施。

DRM_ACCEL 不是某个具体 NPU 的 Runtime，也不替代用户态 Device/Context/Stream/Event API。它提供的是 Linux Kernel Driver 层的公共基础。

## 3. 相关驱动研究入口

| 驱动 | Linux Kernel 源码目录 | 研究关注点 |
|---|---|---|
| AMD XDNA | `drivers/accel/amdxdna/` | PCIe NPU、GEM、Context、任务提交 |
| Intel NPU | `drivers/accel/ivpu/` | NPU Job、MMU Context、IPC、PM、GEM |
| Arm Ethos-U | `drivers/accel/ethosu/` | BO、命令流、Job、Scheduler、Fence、IRQ |
| Qualcomm QAIC | `drivers/accel/qaic/` | 加速设备驱动与用户态接口 |
| Habana Gaudi | `drivers/accel/habanalabs/` | 设备资源、任务和执行管理 |

目录位置说明：上述驱动位于 Linux Kernel 的加速器驱动代码树。具体文件和接口应以对应内核版本源码为准。

## 4. DRM_ACCEL 与 MiniAccel Character Driver 的取舍

| 维度 | Character Driver MVP | DRM_ACCEL |
|---|---|---|
| 设备接口 | 自定义 `/dev/mini_accel` | DRM accel 设备节点 |
| UAPI | 自定义 ioctl | DRM ioctl 体系 |
| Buffer | 自定义内核对象和 Handle | 可使用 DRM GEM |
| 同步 | Wait Queue / poll / Completion Token | 可结合 DMA Fence / Syncobj |
| 调度 | 自定义有界 FIFO / Workqueue | 可研究 DRM Scheduler 等机制 |
| 开发复杂度 | 较低，适合快速完成闭环 | 较高，需要理解 DRM、GEM、同步和内核版本接口 |
| 研究价值 | 验证 Runtime 与 Kernel 的端到端接口 | 更接近现代 Linux AI 加速器驱动模型 |

## 5. 对 MiniAccel 的建议

### 阶段一：完成可运行 MVP

先采用 Character Driver，打通：

```text
C++ Runtime SDK
       |
       v
ioctl / mmap / poll
       |
       v
Linux Character Driver
       |
       v
Client / Context / Buffer / Job
       |
       v
Bounded Queue + Workqueue
       |
       v
Completion / Wait Queue
```

这一阶段的目标是验证 UAPI、对象归属、资源生命周期、任务并发、超时和关闭清理，不追求复刻厂商驱动。

### 阶段二：评估 DRM_ACCEL 增强

在 Runtime 与 Kernel 对象边界稳定后，再评估：

- 将自定义设备模型迁移到 DRM_ACCEL。
- 使用 GEM 管理 Buffer Object。
- 研究 DRM Scheduler 与 DMA Fence。
- 引入 Syncobj 或其他标准同步机制。
- 对照 Ethos-U、AMD XDNA、Intel NPU 等驱动，验证设计差异。

### 设计约束

- Runtime API 不应直接暴露 Character Driver 或 DRM 私有结构。
- UAPI、Backend 和 Kernel Object 应保持清晰边界，便于后续替换。
- MVP 的 Workqueue 完成路径必须标记为软件模拟，不得描述为真实 DMA 或硬件 IRQ。
- 不要在首期同时实现完整 DRM Scheduler 和一套职责重复的自定义 Kernel Scheduler。

## 6. 研究结论

DRM_ACCEL 是 MiniAccel 后续提高 Linux 驱动工程真实性的重要方向，但不应成为首期 Runtime 原型的前置阻塞项。当前建议仍是：先用 Character Driver 验证完整软件闭环，再以 DRM_ACCEL、GEM、Fence/Syncobj 作为下一阶段增强和研究目标。

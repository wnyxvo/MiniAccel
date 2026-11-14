# Xilinx XRT Runtime 源码研究

## 1. 研究定位

XRT 是本次研究中最重要的用户态 Runtime 架构参考。重点研究统一 C++ API 如何管理 Device、Hardware Context、Buffer 和 Command，并通过 SHIM/平台适配层连接不同设备的执行队列和驱动接口。

仓库：[Xilinx/XRT](https://github.com/Xilinx/XRT)。固定研究提交见 [SOURCE_INDEX.md](../SOURCE_INDEX.md)。

特别注意：AMD XDNA 仓库中的 `src/shim/` 是 XRT Runtime 与 XDNA NPU 的适配实现；XRT 仓库中的 XOCL KDS 则是 FPGA 驱动执行路径。二者相关，但不是同一个 Kernel Driver。

## 2. 核心模块与类

| 文件 | 核心类/结构 | 职责 |
|---|---|---|
| `core/common/ishim.h` | `ishim` | Runtime 与平台 SHIM 的接口抽象 |
| `core/common/api/xrt_device.cpp` | `xrt::device` | 用户可见 Device API |
| `core/common/api/xrt_hw_context.cpp` | `hw_context_impl` | Hardware Context 生命周期 |
| `core/common/api/xrt_bo.cpp` | `bo_impl` | Buffer Object 抽象 |
| `core/common/api/hw_queue.cpp` | `hw_queue_impl` | 执行队列抽象 |
| `core/common/api/hw_queue.cpp` | `qds_device` / `kds_device` | 不同队列路径适配 |
| `core/common/api/hw_queue.cpp` | `command_manager` | 用户态 Managed Execution 完成管理 |
| `core/common/api/xrt_kernel.cpp` | `kernel_command` | Kernel 命令封装和执行入口 |

## 3. Runtime 与 SHIM 边界：ishim

`ishim` 定义 Runtime 需要平台实现提供的能力，例如：
- 关闭设备。
- 分配 Buffer。
- 创建 Hardware Context。
- 提交执行 Buffer。
- 等待执行完成。

核心思想是：上层 Runtime 依赖稳定抽象，平台相关的设备句柄、IOCTL 和队列机制由 SHIM 实现。

MiniAccel 可借鉴该思想建立自己的 `IBackend`，但接口应围绕 MiniAccel 的 Device、Context、Buffer、Command 和 Completion 语义设计，不必复制 XRT 的类层次。

## 4. Hardware Context 创建链路

```text
xrt::hw_context
      |
      v
hw_context_impl
      |
      v
xrt_core::device
      |
      v
create_hw_context()
      |
      v
Platform SHIM
      |
      v
Driver IOCTL / Platform Context
```

这里需要区分：
- `xrt::device`：用户可见设备对象。
- `xrt_core::device`：Runtime 内部设备对象。
- `hwctx_handle`：具体平台创建的 Context 句柄。

在 AMD XDNA SHIM 中，设备可根据能力选择 `hwctx_umq` 或 `hwctx_kmq`。因此上层 Context API 不必知道底层采用哪一种 Queue 管理方式。

## 5. Buffer Object 管理

`bo_impl` 是 XRT Buffer 的基础抽象。研究版本涉及 Host Buffer、User Pointer Buffer、Kernel-managed Buffer、Device Buffer、Imported Buffer 和 Sub Buffer 等场景。

XRT 使用共享所有权管理 Buffer 生命周期。应用释放自己的 Buffer 引用时，只要仍有 Command 或其他对象持有引用，底层资源就不应提前销毁。

典型关系：

```text
Application
    |
    v
Buffer Object
    +---- Task A
    +---- Task B
    +---- Task C
```

对 MiniAccel 的启发：Runtime Buffer 管理用户态生命周期和 API 语义，Backend 负责实际分配、映射和释放；Memory Pool 不应直接操作 Kernel 内部对象。

## 6. Hardware Queue 抽象

`hw_queue_impl` 抽象设备执行队列。研究版本中有两类路径：
- `qds_device`：使用平台提供的 Hardware Queue Handle。
- `kds_device`：通过 SHIM 的执行提交和等待接口管理任务。

Runtime 根据 Hardware Context 是否提供具体 Queue Handle 选择执行路径。上层 Command 不必直接了解平台队列内部细节。

这说明 Queue 可以作为 Runtime 与平台执行路径之间的稳定抽象，但不同平台对队列的实现和完成语义仍可能不同。

## 7. Command 与完成管理

`kernel_command` 将一次计算执行封装为 Runtime Command，负责准备执行 Buffer、参数和状态，并调用 Hardware Queue 提交或等待。

`command_manager` 管理用户态 Managed Execution，维护已提交命令、运行中命令和监控线程，在观察到完成后通知 Host 或回调。

值得借鉴的并发细节：提交路径先把 Command 登记到 Runtime 可追踪的数据结构，再触发异步提交。否则任务可能在监控线程登记之前快速完成，造成完成状态丢失或竞态。

边界说明：
- `command_manager` 是用户态完成管理组件，不是硬件调度器。
- `qds_device` 和 `kds_device` 是不同队列适配路径。
- 某些平台的 Managed Execution 能力不同，不能把一个后端的完成机制推广成所有 XRT 平台的共同实现。

## 8. 从 XRT Kernel API 到平台后端

典型上层链路：

```text
xrt::run::start()
       |
       v
kernel_command::run()
       |
       +--> managed_start()
       |
       +--> unmanaged_start()
                    |
                    v
              hw_queue_impl
                    |
                    v
              qds_device / kds_device
                    |
                    v
                Platform SHIM
```

XRT 仓库还包含 XOCL FPGA Kernel Driver 的 KDS 实现，例如 `xocl_ioctl.c`、`xocl_kds.c` 和 `kds_core.c`。这些文件可用于研究 FPGA 命令验证、KDS Job 管理和完成通知，但不要将它们误认为 AMD XDNA 的 AIE2 执行实现。

## 9. 对 MiniAccel 的映射

| XRT 抽象 | MiniAccel 对应 | 研究建议 |
|---|---|---|
| `xrt::device` / Core Device | Device | ADAPT：区分 Public API 与内部设备对象 |
| `hw_context_impl` | Runtime Context | ADOPT：Runtime 管理 Context 生命周期 |
| `ishim` | `IBackend` | ADOPT：Runtime 与平台实现之间建立稳定接口 |
| `bo_impl` | Buffer | ADAPT：异步任务持有 Buffer 引用 |
| `hw_queue_impl` | Backend Queue | ADAPT：统一队列接口，不暴露 Linux IOCTL |
| `qds_device` / `kds_device` | Backend-specific Queue | ADAPT：CPU 与 Kernel Backend 分别实现 |
| `kernel_command` | Runtime Command / Task | ADAPT：区分 Command 描述与 Task 状态 |
| `command_manager` | Completion Dispatcher | ADAPT：先登记任务再提交，消除快速完成竞态 |
| XOCL KDS / ERT Packet | Kernel Scheduler | DEFER：作为 FPGA 专用机制研究 |

## 10. MiniAccel 开发检查点

- Runtime Core 不直接包含 Linux IOCTL 结构。
- 平台差异收敛在 Backend 实现中。
- Runtime Context 和 Backend Context Handle 分层管理。
- Buffer 的异步引用由 Task/Command 持有。
- Task 登记与 Backend submit 的先后顺序必须消除完成竞态。
- Completion Dispatcher 将 Backend 完成事件转换为 Runtime Task/Event 状态。
- 不把 FPGA 专用 ERT、XOCL KDS 误认为通用 NPU Runtime 的必需组件。

## 11. 建议阅读顺序

1. `ishim.h`：先理解 Runtime / SHIM 接口边界。
2. `xrt_device.cpp`：Device API 和内部设备对象。
3. `xrt_hw_context.cpp`：Context 创建和销毁。
4. `xrt_bo.cpp`：Buffer 抽象和生命周期。
5. `hw_queue.cpp`：QDS/KDS 与 Command Manager。
6. `xrt_kernel.cpp`：Command 构造和 Run 入口。
7. 最后结合 AMD XDNA 仓库中的 `src/shim/` 追踪真实 NPU 后端。
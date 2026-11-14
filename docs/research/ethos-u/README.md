# Arm Ethos-U Linux Driver 源码研究

## 1. 研究定位

Ethos-U 是 MiniAccel 学习 Linux NPU Kernel Driver 的第一站。重点理解驱动如何接收用户态提交、解析 Buffer Handle、构建 Job、建立依赖、推入调度器，并在执行完成后通过 IRQ 和 Fence 通知等待方。

Ethos-U 是 Kernel Driver 参考，不是完整通用 C++ Runtime SDK。

源码目录：`drivers/accel/ethosu/`。固定版本见 [SOURCE_INDEX.md](../SOURCE_INDEX.md)。

## 2. 模块与关键对象

| 文件 | 关键符号 | 职责 |
|---|---|---|
| `ethosu_drv.c` | `ethosu_probe()` | 设备初始化与注册 |
| `ethosu_drv.c` | `ethosu_open()` / `ethosu_postclose()` | 文件打开和关闭 |
| `ethosu_drv.c` | `ethosu_ioctl_bo_create()` | Buffer 创建入口 |
| `ethosu_drv.c` | `ethosu_ioctl_submit()` | 任务提交入口 |
| `ethosu_gem.c` | `ethosu_gem_create_with_handle()` | 创建带 Handle 的 GEM 对象 |
| `ethosu_gem.c` | `ethosu_gem_cmdstream_create()` | 命令流 Buffer 创建与验证 |
| `ethosu_job.c` | `ethosu_ioctl_submit_job()` | 构建一次执行 Job |
| `ethosu_job.c` | `ethosu_job_push()` | 依赖处理并推入调度队列 |
| `ethosu_job.c` | `ethosu_job_run()` | Scheduler 调用的执行入口 |
| `ethosu_job.c` | `ethosu_job_handle_irq()` | 处理任务完成 |
| `ethosu_device.h` | `struct ethosu_device` | 设备级状态 |

需要区分：
- `ethosu_device`：设备级对象，包含设备资源和调度状态。
- `ethosu_file_priv`：打开文件对应的私有状态。
- `ethosu_job`：一次任务提交的执行对象。

它们分别属于 Device、Client/File 和 Task/Job 层次，不能合并成一个 Runtime Context。

## 3. IOCTL 与 Buffer Handle

驱动 IOCTL 表包含设备查询、BO 创建、BO 等待、BO mmap offset、命令流 BO 创建和任务提交等入口。

用户态提交 Buffer Handle；Kernel Driver 根据 Handle 查找 GEM 对象并持有引用。用户态不应直接提供并让驱动盲目信任物理地址。

对 MiniAccel 的启发：
- UAPI 使用固定宽度整数和 Handle/ID。
- 内核负责解析、校验、归属检查和资源引用。
- 异步任务必须持有其访问 Buffer 的引用，不能因应用释放句柄而提前释放底层对象。

## 4. Job 提交与执行闭环

```text
Userspace
  |
  | ioctl(DRM_IOCTL_ETHOSU_SUBMIT)
  v
ethosu_ioctl_submit()
  |
  v
ethosu_ioctl_submit_job()
  |  drm_sched_job_init()
  |  根据 GEM Handle 查找命令流和输入/输出 BO
  |  获取 GEM 对象引用
  v
ethosu_job_push()
  |  锁定 GEM Reservation
  |  获取隐式 Fence 依赖
  |  drm_sched_job_arm()
  v
drm_sched_entity_push_job()
  |
  v
DRM Scheduler
  |
  v
ethosu_job_run()
  |
  v
ethosu_job_hw_submit()
  |
  v
NPU Hardware
  |
  v
IRQ Handler -> Threaded IRQ Handler
  |
  v
ethosu_job_handle_irq()
  |
  v
dma_fence_signal(job->done_fence)
  |
  v
Scheduler completion / Job cleanup
```

关键点：`drm_sched_entity_push_job()` 代表 Job 被推入 DRM Scheduler，不代表硬件已经开始执行。实际执行发生在 Scheduler 调用 `ethosu_job_run()` 后。

## 5. Buffer 依赖与 Fence

`ethosu_job_push()` 会锁定 Job 涉及的 GEM Reservation，并通过隐式依赖机制获取相关 Fence。这样可以表达 Buffer 的读写依赖，例如：

```text
Task A: Write Buffer X
       |
       v
     Fence A
       |
       v
Task B: Read Buffer X
```

Task B 应在依赖满足后再读取 Buffer X。

还应区分：
- DRM Scheduler Job：描述要执行的工作。
- DMA Fence：描述任务完成或依赖同步。
- Runtime Event：面向应用的更高层同步语义。

它们可以关联，但不是同一个对象。

## 6. IRQ 与资源清理

Ethos-U 使用主 IRQ Handler 与 Threaded IRQ Handler 分工，最终进入 Job 完成处理并 Signal Fence。完成后由 Job 清理路径释放 Buffer 引用、Fence 和相关设备资源。

MiniAccel 首期没有真实 NPU IRQ 时，可以使用 Workqueue 模拟完成通知，但必须明确这只是软件模拟，不代表真实硬件中断路径。

## 7. 对 MiniAccel 的映射

| Ethos-U 机制 | MiniAccel 对应 | 研究建议 |
|---|---|---|
| `ethosu_device` | Device | ADAPT：保留设备级状态与资源入口 |
| `ethosu_file_priv` | ClientSession | ADAPT：每个打开文件拥有独立资源表 |
| GEM BO / Handle | Buffer / BufferHandle | ADAPT：MVP 可先用自定义 Handle |
| `ethosu_job` | Kernel Job | ADOPT：Job 显式持有资源引用 |
| DRM Scheduler | Kernel Scheduler | DEFER：MVP 先使用有界 FIFO，后续评估 DRM Scheduler |
| DMA Fence | Completion Token | ADAPT：先实现任务状态、Token 和 Wait Queue |
| Threaded IRQ | Workqueue Completion | ADAPT：仅模拟完成语义 |
| SRAM Pool / PM Runtime | 设备内存池 / 电源管理 | DEFER：不属于软件 MVP 必需能力 |

## 8. MiniAccel 开发检查点

- Job 入队前校验 Client、Context、Buffer 的归属和状态。
- Job 对异步访问的 Buffer 获取引用。
- Job 只能进入一个终态；重复完成不得重复释放资源。
- Context/File 关闭时处理仍在排队或执行中的 Job。
- Wait 超时不能被误判为任务已取消。
- 软件 Workqueue 路径与真实 DMA/IRQ 路径必须在文档、日志和测试中区分。

## 9. 建议阅读顺序

1. `ethosu_drv.c`：IOCTL 注册、设备打开和提交入口。
2. `ethosu_gem.c`：GEM Buffer 生命周期。
3. `ethosu_ioctl_submit_job()`：Job 构建和资源引用。
4. `ethosu_job_push()`：Buffer Reservation 与 Fence 依赖。
5. `ethosu_job_run()`：Scheduler 与实际硬件提交的边界。
6. `ethosu_job_handle_irq()`：完成通知与清理。

关联设计建议最终应写入 MiniAccel 的 Driver、Scheduler 和 Test Spec；本文件不构成实现规范。
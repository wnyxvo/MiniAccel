# MiniAccel Linux Character Driver 设计规范

> 文档编号：SPEC-DRV-001　版本：v1.0　状态：**正式基线**  
> 依赖：[ARCHITECTURE.md](./ARCHITECTURE.md)、[RUNTIME_SPEC.md](./RUNTIME_SPEC.md)、[SCHEDULER_SPEC.md](./SCHEDULER_SPEC.md)  
> 研究依据：[Ethos-U](../research/ethos-u/README.md)、[AMD XDNA](../research/amd-xdna/README.md)、[DRM_ACCEL](../research/DRM_ACCEL_FRAMEWORK.md)

## 1. 驱动目标与非目标

实现 `mini_accel.ko` 与 `/dev/mini_accel`，为用户态 KernelDriverBackend 提供设备能力查询、Client / Context / Buffer 管理、Kernel Job 异步提交、Workqueue 模拟执行、poll/read 完成通知、资源正确回收。

使用 Linux Character Device（`alloc_chrdev_region` / `cdev_add` / `class_create` / `device_create`；版本相关 API 以 VM 实际内核为准）。**MVP 不使用** DRM_ACCEL、GEM、DMA Fence、硬件 DMA/IRQ、Firmware、真实 MMIO。Workqueue 的 NOP/DELAY 是软件模拟，不得宣称是真实 AI 计算。

安全范围：不接收用户传入的内核指针、物理地址或 DMA 地址；资源按 Client/Context 隔离；IOCTL 必须验证用户数据、大小、边界、Handle 和生命周期。

## 2. 推荐驱动文件结构

```text
uapi/miniaccel_uapi.h       # 共享 C ABI；严格独立于 C++ 和 Kernel 私有结构
driver/Makefile             # Kbuild
driver/miniaccel_main.c     # init/exit、cdev、open/release
driver/miniaccel_ioctl.c    # unlocked_ioctl 和参数检查
driver/miniaccel_client.c   # per-open Client、Context/Buffer/Job 表
driver/miniaccel_buffer.c   # Buffer 分配与 mmap/VMA 引用
driver/miniaccel_job.c      # Job 创建、状态、取消、完成
driver/miniaccel_sched.c    # bounded FIFO、ordered workqueue
driver/miniaccel_poll.c     # completion FIFO、read、poll
driver/miniaccel_internal.h # 内部结构体与锁规则
```

如果初期代码量较小，可合并 C 文件，但不得合并模块职责或越过生命周期边界。

## 3. C UAPI v1（ABI 契约）

下列结构及 IOCTL 编号构成 **v1 ABI**。应使用 Linux UAPI 固定宽度类型和固定对齐；在用户态也使用兼容类型定义。所有结构必须以 `_Static_assert/ static_assert(sizeof...)` 验证，所有 `reserved` 字段要求用户传 0，Kernel 拒绝非零。

```c
/* uapi/miniaccel_uapi.h, schematic but binding names and layouts */
#include <linux/types.h>
#include <linux/ioctl.h>
#define MA_UAPI_VERSION 1u
#define MA_IOC_MAGIC 'M'
#define MA_CAP_NOP   (1ULL << 0)
#define MA_CAP_DELAY (1ULL << 1)
#define MA_OP_NOP    1u
#define MA_OP_DELAY  2u
#define MA_TASK_QUEUED    0u
#define MA_TASK_COMPLETED 1u
#define MA_TASK_FAILED    2u
#define MA_TASK_CANCELED  3u
#define MA_TASK_RUNNING   4u

struct ma_device_query {
    __u32 abi_version;          /* out */
    __u32 max_contexts;         /* out */
    __u32 max_inflight;         /* out per context */
    __u32 max_queue_depth;      /* out per device */
    __u64 capability_bits;      /* out */
    __u64 reserved[2];          /* in=0 */
};
struct ma_context_create {
    __u32 requested_max_inflight; /* in: [1,16] */
    __u32 context_handle;        /* out */
    __u64 reserved[2];
};
struct ma_context_destroy {
    __u32 context_handle;
    __u32 reserved0;
    __u64 reserved[2];
};
struct ma_buffer_alloc {
    __u32 context_handle;   /* in */
    __u32 buffer_handle;    /* out */
    __u64 bytes;            /* in: 1 .. 16 MiB */
    __u64 mmap_offset;      /* out: page aligned byte offset */
    __u64 reserved[2];
};
struct ma_buffer_free {
    __u32 context_handle;
    __u32 buffer_handle;
    __u64 reserved[2];
};
struct ma_task_submit {
    __u32 context_handle; /* in */
    __u32 opcode;         /* in: NOP or DELAY */
    __u32 delay_ms;       /* in: 0..1000 for DELAY */
    __u32 flags;          /* in: must be 0 */
    __u64 task_sequence;  /* out: context-local; >0 */
    __u64 reserved[2];
};
struct ma_task_wait {
    __u32 context_handle;
    __u32 timeout_ms;     /* 0 nonblocking, UINT32_MAX infinite */
    __u64 task_sequence;
    __u32 state;          /* out: MA_TASK_* */
    __u32 result_errno;   /* out: 0 or positive errno */
    __u64 reserved[2];
};
struct ma_task_reap {
    __u32 context_handle;
    __u32 reserved0;
    __u64 task_sequence;
    __u64 reserved[2];
};
struct ma_task_query {
    __u32 context_handle;
    __u32 state;          /* out: 0 queued, 4 running, or MA_TASK_* terminal */
    __u64 task_sequence;
    __u32 result_errno;   /* out */
    __u32 reserved0;
    __u64 reserved[2];
};
struct ma_completion_record {
    __u32 context_handle;
    __u32 state;          /* MA_TASK_* terminal */
    __u64 task_sequence;
    __u32 result_errno;   /* 0 or positive errno */
    __u32 reserved0;
    __u64 completed_ns;   /* monotonic */
};

#define MA_IOCTL_DEVICE_QUERY   _IOWR(MA_IOC_MAGIC, 0x00, struct ma_device_query)
#define MA_IOCTL_CREATE_CONTEXT _IOWR(MA_IOC_MAGIC, 0x01, struct ma_context_create)
#define MA_IOCTL_DESTROY_CONTEXT _IOW (MA_IOC_MAGIC, 0x02, struct ma_context_destroy)
#define MA_IOCTL_ALLOC_BUFFER   _IOWR(MA_IOC_MAGIC, 0x03, struct ma_buffer_alloc)
#define MA_IOCTL_FREE_BUFFER    _IOW (MA_IOC_MAGIC, 0x04, struct ma_buffer_free)
#define MA_IOCTL_SUBMIT_TASK    _IOWR(MA_IOC_MAGIC, 0x05, struct ma_task_submit)
#define MA_IOCTL_WAIT_TASK      _IOWR(MA_IOC_MAGIC, 0x06, struct ma_task_wait)
#define MA_IOCTL_QUERY_TASK     _IOWR(MA_IOC_MAGIC, 0x07, struct ma_task_query)
#define MA_IOCTL_REAP_TASK      _IOW (MA_IOC_MAGIC, 0x08, struct ma_task_reap)
```

**版本规则**：`DEVICE_QUERY` 提供 ABI 版本；MVP 只支持 version 1，未来不兼容修改必须提升版本。所有数据字段均为显式 `__u32/__u64`，没有用户态裸指针、`size_t`、`long` 或 C++ 类型。兼容性测试至少验证 x86_64 下 `sizeof` 与 offsets；若实际布局存在隐式填充风险，Codex 必须给出 ABI 变更提案而非悄悄改字段。

`ma_task_query.state` 的非终态常量正式定义为 `MA_TASK_QUEUED=0`、`MA_TASK_RUNNING=4`；`MA_TASK_COMPLETED=1`、`MA_TASK_FAILED=2`、`MA_TASK_CANCELED=3` 为终态。用户态不得自行解释其他数字。

## 4. 系统调用及 errno 约定

| 接口 | 成功 | 失败（示例） |
|---|---|---|
| open | 返回 fd、新 Client | `-ENODEV` / `-ENOMEM` |
| DEVICE_QUERY | 返回能力和容量 | `-EINVAL` |
| CREATE_CONTEXT | 返回新 Handle | `-EINVAL`、`-ENOMEM`、`-ENOSPC` |
| DESTROY_CONTEXT | 上下文无在途、无活跃 Buffer Handle 时销毁 | `-ENOENT`、`-EBUSY` |
| ALLOC_BUFFER | 分配可 mmap 的 Buffer 和 offset | `-EINVAL`、`-ENOMEM`、`-ENOSPC` |
| FREE_BUFFER | 移除 Handle，底层对象由引用计数延迟回收 | `-ENOENT`、`-EINVAL` |
| SUBMIT_TASK | 被 Kernel **接受入队**，返回 task_sequence | `-EINVAL`、`-ENOENT`、`-EAGAIN`、`-ESHUTDOWN` |
| WAIT_TASK | 任务已终态，返回 state/result | `-EAGAIN`（0ms 未完成）、`-ETIMEDOUT`、`-ENOENT` |
| QUERY_TASK | 返回当前状态 | `-ENOENT` |
| REAP_TASK | 显式移除已终态 Job 的查询记录；Backend 消费结果后调用 | `-ENOENT`、`-EBUSY`（未终态） |
| read | 返回完整 completion records | `-EAGAIN`、`-EFAULT`、`-EINVAL` |
| poll | `EPOLLIN` 有完成记录、`EPOLLHUP/ERR` 已关闭/失败 | 标准 poll 语义 |

用户态 Backend 将内核 errno 映射到 RUNTIME_SPEC 中稳定的 StatusCode，并保留原始 errno 诊断。错误返回不得改变未成功提交任务的序列号。

## 5. Kernel Object 和所有权

```text
miniaccel_device
  +-- global bounded queue / ordered workqueue
  +-- capacity counters / locks
  +-- open Client list (for shutdown safety)
  |
  +-- miniaccel_client = file->private_data
        +-- Context Handle IDR/XArray
        +-- Buffer Handle IDR/XArray
        +-- Job lookup table
        +-- bounded Completion FIFO + waitqueue
        +-- closing flag / refs
        |
        +-- miniaccel_context
        |     +-- context_handle (client local, monotonic nonreuse)
        |     +-- max_inflight / in_flight
        |     +-- closing / refs
        |
        +-- miniaccel_buffer
        |     +-- context owner / bytes / vmalloc_user area
        |     +-- buffer_handle + mmap_token
        |     +-- kref for handle/VMA/Job
        |
        +-- miniaccel_job
              +-- client / context refs
              +-- context-local task_sequence
              +-- opcode + delay_ms
              +-- state + completion guard
```

- `file->private_data` 持有 Client；`dup(fd)` 共享同一 open file description 的 Client；独立 `open` 分别建立 Client。
- Context/Buffer Handle 仅在 Client 内可查；不能通过其他 fd 使用。
- Context、Buffer、Job 都需要锁保护的状态和引用计数；跨调用不能信任用户送回的 Handle 以外状态。
- Kernel Buffer 按分配的 Client + Context 归属。Buffer Handle 被 FREE 后立即不可再查，但已建立的 VMA/Job 引用仍能延长对象寿命。
- Context Destroy 仅在 `in_flight=0` 且无活跃 Buffer Handle/映射引用时成功，否则 `-EBUSY`；由用户态 Context::close 先完成 drain/free。Context Closing 期间，已有 VMA 仍可以 munmap；不允许新 ALLOC_BUFFER 或 mmap。
- Client Release 进入 closing，阻止新任务，取消未派发任务，安全等待正在运行的 ordered work，销毁表和未消费完成记录；无任何工作可在释放后访问 Client 内存。**仍存活的 mmap VMA 必须独立持有 Buffer 所需引用；fd close 不得提前销毁其映射内存。**

## 6. Buffer 与 mmap 协议

### 6.1 分配

- 最小有效 size=1，最大 16MiB；按 PAGE_SIZE 向上取整必须检查溢出，实际分配使用 `vmalloc_user`（适配用户映射）。
- `mmap_offset` 由 Kernel 为每个 Buffer 分配唯一、页对齐的 **opaque token**，不是物理地址和普通用户指针；值在同一 Client 生命周期内不得复用。
- `mmap(fd, length, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_offset)`；offset 必须与该 Client 现存 Buffer 匹配，length 必须非零、PAGE_SIZE 对齐且不超过 Buffer 页对齐长度。
- `remap_vmalloc_range` 建立用户映射；设置 `VM_DONTEXPAND|VM_DONTDUMP`，VMA `vm_ops.open/close` 管理引用。
- 不能先释放 vmalloc_user 内存再等待 VMA 消亡；`munmap` 归还 VMA 引用。拒绝 MAP_PRIVATE。
- Kernel Backend 的 `syncToDevice/syncFromDevice` 仅对共享内存模拟路径为 no-op，不代表 DMA 一致性动作。

### 6.2 内存上限

默认每 Client 最多 64 个 Buffer，每 Buffer ≤16MiB、总页对齐累计 ≤64MiB；超限 `-ENOSPC` 或 `-ENOMEM`，不能造成无界内核内存耗尽。实际参数允许模块加载时配置，但测试必须记录。

### 6.3 映射竞争

mmap 与 FREE_BUFFER 并发必须通过 Client Buffer Registry 锁和对象 kref 消除查找/释放竞态。Context Destroy 在仍有 VMA 引用时返回 `-EBUSY`。独立 fd 对其他 Client 的 mmap token 不得成功。

## 7. Kernel Job 生命周期与顺序

Kernel 只支持 NOP/DELAY，DELAY 范围 0～1000ms；其他 Opcode 返回 `-EOPNOTSUPP`。

```text
SUBMIT_TASK
  -> 验证 Client/Context/opcode/flags/reserved
  -> 原子检查 device queue capacity & context inflight
  -> 分配 Job、获取 Client/Context 引用
  -> 分配 context-local task_sequence
  -> JobRegistry 登记 Job
  -> FIFO enqueue + wake worker
  -> return success
                            |
                            v
                      Workqueue
                            |
                 set state RUNNING
                            |
                     NOP / DELAY
                            |
                   complete exactly once
                            |
           +----------------+------------------+
           |                                   |
       state terminal                     completion record
           |                                   |
    release inflight credit             enqueue FIFO + wake_up
           |                                   |
    release Job refs                   read/poll/WAIT_TASK
```

**重要实现约束**：
- `SUBMIT_TASK` 成功表示已接受，不表示任务完成。
- Context `max_inflight` 限制 Kernel 中被接受且尚未进入终态的 Job（排队 + 执行）；达到上限返回 `-EAGAIN`。
- Device `max_queue_depth=256` 限制已接受但尚未终态的 Job 总数（包括运行中），不是可绕过的仅排队计数。
- 默认每 Client 完成队列上限 1024 条；**在 SUBMIT_TASK 准入时预留 1 个 Completion FIFO 槽位**，预留数与尚未 read 消费的记录数之和不得超过 1024，满时返回 `-EAGAIN` 且不创建 Job/Sequence。Job 终态时将其预留槽位转换成正式记录，`read` 消费后释放槽位；禁止覆盖/丢弃旧记录。取消或故障终态也必须生成记录。`REAP_TASK` 只回收 Job Registry 的终态查询记录，不清除尚未被 read 的完成记录。
- Job 终态通过互斥/原子一次性转换，禁止重复入 FIFO、重复归还额度。
- Kernel Device 可用有序 `workqueue` 或单 worker + FIFO 实现，保证易于复现。多 Context 公平性为 Scheduler M4 增强，不得声称 FIFO 已保证公平。**为防止提交后错误回滚，所有准入校验和 Completion 槽位预留必须先成功，再分配序号并使 Job 对其他线程可见；任何失败路径须完整释放临时资源和预留额度。**
- 对未开始的 Job 可在 Context/Client 关闭时标记 CANCELED；正在执行的 DELAY 必须安全结束后才能释放对象。
- 可使用 `delayed_work` 或等待方式实现 DELAY，但不能长时间占用自旋锁或在 atomic context 睡眠。
- 所有 Job completion 的 timestamp 用 `ktime_get_ns()`。

## 8. read/poll/WAIT 完成协议

- `read(fd, out, N*sizeof(ma_completion_record))` 返回 1..N 个完整记录；`count < sizeof(record)` 或非整数倍返回 `-EINVAL`；无记录：O_NONBLOCK 返回 `-EAGAIN`，阻塞 fd 等待直到有记录或 Client 关闭。
- `poll` 将 Client completion waitqueue 注册到 `poll_wait`；有记录时返回 `EPOLLIN|EPOLLRDNORM`，设备/Client 关闭时报告 HUP/ERR。
- `WAIT_TASK` 基于 Job Registry 查询指定 context_handle + sequence，等待其成为终态。0ms 未完成返回 `-EAGAIN`，普通超时 `-ETIMEDOUT`；等待中收到信号 `-ERESTARTSYS`。
- `read` 消费完成通知后，`QUERY_TASK/WAIT_TASK` 仍能查询该 Job，直到用户显式调用 `REAP_TASK` 或 Context 销毁。每 Client 最多保留 65536 条尚未 REAP 的 Job 查询记录；超过上限的 SUBMIT_TASK 返回 `-ENOSPC`，不得无界增长。KernelDriverBackend 应在已将完成结果复制到 Runtime TaskRegistry 后执行 `REAP_TASK`；原生 UAPI 使用者可自行决定 REAP 时机。
- Context 被正常 Destroy 后，其任务 ID 均变无效；Destroy 只允许无在途 Job 且 Buffer/映射引用已清空。剩余终态 Job 查询记录可在 Destroy 时统一清理；已经提交到该 Client 完成 FIFO 的记录仍可由 read 消费，且上下文 Handle 不得复用，因此不会发生身份歧义。Client release 则明确丢弃未消费通知并安全销毁。
- Driver 只支持一个 KernelDriverBackend 的 completion reader；其他直接读 fd 的客户端应在自己的 fd 上使用。一个 fd 上多个并发 reader 的分配不保证广播，不得用于同一 Backend 的重复 dispatcher。

## 9. 模块初始化、卸载与锁

- init 路径按 `alloc_chrdev_region → cdev_add → class_create → device_create → workqueue` 等阶段初始化，失败逆序释放。
- exit 不允许仍有运行 worker 或引用悬空；先停止新提交、确保 workqueue 安全退出，再反向销毁节点/对象。
- Client Registry、Context/Buffer/Job Registry 使用 mutex；完成 FIFO 可用 spinlock + waitqueue（不可在 spinlock 下 copy_to_user 或 sleep）；Workqueue 的耗时工作不可持有设备全局锁。
- 推荐锁序：Device queue lock → Client registry lock → Context/Job lock，但实际实现应通过短临界区/引用获取避免锁交叉；不得一边持有 spinlock 一边 `mutex_lock`。
- `copy_from_user/copy_to_user` 返回值必须检查；不允许从内核复制未初始化 padding。
- `CONFIG_KASAN`、`CONFIG_LOCKDEP`、`CONFIG_KCSAN` 有条件时作为增强检查；普通 VM 以 kmemleak/dmesg/反复 load/unload 和压力测试为准。

## 10. 安全与故障测试要求

| ID | 场景 | 期望 |
|---|---|---|
| DRV-01 | load/open/query/close/unload | 无内核警告或资源泄漏 |
| DRV-02 | 非法 magic/size/reserved/opcode | 明确错误码，内核稳定 |
| DRV-03 | 两个独立 fd 交叉使用 Handle | 拒绝，不能访问他人资源 |
| DRV-04 | Context/Buffer 并发删除与查找 | 无 UAF、double free |
| DRV-05 | mmap→FREE_BUFFER→访问→munmap | 有效引用期间不释放底层内存 |
| DRV-06 | NOP/DELAY 并发提交 | 完成状态正确、一次终态 |
| DRV-07 | O_NONBLOCK read/poll/WAIT_TASK | 正确等待、读取完整记录 |
| DRV-08 | 队列容量达到限制 | -EAGAIN、不消耗序号 |
| DRV-09 | Context 销毁在途任务 | -EBUSY，不提前释放 |
| DRV-10 | close(fd) 与 queued/running Job 竞争 | 安全停止/清理 |
| DRV-11 | 完成队列高压/溢出 | 不丢终态、无静默覆盖 |
| DRV-12 | 反复 insmod/rmmod | 无 oops/WARN/残留节点 |

详细运行方法和验收输出见 [TEST_AND_BENCHMARK.md](./TEST_AND_BENCHMARK.md)。

## 11. Codex 实现顺序

D0：仅注册 Character Device 和 DEVICE_QUERY；D1：Client/Context；D2：Buffer/mmap；D3：Job + Workqueue；D4：read/poll/WAIT/QUERY；D5：关闭协议、异常注入、并发回归。每阶段需提交对应源码、独立测试、实际命令输出。DRM_ACCEL 不得提前引入。
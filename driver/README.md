# MiniAccel M1-D0：Character Driver 与 UAPI 查询

权威依据：`docs/specs/DRIVER_SPEC.md` v1.0 §1～4、§9、§11，`docs/specs/TEST_AND_BENCHMARK.md` v1.0.1 §5/§11；`AGENTS.md` 约束继续适用。

## 当前实现范围

- Linux Character Device：`/dev/mini_accel`；按 `alloc_chrdev_region → cdev_add → class_create → device_create → alloc_ordered_workqueue` 初始化，失败路径反向清理；open/release 各自拥有独立的最小 Client。
- `uapi/miniaccel_uapi.h`：声明完整固定宽度 v1 ABI、IOCTL 编码、结构体尺寸和偏移断言；不依赖 C++。
- `DEVICE_QUERY`：从用户态复制并校验 reserved 字段；返回 ABI=1、cap=Nop/Delay、max_contexts=64、max_inflight=16、max_queue_depth=256；输出结构体零初始化。
- 不认识的 magic/size/direction 返回 `ENOTTY`；已声明、未实现的 Context/Buffer/Job IOCTL 暂时返回 `EOPNOTSUPP`，**不能假装已支持**。
- ordered workqueue 当前只完成生命周期搭建，**还不能执行 NOP/DELAY**。真正的 Context/Job、完成 FIFO、poll/read 的完整行为进入后续 M1/M2 子阶段。

**注意**：当前 `read` 返回 `EOPNOTSUPP`、`poll` 返回无事件，是 D0 临时语义；不符合最终 Driver §8 的完成协议。必须在实现 Job/Completion 时替换，不能将 D0 标记为完整 G1 PASS。

## 编译与测试

在 Ubuntu 24.04 VM 中，需确保运行内核有匹配的 headers：

```sh
test -d "/lib/modules/$(uname -r)/build"
make -C driver
cc -std=c11 -Wall -Wextra -Werror -Iuapi tests/driver/test_uapi_abi.c -o /tmp/ma-abi-c
c++ -std=c++17 -Wall -Wextra -Werror -Iuapi tests/driver/test_uapi_abi.cpp -o /tmp/ma-abi-cpp
/tmp/ma-abi-c && /tmp/ma-abi-cpp
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Iuapi tests/driver/kt001_kt010_query.c -o /tmp/ma-query

sudo insmod driver/mini_accel.ko
ls -l /dev/mini_accel
/tmp/ma-query
sudo dmesg --level=warn,err | tail -n 80
sudo rmmod mini_accel
ls -l /dev/mini_accel  # expected absent
```

使用隔离 VM 快照；不要在生产机上实验 Kernel Module。Kbuild 的编译成功不代替 load/open/ioctl/close/unload、`dmesg` 与反复加载验证。根目录 CMake/CTest 的 M0 纯用户态测试不依赖此模块。

## 后续阶段和验收缺口

- D1：Client/Context Registry、单 Client Handle 与销毁约束。
- D2：Buffer/mmap 与 VMA 引用。
- D3：NOP/DELAY Job 及 ordered workqueue 实际执行。
- D4：Completion、read/poll/WAIT/QUERY/REAP。
- D5：Close 并发、拒绝路径、压测回归。
- KT-001/010 的动态行为必须在 VM 中实测；本 D0 若未运行这些步骤，则 G1 应为 `BLOCKED` 而非 PASS。

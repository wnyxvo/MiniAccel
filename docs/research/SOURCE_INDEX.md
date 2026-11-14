# 上游源码版本与阅读入口

本文档固定当前研究采用的上游仓库和版本边界。版本信息用于复现源码阅读，不代表 MiniAccel 对上游项目存在编译或运行时依赖。

## 1. 源码版本基线

以下 Commit SHA 于 2026-11-14 从 GitHub 仓库提交信息读取。

| 项目 | 上游仓库 | 分支/版本 | Commit SHA |
|---|---|---|---|
| Arm Ethos-U | [torvalds/linux](https://github.com/torvalds/linux) | master 仓库 HEAD | `97d73c8251226cf27cc6656b4c542869bb8c69fa` |
| AMD XDNA | [amd/xdna-driver](https://github.com/amd/xdna-driver) | main 仓库 HEAD | `ca385d85d88441715551fc55d229b3afdba973dc` |
| Xilinx XRT | [Xilinx/XRT](https://github.com/Xilinx/XRT) | 固定研究提交 | `06c43a7f93fee129d32def1d372e17a8e69c05d2` |

版本边界说明：
- Linux SHA 是 Linux 仓库整体 HEAD，不等同于 Ethos-U 目录最近一次变更的提交。
- AMD XDNA SHA 是该仓库 main 分支 HEAD。
- XRT 使用固定研究提交，避免以可变分支描述此前的源码级分析。
- 上游分支会继续移动；重新核对时应记录实际 checkout 的 SHA。

## 2. Arm Ethos-U

仓库：[torvalds/linux](https://github.com/torvalds/linux)

固定源码入口：
- [drivers/accel/ethosu/ethosu_drv.c](https://github.com/torvalds/linux/blob/97d73c8251226cf27cc6656b4c542869bb8c69fa/drivers/accel/ethosu/ethosu_drv.c)
- [drivers/accel/ethosu/ethosu_gem.c](https://github.com/torvalds/linux/blob/97d73c8251226cf27cc6656b4c542869bb8c69fa/drivers/accel/ethosu/ethosu_gem.c)
- [drivers/accel/ethosu/ethosu_job.c](https://github.com/torvalds/linux/blob/97d73c8251226cf27cc6656b4c542869bb8c69fa/drivers/accel/ethosu/ethosu_job.c)
- [drivers/accel/ethosu/ethosu_device.h](https://github.com/torvalds/linux/blob/97d73c8251226cf27cc6656b4c542869bb8c69fa/drivers/accel/ethosu/ethosu_device.h)

关键阅读符号：
- `ethosu_probe()`
- `ethosu_open()` / `ethosu_postclose()`
- `ethosu_ioctl_submit()`
- `ethosu_ioctl_submit_job()`
- `ethosu_job_push()`
- `ethosu_job_run()`
- `ethosu_job_handle_irq()`

## 3. AMD XDNA

仓库：[amd/xdna-driver](https://github.com/amd/xdna-driver)

固定源码入口：
- [amdxdna_pci_drv.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/amdxdna_pci_drv.c)
- [amdxdna_drm.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/amdxdna_drm.c)
- [amdxdna_ctx.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/amdxdna_ctx.c)
- [amdxdna_gem.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/amdxdna_gem.c)
- [aie2_ctx.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/aie2_ctx.c)
- [aie2_message.c](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/drivers/accel/amdxdna/aie2_message.c)
- [src/shim/hwq.cpp](https://github.com/amd/xdna-driver/blob/ca385d85d88441715551fc55d229b3afdba973dc/src/shim/hwq.cpp)

重点关注 KMQ/UMQ：
- `src/shim/kmq/hwq.cpp`
- `src/shim/umq/hwq.cpp`
- `src/shim/umq/host_queue.h`
- `src/shim/hwctx.cpp`
- `src/shim/umq/hwctx.cpp`

## 4. Xilinx XRT

仓库：[Xilinx/XRT](https://github.com/Xilinx/XRT)

固定研究提交：`06c43a7f93fee129d32def1d372e17a8e69c05d2`

源码入口：
- [ishim.h](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/ishim.h)
- [xrt_device.cpp](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/api/xrt_device.cpp)
- [xrt_hw_context.cpp](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/api/xrt_hw_context.cpp)
- [xrt_bo.cpp](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/api/xrt_bo.cpp)
- [hw_queue.cpp](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372def1d372e17a8e69c05d2/src/runtime_src/core/common/api/hw_queue.cpp)
- [xrt_kernel.cpp](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/api/xrt_kernel.cpp)
- [xocl_ioctl.c](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/pcie/driver/linux/xocl/userpf/xocl_ioctl.c)
- [xocl_kds.c](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/pcie/driver/linux/xocl/userpf/xocl_kds.c)
- [kds_core.c](https://github.com/Xilinx/XRT/blob/06c43a7f93fee129d32def1d372e17a8e69c05d2/src/runtime_src/core/common/drv/kds_core.c)

注意：XRT 仓库中的 XOCL KDS 是 FPGA 驱动执行路径；AMD XDNA 仓库中的 `src/shim/` 是 XRT Runtime 与 XDNA NPU 的适配实现。二者有关联，但不是同一个 Kernel Driver。

## 5. 复核记录模板

每次更新研究时记录：

```text
项目：
仓库：
Commit SHA：
源码文件：
函数/结构体：
核对日期：
源码事实：
架构解释：
与既有研究的差异：
对 MiniAccel 的影响：
```

不得仅因上游分支更新，就自动修改已有调用链结论。
/* Build on a user-space toolchain; statically validates the shared C ABI. */
#include "miniaccel_uapi.h"
#include <stdio.h>
int main(void)
{
    if (MA_UAPI_VERSION != 1 || _IOC_TYPE(MA_IOCTL_DEVICE_QUERY) != MA_IOC_MAGIC)
        return 1;
    if (_IOC_NR(MA_IOCTL_DEVICE_QUERY) != 0 ||
        _IOC_SIZE(MA_IOCTL_DEVICE_QUERY) != sizeof(struct ma_device_query))
        return 2;
    if (_IOC_NR(MA_IOCTL_REAP_TASK) != 8 ||
        _IOC_SIZE(MA_IOCTL_REAP_TASK) != sizeof(struct ma_task_reap))
        return 3;
    puts("MiniAccel UAPI v1 ABI declarations: PASS (C)");
    return 0;
}

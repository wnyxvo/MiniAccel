// SPDX-License-Identifier: GPL-2.0
/* M1-D0 exposes only DEVICE_QUERY; other v1 IOCTLs are reserved for M2. */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include "miniaccel_internal.h"

static long miniaccel_device_query(unsigned long arg)
{
    struct ma_device_query request;
    struct ma_device_query reply = { 0 };
    void __user *user_arg = (void __user *)arg;

    if (!user_arg)
        return -EFAULT;
    if (copy_from_user(&request, user_arg, sizeof(request)))
        return -EFAULT;
    if (request.reserved[0] || request.reserved[1])
        return -EINVAL;

    reply.abi_version = MA_UAPI_VERSION;
    reply.max_contexts = MA_DRIVER_MAX_CONTEXTS;
    reply.max_inflight = MA_DRIVER_MAX_INFLIGHT;
    reply.max_queue_depth = MA_DRIVER_MAX_QUEUE_DEPTH;
    reply.capability_bits = MA_CAP_NOP | MA_CAP_DELAY;
    /* Zero-initialized reserved fields and no uninitialized padding. */
    if (copy_to_user(user_arg, &reply, sizeof(reply)))
        return -EFAULT;
    return 0;
}

long miniaccel_ioctl(struct file *file, unsigned int command,
                    unsigned long arg)
{
    (void)file;
    if (_IOC_TYPE(command) != MA_IOC_MAGIC)
        return -ENOTTY;

    /* Exact-command matching enforces direction and sizeof ABI layout. */
    if (command == MA_IOCTL_DEVICE_QUERY)
        return miniaccel_device_query(arg);

    switch (command) {
    case MA_IOCTL_CREATE_CONTEXT:
    case MA_IOCTL_DESTROY_CONTEXT:
    case MA_IOCTL_ALLOC_BUFFER:
    case MA_IOCTL_FREE_BUFFER:
    case MA_IOCTL_SUBMIT_TASK:
    case MA_IOCTL_WAIT_TASK:
    case MA_IOCTL_QUERY_TASK:
    case MA_IOCTL_REAP_TASK:
        return -EOPNOTSUPP; /* ABI fixed; functionality in M2. */
    default:
        return -ENOTTY;
    }
}

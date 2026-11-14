/* M1-D0 internal kernel-only declarations. Never included from public Runtime. */
#ifndef MINIACCEL_INTERNAL_H
#define MINIACCEL_INTERNAL_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <miniaccel_uapi.h>

#define MA_DRIVER_MAX_CONTEXTS 64u
#define MA_DRIVER_MAX_INFLIGHT 16u
#define MA_DRIVER_MAX_QUEUE_DEPTH 256u

struct miniaccel_client {
    /* Per-open storage. Context/Buffer/Job registries arrive in M2. */
    bool closing;
};

long miniaccel_ioctl(struct file *file, unsigned int command, unsigned long arg);

#endif /* MINIACCEL_INTERNAL_H */

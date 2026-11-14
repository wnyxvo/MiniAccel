// SPDX-License-Identifier: GPL-2.0
/* MiniAccel M1-D0: character-device lifecycle and ordered workqueue foundation. */
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include "miniaccel_internal.h"

#define MA_DEVICE_NAME "mini_accel"

struct miniaccel_device {
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct workqueue_struct *workqueue;
};
static struct miniaccel_device ma_dev;

static int miniaccel_open(struct inode *inode, struct file *file)
{
    struct miniaccel_client *client;
    int ret;

    client = kzalloc(sizeof(*client), GFP_KERNEL);
    if (!client)
        return -ENOMEM;
    ret = nonseekable_open(inode, file);
    if (ret) {
        kfree(client);
        return ret;
    }
    file->private_data = client;
    return 0;
}

static int miniaccel_release(struct inode *inode, struct file *file)
{
    struct miniaccel_client *client = file->private_data;

    (void)inode;
    if (client) {
        client->closing = true;
        file->private_data = NULL;
        kfree(client);
    }
    return 0;
}

static ssize_t miniaccel_read(struct file *file, char __user *buf,
                             size_t count, loff_t *offset)
{
    (void)file;
    (void)buf;
    (void)count;
    (void)offset;
    /* Completion FIFO and blocking read arrive with M2 jobs. */
    return -EOPNOTSUPP;
}

static __poll_t miniaccel_poll(struct file *file, poll_table *wait)
{
    (void)file;
    (void)wait;
    /* No jobs can be accepted at D0, hence no completion is ready. */
    return 0;
}

static const struct file_operations miniaccel_fops = {
    .owner = THIS_MODULE,
    .open = miniaccel_open,
    .release = miniaccel_release,
    .unlocked_ioctl = miniaccel_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = compat_ptr_ioctl,
#endif
    .read = miniaccel_read,
    .poll = miniaccel_poll,
    .llseek = no_llseek,
};

static int __init miniaccel_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&ma_dev.devt, 0, 1, MA_DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&ma_dev.cdev, &miniaccel_fops);
    ma_dev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&ma_dev.cdev, ma_dev.devt, 1);
    if (ret)
        goto err_unregister;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    ma_dev.class = class_create(THIS_MODULE, MA_DEVICE_NAME);
#else
    ma_dev.class = class_create(MA_DEVICE_NAME);
#endif
    if (IS_ERR(ma_dev.class)) {
        ret = PTR_ERR(ma_dev.class);
        goto err_cdev;
    }

    ma_dev.device = device_create(ma_dev.class, NULL, ma_dev.devt,
                                  NULL, MA_DEVICE_NAME);
    if (IS_ERR(ma_dev.device)) {
        ret = PTR_ERR(ma_dev.device);
        goto err_class;
    }

    /* D0 creates an ordered executor. Job admission/execution is M2 scope. */
    ma_dev.workqueue = alloc_ordered_workqueue("mini_accel_wq", WQ_MEM_RECLAIM);
    if (!ma_dev.workqueue) {
        ret = -ENOMEM;
        goto err_device;
    }

    pr_info("mini_accel: registered ABI v%u (M1-D0; jobs disabled)\n",
            MA_UAPI_VERSION);
    return 0;

err_device:
    device_destroy(ma_dev.class, ma_dev.devt);
err_class:
    class_destroy(ma_dev.class);
err_cdev:
    cdev_del(&ma_dev.cdev);
err_unregister:
    unregister_chrdev_region(ma_dev.devt, 1);
    return ret;
}

static void __exit miniaccel_exit(void)
{
    /* Module refcount via file_operations.owner prevents unload with open fd. */
    destroy_workqueue(ma_dev.workqueue);
    device_destroy(ma_dev.class, ma_dev.devt);
    class_destroy(ma_dev.class);
    cdev_del(&ma_dev.cdev);
    unregister_chrdev_region(ma_dev.devt, 1);
    pr_info("mini_accel: unloaded\n");
}

module_init(miniaccel_init);
module_exit(miniaccel_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MiniAccel software character-device prototype (M1-D0)");
MODULE_AUTHOR("MiniAccel contributors");

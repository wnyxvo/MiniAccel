#ifndef MINIACCEL_UAPI_H
#define MINIACCEL_UAPI_H

/* MiniAccel v1 fixed-width C UAPI. No kernel pointers or C++ types. */
#include <linux/types.h>
#include <linux/ioctl.h>
#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

#define MA_UAPI_VERSION 1u
#define MA_IOC_MAGIC 'M'
#define MA_CAP_NOP   (1ULL << 0)
#define MA_CAP_DELAY (1ULL << 1)

#define MA_OP_NOP   1u
#define MA_OP_DELAY 2u

#define MA_TASK_QUEUED    0u
#define MA_TASK_COMPLETED 1u
#define MA_TASK_FAILED    2u
#define MA_TASK_CANCELED  3u
#define MA_TASK_RUNNING   4u

struct ma_device_query {
    __u32 abi_version;
    __u32 max_contexts;
    __u32 max_inflight;
    __u32 max_queue_depth;
    __u64 capability_bits;
    __u64 reserved[2];
};
struct ma_context_create {
    __u32 requested_max_inflight;
    __u32 context_handle;
    __u64 reserved[2];
};
struct ma_context_destroy {
    __u32 context_handle;
    __u32 reserved0;
    __u64 reserved[2];
};
struct ma_buffer_alloc {
    __u32 context_handle;
    __u32 buffer_handle;
    __u64 bytes;
    __u64 mmap_offset;
    __u64 reserved[2];
};
struct ma_buffer_free {
    __u32 context_handle;
    __u32 buffer_handle;
    __u64 reserved[2];
};
struct ma_task_submit {
    __u32 context_handle;
    __u32 opcode;
    __u32 delay_ms;
    __u32 flags;
    __u64 task_sequence;
    __u64 reserved[2];
};
struct ma_task_wait {
    __u32 context_handle;
    __u32 timeout_ms;
    __u64 task_sequence;
    __u32 state;
    __u32 result_errno;
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
    __u32 state;
    __u64 task_sequence;
    __u32 result_errno;
    __u32 reserved0;
    __u64 reserved[2];
};
struct ma_completion_record {
    __u32 context_handle;
    __u32 state;
    __u64 task_sequence;
    __u32 result_errno;
    __u32 reserved0;
    __u64 completed_ns;
};

#define MA_IOCTL_DEVICE_QUERY   _IOWR(MA_IOC_MAGIC, 0x00, struct ma_device_query)
#define MA_IOCTL_CREATE_CONTEXT _IOWR(MA_IOC_MAGIC, 0x01, struct ma_context_create)
#define MA_IOCTL_DESTROY_CONTEXT _IOW(MA_IOC_MAGIC, 0x02, struct ma_context_destroy)
#define MA_IOCTL_ALLOC_BUFFER   _IOWR(MA_IOC_MAGIC, 0x03, struct ma_buffer_alloc)
#define MA_IOCTL_FREE_BUFFER    _IOW(MA_IOC_MAGIC, 0x04, struct ma_buffer_free)
#define MA_IOCTL_SUBMIT_TASK    _IOWR(MA_IOC_MAGIC, 0x05, struct ma_task_submit)
#define MA_IOCTL_WAIT_TASK      _IOWR(MA_IOC_MAGIC, 0x06, struct ma_task_wait)
#define MA_IOCTL_QUERY_TASK     _IOWR(MA_IOC_MAGIC, 0x07, struct ma_task_query)
#define MA_IOCTL_REAP_TASK      _IOW(MA_IOC_MAGIC, 0x08, struct ma_task_reap)

#ifdef __cplusplus
#define MA_ABI_ASSERT(condition, text) static_assert((condition), text)
#else
#define MA_ABI_ASSERT(condition, text) _Static_assert((condition), text)
#endif
MA_ABI_ASSERT(sizeof(struct ma_device_query) == 40, "ma_device_query layout");
MA_ABI_ASSERT(sizeof(struct ma_context_create) == 24, "ma_context_create layout");
MA_ABI_ASSERT(sizeof(struct ma_context_destroy) == 24, "ma_context_destroy layout");
MA_ABI_ASSERT(sizeof(struct ma_buffer_alloc) == 40, "ma_buffer_alloc layout");
MA_ABI_ASSERT(sizeof(struct ma_buffer_free) == 24, "ma_buffer_free layout");
MA_ABI_ASSERT(sizeof(struct ma_task_submit) == 40, "ma_task_submit layout");
MA_ABI_ASSERT(sizeof(struct ma_task_wait) == 40, "ma_task_wait layout");
MA_ABI_ASSERT(sizeof(struct ma_task_reap) == 32, "ma_task_reap layout");
MA_ABI_ASSERT(sizeof(struct ma_task_query) == 40, "ma_task_query layout");
MA_ABI_ASSERT(sizeof(struct ma_completion_record) == 32, "ma_completion_record layout");
MA_ABI_ASSERT(offsetof(struct ma_device_query, capability_bits) == 16, "device capability offset");
MA_ABI_ASSERT(offsetof(struct ma_device_query, reserved) == 24, "device reserved offset");
MA_ABI_ASSERT(offsetof(struct ma_buffer_alloc, mmap_offset) == 16, "buffer mmap offset");
MA_ABI_ASSERT(offsetof(struct ma_task_submit, task_sequence) == 16, "submit sequence offset");
MA_ABI_ASSERT(offsetof(struct ma_task_wait, task_sequence) == 8, "wait sequence offset");
MA_ABI_ASSERT(offsetof(struct ma_task_query, task_sequence) == 8, "query sequence offset");
MA_ABI_ASSERT(offsetof(struct ma_completion_record, completed_ns) == 24, "completion timestamp offset");
#undef MA_ABI_ASSERT

#endif /* MINIACCEL_UAPI_H */

/* Native ioctl smoke test. Requires mini_accel.ko loaded in a disposable VM. */
#include "miniaccel_uapi.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EXPECT(cond, msg) do { if (!(cond)) { perror(msg); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/mini_accel";
    struct ma_device_query q = { 0 };
    struct ma_context_create ctx = { 0 };
    struct pollfd pfd;
    int fd = open(path, O_RDWR | O_CLOEXEC);
    char c;
    ssize_t rd;

    EXPECT(fd >= 0, "open");
    EXPECT(ioctl(fd, MA_IOCTL_DEVICE_QUERY, &q) == 0, "DEVICE_QUERY");
    EXPECT(q.abi_version == 1u, "abi_version");
    EXPECT(q.capability_bits == (MA_CAP_NOP | MA_CAP_DELAY), "capabilities");
    EXPECT(q.max_inflight == 16 && q.max_queue_depth == 256, "limits");
    EXPECT(q.max_contexts > 0, "max_contexts");
    EXPECT(q.reserved[0] == 0 && q.reserved[1] == 0, "zero output reserved");

    q.reserved[0] = 1;
    errno = 0;
    EXPECT(ioctl(fd, MA_IOCTL_DEVICE_QUERY, &q) == -1 && errno == EINVAL,
           "DEVICE_QUERY invalid reserved");
    q.reserved[0] = 0;

    errno = 0;
    EXPECT(ioctl(fd, _IOWR('X', 0, struct ma_device_query), &q) == -1 &&
           errno == ENOTTY, "invalid ioctl magic");
    errno = 0;
    EXPECT(ioctl(fd, _IOWR(MA_IOC_MAGIC, 0, __u32), &q) == -1 &&
           errno == ENOTTY, "invalid ioctl size");
    errno = 0;
    EXPECT(ioctl(fd, MA_IOCTL_CREATE_CONTEXT, &ctx) == -1 &&
           errno == EOPNOTSUPP, "unimplemented context ioctl");

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    EXPECT(poll(&pfd, 1, 0) == 0, "D0 no completion");
    errno = 0;
    rd = read(fd, &c, sizeof(c));
    EXPECT(rd == -1 && errno == EOPNOTSUPP, "D0 read unimplemented");
    EXPECT(close(fd) == 0, "close");
    puts("KT-001/KT-010 M1-D0 smoke: PASS (not full M1/M2)");
    return 0;
}

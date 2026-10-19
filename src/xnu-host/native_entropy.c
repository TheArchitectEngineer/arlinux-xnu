#define _GNU_SOURCE
#include <errno.h>
#include <sys/random.h>
#include <host/entropy.h>

static unsigned long long provided_bytes;

int
xnu_host_entropy_fill(void *buffer, unsigned long length)
{
    unsigned char *cursor = buffer;
    unsigned long remaining = length;
    while (remaining) {
        ssize_t received = getrandom(cursor, remaining, 0);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return errno;
        }
        if (received == 0)
            return EIO;
        cursor += received;
        remaining -= received;
    }
    __atomic_fetch_add(&provided_bytes, length, __ATOMIC_RELAXED);
    return 0;
}

unsigned long long
xnu_host_entropy_bytes(void)
{
    return __atomic_load_n(&provided_bytes, __ATOMIC_RELAXED);
}

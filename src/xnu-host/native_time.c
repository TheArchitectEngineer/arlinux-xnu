#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <host/time.h>

unsigned long long
xnu_host_time_ns(int continuous)
{
    struct timespec time;
    if (clock_gettime(continuous ? CLOCK_BOOTTIME : CLOCK_MONOTONIC, &time)) {
        perror("XNU host clock_gettime");
        abort();
    }
    return (unsigned long long)time.tv_sec * 1000000000ULL + time.tv_nsec;
}

void
xnu_host_sleep_until(unsigned long long deadline)
{
    struct timespec time = { .tv_sec = deadline / 1000000000ULL,
        .tv_nsec = deadline % 1000000000ULL };
    int error;
    do {
        error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, NULL);
    } while (error == EINTR);
    if (error) {
        fprintf(stderr, "XNU host clock_nanosleep: %s\n", strerror(error));
        abort();
    }
}

/* One actual monotonic, one-shot clock event source for the one virtual CPU.
 * No asynchronous signal handler enters libc or XNU. Kernel interrupt-enable
 * boundaries consume events; guest execution/idle need their own safe points. */
static int decrementer_fd = -1;

int
xnu_host_timer_initialize(void)
{
    if (decrementer_fd != -1)
        return EALREADY;
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd == -1)
        return errno;
    decrementer_fd = fd;
    return 0;
}

int
xnu_host_timer_program(unsigned int ticks)
{
    if (decrementer_fd == -1)
        return ENODEV;
    /* A zero decrementer means immediate expiry, not timerfd disarming. */
    unsigned long long ns = ticks ? ticks : 1;
    struct itimerspec event = {
        .it_value.tv_sec = ns / 1000000000ULL,
        .it_value.tv_nsec = ns % 1000000000ULL
    };
    return timerfd_settime(decrementer_fd, 0, &event, NULL) ? errno : 0;
}

int
xnu_host_timer_state(unsigned int *remaining, int *pending)
{
    if (decrementer_fd == -1 || !remaining || !pending)
        return EINVAL;
    struct itimerspec event;
    if (timerfd_gettime(decrementer_fd, &event))
        return errno;
    unsigned long long ns = (unsigned long long)event.it_value.tv_sec *
        1000000000ULL + event.it_value.tv_nsec;
    *remaining = ns > UINT32_MAX ? UINT32_MAX : (unsigned int)ns;
    struct pollfd source = { .fd = decrementer_fd, .events = POLLIN };
    int result;
    do {
        result = poll(&source, 1, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1)
        return errno;
    if (source.revents & (POLLERR | POLLHUP | POLLNVAL))
        return EIO;
    *pending = (source.revents & POLLIN) != 0;
    return 0;
}

int
xnu_host_timer_take(void)
{
    if (decrementer_fd == -1)
        return -ENODEV;
    uint64_t expirations;
    ssize_t bytes;
    do {
        bytes = read(decrementer_fd, &expirations, sizeof(expirations));
    } while (bytes == -1 && errno == EINTR);
    if (bytes == -1)
        return errno == EAGAIN ? 0 : -errno;
    if (bytes != (ssize_t)sizeof(expirations) || !expirations)
        return -EIO;
    return 1;
}

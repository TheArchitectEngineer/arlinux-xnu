#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

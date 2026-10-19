/* Feed XNU's original ARM rtclock conversion code from Linux nanosecond clocks.
 * Timer interrupt delivery and scheduler context switching remain separate. */
#include <arm/rtclock.h>
#include <kern/assert.h>
#include <kern/clock.h>
#include <kern/debug.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <host/time.h>

/* Normally provided by ARM's boot assembly data area. */
rtclock_data_t RTClockData;

void
PE_register_timebase_callback(timebase_callback_func callback)
{
    struct timebase_freq_t frequency = { .timebase_num = NSEC_PER_SEC,
        .timebase_den = 1 };
    callback(&frequency);
}

uint64_t
ml_get_timebase(void)
{
    return xnu_host_time_ns(0);
}

uint64_t
ml_get_speculative_timebase(void)
{
    return xnu_host_time_ns(0);
}

uint64_t
ml_get_hwclock(void)
{
    return xnu_host_time_ns(1);
}

int
xnu_host_time_bootstrap(void)
{
    rtclock_early_init();
    mach_timebase_info_data_t info;
    clock_timebase_info(&info);
    assert(info.numer == 1 && info.denom == 1);
    uint64_t ticks, nanoseconds;
    const uint64_t sample = 1234567890123ULL;
    nanoseconds_to_absolutetime(sample, &ticks);
    absolutetime_to_nanoseconds(ticks, &nanoseconds);
    assert(ticks == sample && nanoseconds == sample);
    uint64_t before = mach_absolute_time();
    uint64_t interval;
    clock_interval_to_absolutetime_interval(1, NSEC_PER_MSEC, &interval);
    assert(interval == NSEC_PER_MSEC);
    machine_delay_until(interval, before + interval);
    uint64_t after = mach_absolute_time();
    assert(after >= before + interval);
    assert(ml_get_hwclock() >= after);
    kprintf("XNU_HOST_TIME original rtclock conversion/delay passed numer=%u denom=%u elapsed=%llu ns (timer IRQs not connected)\n",
        info.numer, info.denom, after - before);
    return 0;
}

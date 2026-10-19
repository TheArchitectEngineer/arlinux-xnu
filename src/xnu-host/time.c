/* Feed original ARM rtclock from Linux nanoseconds and a real timerfd source.
 * Kernel safe-point FIQ delivery is in context.c; guest/idle delivery is pending. */
#include <arm/rtclock.h>
#include <arm/cpu_data_internal.h>
#include <kern/assert.h>
#include <kern/clock.h>
#include <kern/debug.h>
#include <kern/startup.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <host/time.h>

/* Normally provided by ARM's boot assembly data area. */
rtclock_data_t RTClockData;

/* Match ARM's lock budgets in the Linux nanosecond timebase, rather than
 * copying constants measured at a specific Apple hardware clock frequency.
 * Original machine_timeout_init applies boot overrides during TIMEOUTS. */
MACHINE_TIMEOUT_DEV_WRITEABLE(LockTimeOut, "lock", NSEC_PER_SEC / 4,
    MACHINE_TIMEOUT_UNIT_TIMEBASE, NULL);
MACHINE_TIMEOUT_DEV_WRITEABLE(TLockTimeOut, "ticket-lock", NSEC_PER_SEC / 8,
    MACHINE_TIMEOUT_UNIT_TIMEBASE, NULL);
MACHINE_TIMEOUT_DEV_WRITEABLE(MutexSpin, "mutex-spin", 10 * NSEC_PER_USEC,
    MACHINE_TIMEOUT_UNIT_TIMEBASE, NULL);

boolean_t
ml_delay_should_spin(uint64_t interval)
{
    /* Original ARM policy uses the virtual CPU's measured idle latency. When
     * unknown (including early boot), original clock.c decides whether its
     * interrupt/preemption context requires spinning or permits a real wait. */
    uint64_t latency = current_cpu_datap()->cpu_idle_latency;
    return latency && interval < latency;
}

boolean_t
machine_timeout_suspended(void)
{
    /* As on upstream ARM64, lock timeout accounting remains active. */
    return FALSE;
}


void
PE_register_timebase_callback(timebase_callback_func callback)
{
    struct timebase_freq_t frequency = { .timebase_num = NSEC_PER_SEC,
        .timebase_den = 1 };
    callback(&frequency);
}

void
ml_set_decrementer(uint32_t ticks)
{
    int error = xnu_host_timer_program(ticks);
    if (error)
        panic("host decrementer program ticks=%u error=%d", ticks, error);
}

uint32_t
ml_get_decrementer(void)
{
    unsigned int remaining;
    int pending;
    int error = xnu_host_timer_state(&remaining, &pending);
    if (error)
        panic("host decrementer query error=%d", error);
    return remaining;
}

boolean_t
ml_get_timer_pending(void)
{
    if (xnu_host_timer_interrupt_active())
        return TRUE;
    unsigned int remaining;
    int pending;
    int error = xnu_host_timer_state(&remaining, &pending);
    if (error)
        panic("host timer pending query error=%d", error);
    return pending;
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

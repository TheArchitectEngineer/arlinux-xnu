#ifndef ARLINUX_XNU_HOST_TIME_H
#define ARLINUX_XNU_HOST_TIME_H
/* Nanosecond ticks across the Linux/XNU ABI, independent of libc typedefs. */
unsigned long long xnu_host_time_ns(int continuous);
void xnu_host_sleep_until(unsigned long long deadline);
int xnu_host_time_bootstrap(void);
/* Linux timerfd errors are errno values; take returns 1/0 or negative errno. */
int xnu_host_timer_initialize(void);
int xnu_host_timer_program(unsigned int ticks);
int xnu_host_timer_state(unsigned int *remaining, int *pending);
int xnu_host_timer_take(void);
/* Kernel-side latch while original FIQ handles an already consumed event. */
int xnu_host_timer_interrupt_active(void);

#endif

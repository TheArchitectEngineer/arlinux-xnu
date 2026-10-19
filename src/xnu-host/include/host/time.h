#ifndef ARLINUX_XNU_HOST_TIME_H
#define ARLINUX_XNU_HOST_TIME_H
/* Nanosecond ticks across the Linux/XNU ABI, independent of libc typedefs. */
unsigned long long xnu_host_time_ns(int continuous);
void xnu_host_sleep_until(unsigned long long deadline);
int xnu_host_time_bootstrap(void);
#endif

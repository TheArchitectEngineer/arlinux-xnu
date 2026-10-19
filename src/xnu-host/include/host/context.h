#ifndef ARLINUX_XNU_HOST_CONTEXT_H
#define ARLINUX_XNU_HOST_CONTEXT_H

/* Pointer-sized lengths keep this ABI independent of host/XNU libc headers. */
int xnu_host_context_initialize(void *stack_base, unsigned long stack_size,
    void *exception_base, unsigned long exception_size);

void xnu_host_context_adopt_boot_thread(void);
void xnu_host_timer_cpu_initialize(void);
void xnu_host_boot_phase(const char *message);
unsigned long xnu_host_kernel_stack_size(void);
unsigned long xnu_host_kernel_stack_top(void);
int xnu_host_run_on_stack(int (*entry)(void), unsigned long stack_top);

#endif

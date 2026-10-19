#ifndef ARLINUX_XNU_HOST_CONTEXT_H
#define ARLINUX_XNU_HOST_CONTEXT_H

/* Pointer-sized lengths keep this ABI independent of host/XNU libc headers. */
int xnu_host_context_initialize(void *stack_base, unsigned long stack_size,
    void *exception_base, unsigned long exception_size);

#endif

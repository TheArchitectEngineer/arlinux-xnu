/* Linux-facing services for the userspace kernel. This translation unit uses
 * host libc; the kernel translation units use the pinned XNU headers. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <usermode_vm.h>

#include <host/context.h>
#include <host/guest.h>
#include <host/fault.h>
#include <host/time.h>

static int boot_argc;
static char **boot_argv;

static const char *
boot_value(const char *name)
{
    size_t length = strlen(name);
    for (int i = 1; i < boot_argc; i++) {
        if (strncmp(boot_argv[i], name, length) == 0) {
            if (boot_argv[i][length] == '=')
                return boot_argv[i] + length + 1;
            if (boot_argv[i][length] == '\0')
                return "1";
        }
    }
    return NULL;
}

int
PE_parse_boot_argn(const char *name, void *output, int bytes)
{
    const char *value = boot_value(name);
    if (!value)
        return 0;
    if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8)
        return 0;
    char *end;
    errno = 0;
    uint64_t number = strtoull(value, &end, 0);
    if (errno || end == value || *end)
        return 0;
    /* Both the host and guest are little-endian ARM64. */
    memcpy(output, &number, (size_t)bytes);
    fprintf(stderr, "BOOT_ARG %s=%s bytes=%d\n", name, value, bytes);
    return 1;
}

int
PE_parse_boot_arg_str(const char *name, char *output, int bytes)
{
    const char *value = boot_value(name);
    if (!value || bytes <= 0 || strlen(value) >= (size_t)bytes)
        return 0;
    memcpy(output, value, strlen(value) + 1);
    return 1;
}

void
kprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void
xnu_host_boot_phase(const char *message)
{
    fprintf(stderr, "XNU_BOOT_PHASE %s\n", message);
}

#ifndef XNU_HOST_BOOT_VM
/* The standalone diagnostic does not link the full trace service. Real VM
 * startup uses original kdebug.c; its early trace buffer is not stdout. */
void
kernel_debug_string_early(const char *message)
{
    xnu_host_boot_phase(message);
}
#endif

_Noreturn void
panic(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fputs("XNU_PANIC ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    abort();
}

_Noreturn void
Assert(const char *file, int line, const char *expression)
{
    panic("%s:%d: %s", file, line, expression);
}

extern int xnu_host_kernel_early_startup(void);
#ifdef XNU_HOST_BOOT_VM
extern int xnu_host_vm_bootstrap(void);
#endif

int
main(int argc, char **argv)
{
    boot_argc = argc;
    boot_argv = argv;
    pthread_attr_t attributes;
    void *stack_base;
    size_t stack_size;
    int error = pthread_getattr_np(pthread_self(), &attributes);
    if (error) {
        fprintf(stderr, "pthread_getattr_np: %s\n", strerror(error));
        return 1;
    }
    error = pthread_attr_getstack(&attributes, &stack_base, &stack_size);
    pthread_attr_destroy(&attributes);
    if (error) {
        fprintf(stderr, "pthread_attr_getstack: %s\n", strerror(error));
        return 1;
    }
#ifdef XNU_HOST_BOOT_VM
    stack_size = xnu_host_kernel_stack_size();
    if (stack_size > XNU_HOST_PHYSICAL_VIEW - XNU_HOST_BOOT_STACK_BASE) {
        fputs("XNU bootstrap stack exceeds its reserved arena\n", stderr);
        return 1;
    }
    stack_base = mmap((void *)XNU_HOST_BOOT_STACK_BASE, stack_size,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack_base == MAP_FAILED) {
        perror("XNU bootstrap stack mmap");
        return 1;
    }
    if ((uintptr_t)stack_base != XNU_HOST_BOOT_STACK_BASE) {
        fputs("XNU bootstrap stack mapped at an unexpected address\n", stderr);
        return 1;
    }
#endif
    /* These stacks live for the host process lifetime. */
    size_t exception_size = 128 * 1024;
    void *exception_base = malloc(exception_size);
    if (!exception_base) {
        perror("exception stack");
        return 1;
    }
    stack_t alternate = { .ss_sp = exception_base, .ss_size = exception_size };
    if (sigaltstack(&alternate, NULL)) {
        perror("sigaltstack");
        return 1;
    }
    error = xnu_host_fault_initialize();
    if (error) {
        fprintf(stderr, "fault handler initialization: %s\n", strerror(error));
        return 1;
    }
    error = xnu_host_timer_initialize();
    if (error) {
        fprintf(stderr, "timer initialization: %s\n", strerror(error));
        return 1;
    }
#ifdef XNU_HOST_BOOT_VM
    error = xnu_host_guest_initialize();
    if (error) {
        fprintf(stderr, "guest CPU native stack initialization: %s\n", strerror(error));
        return 1;
    }
#endif
    if (xnu_host_context_initialize(stack_base, stack_size, exception_base, exception_size))
        return 1;
#ifdef XNU_HOST_BOOT_VM
    return xnu_host_run_on_stack(xnu_host_vm_bootstrap, xnu_host_kernel_stack_top());
#else
    return xnu_host_kernel_early_startup();
#endif
}

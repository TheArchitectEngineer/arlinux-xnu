/* Linux-facing services for the userspace kernel. This translation unit uses
 * host libc; the kernel translation units use the pinned XNU headers. */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
kernel_debug_string_early(const char *message)
{
    fprintf(stderr, "XNU_BOOT_PHASE %s\n", message);
}

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

int
main(int argc, char **argv)
{
    boot_argc = argc;
    boot_argv = argv;
    return xnu_host_kernel_early_startup();
}

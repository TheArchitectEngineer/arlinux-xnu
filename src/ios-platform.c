/* A genuine injected Mach-O platform library. Its normal dyld constructor runs
 * after its UIKit/CF/CG dependencies initialize and before the application main.
 * Application binaries and their controls are not patched or replaced.
 */
#include "ios-platform-abi.h"
void arlinux_ios_lifecycle_diagnose(const struct arlinux_ios_platform_api *api);
void arlinux_ios_lifecycle_start(const struct arlinux_ios_platform_api *api);
void arlinux_ios_rendering_start(void);
void arlinux_ios_input_start(const struct arlinux_ios_platform_api *api);
void arlinux_ios_filesystem_start(void);
void arlinux_ios_color_trace_start(void);
__attribute__((constructor)) static void arlinux_ios_platform_start(void)
{
    register unsigned long version __asm__("x0")=ARLINUX_IOS_PLATFORM_ABI_VERSION;
    register unsigned long operation __asm__("x3")=ARLINUX_IOS_PLATFORM_QUERY;
    register unsigned long number __asm__("x16")=0x80000000UL; /* Darwin platform syscall */
    unsigned int failed;
    __asm__ volatile("svc #0x80\n\tcset %w1, cs"
        : "+r"(version),"=r"(failed) : "r"(operation),"r"(number) : "cc","memory");
    if (failed || !version) __builtin_trap();
    const struct arlinux_ios_platform_api *api=(const void *)version;
    if (api->version!=ARLINUX_IOS_PLATFORM_ABI_VERSION || api->size!=sizeof(*api) ||
        !api->attach || !api->read_window || !api->read_input || api->attach()) __builtin_trap();
    arlinux_ios_lifecycle_diagnose(api);
    arlinux_ios_color_trace_start();
    arlinux_ios_filesystem_start();
    arlinux_ios_rendering_start();
    arlinux_ios_input_start(api);
    arlinux_ios_lifecycle_start(api);
}

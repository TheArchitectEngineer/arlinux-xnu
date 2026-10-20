/* A genuine injected Mach-O platform library. Its normal dyld constructor runs
 * after its UIKit/CF/CG dependencies initialize and before the application main.
 * Application binaries and their controls are not patched or replaced.
 */
#include "ios-platform-abi.h"
__attribute__((constructor)) static void arlinux_ios_platform_start(void)
{
    register unsigned long version __asm__("x0")=ARLINUX_IOS_PLATFORM_ABI_VERSION;
    register unsigned long operation __asm__("x3")=ARLINUX_IOS_PLATFORM_QUERY;
    register unsigned long number __asm__("x16")=0x80000000UL; /* Darwin platform syscall */
    unsigned int failed;
    __asm__ volatile("svc #0x80\n\tcset %w1, cs"
        : "+r"(version),"=r"(failed) : "r"(operation),"r"(number) : "cc","memory");
    if (failed || !version) __builtin_trap();
    if (((arlinux_ios_platform_attach_fn)version)()) __builtin_trap();
}

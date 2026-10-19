/* Host platform preparation followed by original XNU kernel bootstrap.
 * No allocator sample or substitute task/syscall startup is an acceptance gate. */
#include <host/context.h>
#include <kern/debug.h>
#include <kern/startup.h>
#include <kern/processor.h>
#include <kern/timer_call.h>

extern int xnu_host_kernel_initialize(void);

int
xnu_host_vm_bootstrap(void)
{
    xnu_host_context_adopt_boot_thread();
    int result = xnu_host_kernel_initialize();
    if (result)
        return result;
    /* Original arm/arm_init.c performs these before machine_startup reaches
     * kernel_bootstrap. Original kernel_bootstrap itself calls VM exactly once,
     * then advances real startup phases and creates kernel_bootstrap_thread. */
    xnu_host_boot_phase("timer_call_init");
    timer_call_init();
    xnu_host_boot_phase("timer_cpu_initialize");
    xnu_host_timer_cpu_initialize();
    xnu_host_boot_phase("processor_bootstrap");
    processor_bootstrap();
    xnu_host_boot_phase("original kernel_bootstrap");
    kernel_bootstrap();
    panic("original kernel_bootstrap returned instead of loading its first thread");
}

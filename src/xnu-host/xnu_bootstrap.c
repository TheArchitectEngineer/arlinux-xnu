/* Entry into the real XNU early initialization path from a Linux process.
 * VM, tasks, interrupts and Mach/BSD dispatch are subsequent bring-up stages. */
#include <arm/cpu_data.h>
#include <arm/cpu_data_internal.h>
#include <kern/thread.h>
#include <kern/assert.h>
#include <kern/startup.h>
#include <pexpert/pexpert.h>
#include <host/memory.h>
#include <host/time.h>

int
xnu_host_kernel_early_startup(void)
{
    assert(current_thread() == current_thread_volatile());
    assert(current_cpu_datap()->cpu_active_thread == current_thread());
    assert(current_cpu_datap()->cpu_number == 0);
    assert(exception_stack_pointer() != 0);
    int error = xnu_host_pmap_bootstrap();
    if (error)
        return error;
    error = xnu_host_time_bootstrap();
    if (error)
        return error;
    kernel_startup_bootstrap();
    assert(startup_phase == STARTUP_SUB_LOCKS);
    kprintf("XNU_EARLY_STARTUP phase=%u debug=%u (VM not initialized)\n",
        startup_phase, startup_debug);
    return 0;
}

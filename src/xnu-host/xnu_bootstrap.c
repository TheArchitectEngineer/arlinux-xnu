/* Entry into the real XNU early initialization path from a Linux process.
 * VM, tasks, interrupts and Mach/BSD dispatch are subsequent bring-up stages. */
#include <kern/assert.h>
#include <kern/startup.h>
#include <pexpert/pexpert.h>

int
xnu_host_kernel_early_startup(void)
{
    kernel_startup_bootstrap();
    assert(startup_phase == STARTUP_SUB_LOCKS);
    kprintf("XNU_EARLY_STARTUP phase=%u debug=%u (VM not initialized)\n",
        startup_phase, startup_debug);
    return 0;
}

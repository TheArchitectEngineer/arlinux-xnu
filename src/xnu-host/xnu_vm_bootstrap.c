/* The next real-kernel entry: keep this separate from the verified early boot
 * while VM's required kernel/platform dependencies are being brought up. */
#include <host/context.h>
#include <vm/vm_init.h>
#include <pexpert/pexpert.h>

extern int xnu_host_kernel_early_startup(void);

int
xnu_host_vm_bootstrap(void)
{
    xnu_host_context_adopt_boot_thread();
    int result = xnu_host_kernel_early_startup();
    if (result)
        return result;
    kprintf("XNU_VM_BOOTSTRAP begin\n");
    vm_mem_bootstrap();
    kprintf("XNU_VM_BOOTSTRAP initialized (guest execution not connected)\n");
    return 0;
}

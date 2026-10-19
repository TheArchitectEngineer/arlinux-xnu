/* The next real-kernel entry: keep this separate from the verified early boot
 * while VM's required kernel/platform dependencies are being brought up. */
#include <host/context.h>
#include <vm/vm_init.h>
#include <pexpert/pexpert.h>
#include <kern/assert.h>
#include <kern/startup.h>
#include <kern/kalloc.h>
#include <kern/processor.h>
#include <kern/timer_call.h>
#include <sys/kdebug_kernel.h>

extern int xnu_host_kernel_early_startup(void);

int
xnu_host_vm_bootstrap(void)
{
    xnu_host_context_adopt_boot_thread();
    int result = xnu_host_kernel_early_startup();
    if (result)
        return result;
    /* Match ARM's pre-VM order: locks/tunables, timer infrastructure, then
     * the real boot processor/pset and global task/thread queues. */
    kernel_debug_string_early("timer_call_init");
    timer_call_init();
    kernel_debug_string_early("processor_bootstrap");
    processor_bootstrap();
    kprintf("XNU_VM_BOOTSTRAP begin\n");
    vm_mem_bootstrap();
    assert(startup_phase == STARTUP_SUB_ZALLOC);
    void *buffer = kalloc_data(PAGE_SIZE, Z_WAITOK | Z_ZERO);
    assert(buffer != NULL);
    volatile unsigned char *bytes = buffer;
    for (vm_size_t i = 0; i < PAGE_SIZE; i++) {
        assert(bytes[i] == 0);
        bytes[i] = (unsigned char)i;
    }
    for (vm_size_t i = 0; i < PAGE_SIZE; i++)
        assert(bytes[i] == (unsigned char)i);
    kfree_data(buffer, PAGE_SIZE);
    kprintf("XNU_VM_ALLOCATION real kalloc_data zero/write/read/free passed bytes=%lu\n", (unsigned long)PAGE_SIZE);
    kprintf("XNU_VM_BOOTSTRAP initialized (guest execution not connected)\n");
    return 0;
}

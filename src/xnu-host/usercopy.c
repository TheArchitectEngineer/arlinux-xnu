/* User copy transport for an isolated instruction-engine address space.
 * Original copyio.c still validates every public call. VM resolves permissions,
 * pager faults and COW; no Unicorn memory API or raw guest VA dereference. */
#include <arm64/usermode_copyio.h>
#include <host/usercopy.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <kern/cpu_data.h>
#include <machine/machine_routines.h>
#include <sys/errno.h>
#include <mach/vm_statistics.h>
#include <vm/vm_map.h>
#include <vm/vm_fault.h>
#include <string.h>

/* Return with preemption disabled only on success. The thread owns its current
 * map, exactly as in original kernel copyio abort handling. Faulting may block;
 * after it returns re-read current map and revalidate the pmap before access.
 * No XNU thread can unmap/reclaim this frame during the subsequent CPU copy. */
static int
user_span(vm_map_offset_t address, vm_size_t bytes, vm_prot_t access, void **backing)
{
    for (;;) {
        disable_preemption();
        vm_map_t map = current_thread()->map;
        if (map == VM_MAP_NULL || map->pmap == kernel_pmap ||
            address < vm_map_min(map) || address >= vm_map_max(map) ||
            bytes > vm_map_max(map) - address) {
            enable_preemption();
            return EFAULT;
        }
        pmap_paddr_t physical;
        if (xnu_usermode_pmap_page(map->pmap, address, access, &physical)) {
            *backing = (void *)phystokv(physical);
            return 0;
        }
        enable_preemption();
        /* Match the original kernel abort's interrupt-context restriction.
         * A copy needing a blocking VM fault cannot run under a spinlock or
         * masked interrupts. Resident accesses above do not require a fault. */
        if (ml_at_interrupt_context() || get_preemption_level() != 0 ||
            !ml_get_interrupts_enabled())
            panic("host user copy requires VM fault in nonfaultable context va=%#llx", address);
        kern_return_t result = vm_fault(map, address, access, FALSE,
            VM_KERN_MEMORY_NONE, THREAD_UNINT, PMAP_NULL, 0);
        if (result != KERN_SUCCESS)
            return EFAULT;
        /* A competing map/protection/pageout operation may have run before
         * we regained the CPU. Never use an unpinned result from vm_fault. */
    }
}

static void
finish_span(void)
{
    enable_preemption();
    /* Native kernel IRQ delivery is cooperative. Each page boundary is a
     * safe point; preserve the caller's virtual interrupt mask. */
    if (ml_get_interrupts_enabled())
        (void)ml_set_interrupts_enabled(TRUE);
}

static int
copy_bytes(vm_map_offset_t user, void *kernel, vm_size_t bytes, bool write)
{
    unsigned char *buffer = kernel;
    while (bytes) {
        vm_size_t count = MIN(bytes, PAGE_SIZE - (user & PAGE_MASK));
        void *backing;
        int result = user_span(user, count, write ? VM_PROT_WRITE : VM_PROT_READ, &backing);
        if (result)
            return result;
        if (write)
            memcpy(backing, buffer, count);
        else
            memcpy(buffer, backing, count);
        finish_span();
        user += count;
        buffer += count;
        bytes -= count;
    }
    return 0;
}

int
xnu_usermode_copyin(vm_map_offset_t user, void *kernel, vm_size_t bytes)
{
    return copy_bytes(user, kernel, bytes, false);
}

int
xnu_usermode_copyout(const void *kernel, vm_map_offset_t user, vm_size_t bytes)
{
    return copy_bytes(user, (void *)kernel, bytes, true);
}

int
xnu_usermode_copyinstr(vm_map_offset_t user, char *kernel, vm_size_t maximum,
    vm_size_t *actual)
{
    vm_size_t copied = 0;
    *actual = 0;
    while (copied < maximum) {
        vm_size_t count = MIN(maximum - copied, PAGE_SIZE - (user & PAGE_MASK));
        void *frame;
        int result = user_span(user, count, VM_PROT_READ, &frame);
        if (result)
            return result;
        unsigned char *backing = frame;
        bool terminated = false;
        vm_size_t consumed = 0;
        while (consumed < count) {
            unsigned char byte = backing[consumed++];
            kernel[copied++] = byte;
            if (byte == 0) {
                terminated = true;
                break;
            }
        }
        finish_span();
        if (terminated) {
            *actual = copied;
            return 0;
        }
        user += consumed;
    }
    *actual = copied;
    return ENAMETOOLONG;
}

int
xnu_usermode_copy_atomic(vm_map_offset_t user, void *value,
    unsigned int bytes, int write)
{
    if ((bytes != 4 && bytes != 8) || (user & (bytes - 1)))
        return EINVAL;
    void *backing;
    int result = user_span(user, bytes, write ? VM_PROT_WRITE : VM_PROT_READ, &backing);
    if (result)
        return result;
    if (bytes == 4) {
        uint32_t number;
        if (write) {
            memcpy(&number, value, sizeof(number));
            __atomic_store_n((uint32_t *)backing, number, __ATOMIC_RELAXED);
        } else {
            number = __atomic_load_n((uint32_t *)backing, __ATOMIC_RELAXED);
            memcpy(value, &number, sizeof(number));
        }
    } else {
        uint64_t number;
        if (write) {
            memcpy(&number, value, sizeof(number));
            __atomic_store_n((uint64_t *)backing, number, __ATOMIC_RELAXED);
        } else {
            number = __atomic_load_n((uint64_t *)backing, __ATOMIC_RELAXED);
            memcpy(value, &number, sizeof(number));
        }
    }
    finish_span();
    return 0;
}

int
xnu_usermode_wait32(vm_map_offset_t user, uint32_t expected)
{
    uint32_t observed;
    int result = xnu_usermode_copy_atomic(user, &observed, 4, false);
    if (result)
        return result;
    /* WFE is a hint and may complete spuriously. With one virtual CPU, do not
     * block the host waiting for a store from another stopped guest thread.
     * This is not ulock completion: original ulock scheduling remains required. */
    return observed == expected ? 0 : ESTALE;
}

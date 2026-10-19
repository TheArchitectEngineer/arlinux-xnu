/* Host pmap primitives used by XNU's original vm_resident bootstrap.
 * This is not the ARM hardware page-table implementation. Linux owns mappings
 * and TLB/cache maintenance; XNU still owns VM objects/maps and page allocation.
 * User pmaps, reverse mappings and their accounting are not connected yet. */
#include <arm/usermode_vm.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <vm/pmap.h>
#include <string.h>
#include <host/memory.h>

static struct pmap kernel_pmap_store;
const pmap_t kernel_pmap = &kernel_pmap_store;
unsigned long gVirtBase, gPhysBase, gPhysSize;
uint64_t gDramBase, gDramSize;
uint64_t max_mem, max_mem_actual, sane_size;
static ppnum_t next_frame;
static ppnum_t frame_limit;

uint_t
pmap_free_pages(void)
{
    assert(next_frame != 0);
    return frame_limit - next_frame;
}

uint_t
pmap_free_pages_span(void)
{
    return pmap_free_pages();
}

boolean_t
pmap_next_page(ppnum_t *page)
{
    assert(next_frame != 0);
    if (next_frame == frame_limit)
        return FALSE;
    *page = next_frame++;
    return TRUE;
}

boolean_t
pmap_next_page_hi(ppnum_t *page, boolean_t might_free)
{
    (void)might_free;
    return pmap_next_page(page);
}

void
pmap_virtual_space(vm_offset_t *start, vm_offset_t *end)
{
    *start = XNU_HOST_HEAP_BASE;
    *end = XNU_HOST_KERNEL_LIMIT;
}

boolean_t
pmap_virtual_region(unsigned int region, vm_map_offset_t *start, vm_map_size_t *size)
{
    if (region != 0)
        return FALSE;
    *start = XNU_HOST_KERNEL_BASE;
    *size = XNU_HOST_HEAP_BASE - XNU_HOST_KERNEL_BASE;
    return TRUE;
}

void
pmap_init(void)
{
    assert(next_frame != 0);
    kprintf("XNU_HOST_PMAP init remaining=%u (kernel mappings only)\n", pmap_free_pages());
}

kern_return_t
pmap_enter(pmap_t pmap, vm_map_offset_t address, ppnum_t page, vm_prot_t protection,
    vm_prot_t fault_type, unsigned int flags, boolean_t wired)
{
    (void)fault_type;
    (void)flags; /* Hardware cacheability is owned by Linux. */
    (void)wired; /* Linux's shared memfd remains the physical backing store. */
    if (pmap != kernel_pmap)
        return KERN_NOT_SUPPORTED;
    int error = xnu_host_memory_map(address, page, protection);
    return error ? KERN_FAILURE : KERN_SUCCESS;
}

ppnum_t
pmap_find_phys(pmap_t pmap, addr64_t address)
{
    if (pmap != kernel_pmap)
        return 0;
    return xnu_host_memory_lookup(address);
}

vm_map_address_t
phystokv(pmap_paddr_t physical)
{
    assert(physical >= gPhysBase && physical - gPhysBase < gPhysSize);
    return gVirtBase + physical - gPhysBase;
}

void
(pmap_zero_part_page)(ppnum_t page, vm_offset_t offset, vm_size_t length)
{
    assert(page && page < frame_limit);
    assert(offset <= PAGE_SIZE && length <= PAGE_SIZE - offset);
    memset((void *)(phystokv((pmap_paddr_t)page * PAGE_SIZE) + offset), 0, length);
}

void
(pmap_zero_page)(ppnum_t page)
{
    (pmap_zero_part_page)(page, 0, PAGE_SIZE);
}

void
(pmap_copy_part_page)(ppnum_t source, vm_offset_t source_offset,
    ppnum_t target, vm_offset_t target_offset, vm_size_t length)
{
    assert(source && source < frame_limit && target && target < frame_limit);
    assert(source_offset <= PAGE_SIZE && length <= PAGE_SIZE - source_offset);
    assert(target_offset <= PAGE_SIZE && length <= PAGE_SIZE - target_offset);
    memmove((void *)(phystokv((pmap_paddr_t)target * PAGE_SIZE) + target_offset),
        (void *)(phystokv((pmap_paddr_t)source * PAGE_SIZE) + source_offset), length);
}

void
(pmap_copy_page)(ppnum_t source, ppnum_t target)
{
    (pmap_copy_part_page)(source, 0, target, 0, PAGE_SIZE);
}

int
xnu_host_pmap_bootstrap(void)
{
    assert(PAGE_SIZE == XNU_HOST_PAGE_SIZE);
    assert(next_frame == 0);
    int error = xnu_host_memory_initialize();
    if (error) {
        kprintf("XNU_HOST_MEMORY initialize failed error=%d\n", error);
        return error;
    }
    gVirtBase = XNU_HOST_PHYSICAL_VIEW;
    gPhysBase = PAGE_SIZE; /* ppn 0 denotes an absent mapping. */
    gPhysSize = XNU_HOST_PHYSICAL_BYTES;
    gDramBase = gPhysBase;
    gDramSize = gPhysSize;
    max_mem = max_mem_actual = sane_size = gPhysSize;
    next_frame = 1;
    frame_limit = 1 + gPhysSize / PAGE_SIZE;
    kernel_pmap->min = XNU_HOST_KERNEL_BASE;
    kernel_pmap->max = XNU_HOST_KERNEL_LIMIT;
    kernel_pmap->type = PMAP_TYPE_KERNEL;
    kernel_pmap->ref_count = 1;
    kernel_pmap->nx_enabled = true;
    kernel_pmap->is_64bit = true;

    /* Exercise the backend through its real pmap API before VM consumes it.
     * Two bootstrap frames stay reserved, like other early-boot allocations. */
    ppnum_t first, second;
    assert(pmap_next_page(&first) && pmap_next_page(&second));
    vm_map_offset_t a = XNU_HOST_HEAP_BASE;
    vm_map_offset_t b = a + PAGE_SIZE;
    assert(pmap_enter(kernel_pmap, a, first, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    assert(pmap_enter(kernel_pmap, b, first, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    volatile unsigned char *left = (void *)a, *right = (void *)b;
    left[0] = 0x5a;
    left[PAGE_SIZE - 1] = 0xa5;
    assert(right[0] == 0x5a && right[PAGE_SIZE - 1] == 0xa5);
    assert(pmap_find_phys(kernel_pmap, b + PAGE_SIZE - 1) == first);
    (pmap_copy_page)(first, second);
    volatile unsigned char *copy = (void *)phystokv((pmap_paddr_t)second * PAGE_SIZE);
    assert(copy[0] == 0x5a && copy[PAGE_SIZE - 1] == 0xa5);
    (pmap_zero_page)(first);
    assert(right[0] == 0 && right[PAGE_SIZE - 1] == 0);
    assert(copy[0] == 0x5a && copy[PAGE_SIZE - 1] == 0xa5);
    (pmap_zero_page)(second);
    assert(xnu_host_memory_unmap(a) == 0 && xnu_host_memory_unmap(b) == 0);
    assert(pmap_find_phys(kernel_pmap, a) == 0);
    kprintf("XNU_HOST_PMAP shared-alias/copy/zero/unmap passed free_pages=%u (VM not initialized)\n",
        pmap_free_pages());
    return 0;
}

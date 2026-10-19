/* Host pmap primitives used by XNU's original vm_resident bootstrap.
 * This is not the ARM hardware page-table implementation. Linux owns mappings
 * and TLB/cache maintenance; XNU still owns VM objects/maps and page allocation.
 * User pmaps and pmap accounting are not connected yet. */
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

void
pmap_require(pmap_t pmap)
{
    assert(pmap == kernel_pmap); /* User pmaps are not allocated by this host yet. */
}

bool
pmap_verify_free(ppnum_t page)
{
    int mapped = xnu_host_memory_page_has_mappings(page);
    assert(mapped >= 0);
    return mapped == 0;
}

void
pmap_assert_free(ppnum_t page)
{
    assert(pmap_verify_free(page));
}

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

kern_return_t
pmap_enter_options(pmap_t pmap, vm_map_offset_t address, ppnum_t page,
    vm_prot_t protection, vm_prot_t fault_type, unsigned int flags,
    boolean_t wired, unsigned int options, void *arg)
{
    (void)options;
    (void)arg;
    return pmap_enter(pmap, address, page, protection, fault_type, flags, wired);
}

kern_return_t
pmap_enter_options_addr(pmap_t pmap, vm_map_offset_t address, pmap_paddr_t physical,
    vm_prot_t protection, vm_prot_t fault_type, unsigned int flags,
    boolean_t wired, unsigned int options, void *arg)
{
    if (physical & PAGE_MASK)
        return KERN_INVALID_ARGUMENT;
    if (physical / PAGE_SIZE > (ppnum_t)~0u)
        return KERN_INVALID_ARGUMENT;
    return pmap_enter_options(pmap, address, physical / PAGE_SIZE, protection,
        fault_type, flags, wired, options, arg);
}

static void
kernel_mapping_range(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end)
{
    assert(pmap == kernel_pmap);
    assert(start >= XNU_HOST_HEAP_BASE && start <= end && end <= XNU_HOST_KERNEL_LIMIT);
    assert(((start | end) & PAGE_MASK) == 0);
}

mach_vm_size_t
pmap_query_resident(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end,
    mach_vm_size_t *compressed_bytes)
{
    if (compressed_bytes)
        *compressed_bytes = 0; /* This backend has no compressed mappings. */
    if (pmap == PMAP_NULL)
        return 0;
    pmap_require(pmap);
    assert(start >= XNU_HOST_KERNEL_BASE && start <= end && end <= XNU_HOST_KERNEL_LIMIT);
    assert(((start | end) & PAGE_MASK) == 0);
    mach_vm_size_t resident = 0;
    for (vm_map_offset_t address = start; address < end; address += PAGE_SIZE) {
        if (xnu_host_memory_lookup(address))
            resident += PAGE_SIZE;
    }
    /* Count guest mapped bytes, including aliases, independently of Linux RSS. */
    return resident;
}

kern_return_t
pmap_map_block_addr(pmap_t pmap, addr64_t address, pmap_paddr_t physical,
    uint32_t pages, vm_prot_t protection, int attr, unsigned int flags)
{
    (void)flags; /* Hardware block/PTE behavior is owned by Linux. */
    for (uint32_t index = 0; index < pages; index++) {
        kern_return_t result = pmap_enter_options_addr(pmap, address, physical,
            protection, VM_PROT_NONE, attr, TRUE, 0, NULL);
        if (result != KERN_SUCCESS)
            return result; /* Preserve the actual mapping failure. */
        address += PAGE_SIZE;
        physical += PAGE_SIZE;
    }
    return KERN_SUCCESS;
}

void
pmap_remove_options(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end, int options)
{
    (void)options;
    kernel_mapping_range(pmap, start, end);
    for (vm_map_offset_t address = start; address < end; address += PAGE_SIZE) {
        if (xnu_host_memory_lookup(address)) {
            int error = xnu_host_memory_unmap(address);
            if (error)
                panic("host pmap_remove address=%#llx error=%d", address, error);
        }
    }
}

void
pmap_remove(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end)
{
    pmap_remove_options(pmap, start, end, 0);
}

void
pmap_protect_options(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end,
    vm_prot_t protection, unsigned int options, void *arg)
{
    (void)options;
    (void)arg;
    kernel_mapping_range(pmap, start, end);
    if (protection == VM_PROT_NONE) {
        pmap_remove(pmap, start, end);
        return;
    }
    for (vm_map_offset_t address = start; address < end; address += PAGE_SIZE) {
        int error = xnu_host_memory_protect(address, protection);
        if (error)
            panic("host pmap_protect address=%#llx error=%d", address, error);
    }
}

void
pmap_protect(pmap_t pmap, vm_map_offset_t start, vm_map_offset_t end, vm_prot_t protection)
{
    pmap_protect_options(pmap, start, end, protection, 0, NULL);
}

void
pmap_page_protect_options(ppnum_t page, vm_prot_t protection,
    unsigned int options, void *arg)
{
    (void)options;
    (void)arg;
    assert(page && page < frame_limit);
    int error = xnu_host_memory_protect_page(page, protection);
    if (error)
        panic("host pmap_page_protect page=%u error=%d", page, error);
}

void
pmap_page_protect(ppnum_t page, vm_prot_t protection)
{
    pmap_page_protect_options(page, protection, 0, NULL);
}

unsigned int
(pmap_disconnect_options)(ppnum_t page, unsigned int options, void *arg)
{
    (void)options;
    (void)arg;
    assert(page && page < frame_limit);
    unsigned int attributes = 0;
    int error = xnu_host_memory_disconnect(page, &attributes);
    if (error)
        panic("host pmap_disconnect page=%u error=%d", page, error);
    /* Conservative mapping history, not precise per-access dirty tracking. */
    return ((attributes & XNU_HOST_PAGE_MODIFIED) ? VM_MEM_MODIFIED : 0) |
        ((attributes & XNU_HOST_PAGE_REFERENCED) ? VM_MEM_REFERENCED : 0);
}

unsigned int
(pmap_disconnect)(ppnum_t page)
{
    return (pmap_disconnect_options)(page, 0, NULL);
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
    pmap_require(kernel_pmap);
    pmap_assert_free(first);
    pmap_assert_free(second);
    assert(second == first + 1);
    assert(pmap_map_block_addr(kernel_pmap, a, (pmap_paddr_t)first * PAGE_SIZE,
        2, VM_PROT_READ | VM_PROT_WRITE, 0, 0) == KERN_SUCCESS);
    assert(pmap_find_phys(kernel_pmap, a) == first &&
        pmap_find_phys(kernel_pmap, b) == second);
    mach_vm_size_t compressed = 123;
    assert(pmap_query_resident(kernel_pmap, a, b + 2 * PAGE_SIZE, &compressed) ==
        2 * PAGE_SIZE && compressed == 0);
    assert(!pmap_verify_free(first) && !pmap_verify_free(second));
    pmap_remove(kernel_pmap, a, b + PAGE_SIZE);
    pmap_assert_free(first);
    pmap_assert_free(second);
    assert(pmap_enter(kernel_pmap, a, first, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    assert(pmap_enter(kernel_pmap, b, first, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    assert(pmap_query_resident(kernel_pmap, a, b + PAGE_SIZE, NULL) == 2 * PAGE_SIZE);
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
    pmap_protect(kernel_pmap, a, a + PAGE_SIZE, VM_PROT_READ);
    assert(xnu_host_memory_expect_fault(a, 1) == 0);
    right[0] = 0x33;
    assert(left[0] == 0x33); /* The protected alias still shares the frame. */
    right[0] = 0;
    pmap_page_protect(first, VM_PROT_READ);
    assert(left[0] == 0 && right[0] == 0);
    assert(xnu_host_memory_expect_fault(b, 1) == 0);
    /* Replacing an alias must detach it from the previous frame's chain. */
    assert(pmap_enter(kernel_pmap, b, second, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    right[0] = 0x7b;
    assert(left[0] == 0 && copy[0] == 0x7b);
    unsigned int attributes = (pmap_disconnect)(first);
    assert((attributes & (VM_MEM_MODIFIED | VM_MEM_REFERENCED)) ==
        (VM_MEM_MODIFIED | VM_MEM_REFERENCED));
    assert(pmap_find_phys(kernel_pmap, a) == 0);
    assert(pmap_find_phys(kernel_pmap, b) == second && right[0] == 0x7b);
    assert(pmap_query_resident(kernel_pmap, a, b + PAGE_SIZE, &compressed) ==
        PAGE_SIZE && compressed == 0);
    assert(xnu_host_memory_expect_fault(a, 0) == 0);
    pmap_assert_free(first);
    assert(!pmap_verify_free(second));
    pmap_remove(kernel_pmap, b, b + PAGE_SIZE);
    assert(pmap_find_phys(kernel_pmap, b) == 0 && copy[0] == 0x7b);
    assert(xnu_host_memory_expect_fault(b, 0) == 0);
    pmap_assert_free(second);
    assert(pmap_query_resident(kernel_pmap, a, b + PAGE_SIZE, &compressed) == 0 &&
        compressed == 0 && copy[0] == 0x7b);
    (pmap_zero_page)(second);
    kprintf("XNU_HOST_PMAP resident/block/reclaim/shared-alias/copy/zero/protect/disconnect/fault passed free_pages=%u (VM not initialized)\n",
        pmap_free_pages());
    return 0;
}

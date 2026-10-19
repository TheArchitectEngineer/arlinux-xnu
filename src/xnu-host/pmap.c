/* Host pmap primitives used by XNU's original vm_resident bootstrap.
 * This is not the ARM hardware page-table implementation. Linux owns mappings
 * and TLB/cache maintenance; XNU still owns VM objects/maps and page allocation.
 * User pmap ownership is backed by real XNU allocation/ledgers and an engine.
 * User mapping, task accounting and execution are not connected yet. */
#include <arm/usermode_vm.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/locks.h>
#include <kern/cpu_data.h>
#include <kern/kalloc.h>
#include <kern/ledger.h>
#include <kern/startup.h>
#include <os/atomic_private.h>
#include <arm/pmap/pmap_pt_geometry.h>
#include <vm/pmap.h>
#include <mach/machine.h>
#include <machine/machine_routines.h>
#include <string.h>
#include <host/memory.h>
#include <host/fault.h>
#include <host/guest.h>

static struct pmap kernel_pmap_store;
const pmap_t kernel_pmap = &kernel_pmap_store;
unsigned long gVirtBase, gPhysBase, gPhysSize;
uint64_t gDramBase, gDramSize;
uint64_t max_mem, max_mem_actual, sane_size;
const vm_map_address_t physmap_base = XNU_HOST_PHYSICAL_VIEW;
const vm_map_address_t physmap_end = XNU_HOST_PHYSICAL_VIEW + XNU_HOST_PHYSICAL_BYTES;
static ppnum_t next_frame;
static ppnum_t frame_limit;

/* Retain real XNU lock/preemption behavior. There is one virtual CPU, so
 * non-preemptible native metadata operations cannot race another XNU thread.
 * VM's explicit physical-page interlocks still use distinct original locks. */
static LCK_GRP_DECLARE(host_pmap_lock_group, "usermode-pmap");
static LCK_SPIN_DECLARE(host_pmap_foreign_lock, &host_pmap_lock_group);
static lck_spin_t host_page_locks[XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE];
static bool host_page_locks_ready;

__startup_func
static void
host_pmap_locks_initialize(void)
{
    assert(MAX_CPUS == 1 && next_frame != 0);
    for (unsigned int index = 0; index < sizeof(host_page_locks) / sizeof(host_page_locks[0]); index++)
        lck_spin_init(&host_page_locks[index], &host_pmap_lock_group, LCK_ATTR_NULL);
    host_page_locks_ready = true;
}
STARTUP(LOCKS, STARTUP_RANK_LAST, host_pmap_locks_initialize);

static lck_spin_t *
host_physical_lock(ppnum_t page)
{
    assert(host_page_locks_ready);
    /* As on original ARM, non-managed/fictitious physical identifiers share a
     * separate backup interlock; they are not invented managed RAM frames. */
    return page && page < frame_limit ? &host_page_locks[page - 1] :
        &host_pmap_foreign_lock;
}

void
pmap_lock_phys_page(ppnum_t page)
{
    lck_spin_lock(host_physical_lock(page));
}

void
pmap_unlock_phys_page(ppnum_t page)
{
    lck_spin_unlock(host_physical_lock(page));
}


void
fill32_nt(addr64_t address, vm_size_t size, uint32_t pattern)
{
    assert((address & 127) == 0 && (size & 127) == 0);
    uint32_t *words = (void *)(uintptr_t)address;
    for (vm_size_t i = 0; i < size / sizeof(*words); i++)
        words[i] = pattern;
}

void
fill32_dczva(addr64_t address, vm_size_t size)
{
    /* Preserve exact byte semantics without assuming host DC ZVA support. */
    fill32_nt(address, size, 0);
}

uint64_t
ml_get_booter_memory_size(void)
{
    assert(gPhysSize != 0);
    return gPhysSize;
}

/* VM consumes this geometry even though host translations have no hardware
 * page tables. Preserve the pinned ARM 16 KiB sizes/indices. No hardware ops
 * are installed: ARM TTE walking/ASID/MMIO code is not this pmap backend. */
#if __ARM_MIXED_PAGE_SIZE__
#error USERMODE supports only the fixed 16 KiB guest geometry
#endif
#define HOST_LEVEL(n, valid, mask, block) { \
    .size = ARM_16K_TT_L##n##_SIZE, .offmask = ARM_16K_TT_L##n##_OFFMASK, \
    .shift = ARM_16K_TT_L##n##_SHIFT, .index_mask = ARM_16K_TT_L##n##_INDEX_MASK, \
    .valid_mask = (valid), .type_mask = (mask), .type_block = (block) }
const struct page_table_level_info pmap_table_level_info_16k[] = {
    HOST_LEVEL(0, ARM_TTE_VALID, ARM_TTE_TYPE_MASK, ARM_TTE_TYPE_BLOCK),
    HOST_LEVEL(1, ARM_TTE_VALID, ARM_TTE_TYPE_MASK, ARM_TTE_TYPE_BLOCK),
    HOST_LEVEL(2, ARM_TTE_VALID, ARM_TTE_TYPE_MASK, ARM_TTE_TYPE_BLOCK),
    HOST_LEVEL(3, ARM_PTE_TYPE_VALID, ARM_PTE_TYPE_MASK, ARM_TTE_TYPE_L3BLOCK),
};
#undef HOST_LEVEL
const struct page_table_attr pmap_pt_attr_16k = {
    .pta_level_info = pmap_table_level_info_16k,
    .pta_ops = NULL,
    .ap_ro = ARM_PTE_AP(AP_RORO), .ap_rw = ARM_PTE_AP(AP_RWRW),
    .ap_rona = ARM_PTE_AP(AP_RONA), .ap_rwna = ARM_PTE_AP(AP_RWNA),
    .ap_xn = ARM_PTE_PNX | ARM_PTE_NX, .ap_x = ARM_PTE_PNX,
    .pta_root_level = PMAP_TT_L1_LEVEL, .pta_commpage_level = PMAP_TT_L2_LEVEL,
    .pta_max_level = PMAP_TT_L3_LEVEL,
    .pta_page_size = XNU_HOST_PAGE_SIZE, .pta_page_shift = 14,
};
const struct page_table_attr *const native_pt_attr = &pmap_pt_attr_16k;

struct host_user_pmap {
    struct pmap map;
    struct xnu_host_guest *guest;
    struct host_user_pmap *next;
};
static struct host_user_pmap *user_pmaps;

/* Callers hold a pmap reference and disable preemption. The single virtual
 * CPU cannot switch XNU threads while registry/engine ownership is changing. */
static struct host_user_pmap **
user_pmap_link(pmap_t pmap)
{
    assert(get_preemption_level() > 0);
    struct host_user_pmap **link = &user_pmaps;
    while (*link && &(*link)->map != pmap)
        link = &(*link)->next;
    return link;
}

static void
require_pmap_locked(pmap_t pmap)
{
    assert(pmap != PMAP_NULL && os_atomic_load(&pmap->ref_count, relaxed) > 0);
    if (pmap != kernel_pmap) {
        struct host_user_pmap *user = *user_pmap_link(pmap);
        assert(user && user->guest && pmap->type == PMAP_TYPE_USER);
    }
}

void
pmap_require(pmap_t pmap)
{
    disable_preemption();
    require_pmap_locked(pmap);
    enable_preemption();
}

pmap_t
pmap_create_options(ledger_t ledger, vm_map_size_t size, unsigned int flags)
{
    /* flags=0 is also used by commpage/submap construction; it does not
     * enable AArch32 execution. Stage-2/4K/Rosetta/test modes are unsupported. */
    if (size || (flags & ~PMAP_CREATE_64BIT))
        return PMAP_NULL;
    assert(startup_phase >= STARTUP_SUB_ZALLOC && PAGE_SIZE == XNU_HOST_PAGE_SIZE);
    struct host_user_pmap *user = kalloc_type(struct host_user_pmap, Z_WAITOK | Z_ZERO);
    if (!user)
        return PMAP_NULL;
    pmap_t pmap = &user->map;
    pmap->min = 0;
    pmap->max = 1ULL << (64 - T0SZ_BOOT);
    pmap->type = PMAP_TYPE_USER;
    pmap->is_64bit = (flags & PMAP_CREATE_64BIT) != 0;
    pmap->nx_enabled = true;
    pmap->nested_region_true_end = ~0ULL;
#if ARM_PARAMETERIZED_PMAP
    pmap->pmap_pt_attr = native_pt_attr;
#endif
#if MACH_ASSERT
    memcpy(pmap->pmap_procname, "<nil>", sizeof("<nil>"));
#endif
    lck_rw_init(&pmap->rwlock, &host_pmap_lock_group, LCK_ATTR_NULL);
    disable_preemption();
    int error = xnu_host_guest_create(&user->guest);
    if (!error) {
        /* One ledger reference accompanies each pmap reference, including
         * later references retained by other VM maps. No accounting is faked. */
        ledger_reference(ledger);
        pmap->ledger = ledger;
        os_atomic_init(&pmap->ref_count, 1);
        user->next = user_pmaps;
        user_pmaps = user;
    }
    enable_preemption();
    if (error) {
        lck_rw_destroy(&pmap->rwlock, &host_pmap_lock_group);
        kfree_type(struct host_user_pmap, user);
        kprintf("host user pmap engine creation failed uc_err=%d\n", error);
        return PMAP_NULL;
    }
    return pmap;
}

void
pmap_reference(pmap_t pmap)
{
    if (pmap == PMAP_NULL)
        return;
    disable_preemption();
    require_pmap_locked(pmap);
    ledger_reference(pmap->ledger);
    int32_t references = os_atomic_inc(&pmap->ref_count, relaxed);
    if (references <= 1)
        panic("host pmap reference overflow pmap=%p refs=%d", pmap, references);
    enable_preemption();
}

void
pmap_destroy(pmap_t pmap)
{
    if (pmap == PMAP_NULL)
        return;
    disable_preemption();
    require_pmap_locked(pmap);
    ledger_t ledger = pmap->ledger;
    int32_t references = os_atomic_dec(&pmap->ref_count, release);
    struct host_user_pmap *user = NULL;
    if (references == 0) {
        os_atomic_thread_fence(acquire);
        if (pmap == kernel_pmap)
            panic("host pmap attempt to destroy kernel pmap");
        struct host_user_pmap **link = user_pmap_link(pmap);
        user = *link;
        assert(user && pmap->nested_pmap == PMAP_NULL);
        /* A successful close requires no remaining engine mappings. Never
         * discard mappings or ledger balances merely to free the object. */
        int error = xnu_host_guest_destroy(user->guest);
        if (error)
            panic("host pmap final engine release failed pmap=%p uc_err=%d", pmap, error);
        *link = user->next;
        lck_rw_destroy(&pmap->rwlock, &host_pmap_lock_group);
    } else if (references < 0) {
        panic("host pmap reference underflow pmap=%p refs=%d", pmap, references);
    }
    enable_preemption();
    if (user)
        kfree_type(struct host_user_pmap, user);
    ledger_dereference(ledger);
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
    if (pmap != kernel_pmap)
        return KERN_NOT_SUPPORTED;
    disable_preemption();
    int error = xnu_host_memory_map(address, page, protection, wired);
    if (error < 0)
        panic("host pmap_enter native mapping state address=%#llx page=%u error=%d",
            address, page, -error);
    enable_preemption();
    return error ? KERN_FAILURE : KERN_SUCCESS;
}

void
pmap_sync_page_data_phys(ppnum_t page)
{
    assert(page && page < frame_limit);
    disable_preemption();
    int error = xnu_host_memory_sync_code(page);
    if (error)
        panic("host pmap_sync_page_data_phys page=%u error=%d", page, error);
    enable_preemption();
}

void
pmap_sync_page_attributes_phys(ppnum_t page)
{
    assert(page && page < frame_limit);
    disable_preemption();
    int error = xnu_host_memory_sync_attributes(page);
    if (error)
        panic("host pmap_sync_page_attributes_phys page=%u error=%d", page, error);
    enable_preemption();
}

kern_return_t
pmap_attribute_cache_sync(ppnum_t page, vm_size_t size,
    vm_machine_attribute_t attribute, vm_machine_attribute_val_t *value)
{
    (void)attribute;
    (void)value;
    /* Same one-page contract as original ARM pmap_attribute_cache_sync. */
    assert(size <= PAGE_SIZE);
    pmap_sync_page_data_phys(page);
    return KERN_SUCCESS;
}

void
pmap_change_wiring(pmap_t pmap, vm_map_address_t address, boolean_t wired)
{
    pmap_require(pmap);
    assert(pmap == kernel_pmap); /* User wiring/accounting is not connected. */
    disable_preemption();
    int error = xnu_host_memory_change_wiring(address, wired);
    if (error)
        panic("host pmap_change_wiring address=%#llx wired=%d error=%d",
            address, wired, error);
    enable_preemption();
}

kern_return_t
pmap_enter_options(pmap_t pmap, vm_map_offset_t address, ppnum_t page,
    vm_prot_t protection, vm_prot_t fault_type, unsigned int flags,
    boolean_t wired, unsigned int options, void *arg)
{
    (void)arg;
    if (pmap != kernel_pmap)
        return KERN_NOT_SUPPORTED;
    if (options & PMAP_OPTIONS_NOENTER) {
        /* vm_fault retries with page=0 after dropping VM locks. The host
         * reserves the entire kernel VA and its mapping metadata up front,
         * so this capacity request needs no expansion and must not install
         * or replace a physical mapping. User pmaps are still unsupported. */
        if (address < XNU_HOST_HEAP_BASE || address >= XNU_HOST_KERNEL_LIMIT ||
            (address & PAGE_MASK))
            return KERN_INVALID_ARGUMENT;
        return KERN_SUCCESS;
    }
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
    if (pmap != kernel_pmap) {
        assert(start >= pmap->min && start <= end && end <= pmap->max);
        assert(((start | end) & PAGE_MASK) == 0);
        disable_preemption();
        struct host_user_pmap *user = *user_pmap_link(pmap);
        unsigned long long resident;
        int error = xnu_host_guest_query(user->guest, start, end, &resident);
        if (error)
            panic("host user pmap query failed pmap=%p uc_err=%d", pmap, error);
        enable_preemption();
        return resident;
    }
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
    if (pmap != kernel_pmap) {
        /* Empty ranges are real holes, so failed exec/map setup can unwind
         * without inventing a removal. Nonempty removal needs ledger debit. */
        if (pmap_query_resident(pmap, start, end, NULL))
            panic("host user pmap nonempty removal/accounting not connected pmap=%p", pmap);
        return;
    }
    kernel_mapping_range(pmap, start, end);
    disable_preemption();
    for (vm_map_offset_t address = start; address < end; address += PAGE_SIZE) {
        if (xnu_host_memory_lookup(address)) {
            int error = xnu_host_memory_unmap(address);
            if (error)
                panic("host pmap_remove address=%#llx error=%d", address, error);
        }
    }
    enable_preemption();
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
    (void)arg;
    kernel_mapping_range(pmap, start, end);
    boolean_t immediate = (options & PMAP_OPTIONS_PROTECT_IMMEDIATE) != 0;
    if (!immediate) {
        /* Match original ARM's protection selector. Writable requests leave
         * PTEs unchanged; VM handles any later write fault and COW itself. */
        switch (protection) {
        case VM_PROT_EXECUTE:
            /* ARM's kernel PTE has no execute-only AP encoding. */
            protection |= VM_PROT_READ;
            break;
        case VM_PROT_READ:
        case VM_PROT_READ | VM_PROT_EXECUTE:
            break;
        case VM_PROT_READ | VM_PROT_WRITE:
        case VM_PROT_ALL:
            return;
        default:
            protection = VM_PROT_NONE;
        }
    }
    if (protection == VM_PROT_NONE) {
        pmap_remove(pmap, start, end);
        return;
    }
    disable_preemption();
    for (vm_map_offset_t address = start; address < end; address += PAGE_SIZE) {
        int error = xnu_host_memory_protect_options(address, protection, immediate);
        if (error)
            panic("host pmap_protect address=%#llx error=%d", address, error);
    }
    enable_preemption();
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
    switch (protection) {
    case VM_PROT_ALL:
        return;
    case VM_PROT_READ:
    case VM_PROT_READ | VM_PROT_EXECUTE:
        break;
    default:
        protection = VM_PROT_NONE;
    }
    disable_preemption();
    int error = xnu_host_memory_protect_page(page, protection);
    if (error)
        panic("host pmap_page_protect page=%u error=%d", page, error);
    enable_preemption();
}

void
pmap_page_protect(ppnum_t page, vm_prot_t protection)
{
    pmap_page_protect_options(page, protection, 0, NULL);
}

_Static_assert(VM_MEM_MODIFIED == XNU_HOST_PAGE_MODIFIED &&
    VM_MEM_REFERENCED == XNU_HOST_PAGE_REFERENCED, "host/XNU page attribute ABI");

unsigned int
pmap_get_refmod(ppnum_t page)
{
    unsigned int attributes;
    int error = xnu_host_memory_attributes(page, &attributes);
    if (error)
        panic("host pmap_get_refmod page=%u error=%d", page, error);
    return attributes;
}

boolean_t
pmap_is_modified(ppnum_t page)
{
    return (pmap_get_refmod(page) & VM_MEM_MODIFIED) != 0;
}

boolean_t
(pmap_is_referenced)(ppnum_t page)
{
    return (pmap_get_refmod(page) & VM_MEM_REFERENCED) != 0;
}

void
pmap_set_modify(ppnum_t page)
{
    disable_preemption();
    int error = xnu_host_memory_update_attributes(page, 0, VM_MEM_MODIFIED);
    if (error)
        panic("host pmap_set_modify page=%u error=%d", page, error);
    enable_preemption();
}

void
pmap_clear_refmod_options(ppnum_t page, unsigned int mask,
    unsigned int options, void *argument)
{
    /* Linux mprotect completes permission/TLB changes synchronously. Deferred
     * flush batching is unnecessary, even when the caller requests it. */
    (void)options;
    (void)argument;
    disable_preemption();
    int error = xnu_host_memory_update_attributes(page, mask, 0);
    if (error)
        panic("host pmap_clear_refmod page=%u mask=%u error=%d", page, mask, error);
    enable_preemption();
}

void
pmap_clear_refmod(ppnum_t page, unsigned int mask)
{
    pmap_clear_refmod_options(page, mask, 0, NULL);
}

void
pmap_clear_modify(ppnum_t page)
{
    pmap_clear_refmod(page, VM_MEM_MODIFIED);
}

void
pmap_clear_reference(ppnum_t page)
{
    pmap_clear_refmod(page, VM_MEM_REFERENCED);
}

unsigned int
(pmap_disconnect_options)(ppnum_t page, unsigned int options, void *arg)
{
    (void)options;
    (void)arg;
    assert(page && page < frame_limit);
    unsigned int attributes = 0;
    disable_preemption();
    int error = xnu_host_memory_disconnect(page, &attributes);
    if (error)
        panic("host pmap_disconnect page=%u error=%d", page, error);
    enable_preemption();
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
    if (pmap == PMAP_NULL)
        return 0;
    if (pmap == kernel_pmap)
        return xnu_host_memory_lookup(address);
    pmap_require(pmap);
    if (address < pmap->min || address >= pmap->max)
        return 0;
    disable_preemption();
    struct host_user_pmap *user = *user_pmap_link(pmap);
    unsigned int page;
    int error = xnu_host_guest_lookup(user->guest, address, &page);
    if (error)
        panic("host user pmap lookup failed pmap=%p va=%#llx uc_err=%d", pmap, address, error);
    enable_preemption();
    return page;
}

pmap_paddr_t
kvtophys(vm_offset_t address)
{
    ppnum_t page = pmap_find_phys(kernel_pmap, address);
    return page ? (pmap_paddr_t)page * PAGE_SIZE + (address & PAGE_MASK) : 0;
}

vm_map_address_t
phystokv(pmap_paddr_t physical)
{
    assert(physical >= gPhysBase && physical - gPhysBase < gPhysSize);
    return gVirtBase + physical - gPhysBase;
}

void
xnu_host_pmap_modify(unsigned long address, const void *source, unsigned long bytes)
{
    assert(address >= XNU_HOST_HEAP_BASE && address <= XNU_HOST_KERNEL_LIMIT &&
        bytes <= XNU_HOST_KERNEL_LIMIT - address);
    const unsigned char *input = source;
    while (bytes) {
        ppnum_t page = xnu_host_memory_lookup(address);
        assert(page != 0);
        unsigned long offset = address & PAGE_MASK;
        unsigned long chunk = PAGE_SIZE - offset;
        if (chunk > bytes)
            chunk = bytes;
        void *target = (void *)(phystokv((pmap_paddr_t)page * PAGE_SIZE) + offset);
        if (input) {
            memcpy(target, input, chunk);
            input += chunk;
        } else {
            memset(target, 0, chunk);
        }
        address += chunk;
        bytes -= chunk;
    }
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
fillPage(ppnum_t page, unsigned int fill)
{
    assert(page && page < frame_limit);
    fill32_nt(phystokv((pmap_paddr_t)page * PAGE_SIZE), PAGE_SIZE, fill);
}

void *
pmap_map_compressor_page(ppnum_t page)
{
    return (void *)phystokv((pmap_paddr_t)page * PAGE_SIZE);
}

void
pmap_unmap_compressor_page(ppnum_t page, void *address)
{
    /* Like the original non-PPL physmap, the physical aperture is permanent. */
    assert(address == (void *)phystokv((pmap_paddr_t)page * PAGE_SIZE));
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
    machine_info.max_mem = gPhysSize;
    machine_info.memory_size = (uint32_t)MIN(gPhysSize, 0x80000000UL);
    next_frame = 1;
    frame_limit = 1 + gPhysSize / PAGE_SIZE;
    kernel_pmap->min = XNU_HOST_KERNEL_BASE;
    kernel_pmap->max = XNU_HOST_KERNEL_LIMIT;
    kernel_pmap->type = PMAP_TYPE_KERNEL;
    kernel_pmap->ref_count = 1;
    kernel_pmap->nx_enabled = true;
    kernel_pmap->is_64bit = true;
#if ARM_PARAMETERIZED_PMAP
    kernel_pmap->pmap_pt_attr = native_pt_attr;
#endif
    return 0;
}

int
xnu_host_pmap_diagnostics(void)
{
    /* Diagnostic entry only; the real VM boot path never allocates test frames.
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
    fillPage(second, 0x89abcdefu);
    volatile unsigned int *filled = (void *)copy;
    for (unsigned int index = 0; index < PAGE_SIZE / sizeof(*filled); index++)
        assert(filled[index] == 0x89abcdefu);
    assert(left[0] == 0 && right[PAGE_SIZE - 1] == 0);
    (pmap_zero_page)(second);
    pmap_protect(kernel_pmap, a, a + PAGE_SIZE, VM_PROT_READ);
    assert(xnu_host_memory_expect_fault(a, 1) == 0);
    right[0] = 0x33;
    assert(left[0] == 0x33); /* The protected alias still shares the frame. */
    right[0] = 0;
    pmap_page_protect(first, VM_PROT_READ);
    assert(left[0] == 0 && right[0] == 0);
    assert(xnu_host_memory_expect_fault(b, 1) == 0);
    unsigned char replacement = 0x61;
    xnu_host_pmap_modify(a, &replacement, 1);
    assert(left[0] == 0x61 && right[0] == 0x61);
    assert(xnu_host_memory_expect_fault(a, 1) == 0);
    xnu_host_pmap_modify(a, NULL, 1);
    assert(left[0] == 0 && right[0] == 0);
    /* Replacing an alias must detach it from the previous frame's chain. */
    assert(pmap_enter(kernel_pmap, b, second, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    right[0] = 0x7b;
    assert(left[0] == 0 && copy[0] == 0x7b);
    xnu_host_ticket_bootstrap(b);
    xnu_host_copyio_bootstrap(b);
    xnu_host_codec_bootstrap(second);
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
    kprintf("XNU_HOST_PMAP aperture/fill/resident/block/reclaim/shared-alias/copy/zero/protect/disconnect/fault passed free_pages=%u (VM not initialized)\n",
        pmap_free_pages());
    return 0;
}

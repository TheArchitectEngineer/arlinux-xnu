/* Exercise the real reservation primitive before the VM allocator consumes it. */
#define LOCK_PRIVATE 1
#include <kern/locks_internal.h>
#include <kern/ticket_lock.h>
#include <kern/assert.h>
#include <vm/pmap.h>
#include <pexpert/pexpert.h>
#include <host/fault.h>
#include <host/memory.h>
#include <string.h>

extern hw_lck_ticket_t hw_lck_ticket_reserve_orig_allow_invalid(hw_lck_ticket_t *lock);

void
xnu_host_ticket_bootstrap(unsigned long address)
{
    hw_lck_ticket_t *lock = (void *)address;
    ppnum_t page = pmap_find_phys(kernel_pmap, address);
    assert(page != 0 && (address & PAGE_MASK) == 0);
    uint32_t saved = lock->lck_value;
    hw_lck_ticket_t seed = { .lck_type = LCK_TYPE_TICKET, .lck_valid = 1,
        .cticket = 2, .nticket = 2 };
    *lock = seed;
    hw_lck_ticket_t old = hw_lck_ticket_reserve_orig_allow_invalid(lock);
    assert(old.lck_value == seed.lck_value && lock->cticket == 2 &&
        lock->nticket == 2 + HW_LCK_TICKET_LOCK_INCREMENT);
    lock->lck_valid = 0;
    uint32_t invalid = lock->lck_value;
    assert(hw_lck_ticket_reserve_orig_allow_invalid(lock).lck_value == 0 &&
        lock->lck_value == invalid);
    /* A valid-looking, writable unaligned word must be rejected unchanged.
     * Some hosts permit its exclusive load, so a hardware fault is not a guard. */
    void *misaligned = (void *)(address + 9);
    uint32_t saved_unaligned;
    memcpy(&saved_unaligned, misaligned, sizeof(saved_unaligned));
    memcpy(misaligned, &seed, sizeof(seed));
    assert(hw_lck_ticket_reserve_orig_allow_invalid(misaligned).lck_value == 0);
    assert(memcmp(misaligned, &seed, sizeof(seed)) == 0);
    memcpy(misaligned, &saved_unaligned, sizeof(saved_unaligned));
    *lock = seed;
    pmap_protect(kernel_pmap, address, address + PAGE_SIZE, VM_PROT_READ);
    unsigned long before, alternate_before, after, alternate_after;
    unsigned long segv_before, bus_before, segv_after, bus_after;
    xnu_host_fault_statistics(&before, &alternate_before);
    xnu_host_fault_signal_statistics(&segv_before, &bus_before);
    for (int attempt = 0; attempt < 2; attempt++)
        assert(hw_lck_ticket_reserve_orig_allow_invalid(lock).lck_value == 0 &&
            lock->lck_value == seed.lck_value);
    unsigned long readonly_count, readonly_alternate, unaligned_count, unaligned_alternate;
    xnu_host_fault_statistics(&readonly_count, &readonly_alternate);
    assert(hw_lck_ticket_reserve_orig_allow_invalid((void *)(address + 1)).lck_value == 0);
    xnu_host_fault_statistics(&unaligned_count, &unaligned_alternate);
    for (int attempt = 0; attempt < 2; attempt++)
        assert(hw_lck_ticket_reserve_orig_allow_invalid((void *)(address + PAGE_SIZE)).lck_value == 0);
    unsigned long unmapped_count, unmapped_alternate, bus_address;
    xnu_host_fault_statistics(&unmapped_count, &unmapped_alternate);
    assert(xnu_host_fault_bus_mapping(&bus_address) == 0);
    assert(hw_lck_ticket_reserve_orig_allow_invalid((void *)bus_address).lck_value == 0);
    assert(xnu_host_fault_bus_unmap(bus_address) == 0);
    xnu_host_fault_statistics(&after, &alternate_after);
    xnu_host_fault_signal_statistics(&segv_after, &bus_after);
    kprintf("XNU_HOST_TICKET recovery readonly=%lu/%lu unaligned=%lu/%lu unmapped=%lu/%lu bus=%lu/%lu (total/alternate)\n",
        readonly_count - before, readonly_alternate - alternate_before,
        unaligned_count - readonly_count, unaligned_alternate - readonly_alternate,
        unmapped_count - unaligned_count, unmapped_alternate - unaligned_alternate,
        after - unmapped_count, alternate_after - unmapped_alternate);
    assert(readonly_count - before == 2 && readonly_alternate - alternate_before == 2);
    assert(unaligned_count == readonly_count && unaligned_alternate == readonly_alternate);
    assert(unmapped_count - unaligned_count == 2 && unmapped_alternate - unaligned_alternate == 2);
    assert(after - unmapped_count == 1 && alternate_after - unmapped_alternate == 1);
    assert(after - before == 5 && alternate_after - alternate_before == 5);
    assert(segv_after - segv_before == 4 && bus_after - bus_before == 1);
    assert(pmap_enter(kernel_pmap, address, page, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    seed.nticket = 255;
    *lock = seed;
    old = hw_lck_ticket_reserve_orig_allow_invalid(lock);
    assert(old.nticket == 255 && lock->nticket == (uint8_t)(255 + HW_LCK_TICKET_LOCK_INCREMENT));
    lock->lck_value = saved;
    kprintf("XNU_HOST_FAULT diagnostic: expect unhandled child SIGSEGV\n");
    assert(xnu_host_memory_expect_unhandled_fault(address + PAGE_SIZE) == 0);
    kprintf("XNU_HOST_TICKET atomic/invalid/readonly/unaligned/unmapped/bus/wrap passed recovered=%lu alternate=%lu segv=%lu bus=%lu (VM not initialized)\n",
        after - before, alternate_after - alternate_before,
        segv_after - segv_before, bus_after - bus_before);
}

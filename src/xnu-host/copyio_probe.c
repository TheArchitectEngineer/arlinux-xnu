/* Exercise the actual faultable copy primitives on owned pmap mappings.
 * No fake vm_map/task is installed: copyio.c's public validation wrappers
 * cannot be exercised until real user maps and zone allocation are running. */
#include <kern/assert.h>
#include <vm/pmap.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <string.h>
#include <host/fault.h>

extern int _bcopyin(const char *, char *, vm_size_t);
extern int _bcopyout(const char *, char *, vm_size_t);
extern int _bcopyinstr(const char *, char *, vm_size_t, vm_size_t *);
extern int _copyin_atomic32(const char *, uint32_t *);
extern int _copyin_atomic64(const char *, uint64_t *);
extern int _copyout_atomic32(uint32_t, const char *);
extern int _copyout_atomic64(uint64_t, const char *);

void
xnu_host_copyio_bootstrap(unsigned long address)
{
    char *mapped = (char *)address;
    char saved[64], saved_tail[2], result[32];
    const char text[] = "XNU copy boundary";
    memcpy(saved, mapped, sizeof(saved));
    memcpy(saved_tail, mapped + PAGE_SIZE - 2, sizeof(saved_tail));
    assert(_bcopyout(text, mapped, sizeof(text)) == 0);
    assert(_bcopyin(mapped, result, sizeof(text)) == 0);
    assert(memcmp(text, result, sizeof(text)) == 0);
    vm_size_t actual = 0;
    assert(_bcopyinstr(mapped, result, sizeof(result), &actual) == 0);
    assert(actual == sizeof(text) && memcmp(text, result, actual) == 0);
    assert(_bcopyinstr(mapped, result, 4, &actual) == ENAMETOOLONG && actual == 4);
    assert(_bcopyinstr(mapped, result, 0, &actual) == ENAMETOOLONG && actual == 0);
    uint32_t word32;
    uint64_t word64;
    assert(_copyout_atomic32(0x12345678, mapped + 32) == 0);
    assert(_copyin_atomic32(mapped + 32, &word32) == 0 && word32 == 0x12345678);
    assert(_copyout_atomic64(0x123456789abcdef0ULL, mapped + 40) == 0);
    assert(_copyin_atomic64(mapped + 40, &word64) == 0 && word64 == 0x123456789abcdef0ULL);
    assert(_copyin_atomic64(mapped + 41, &word64) == EINVAL);
    assert(_copyout_atomic32(0, mapped + 33) == EINVAL);
    mapped[PAGE_SIZE - 2] = 'a';
    mapped[PAGE_SIZE - 1] = 'b';
    unsigned long before, alternate_before, after, alternate_after;
    unsigned long segv_before, bus_before, segv_after, bus_after;
    xnu_host_fault_statistics(&before, &alternate_before);
    xnu_host_fault_signal_statistics(&segv_before, &bus_before);
    pmap_protect(kernel_pmap, address, address + PAGE_SIZE, VM_PROT_READ);
    assert(_bcopyout(text, mapped, sizeof(text)) == EFAULT);
    assert(_copyout_atomic32(0, mapped + 32) == EFAULT);
    assert(_bcopyin(mapped, result, sizeof(text)) == 0 && memcmp(text, result, sizeof(text)) == 0);
    assert(_bcopyin(mapped + PAGE_SIZE, result, 1) == EFAULT);
    assert(_bcopyout(text, mapped + PAGE_SIZE, 1) == EFAULT);
    actual = 0;
    assert(_bcopyinstr(mapped + PAGE_SIZE - 2, result, 4, &actual) == EFAULT && actual == 0);
    unsigned long bus_address;
    assert(xnu_host_fault_bus_mapping(&bus_address) == 0);
    assert(_bcopyin((char *)bus_address, result, 1) == EFAULT);
    assert(xnu_host_fault_bus_unmap(bus_address) == 0);
    xnu_host_fault_statistics(&after, &alternate_after);
    xnu_host_fault_signal_statistics(&segv_after, &bus_after);
    assert(after - before == 6 && alternate_after - alternate_before == 6);
    assert(segv_after - segv_before == 5 && bus_after - bus_before == 1);
    ppnum_t page = pmap_find_phys(kernel_pmap, address);
    assert(pmap_enter(kernel_pmap, address, page, VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_NONE, 0, TRUE) == KERN_SUCCESS);
    assert(_bcopyout(saved, mapped, sizeof(saved)) == 0);
    memcpy(mapped + PAGE_SIZE - 2, saved_tail, sizeof(saved_tail));
    kprintf("XNU_HOST_COPYIO byte/string/scalar/readonly/unmapped/bus passed recovered=%lu alternate=%lu segv=%lu bus=%lu (raw primitives; user maps not initialized)\n",
        after - before, alternate_after - alternate_before,
        segv_after - segv_before, bus_after - bus_before);
}

/* Single-vCPU read-only zone mutations through the Linux physical backing.
 * XNU validates actual zone membership; alias permissions remain unchanged.
 * PPL hardware lockdown is not implemented by this userspace host. */
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/zalloc_internal.h>
#include <vm/pmap.h>
#include <host/memory.h>

static vm_offset_t
checked_destination(zone_id_t zid, vm_offset_t element, vm_offset_t offset, vm_size_t bytes)
{
    assert(zid >= ZONE_ID__FIRST_RO && zid <= ZONE_ID__LAST_RO);
    vm_size_t size = zone_ro_size_params[zid].z_elem_size;
    zone_require_ro(zid, size, (void *)element);
    assert(offset < size && bytes <= size - offset);
    assert(element <= (vm_offset_t)~0UL - offset);
    return element + offset;
}

void
pmap_ro_zone_memcpy(zone_id_t zid, vm_offset_t element, vm_offset_t offset,
    vm_offset_t source, vm_size_t bytes)
{
    if (!source || !bytes)
        return; /* Match the original pmap contract for empty copies. */
    assert(bytes <= (vm_offset_t)~0UL - source);
    vm_offset_t target = checked_destination(zid, element, offset, bytes);
    xnu_host_pmap_modify(target, (const void *)source, bytes);
}

void
pmap_ro_zone_bzero(zone_id_t zid, vm_offset_t element, vm_offset_t offset, vm_size_t bytes)
{
    vm_offset_t target = checked_destination(zid, element, offset, bytes);
    xnu_host_pmap_modify(target, NULL, bytes);
}

uint64_t
pmap_ro_zone_atomic_op(zone_id_t zid, vm_offset_t element, vm_offset_t offset,
    uint32_t op, uint64_t value)
{
    vm_size_t bytes = op & 0xf;
    assert(bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8);
    vm_offset_t target = checked_destination(zid, element, offset, bytes);
    assert((target & (bytes - 1)) == 0 && bytes <= PAGE_SIZE - (target & PAGE_MASK));
    ppnum_t page = pmap_find_phys(kernel_pmap, target);
    assert(page != 0);
    vm_offset_t physical_view = phystokv((pmap_paddr_t)page * PAGE_SIZE) + (target & PAGE_MASK);
    return __zalloc_ro_mut_atomic(physical_view, (zro_atomic_op_t)op, value);
}

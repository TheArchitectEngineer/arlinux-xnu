#ifndef ARLINUX_XNU_HOST_USERCOPY_H
#define ARLINUX_XNU_HOST_USERCOPY_H
#include <vm/pmap.h>
/* Kernel-only helper. Caller holds its map reference and disables preemption
 * through the complete physical access on this single virtual CPU. */
bool xnu_usermode_pmap_page(pmap_t pmap, vm_map_offset_t address,
    vm_prot_t access, pmap_paddr_t *physical);
#endif

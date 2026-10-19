#ifndef ARLINUX_XNU_HOST_MEMORY_H
#define ARLINUX_XNU_HOST_MEMORY_H
/* No Linux or XNU libc types cross this boundary. ppn 0 is never a frame. */
int xnu_host_memory_initialize(void);
int xnu_host_memory_map(unsigned long address, unsigned int page, int protection);
int xnu_host_memory_unmap(unsigned long address);
int xnu_host_memory_protect(unsigned long address, int protection);
unsigned int xnu_host_memory_lookup(unsigned long address);
int xnu_host_pmap_bootstrap(void);
#endif

#ifndef ARLINUX_XNU_HOST_MEMORY_H
#define ARLINUX_XNU_HOST_MEMORY_H
/* No Linux or XNU libc types cross this boundary. ppn 0 is never a frame. */
int xnu_host_memory_initialize(void);
int xnu_host_memory_map(unsigned long address, unsigned int page, int protection);
int xnu_host_memory_unmap(unsigned long address);
int xnu_host_memory_protect(unsigned long address, int protection);
enum {
    XNU_HOST_PAGE_MODIFIED = 1,
    XNU_HOST_PAGE_REFERENCED = 2
};
int xnu_host_memory_protect_page(unsigned int page, int protection);
int xnu_host_memory_disconnect(unsigned int page, unsigned int *attributes);
int xnu_host_memory_page_has_mappings(unsigned int page);
int xnu_host_memory_expect_fault(unsigned long address, int write_access);
unsigned int xnu_host_memory_lookup(unsigned long address);
int xnu_host_pmap_bootstrap(void);
/* Data-only aperture writes; NULL source zeroes bytes without changing aliases. */
void xnu_host_pmap_modify(unsigned long address, const void *source, unsigned long bytes);
#endif

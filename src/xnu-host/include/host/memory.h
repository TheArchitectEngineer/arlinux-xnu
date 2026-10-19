#ifndef ARLINUX_XNU_HOST_MEMORY_H
#define ARLINUX_XNU_HOST_MEMORY_H
/* No Linux or XNU libc types cross this boundary. ppn 0 is never a frame. */
int xnu_host_memory_initialize(void);
/* Map: positive errno is validation failure before a native mapping attempt.
 * Negative errno is a native MAP_FIXED or post-install tracking/cache failure.
 * Stop on a negative result: native VA state may have changed and VM must not
 * recycle a frame that can still have a live alias. */
int xnu_host_memory_map(unsigned long address, unsigned int page, int protection, int wired);
int xnu_host_memory_unmap(unsigned long address);
int xnu_host_memory_change_wiring(unsigned long address, int wired);
int xnu_host_memory_protect(unsigned long address, int protection);
int xnu_host_memory_protect_options(unsigned long address, int protection, int immediate);
enum {
    XNU_HOST_PAGE_MODIFIED = 1,
    XNU_HOST_PAGE_REFERENCED = 2
};
int xnu_host_memory_protect_page(unsigned int page, int protection);
/* Synchronize generated/loaded instructions across the physical view and aliases. */
int xnu_host_memory_sync_code(unsigned int page);
int xnu_host_memory_disconnect(unsigned int page, unsigned int *attributes);
int xnu_host_memory_page_has_mappings(unsigned int page);
int xnu_host_memory_attributes(unsigned int page, unsigned int *attributes);
int xnu_host_memory_update_attributes(unsigned int page, unsigned int clear, unsigned int set);
int xnu_host_memory_tracking_fault(unsigned long address, unsigned int access);
int xnu_host_memory_expect_fault(unsigned long address, int write_access);
int xnu_host_memory_expect_unhandled_fault(unsigned long address);
unsigned int xnu_host_memory_lookup(unsigned long address);
int xnu_host_pmap_bootstrap(void);
int xnu_host_pmap_diagnostics(void);
void xnu_host_codec_bootstrap(unsigned int page);
/* Data-only aperture writes; NULL source zeroes bytes without changing aliases. */
void xnu_host_pmap_modify(unsigned long address, const void *source, unsigned long bytes);
#endif

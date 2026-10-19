#ifndef ARLINUX_XNU_HOST_GUEST_H
#define ARLINUX_XNU_HOST_GUEST_H

/* Native/XNU boundary: no libc, Unicorn or XNU structures cross it.
 * One engine owns one user pmap. XNU owns frame backing and thread state.
 * No syscall implementation, Mach-O loader or implicit memory allocation. */
struct xnu_host_guest;
struct xnu_host_guest_state {
    unsigned long long x[31], sp, pc;
    unsigned long long q[32][2];
    unsigned long long tpidr, tpidrro;
    unsigned int pstate, fpcr, fpsr;
};
enum xnu_host_guest_stop {
    XNU_HOST_GUEST_SLICE,
    XNU_HOST_GUEST_EXCEPTION,
    XNU_HOST_GUEST_MEMORY,
    XNU_HOST_GUEST_UNDEFINED
};
struct xnu_host_guest_event {
    enum xnu_host_guest_stop reason;
    unsigned int exception; /* Pinned ARM engine exception number, not XNU ESR. */
    unsigned long long address;
    unsigned int access; /* VM_PROT_READ/WRITE/EXECUTE values 1/2/4. */
    unsigned int protection_fault;
    int size;
};
/* Allocate the one native CPU stack before booting XNU. Returns native errno. */
int xnu_host_guest_initialize(void);
/* Zero is success; errors use Unicorn uc_err codes. Bad arguments
 * return UC_ERR_ARG. An event is valid only after a successful run call. */
int xnu_host_guest_create(struct xnu_host_guest **result);
/* Destroy requires no mappings. Native mutation errors poison the engine;
 * retain its references and stop kernel execution rather than reclaiming RAM
 * behind an uncertain mapping. No further operation may resume that engine. */
int xnu_host_guest_destroy(struct xnu_host_guest *guest);
/* Reduce/remove every engine alias of a managed physical frame. */
int xnu_host_guest_page_protect(unsigned int page, unsigned int protection);
int xnu_host_guest_map(struct xnu_host_guest *guest, unsigned long long address,
    void *frame, unsigned int protection);
int xnu_host_guest_unmap(struct xnu_host_guest *guest, unsigned long long address);
int xnu_host_guest_protect(struct xnu_host_guest *guest, unsigned long long address,
    unsigned int protection);
int xnu_host_guest_run(struct xnu_host_guest *guest,
    struct xnu_host_guest_state *state, unsigned long instructions,
    struct xnu_host_guest_event *event);
#endif

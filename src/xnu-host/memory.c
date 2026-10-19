/* Native Linux backing for the XNU pmap. One virtual CPU owns this state. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <usermode_vm.h>
#include <host/memory.h>

extern char __executable_start[], _end[];
static int physical_fd = -1;
static unsigned int *kernel_pages;
static unsigned char *kernel_protections;
/* Guest wiring belongs to the mapping, independently of Linux residency.
 * Linux may manage the memfd's residency; XNU must retain its own wired state. */
#define MAPPING_WIRED 8
struct mapping_link {
    unsigned int previous, next; /* VA index plus one; zero terminates a chain. */
};
static struct mapping_link *kernel_links;
static unsigned int physical_heads[XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE];
static unsigned char physical_attributes[XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE];

/* Linux has no userspace PTE access/dirty bits. After XNU clears them,
 * temporarily restrict every alias, including the permanent physical view.
 * A synchronous fault restores only the mapping's original permissions. */
static int
tracked_protection(int protection, unsigned int attributes)
{
    if (!(attributes & XNU_HOST_PAGE_REFERENCED))
        return PROT_NONE;
    if (!(attributes & XNU_HOST_PAGE_MODIFIED))
        protection &= ~2;
    return ((protection & 1) ? PROT_READ : 0) |
        ((protection & 2) ? PROT_WRITE : 0) | ((protection & 4) ? PROT_EXEC : 0);
}

static int
apply_page_tracking(unsigned int page)
{
    unsigned int attributes = physical_attributes[page - 1];
    unsigned long physical = XNU_HOST_PHYSICAL_VIEW +
        (unsigned long)(page - 1) * XNU_HOST_PAGE_SIZE;
    /* Direct Linux syscall: this path is also called on the signal stack.
     * It must not allocate, take a libc lock or touch tracked guest memory. */
    if (syscall(SYS_mprotect, (void *)physical, XNU_HOST_PAGE_SIZE,
        tracked_protection(3, attributes)))
        return errno;
    for (unsigned int node = physical_heads[page - 1]; node;
        node = kernel_links[node - 1].next) {
        unsigned long index = node - 1;
        unsigned long address = XNU_HOST_HEAP_BASE + index * XNU_HOST_PAGE_SIZE;
        if (syscall(SYS_mprotect, (void *)address, XNU_HOST_PAGE_SIZE,
            tracked_protection(kernel_protections[index] & 7, attributes)))
            return errno;
    }
    return 0;
}

static void
unlink_mapping(unsigned long index)
{
    unsigned int page = kernel_pages[index];
    if (!page)
        return;
    struct mapping_link link = kernel_links[index];
    if (link.previous)
        kernel_links[link.previous - 1].next = link.next;
    else
        physical_heads[page - 1] = link.next;
    if (link.next)
        kernel_links[link.next - 1].previous = link.previous;
    kernel_links[index] = (struct mapping_link){0};
}

static void
link_mapping(unsigned long index, unsigned int page, int protection)
{
    unsigned int previous_head = physical_heads[page - 1];
    kernel_links[index] = (struct mapping_link){ .next = previous_head };
    if (previous_head)
        kernel_links[previous_head - 1].previous = index + 1;
    physical_heads[page - 1] = index + 1;
    /* Conservative access history: writable mappings may have been written.
     * Precise per-access reference/dirty tracking is not implemented yet. */
    physical_attributes[page - 1] |= XNU_HOST_PAGE_REFERENCED |
        ((protection & 2) ? XNU_HOST_PAGE_MODIFIED : 0);
}

static int
heap_address(unsigned long address)
{
    return address >= XNU_HOST_HEAP_BASE && address < XNU_HOST_KERNEL_LIMIT &&
        !(address & (XNU_HOST_PAGE_SIZE - 1));
}

int
xnu_host_memory_initialize(void)
{
    if (physical_fd != -1)
        return EALREADY;
    long host_page = sysconf(_SC_PAGESIZE);
    if (host_page <= 0 || XNU_HOST_PAGE_SIZE % host_page)
        return ENOTSUP;
    unsigned long image_start = (unsigned long)__executable_start;
    unsigned long image_end = (unsigned long)_end;
    /* Keep at least 1 GiB above the image for Linux's randomized native brk.
     * See Linux mm/util.c arch_randomize_brk; the XNU heap starts independently. */
    if (image_start != XNU_HOST_KERNEL_BASE ||
        image_end >= XNU_HOST_PHYSICAL_VIEW - 0x40000000UL)
        return ERANGE;
    /* The image arena also contains libc's randomized brk heap. Reserve only
     * the dedicated physical-view/VM arena, without replacing native mappings. */
    unsigned long reserve = XNU_HOST_PHYSICAL_VIEW;
    unsigned long length = XNU_HOST_KERNEL_LIMIT - reserve;
    void *region = mmap((void *)reserve, length, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (region == MAP_FAILED) {
        int error = errno;
        fprintf(stderr, "XNU_HOST_MEMORY reserve [%#lx,%#lx) failed error=%d image_end=%#lx brk=%p\n",
            reserve, (unsigned long)XNU_HOST_KERNEL_LIMIT, error, image_end, sbrk(0));
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[1024];
            while (fgets(line, sizeof(line), maps)) {
                unsigned long start, end;
                if (sscanf(line, "%lx-%lx", &start, &end) == 2 &&
                    start < XNU_HOST_KERNEL_LIMIT && end > reserve)
                    fprintf(stderr, "XNU_HOST_MEMORY conflict %s", line);
            }
            fclose(maps);
        }
        return error;
    }
    if (region != (void *)reserve) {
        munmap(region, length);
        return ENOTSUP; /* Never replace mappings without owning the exact VA. */
    }
    fprintf(stderr, "XNU_HOST_MEMORY image_end=%#lx native_brk=%p reserve_start=%#lx\n",
        image_end, sbrk(0), reserve);
    int fd = memfd_create("xnu-physical", MFD_CLOEXEC);
    if (fd == -1) {
        int error = errno;
        munmap(region, length);
        return error;
    }
    int error = 0;
    if (ftruncate(fd, XNU_HOST_PHYSICAL_BYTES)) {
        error = errno;
        goto fail;
    }
    void *view = mmap((void *)XNU_HOST_PHYSICAL_VIEW, XNU_HOST_PHYSICAL_BYTES,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (view == MAP_FAILED) {
        error = errno;
        goto fail;
    }
    kernel_pages = calloc((XNU_HOST_KERNEL_LIMIT - XNU_HOST_HEAP_BASE) /
        XNU_HOST_PAGE_SIZE, sizeof(*kernel_pages));
    kernel_protections = calloc((XNU_HOST_KERNEL_LIMIT - XNU_HOST_HEAP_BASE) /
        XNU_HOST_PAGE_SIZE, sizeof(*kernel_protections));
    kernel_links = calloc((XNU_HOST_KERNEL_LIMIT - XNU_HOST_HEAP_BASE) /
        XNU_HOST_PAGE_SIZE, sizeof(*kernel_links));
    if (!kernel_pages || !kernel_protections || !kernel_links) {
        free(kernel_pages);
        free(kernel_protections);
        free(kernel_links);
        kernel_pages = NULL;
        kernel_protections = NULL;
        kernel_links = NULL;
        error = ENOMEM;
        goto fail;
    }
    /* Until a clear arms tracking, conservatively account for direct writes
     * through the physical aperture, not just writes through virtual aliases. */
    for (unsigned int page = 0; page < sizeof(physical_attributes); page++)
        physical_attributes[page] = XNU_HOST_PAGE_REFERENCED | XNU_HOST_PAGE_MODIFIED;
    physical_fd = fd;
    fprintf(stderr, "XNU_HOST_MEMORY host_page=%ld guest_page=%d physical=%d kernel=[%#lx,%#lx)\n",
        host_page, XNU_HOST_PAGE_SIZE, XNU_HOST_PHYSICAL_BYTES,
        (unsigned long)XNU_HOST_KERNEL_BASE, (unsigned long)XNU_HOST_KERNEL_LIMIT);
    return 0;
fail:
    close(fd);
    munmap(region, length);
    return error;
}

/* Linux owns cache-maintenance access to the CPU. Its compiler runtime emits
 * the architecture's data-clean / instruction-invalidate sequence and barriers.
 * Use a readable view even when ref tracking or execute-only permissions have
 * removed read access. No guest runs concurrently on this single virtual CPU. */
static int
sync_code_alias(unsigned long address, int protection, unsigned int attributes)
{
    int temporary = PROT_READ | ((protection & 4) ? PROT_EXEC : 0);
    int restore = tracked_protection(protection, attributes);
    if (syscall(SYS_mprotect, (void *)address, XNU_HOST_PAGE_SIZE, temporary))
        return errno;
    __builtin___clear_cache((char *)address, (char *)address + XNU_HOST_PAGE_SIZE);
    if (syscall(SYS_mprotect, (void *)address, XNU_HOST_PAGE_SIZE, restore))
        return errno; /* The kernel caller must stop on restoration failure. */
    return 0;
}

int
xnu_host_memory_sync_code(unsigned int page)
{
    if (physical_fd == -1 || !page ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE)
        return EINVAL;
    unsigned int attributes = physical_attributes[page - 1];
    unsigned long physical = XNU_HOST_PHYSICAL_VIEW +
        (unsigned long)(page - 1) * XNU_HOST_PAGE_SIZE;
    /* Clean the view used by page-in/copy/zero, then invalidate each executable
     * virtual alias as well. Flushing only one VA need not cover I-cache aliases. */
    int error = sync_code_alias(physical, 3, attributes);
    if (error)
        return error;
    for (unsigned int node = physical_heads[page - 1]; node;
        node = kernel_links[node - 1].next) {
        unsigned long index = node - 1;
        int protection = kernel_protections[index] & 7;
        if (protection & 4) {
            error = sync_code_alias(XNU_HOST_HEAP_BASE + index * XNU_HOST_PAGE_SIZE,
                protection, attributes);
            if (error)
                return error;
        }
    }
    return 0;
}

int
xnu_host_memory_map(unsigned long address, unsigned int page, int protection, int wired)
{
    if (physical_fd == -1 || !heap_address(address) || !page ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE || (protection & ~7))
        return EINVAL;
    int native_protection = ((protection & 1) ? PROT_READ : 0) |
        ((protection & 2) ? PROT_WRITE : 0) | ((protection & 4) ? PROT_EXEC : 0);
    off_t offset = (off_t)(page - 1) * XNU_HOST_PAGE_SIZE;
    void *mapped = mmap((void *)address, XNU_HOST_PAGE_SIZE, native_protection,
        MAP_SHARED | MAP_FIXED, physical_fd, offset);
    if (mapped == MAP_FAILED)
        /* A failed replacement does not establish that the previous native
         * VA is intact. Stop instead of treating unknown state as retryable. */
        return -errno;
    unsigned long index = (address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE;
    unlink_mapping(index);
    kernel_pages[index] = page;
    kernel_protections[index] = protection | (wired ? MAPPING_WIRED : 0);
    link_mapping(index, page, protection);
    int error = apply_page_tracking(page);
    if (!error && (protection & 4))
        error = xnu_host_memory_sync_code(page);
    /* The alias is already installed. Report a distinct fatal state instead of
     * telling VM that insertion failed and letting it reclaim the mapped page. */
    return error ? -error : 0;
}

int
xnu_host_memory_unmap(unsigned long address)
{
    if (physical_fd == -1 || !heap_address(address))
        return EINVAL;
    /* Retain ownership of the VA after removing the physical alias. */
    if (mmap((void *)address, XNU_HOST_PAGE_SIZE, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        return errno;
    unlink_mapping((address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE);
    kernel_pages[(address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE] = 0;
    kernel_protections[(address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE] = 0;
    return 0;
}

int
xnu_host_memory_protect_options(unsigned long address, int protection, int immediate)
{
    if (physical_fd == -1 || !heap_address(address) || (protection & ~7) ||
        (immediate != 0 && immediate != 1))
        return EINVAL;
    unsigned long index = (address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE;
    unsigned int page = kernel_pages[index];
    if (!page)
        return 0; /* Preserve holes in the reserved VA arena. */
    int requested_write = protection & 2;
    protection &= kernel_protections[index] & 7;
    unsigned int attributes = physical_attributes[page - 1];
    if (immediate) {
        /* Original ARM immediate requests set referenced, and force-write
         * additionally sets modified. Ordinary protection cannot upgrade COW.
         * Even immediate mode never grants a new execute permission. */
        attributes |= XNU_HOST_PAGE_REFERENCED;
        if (requested_write) {
            protection |= 3; /* ARM's writable mapping is readable as well. */
            attributes |= XNU_HOST_PAGE_MODIFIED;
        }
    }
    int native_protection = tracked_protection(protection, attributes);
    if (mprotect((void *)address, XNU_HOST_PAGE_SIZE, native_protection))
        return errno;
    kernel_protections[index] = protection | (kernel_protections[index] & MAPPING_WIRED);
    if (immediate) {
        physical_attributes[page - 1] = attributes;
        /* Ref/mod tracking is physical-page state, shared by every alias. */
        return apply_page_tracking(page);
    }
    return 0;
}

int
xnu_host_memory_protect(unsigned long address, int protection)
{
    return xnu_host_memory_protect_options(address, protection, 0);
}

int
xnu_host_memory_change_wiring(unsigned long address, int wired)
{
    if (physical_fd == -1 || !heap_address(address))
        return EINVAL;
    unsigned long index = (address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE;
    /* Like ARM pmap: unwiring a previously removed mapping is valid;
     * wiring a hole is an error, never an invented resident page. */
    if (!kernel_pages[index])
        return wired ? ENOENT : 0;
    if (wired)
        kernel_protections[index] |= MAPPING_WIRED;
    else
        kernel_protections[index] &= ~MAPPING_WIRED;
    return 0;
}

int
xnu_host_memory_protect_page(unsigned int page, int protection)
{
    if (physical_fd == -1 || !page ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE || (protection & ~7))
        return EINVAL;
    unsigned int node = physical_heads[page - 1];
    while (node) {
        unsigned long index = node - 1;
        unsigned int next = kernel_links[index].next;
        unsigned long address = XNU_HOST_HEAP_BASE + index * XNU_HOST_PAGE_SIZE;
        int error = protection ? xnu_host_memory_protect(address, protection) :
            xnu_host_memory_unmap(address);
        if (error)
            return error;
        node = next;
    }
    return 0;
}

int
xnu_host_memory_disconnect(unsigned int page, unsigned int *attributes)
{
    if (physical_fd == -1 || !page || !attributes ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE)
        return EINVAL;
    int error = xnu_host_memory_protect_page(page, 0);
    if (!error)
        *attributes = physical_attributes[page - 1];
    /* The permanent physical view and its bytes remain available to XNU. */
    return error;
}

int
xnu_host_memory_page_has_mappings(unsigned int page)
{
    if (physical_fd == -1 || !page ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE)
        return -EINVAL;
    /* The permanent physical view is backing access, not a reclaimable alias. */
    return physical_heads[page - 1] != 0;
}

int
xnu_host_memory_attributes(unsigned int page, unsigned int *attributes)
{
    if (physical_fd == -1 || !page || !attributes ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE)
        return EINVAL;
    *attributes = physical_attributes[page - 1];
    return 0;
}

int
xnu_host_memory_update_attributes(unsigned int page, unsigned int clear,
    unsigned int set)
{
    if (physical_fd == -1 || !page ||
        page > XNU_HOST_PHYSICAL_BYTES / XNU_HOST_PAGE_SIZE || ((clear | set) & ~3u))
        return EINVAL;
    physical_attributes[page - 1] = (physical_attributes[page - 1] & ~clear) | set;
    return apply_page_tracking(page);
}

int
xnu_host_memory_tracking_fault(unsigned long address, unsigned int access)
{
    if (physical_fd == -1 || (access != 1 && access != 2 && access != 4))
        return 0;
    unsigned int page;
    unsigned int protection;
    if (address >= XNU_HOST_PHYSICAL_VIEW &&
        address < XNU_HOST_PHYSICAL_VIEW + XNU_HOST_PHYSICAL_BYTES) {
        page = 1 + (address - XNU_HOST_PHYSICAL_VIEW) / XNU_HOST_PAGE_SIZE;
        protection = 3;
    } else {
        unsigned long aligned = address & ~(XNU_HOST_PAGE_SIZE - 1UL);
        if (!heap_address(aligned))
            return 0;
        unsigned long index = (aligned - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE;
        page = kernel_pages[index];
        protection = kernel_protections[index] & 7;
    }
    if (!page || !(protection & access))
        return 0; /* Never turn an actual protection violation into success. */
    unsigned int old = physical_attributes[page - 1];
    unsigned int observed = old | XNU_HOST_PAGE_REFERENCED |
        ((access == 2) ? XNU_HOST_PAGE_MODIFIED : 0);
    if (old == observed)
        return 0; /* This fault was not caused by an armed tracking bit. */
    physical_attributes[page - 1] = observed;
    int error = apply_page_tracking(page);
    return error ? -error : 1;
}

static int
expect_fault(unsigned long address, int write_access, int reset_handler)
{
    if (!heap_address(address))
        return EINVAL;
    pid_t child = fork();
    if (child == -1)
        return errno;
    if (!child) {
        const struct rlimit core_limit = {0, 0};
        if (setrlimit(RLIMIT_CORE, &core_limit) ||
            (reset_handler && signal(SIGSEGV, SIG_DFL) == SIG_ERR))
            _exit(100);
        if (write_access)
            *(volatile unsigned char *)address = 0x7e;
        else
            (void)*(volatile unsigned char *)address;
        _exit(0);
    }
    int status;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1)
        return errno;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV ? 0 : EPROTO;
}

int
xnu_host_memory_expect_fault(unsigned long address, int write_access)
{
    return expect_fault(address, write_access, 1);
}

int
xnu_host_memory_expect_unhandled_fault(unsigned long address)
{
    return expect_fault(address, 1, 0);
}

unsigned int
xnu_host_memory_lookup(unsigned long address)
{
    if (physical_fd == -1)
        return 0;
    if (address >= XNU_HOST_PHYSICAL_VIEW &&
        address < XNU_HOST_PHYSICAL_VIEW + XNU_HOST_PHYSICAL_BYTES)
        return 1 + (address - XNU_HOST_PHYSICAL_VIEW) / XNU_HOST_PAGE_SIZE;
    address &= ~(XNU_HOST_PAGE_SIZE - 1UL);
    if (!heap_address(address))
        return 0;
    return kernel_pages[(address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE];
}

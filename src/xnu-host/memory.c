/* Native Linux backing for the XNU pmap. One virtual CPU owns this state. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <usermode_vm.h>
#include <host/memory.h>

extern char __executable_start[], _end[];
static int physical_fd = -1;
static unsigned int *kernel_pages;

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
    if (image_start != XNU_HOST_KERNEL_BASE || image_end >= XNU_HOST_PHYSICAL_VIEW)
        return ERANGE;
    /* The image arena also contains libc's randomized brk heap. Reserve only
     * the dedicated physical-view/VM arena, without replacing native mappings. */
    unsigned long reserve = XNU_HOST_PHYSICAL_VIEW;
    unsigned long length = XNU_HOST_KERNEL_LIMIT - reserve;
    void *region = mmap((void *)reserve, length, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (region == MAP_FAILED)
        return errno;
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
    if (!kernel_pages) {
        error = ENOMEM;
        goto fail;
    }
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

int
xnu_host_memory_map(unsigned long address, unsigned int page, int protection)
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
        return errno;
    kernel_pages[(address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE] = page;
    if (protection & 4)
        __builtin___clear_cache((char *)address, (char *)address + XNU_HOST_PAGE_SIZE);
    return 0;
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
    kernel_pages[(address - XNU_HOST_HEAP_BASE) / XNU_HOST_PAGE_SIZE] = 0;
    return 0;
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

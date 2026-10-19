#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <host/boot_image.h>

static void *image;
static unsigned long image_bytes;

int
xnu_host_boot_image_initialize(const char *path)
{
    if (!path || image)
        return EINVAL;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno;
    struct stat info;
    int error = 0;
    if (fstat(fd, &info) < 0) {
        error = errno;
        goto close_input;
    }
    /* memdev counts 4 KiB units even for a 16 KiB XNU kernel. Padding uses
     * owned zeroed memory, avoiding mmap file-tail SIGBUS or leaked bytes. */
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uint64_t)info.st_size > ULONG_MAX - 16383UL ||
        (((uint64_t)info.st_size + 16383UL) & ~16383UL) / 4096 > UINT_MAX) {
        error = EINVAL;
        goto close_input;
    }
    unsigned long bytes = ((unsigned long)info.st_size + 16383UL) & ~16383UL;
    void *backing = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (backing == MAP_FAILED) {
        error = errno;
        goto close_input;
    }
    unsigned long copied = 0;
    while (copied < (unsigned long)info.st_size) {
        ssize_t count = read(fd, (char *)backing + copied,
            (unsigned long)info.st_size - copied);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            error = count < 0 ? errno : EIO;
            munmap(backing, bytes);
            goto close_input;
        }
        copied += (unsigned long)count;
    }
    if (close(fd) < 0) {
        error = errno;
        munmap(backing, bytes);
        return error;
    }
    image = backing;
    image_bytes = bytes;
    fprintf(stderr, "XNU_HOST_ROOT_IMAGE file_bytes=%lu device_bytes=%lu (virtual memdev backing)\n",
        copied, bytes);
    return 0;
close_input:
    close(fd);
    return error;
}

int
xnu_host_boot_image_view(unsigned long *base, unsigned long *bytes)
{
    if (!image)
        return ENXIO;
    *base = (unsigned long)image;
    *bytes = image_bytes;
    return 0;
}

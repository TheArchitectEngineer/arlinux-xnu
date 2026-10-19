/* Minimal non-IOKit platform: hand real image backing to the original memory
 * device, devfs and VFS root discovery. No host exec/Mach-O/syscall handling.
 */
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <mach/kern_return.h>
#include <mach/boolean.h>
#include <kern/debug.h>
#include <pexpert/pexpert.h>
#include <host/boot_image.h>

#if IOKIT || CONFIG_SECURE_BSD_ROOT
#error This headless RAM-disk platform does not implement IOKit or secure-root policy
#endif

extern dev_t mdevadd(int, uint64_t, unsigned int, int);
extern dev_t mdevlookup(int);
static dev_t boot_device = NODEV;

kern_return_t
IOKitBSDInit(void)
{
    if (boot_device != NODEV)
        return KERN_SUCCESS;
    unsigned long base, bytes;
    if (xnu_host_boot_image_view(&base, &bytes) ||
        (base & 4095) || !bytes || (bytes & 4095) || bytes / 4096 > UINT_MAX)
        return KERN_FAILURE;
    /* Virtual mapping, not a physical page number. The original memdev does
     * actual buffer I/O and the original mockfs pager/exec consumes the file. */
    boot_device = mdevadd(0, base >> 12, (unsigned int)(bytes >> 12), FALSE);
    if (boot_device == NODEV)
        return KERN_FAILURE;
    kprintf("XNU_HOST_ROOT_DEVICE md0 dev=%#x bytes=%lu\n", boot_device, bytes);
    return KERN_SUCCESS;
}

kern_return_t
IOFindBSDRoot(char *name, unsigned int capacity, dev_t *device, u_int32_t *flags)
{
    if (boot_device == NODEV || mdevlookup(0) != boot_device)
        return KERN_FAILURE;
    if (capacity < sizeof("md0"))
        return KERN_NO_SPACE;
    strlcpy(name, "md0", capacity);
    *device = boot_device;
    *flags = 0;                 /* actual local block device, not netboot */
    return KERN_SUCCESS;
}

void
IOSecureBSDRoot(const char *name)
{
    /* Same inactive policy as original IOKitBSDInit.cpp when
     * CONFIG_SECURE_BSD_ROOT is off; no claim of root authentication. */
    (void)name;
}

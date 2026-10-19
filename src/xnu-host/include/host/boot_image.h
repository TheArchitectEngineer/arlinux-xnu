#ifndef ARLINUX_XNU_HOST_BOOT_IMAGE_H
#define ARLINUX_XNU_HOST_BOOT_IMAGE_H
/* Native image backing stays alive for the whole kernel process. Kernel VFS
 * reads it through the original virtual memory-device strategy, not a loader.
 */
int xnu_host_boot_image_initialize(const char *path);
int xnu_host_boot_image_view(unsigned long *base, unsigned long *bytes);
#endif

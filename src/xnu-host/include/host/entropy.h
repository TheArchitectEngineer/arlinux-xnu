#ifndef ARLINUX_XNU_HOST_ENTROPY_H
#define ARLINUX_XNU_HOST_ENTROPY_H
int xnu_host_entropy_fill(void *buffer, unsigned long length);
unsigned long long xnu_host_entropy_bytes(void);
int xnu_host_entropy_bootstrap(void);
#endif

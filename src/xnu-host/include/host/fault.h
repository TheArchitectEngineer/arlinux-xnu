#ifndef ARLINUX_XNU_HOST_FAULT_H
#define ARLINUX_XNU_HOST_FAULT_H
int xnu_host_fault_initialize(void);
void xnu_host_fault_statistics(unsigned long *recovered, unsigned long *alternate);
void xnu_host_ticket_bootstrap(unsigned long address);
#endif

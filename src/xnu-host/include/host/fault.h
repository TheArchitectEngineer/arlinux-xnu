#ifndef ARLINUX_XNU_HOST_FAULT_H
#define ARLINUX_XNU_HOST_FAULT_H
int xnu_host_fault_initialize(void);
void xnu_host_fault_statistics(unsigned long *recovered, unsigned long *alternate);
void xnu_host_fault_signal_statistics(unsigned long *segv, unsigned long *bus);
void xnu_host_ticket_bootstrap(unsigned long address);
/* Diagnostic mapping with no file bytes: an actual load generates SIGBUS. */
int xnu_host_fault_bus_mapping(unsigned long *address);
int xnu_host_fault_bus_unmap(unsigned long address);
#endif

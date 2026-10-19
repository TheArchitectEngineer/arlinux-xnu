#ifndef ARLINUX_XNU_HOST_USEREXEC_H
#define ARLINUX_XNU_HOST_USEREXEC_H
/* Kernel-only CPU-return interfaces, never included by Linux-facing TUs. */
#include <stdint.h>
#include <mach/arm/thread_status.h>
#include <vm/pmap.h>
#include <host/guest.h>
struct xnu_host_guest *xnu_usermode_pmap_engine(pmap_t pmap);
void xnu_usermode_timer_poll(arm_saved_state_t *state);
void xnu_usermode_user_interrupts_enable(void);
__attribute__((noreturn)) void xnu_usermode_user_return(void);
#endif

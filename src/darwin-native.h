#pragma once
#include <stdint.h>

long darwin_native_memory(unsigned long size,int protection);
void darwin_native_connect(void);
int darwin_native_services_enabled(void);
void *darwin_native_service_run(void *);
int darwin_native_host_port_refs(uint32_t port,int delta);
uint32_t darwin_native_host_task(void);
int darwin_native_host_identity(uint32_t *pid,uint32_t *incarnation);
int arlinux_xpc_bootstrap(void *request,void *response,unsigned long capacity,uint32_t bootstrap,uint32_t *held_port);
int arlinux_xpc_processinfo(void *request,unsigned long length,void *response,unsigned long capacity,uint32_t listener,uint32_t set);
int darwin_native_host_join(uint32_t port,uint32_t set);
int darwin_native_system_lookup(const char *name,uint64_t type,uint64_t flags,uint32_t *port);
int darwin_native_display_register(uint32_t width,uint32_t height,uint32_t refresh_millihz,
                                   uint64_t period_bits,int32_t backend_pid);
uint32_t darwin_native_service_lookup(uint32_t bootstrap,uint64_t name,uint64_t output,
                                    int32_t target,uint64_t instance,uint64_t flags);
uint32_t darwin_native_service_register(uint32_t bootstrap,uint64_t name,uint32_t port,uint64_t flags);
uint32_t darwin_native_service_check_in(uint32_t bootstrap,uint64_t name,uint64_t output,uint64_t flags);

/* Single native guest process. No host libc use after guest TLS takes over. */
void darwin_native_prepare(const char *root,const char *program,uint64_t cache_base,const unsigned char uuid[16],uint32_t signature_bytes);
__attribute__((noreturn)) void darwin_native_enter(uint64_t stack, uint64_t entry);

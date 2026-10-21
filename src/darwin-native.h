#pragma once
#include <stdint.h>
/* Guest pthread slots plus the one native Mach-service thread created by the
 * launcher. Transport bookkeeping must cover both, not fail one thread early. */
enum { D_NATIVE_GUEST_THREAD_CAPACITY=32, D_NATIVE_SERVICE_THREADS=1 };
/* TLS-free native helper; inherits the caller's actual syscall filter. */
long darwin_clone_callback(uint64_t stack,void (*callback)(void *),void *argument);
extern char darwin_clone_callback_return[];

long darwin_native_memory(unsigned long size,int protection);
long darwin_native_main_stack(unsigned long size);
void darwin_native_connect(void);
int darwin_native_dyld_info(uint64_t address,uint64_t length);
int darwin_native_services_enabled(void);
int darwin_native_session_start(void);
int darwin_native_session_join(void);
void *darwin_native_service_run(void *);
int darwin_native_host_port_refs(uint32_t port,int delta);
int darwin_native_host_port_deallocate(uint32_t port);
int darwin_native_host_memory_deallocate(uint64_t address,uint64_t size);
uint32_t darwin_native_host_task(void);
int darwin_native_host_identity(uint32_t *pid,uint32_t *incarnation);
/* Private mapping adapter: the original getter's LR identifies its one audited
 * predicate. Route returns two AAPCS64 registers; no caller action is injected. */
struct darwin_bootstrap_route { uint64_t continuation; uint32_t pid,euid; };
int darwin_native_bootstrap_policy(uint64_t entry,uint32_t token_offset,uint64_t accepted,uint64_t rejected);
struct darwin_bootstrap_route darwin_native_bootstrap_route(uint64_t stack,uint64_t entry);
void darwin_bootstrap_policy_enter(void);
int arlinux_xpc_bootstrap(void *request,void *response,unsigned long capacity,uint32_t bootstrap,uint32_t *held_port,const uint32_t *requester);
enum darwin_session_service { D_SESSION_PROCESSINFO, D_SESSION_WORKSPACE,
    D_SESSION_SYSTEMAPP, D_SESSION_HIDFOCUS, D_SESSION_SERVICE_COUNT };
#define D_SESSION_PROCESSINFO_NAME "com.apple.assertiond.processinfoservice"
#define D_SESSION_WORKSPACE_NAME "com.apple.frontboard.workspace"
#define D_SESSION_SYSTEMAPP_NAME "com.apple.frontboard.systemappservices"
#define D_SESSION_HIDFOCUS_NAME "com.apple.backboard.hid.focus"
int arlinux_xpc_session(void *request,unsigned long length,void *response,unsigned long capacity,const uint32_t *listeners,uint32_t set,const uint32_t *audit);
void arlinux_xpc_session_tick(void);
/* Private native display registration, delivered over real audited Mach IPC. */
/* One registered virtual Wayland display, independent of device model. */
#define D_WAYLAND_MAIN_DISPLAY_ID 1
#define D_SESSION_DISPLAY_REGISTER 0x41524450U
int arlinux_session_display(void *request,unsigned long length,const uint32_t *audit);

int darwin_native_host_receive_create(uint32_t *port);
int darwin_native_host_endpoint_create(uint32_t *port);
int darwin_native_host_send(void *message,uint32_t size);
int darwin_native_host_process_matches(uint32_t pid,uint32_t incarnation);
int darwin_native_host_child(uint32_t pid,uint32_t parent);
int darwin_native_host_receive_destroy(uint32_t port);
int darwin_native_host_join(uint32_t port,uint32_t set);
int darwin_native_system_lookup(const char *name,uint64_t type,uint64_t flags,uint32_t *port);
int darwin_native_display_register(uint32_t width,uint32_t height,uint32_t refresh_millihz,
                                   uint64_t period_bits,int32_t backend_pid);
/* Real QuartzCore entry points, resolved from the currently loaded cache. */
#include "ios-framework-api.h"
#include "ios-platform-abi.h"
enum darwin_frame_state { D_FRAME_FREE, D_FRAME_DRAWING, D_FRAME_READY, D_FRAME_BUSY };
struct darwin_frame_control {
    uint32_t state[2],sequence[2];
    /* Single host writer. Odd window_sequence means publication in progress. */
    uint32_t window_sequence,width,height,refresh_millihz,window_flags;
    uint32_t input_read,input_write,input_overflow;
    struct arlinux_ios_input_event input[ARLINUX_IOS_INPUT_CAPACITY];
};
void darwin_native_ui_api(const struct darwin_ui_api *api);
void darwin_native_ui_output(void *pixels,uint32_t width,uint32_t height,
                            struct darwin_frame_control *control,int notify);
uint32_t darwin_native_service_lookup(uint32_t bootstrap,uint64_t name,uint64_t output,
                                    int32_t target,uint64_t instance,uint64_t flags);
uint32_t darwin_native_service_register(uint32_t bootstrap,uint64_t name,uint32_t port,uint64_t flags);
uint32_t darwin_native_service_check_in(uint32_t bootstrap,uint64_t name,uint64_t output,uint64_t flags);

/* Single native guest process. No host libc use after guest TLS takes over. */
void darwin_native_prepare(const char *root,const char *program,const char *container,uint64_t cache_base,const unsigned char uuid[16],uint32_t signature_bytes);
__attribute__((noreturn)) void darwin_native_enter(uint64_t stack, uint64_t entry);

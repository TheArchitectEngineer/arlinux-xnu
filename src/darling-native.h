/* Native client for the pinned Darling-generated RPC. Every hook after guest
 * entry uses the allowed raw Linux SVC site, never host libc or host TLS. */
#pragma once
static int darling_active;
#ifdef ARLINUX_DARLING
#include <sys/socket.h>
#include <sys/un.h>
#include <darlingserver/rpc.h>
#include <darlingserver/rpc-supplement.h>
static struct sockaddr_un darling_address;
static struct { int tid,fd; } darling_clients[32];
static int darling_socket(void);
static void *darling_copy(void *destination,const void *source,size_t size)
{
    volatile unsigned char *out=destination;
    const volatile unsigned char *in=source;
    while (size--) *out++=*in++;
    return destination;
}
static void darling_error(const char *operation)
{
    size_t length=0; while (operation[length]) length++;
    RAW(SYS_write,2,"DARLING_RPC_ERROR ",18,0,0,0);
    RAW(SYS_write,2,operation,length,0,0,0);
    RAW(SYS_write,2,"\n",1,0,0,0);
}
static void darling_abort(const char *operation)
{
    darling_error(operation);
    RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0);
    __builtin_unreachable();
}
#include "darling-memory.h"
static long darling_receive(int fd,struct msghdr *message)
{
    struct msghdr original=*message;
    struct darling_memory_updates updates; updates.count=0;
    for (;;) {
        *message=original;
        long result=RAW(SYS_recvmsg,fd,message,0,0,0,0);
        if (result==-EINTR) continue;
        if (result>=(long)sizeof(dserver_s2c_callhdr_t)) {
            dserver_s2c_call_t *call=message->msg_iov[0].iov_base;
            if (call->mmap.header.call_number==dserver_callnum_s2c) {
                if (message->msg_flags&(MSG_TRUNC|MSG_CTRUNC)) darling_abort("truncated S2C memory request");
                darling_memory_reply(fd,call,result,&updates);
                continue;
            }
        }
        darling_memory_complete(&updates);
        return result;
    }
}
static void darling_block(uint64_t *saved)
{
    uint64_t all=UINT64_MAX;
    if (RAW(SYS_rt_sigprocmask,SIG_BLOCK,&all,saved,8,0,0)) darling_abort("block signals");
}
static void darling_unblock(uint64_t *saved)
{
    if (RAW(SYS_rt_sigprocmask,SIG_SETMASK,saved,0,8,0,0)) darling_abort("restore signals");
}
#define dserver_rpc_hooks_msghdr_t struct msghdr
#define dserver_rpc_hooks_iovec_t struct iovec
#define dserver_rpc_hooks_cmsghdr_t struct cmsghdr
#define DSERVER_RPC_HOOKS_CMSG_SPACE CMSG_SPACE
#define DSERVER_RPC_HOOKS_CMSG_FIRSTHDR CMSG_FIRSTHDR
#define DSERVER_RPC_HOOKS_SOL_SOCKET SOL_SOCKET
#define DSERVER_RPC_HOOKS_SCM_RIGHTS SCM_RIGHTS
#define DSERVER_RPC_HOOKS_CMSG_LEN CMSG_LEN
#define DSERVER_RPC_HOOKS_CMSG_DATA CMSG_DATA
#define DSERVER_RPC_HOOKS_ATTRIBUTE static
#define dserver_rpc_hooks_get_pid() ((pid_t)RAW(SYS_getpid,0,0,0,0,0,0))
#define dserver_rpc_hooks_get_tid() ((pid_t)RAW(SYS_gettid,0,0,0,0,0,0))
#define dserver_rpc_hooks_get_architecture() dserver_rpc_architecture_arm64
#define dserver_rpc_hooks_get_server_address() ((void *)&darling_address)
#define dserver_rpc_hooks_get_server_address_length() sizeof(darling_address)
#define dserver_rpc_hooks_memcpy darling_copy
#define dserver_rpc_hooks_send_message(fd,msg) RAW(SYS_sendmsg,fd,msg,MSG_NOSIGNAL,0,0,0)
#define dserver_rpc_hooks_receive_message darling_receive
#define dserver_rpc_hooks_get_bad_message_status() (-EBADMSG)
#define dserver_rpc_hooks_get_communication_error_status() (-ECOMM)
#define dserver_rpc_hooks_get_broken_pipe_status() (-EPIPE)
#define dserver_rpc_hooks_close_fd(fd) RAW(SYS_close,fd,0,0,0,0,0)
#define dserver_rpc_hooks_get_socket darling_socket
#define dserver_rpc_hooks_printf(...) darling_error(__func__)
#define dserver_rpc_hooks_atomic_save_t uint64_t
#define dserver_rpc_hooks_atomic_begin darling_block
#define dserver_rpc_hooks_atomic_end darling_unblock
#define dserver_rpc_hooks_get_interrupt_status() (-EINTR)
#define dserver_rpc_hooks_push_reply(fd,msg,size) darling_abort("unexpected deferred RPC reply")
/* The exact upstream generator output is built on the runner and consumed as
 * a system include, retaining upstream diagnostics policy for generated code. */
#include <src/rpc.c>

static int darling_socket(void)
{
    int tid=RAW(SYS_gettid,0,0,0,0,0,0);
    for (unsigned int i=0;i<32;i++)
        if (__atomic_load_n(&darling_clients[i].tid,__ATOMIC_ACQUIRE)==tid) return darling_clients[i].fd;
    for (unsigned int i=0;i<32;i++) {
        int unused=0;
        if (!__atomic_compare_exchange_n(&darling_clients[i].tid,&unused,tid,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        int fd=RAW(SYS_socket,AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0,0,0,0);
        if (fd<0) darling_abort("create client socket");
        struct sockaddr_un local={.sun_family=AF_UNIX};
        if (RAW(SYS_bind,fd,&local,sizeof(local.sun_family),0,0,0)) darling_abort("autobind client socket");
        darling_clients[i].fd=fd;
        int result=dserver_rpc_explicit_checkin(fd,0,&local,-1);
        if (result) { trace_call(0,0,result); darling_abort("thread checkin"); }
        return fd;
    }
    darling_abort("client thread capacity");
    return -EMFILE;
}
static void darling_thread_close(void)
{
    int tid=RAW(SYS_gettid,0,0,0,0,0,0);
    for (unsigned int i=0;i<32;i++) if (darling_clients[i].tid==tid) {
        if (dserver_rpc_explicit_checkout(darling_clients[i].fd,-1,0)) darling_abort("thread checkout");
        RAW(SYS_close,darling_clients[i].fd,0,0,0,0,0);
        __atomic_store_n(&darling_clients[i].tid,0,__ATOMIC_RELEASE);
        return;
    }
}
static uint32_t darling_thread_self(void)
{
    uint32_t port=0;
    int result=dserver_rpc_thread_self_trap(&port);
    if (result || !port) darling_abort("thread self");
    return port;
}
static void darling_prepare(void)
{
    if (darling_active) return;
    const char *prefix=getenv("ARLINUX_DARLING_PREFIX");
    if (!prefix) return;
    const char suffix[]="/.darlingserver.sock";
    size_t length=strlen(prefix);
    if (length+sizeof(suffix)>sizeof(darling_address.sun_path)) darling_abort("prefix path length");
    darling_address.sun_family=AF_UNIX;
    darling_copy(darling_address.sun_path,prefix,length);
    darling_copy(darling_address.sun_path+length,suffix,sizeof(suffix));
    const char *server=getenv("ARLINUX_DARLING_PID");
    char *end=NULL; long pid=server ? strtol(server,&end,10) : 0;
    if (!server || !*server || *end || pid<=0 || pid>INT32_MAX ||
        prctl(PR_SET_PTRACER,pid,0,0,0)) darling_abort("authorize exact server memory access");
    darling_active=1;
    if (dserver_rpc_task_self_trap(&darling_task_port) || !darling_task_port ||
        dserver_rpc_host_self_trap(&darling_host_port) || !darling_host_port) darling_abort("task/host self");
    const char marker[]="DARLING_NATIVE_CONNECTED task/host ports follow\n";
    RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
    trace_call(0,0,darling_task_port); trace_call(1,0,darling_host_port);
}
static long darling_mach(unsigned long number,const unsigned long long *r)
{
    uint32_t port=0;
    int result;
    switch (number) {
    case D_TRAP_TASK_SELF: result=dserver_rpc_task_self_trap(&port); return result ? 0 : port;
    case D_TRAP_HOST_SELF: result=dserver_rpc_host_self_trap(&port); return result ? 0 : port;
    case D_TRAP_THREAD_SELF: return darling_thread_self();
    case D_TRAP_REPLY_PORT: result=dserver_rpc_mach_reply_port(&port); return result ? 0 : port;
    case D_TRAP_SPECIAL_REPLY_PORT: result=dserver_rpc_thread_get_special_reply_port(&port); return result ? 0 : port;
    case D_TRAP_MSG: case D_TRAP_MSG_OVERWRITE:
        return dserver_rpc_mach_msg_overwrite((void *)r[0],r[1],r[2],r[3],r[4],r[5],r[6],number==D_TRAP_MSG_OVERWRITE ? (void *)r[7] : NULL);
    case D_TRAP_PORT_ALLOCATE: return dserver_rpc_mach_port_allocate(r[0],r[1],(uint32_t *)r[2]);
    case D_TRAP_PORT_DEALLOCATE: return dserver_rpc_mach_port_deallocate(r[0],r[1]);
    case D_TRAP_PORT_MOD_REFS: return dserver_rpc_mach_port_mod_refs(r[0],r[1],r[2],r[3]);
    case D_TRAP_PORT_INSERT_RIGHT: return dserver_rpc_mach_port_insert_right(r[0],r[1],r[2],r[3]);
    case D_TRAP_PORT_INSERT_MEMBER: return dserver_rpc_mach_port_insert_member(r[0],r[1],r[2]);
    case D_TRAP_PORT_EXTRACT_MEMBER: return dserver_rpc_mach_port_extract_member(r[0],r[1],r[2]);
    case D_TRAP_PORT_CONSTRUCT: {
        struct darwin_port_options options={0};
        int readable=!guest_copy(&options,r[1],sizeof(options),0);
        long result=dserver_rpc_mach_port_construct(r[0],(void *)r[1],r[2],(uint32_t *)r[3]);
        uint32_t name=0;
        if (!result) guest_copy(&name,r[3],sizeof(name),0);
        static unsigned int traces;
        if (__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char marker[]="DARLING_PORT_CONSTRUCT target/flags/context/name/result follows\n";
            RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
            trace_call(0,0,r[0]); trace_call(1,0,readable ? options.flags : UINT32_MAX);
            trace_call(2,0,r[2]); trace_call(3,0,name); trace_call(4,0,result);
        }
        return result;
    }
    case D_TRAP_PORT_DESTRUCT: return dserver_rpc_mach_port_destruct(r[0],r[1],r[2],r[3]);
    case D_TRAP_PORT_GUARD: case D_TRAP_PORT_UNGUARD: {
        long result=number==D_TRAP_PORT_GUARD ?
            dserver_rpc_mach_port_guard(r[0],r[1],r[2],r[3]) :
            dserver_rpc_mach_port_unguard(r[0],r[1],r[2]);
        static unsigned int traces;
        if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char marker[]="DARLING_PORT_GUARD trap/name/context/strict/result follows\n";
            RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
            trace_call(0,0,number); trace_call(1,0,r[1]); trace_call(2,0,r[2]);
            trace_call(3,0,number==D_TRAP_PORT_GUARD ? r[3] : 0); trace_call(4,0,result);
        }
        return result;
    }
    case D_TRAP_SEMAPHORE_SIGNAL: return dserver_rpc_semaphore_signal(r[0]);
    case D_TRAP_SEMAPHORE_WAIT: return dserver_rpc_semaphore_wait(r[0]);
    case D_TRAP_TIMER_CREATE: result=dserver_rpc_mk_timer_create(&port); return result ? 0 : port;
    case D_TRAP_TIMER_DESTROY: return dserver_rpc_mk_timer_destroy(r[0]);
    case D_TRAP_TIMER_ARM: return dserver_rpc_mk_timer_arm(r[0],r[1]);
    case D_TRAP_TIMER_CANCEL: return dserver_rpc_mk_timer_cancel(r[0],(uint64_t *)r[1]);
    default: return D_KERN_NOT_SUPPORTED;
    }
}
#else
static void darling_prepare(void)
{
    if (getenv("ARLINUX_DARLING_PREFIX")) {
        fprintf(stderr,"Darling was requested but is absent from this build\n"); exit(1);
    }
}
#endif

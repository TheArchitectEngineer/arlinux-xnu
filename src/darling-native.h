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
static int darling_server_pid,darling_fork_checkin;
static struct { int tid,fd; } darling_clients[D_NATIVE_GUEST_THREAD_CAPACITY+D_NATIVE_SERVICE_THREADS];
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
    RAW(SYS_write,native_trace_fd,"DARLING_RPC_ERROR ",18,0,0,0);
    RAW(SYS_write,native_trace_fd,operation,length,0,0,0);
    RAW(SYS_write,native_trace_fd,"\n",1,0,0,0);
}
static void darling_abort(const char *operation)
{
    darling_error(operation);
    /* Keep the original client threads for the existing opt-in fault collector
     * when a service rejects a request. Otherwise exit_group loses the caller
     * that was awaiting this reply. This remains a fatal journey failure. */
    const char fatal[]="DARLING_RPC_FATAL original caller retained for diagnosis\n";
    RAW(SYS_write,native_trace_fd,fatal,sizeof(fatal)-1,0,0,0);
    /* RPC can hold an all-signals-blocked mask. A pending SIGABRT followed by
     * exit_group can lose this caller before collection. SIGSTOP is synchronous
     * and unblockable; resume still executes the actual fatal exit below. */
    if (native_stop_on_fault)
        RAW(SYS_kill,RAW(SYS_getpid,0,0,0,0,0,0),SIGSTOP,0,0,0,0);
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
    for (unsigned int i=0;i<sizeof(darling_clients)/sizeof(*darling_clients);i++)
        if (__atomic_load_n(&darling_clients[i].tid,__ATOMIC_ACQUIRE)==tid) return darling_clients[i].fd;
    for (unsigned int i=0;i<sizeof(darling_clients)/sizeof(*darling_clients);i++) {
        int unused=0;
        if (!__atomic_compare_exchange_n(&darling_clients[i].tid,&unused,tid,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        int fd=RAW(SYS_socket,AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0,0,0,0);
        if (fd<0) darling_abort("create client socket");
        struct sockaddr_un local={.sun_family=AF_UNIX};
        if (RAW(SYS_bind,fd,&local,sizeof(local.sun_family),0,0,0)) darling_abort("autobind client socket");
        darling_clients[i].fd=fd;
        int result=dserver_rpc_explicit_checkin(fd,darling_fork_checkin,&local,-1);
        if (result) { trace_call(0,0,result); darling_abort("thread checkin"); }
        darling_fork_checkin=0;
        return fd;
    }
    darling_abort("client thread capacity");
    return -EMFILE;
}
static void darling_thread_close(void)
{
    int tid=RAW(SYS_gettid,0,0,0,0,0,0);
    for (unsigned int i=0;i<sizeof(darling_clients)/sizeof(*darling_clients);i++) if (darling_clients[i].tid==tid) {
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
static void darling_after_fork(void)
{
    /* Closing this child's inherited references must not check out the still
     * live parent threads. Each child gets its own authenticated RPC socket. */
    for(unsigned int i=0;i<sizeof(darling_clients)/sizeof(*darling_clients);i++) {
        if(darling_clients[i].tid)RAW(SYS_close,darling_clients[i].fd,0,0,0,0,0);
        darling_clients[i].tid=0;darling_clients[i].fd=0;
    }
    if(RAW(SYS_prctl,PR_SET_PTRACER,darling_server_pid,0,0,0,0))
        darling_abort("authorize child memory access");
    darling_fork_checkin=1;
    if(dserver_rpc_task_self_trap(&darling_task_port) || !darling_task_port ||
       dserver_rpc_host_self_trap(&darling_host_port) || !darling_host_port)
        darling_abort("child task/host self");
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
    darling_server_pid=pid;
    darling_active=1;
    if (dserver_rpc_task_self_trap(&darling_task_port) || !darling_task_port ||
        dserver_rpc_host_self_trap(&darling_host_port) || !darling_host_port) darling_abort("task/host self");
    const char marker[]="DARLING_NATIVE_CONNECTED task/host ports follow\n";
    RAW(SYS_write,native_trace_fd,marker,sizeof(marker)-1,0,0,0);
    trace_call(0,0,darling_task_port); trace_call(1,0,darling_host_port);
}
static long darling_mach_msg2(const unsigned long long *r)
{
    /* XNU mach/message.h mach_msg2() and ipc/mach_msg.c mach_msg2_trap:
     * eight packed arguments, unlike legacy mach_msg_overwrite's flat ABI.
     * Ordinary canonical-buffer calls use the same real Darling queues and
     * MIG kobjects. This trusted-input runtime does not enforce Apple's CFI
     * routing policy. Vectors may carry a normal message and receive an empty
     * aux EOF. Nonempty auxiliary sends and separate-header sends still fail;
     * the legacy core cannot carry their data, so it must not discard it. */
    static unsigned int traces;
    int diagnostic=__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT;
    if(diagnostic) trace_record("DARLING_MSG2_BEGIN options=0x",r[1],r[4]>>32,r[2]>>32);
    uint64_t routing=r[1]&(D_MACH64_SEND_KOBJECT|D_MACH64_SEND_QUEUE|D_MACH64_SEND_ANY);
    int vector=(r[1]&D_MACH64_MSG_VECTOR)!=0;
    if ((r[1]&~(UINT64_C(0xffffffff)|D_MACH64_MSG_VECTOR|D_MACH64_SEND_KOBJECT|D_MACH64_SEND_QUEUE|D_MACH64_SEND_ANY)) ||
        (vector && (routing&D_MACH64_SEND_KOBJECT)) ||
        (routing && (routing&(routing-1)))) return D_MACH_SEND_INVALID_OPTIONS;
    uint32_t options=r[1],send_size=r[2]>>32,receive_size=r[6],receive_name=r[5]>>32;
    uint64_t message=r[0],receive=0,auxiliary=0;long receive_error=0;
    if(vector) {
        uint32_t send_count=send_size,receive_count=receive_size;
        uint32_t count=send_count>receive_count ? send_count : receive_count;
        struct darwin_msg_vector vectors[2]={{0},{0}};
        if(count>2 || !count) return options&D_MACH_SEND_MSG ?
            (count ? D_MACH_SEND_INVALID_DATA : D_MACH_SEND_MSG_TOO_SMALL) : D_MACH_RCV_INVALID_ARGUMENTS;
        if(guest_copy(vectors,r[0],count*sizeof(vectors[0]),0))
            return options&D_MACH_SEND_MSG ? D_MACH_SEND_INVALID_DATA : D_MACH_RCV_INVALID_ARGUMENTS;
        message=vectors[0].data;send_size=vectors[0].send_size;
        receive=vectors[0].receive_address ? vectors[0].receive_address : vectors[0].data;
        receive_size=vectors[0].receive_size;
        if((options&D_MACH_SEND_MSG) && (!send_count || !message || send_size<24 || (send_size&3)))
            return !message ? D_MACH_SEND_INVALID_DATA : D_MACH_SEND_MSG_TOO_SMALL;
        if((options&D_MACH_SEND_MSG) && send_count==2 && vectors[1].send_size)
            return D_MACH_SEND_INVALID_OPTIONS;
        if(options&D_MACH_RCV_MSG) {
            if(!receive_count)receive_error=D_MACH_RCV_INVALID_ARGUMENTS;
            if(receive_count==2) {
                auxiliary=vectors[1].receive_address ? vectors[1].receive_address : vectors[1].data;
                if(!auxiliary || vectors[1].receive_size<sizeof(struct darwin_msg_aux_header))
                    receive_error=D_MACH_RCV_INVALID_ARGUMENTS;
            }
        }
    }
    if(options&D_MACH_SEND_MSG) {
        uint32_t header[6];
        if(send_size<sizeof(header) || guest_copy(header,message,sizeof(header),0)) return D_MACH_SEND_INVALID_DATA;
        /* The existing kernel reads its header from the buffer. Explicitly
         * reject the unimplemented separate-header form instead of sending a
         * different message than the packed arguments requested. */
        if(header[0]!=(uint32_t)r[2] || header[2]!=(uint32_t)r[3] || header[3]!=(uint32_t)(r[3]>>32) ||
           header[4]!=(uint32_t)r[4] || header[5]!=(uint32_t)(r[4]>>32)) return D_MACH_SEND_INVALID_HEADER;
        uint32_t descriptors=0;
        if(header[0]&D_MACH_MSG_COMPLEX) {
            if(send_size<sizeof(header)+sizeof(descriptors) ||
               guest_copy(&descriptors,message+sizeof(header),sizeof(descriptors),0)) return D_MACH_SEND_INVALID_DATA;
        }
        if(descriptors!=(uint32_t)r[5]) return D_MACH_SEND_INVALID_HEADER;
    }
    /* Like XNU, invalid receive-vector arguments do not cancel a valid send.
     * The real send/rights effect must happen before reporting receive error. */
    uint32_t effective_options=receive_error ? options&~D_MACH_RCV_MSG : options;
    /* mach_msg2 carries the sync-send boosting port in packed remote_port.
     * The imported legacy receive ABI reuses priority for that port when
     * RCV_SYNC_WAIT is set without SEND_SYNC_OVERRIDE. Preserve the actual
     * right so the core can validate/link its genuine special reply port. */
    uint32_t legacy_priority=r[6]>>32;
    if((effective_options&D_MACH_RCV_MSG) && (options&D_MACH_RCV_SYNC_WAIT) &&
       !(options&D_MACH_SEND_SYNC_OVERRIDE)) legacy_priority=(uint32_t)r[3];
    long result=0;
    if(!receive_error || (options&D_MACH_SEND_MSG))
        result=dserver_rpc_mach_msg_overwrite((void *)message,effective_options,send_size,receive_size,
            receive_name,(uint32_t)r[7],legacy_priority,(void *)receive);
    if(!result && receive_error)result=receive_error;
    if(auxiliary && !receive_error && (options&D_MACH_RCV_MSG)) {
        /* ipc_kmsg_put_vector_to_user: a scalar/no-aux message produces an
         * actual zero-size EOF header in an offered auxiliary receive buffer.
         * Every accepted send uses the legacy no-aux core; payload aux sends
         * are rejected above, so no incoming auxiliary data can be lost here. */
        struct darwin_msg_aux_header eof={0};
        if(!result && guest_copy(&eof,auxiliary,sizeof(eof),1))result=D_MACH_RCV_INVALID_DATA;
        else if(result==D_MACH_RCV_TOO_LARGE && (options&D_MACH_RCV_LARGE) &&
                guest_copy(&eof.size,auxiliary,sizeof(eof.size),1))result=D_MACH_RCV_INVALID_DATA;
    }
    if(diagnostic)
        trace_record("DARLING_MSG2 options=0x",r[1],r[4]>>32,result);
    return result;
}
static long darling_mach(unsigned long number,const unsigned long long *r)
{
    uint32_t port=0;
    int result;
    switch (number) {
    case D_TRAP_MSG2: return darling_mach_msg2(r);
    case D_TRAP_TASK_SELF: result=dserver_rpc_task_self_trap(&port); return result ? 0 : port;
    case D_TRAP_HOST_SELF: result=dserver_rpc_host_self_trap(&port); return result ? 0 : port;
    case D_TRAP_THREAD_SELF: return darling_thread_self();
    case D_TRAP_REPLY_PORT: result=dserver_rpc_mach_reply_port(&port); return result ? 0 : port;
    case D_TRAP_SPECIAL_REPLY_PORT: result=dserver_rpc_thread_get_special_reply_port(&port); return result ? 0 : port;
    case D_TRAP_MSG: case D_TRAP_MSG_OVERWRITE: {
        result=dserver_rpc_mach_msg_overwrite((void *)r[0],r[1],r[2],r[3],r[4],r[5],r[6],number==D_TRAP_MSG_OVERWRITE ? (void *)r[7] : NULL);
        /* Observe genuine QuartzCore requests/replies after kernel copyout.
         * Bounded header read only; never call a guest method or alter rights. */
        if (!result && (r[1]&D_MACH_RCV_MSG)) {
            uint32_t header[6];
            uint64_t buffer=number==D_TRAP_MSG_OVERWRITE && r[7] ? r[7] : r[0];
            if (r[3]>=sizeof(header) && !guest_copy(header,buffer,sizeof(header),0) &&
                header[5]>=40200 && header[5]<40400)
                trace_record("DARLING_RENDER_RECEIVED id=0x",header[5],header[3],header[2]);
        }
        return result;
    }
    case D_TRAP_PORT_ALLOCATE: return dserver_rpc_mach_port_allocate(r[0],r[1],(uint32_t *)r[2]);
    case D_TRAP_PORT_DEALLOCATE: return dserver_rpc_mach_port_deallocate(r[0],r[1]);
    case D_TRAP_PORT_TYPE: {
        long result=dserver_rpc_mach_port_type(r[0],r[1],(uint32_t *)r[2]);
        uint32_t type=0;
        if (!result) guest_copy(&type,r[2],sizeof(type),0);
        trace_record("DARLING_PORT_TYPE name=0x",r[1],type,result);
        return result;
    }
    case D_TRAP_PORT_REQUEST_NOTIFICATION:
        return dserver_rpc_mach_port_request_notification(r[0],r[1],r[2],r[3],r[4],r[5],(uint32_t *)r[6]);
    case D_TRAP_PORT_MOD_REFS: return dserver_rpc_mach_port_mod_refs(r[0],r[1],r[2],r[3]);
    case D_TRAP_PORT_MOVE_MEMBER: {
        result=dserver_rpc_mach_port_move_member(r[0],r[1],r[2]);
        trace_record("DARLING_PORT_MOVE_MEMBER member=0x",r[1],r[2],result);
        return result;
    }
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
            RAW(SYS_write,native_trace_fd,marker,sizeof(marker)-1,0,0,0);
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
            RAW(SYS_write,native_trace_fd,marker,sizeof(marker)-1,0,0,0);
            trace_call(0,0,number); trace_call(1,0,r[1]); trace_call(2,0,r[2]);
            trace_call(3,0,number==D_TRAP_PORT_GUARD ? r[3] : 0); trace_call(4,0,result);
        }
        return result;
    }
    case D_TRAP_SEMAPHORE_SIGNAL: return dserver_rpc_semaphore_signal(r[0]);
    case D_TRAP_SEMAPHORE_SIGNAL_ALL: return dserver_rpc_semaphore_signal_all(r[0]);
    case D_TRAP_SEMAPHORE_WAIT: return dserver_rpc_semaphore_wait(r[0]);
    case D_TRAP_SEMAPHORE_WAIT_SIGNAL: return dserver_rpc_semaphore_wait_signal(r[0],r[1]);
    /* The imported XNU/RPC ABI takes separate uint32 seconds/nanoseconds.
     * Use the same owned Mach semaphore and real timed wait, not a sleep or
     * successful placeholder for libdispatch's timeout path. */
    case D_TRAP_SEMAPHORE_TIMEDWAIT: return dserver_rpc_semaphore_timedwait(r[0],r[1],r[2]);
    case D_TRAP_SEMAPHORE_TIMEDWAIT_SIGNAL: return dserver_rpc_semaphore_timedwait_signal(r[0],r[1],r[2],r[3]);
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

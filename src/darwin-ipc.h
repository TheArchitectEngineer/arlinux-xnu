/* Initial process-local Mach service transport, with real kernel queues.
 * Implements observed Host/Clock queries and semaphore creation.
 * Queued replies remain observable by later receive calls; send success never
 * stands in for a fabricated receive. Port sets aggregate actual receive queues
 * using epoll. General descriptor/port transfer, notifications and cross-process
 * namespace semantics are not provided. */
#pragma once
#include <sys/socket.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <linux/futex.h>
static unsigned long host_send_refs,clock_send_refs[2];
#define IPC_ENDPOINTS 64
/* Bounded inline transport, one guest page. The observed XPC request is
 * 1260 bytes; the former 1024-byte buffer misreported it as invalid data.
 * Larger messages still require dynamically owned message storage. */
#define IPC_BYTES D_GUEST_PAGE_SIZE
struct ipc_endpoint {
    uint32_t name,generation; int used,kind,read_fd,write_fd;
    unsigned long send_refs,dead_refs; uint64_t context; uint32_t options;
    int queue_mutex,special_tid; uint32_t queue_limit,queued,sequence,space_sequence;
    int packet_fd,timer_fd,timer_pending; uint64_t timer_deadline;
    uint32_t sync_destination;
};
static struct ipc_endpoint ipc_endpoints[IPC_ENDPOINTS];
static uint32_t native_bootstrap_port,native_display_port,native_render_port;
static uint32_t native_session_ports[D_SESSION_SERVICE_COUNT];
static const char *const native_session_names[D_SESSION_SERVICE_COUNT]={
    D_SESSION_PROCESSINFO_NAME,D_SESSION_WORKSPACE_NAME,D_SESSION_SYSTEMAPP_NAME,D_SESSION_HIDFOCUS_NAME};
static int native_session_manager;
static uint32_t native_provider_pid,native_provider_incarnation;

static uint64_t native_render_start_address,native_render_get_port_address;
void darwin_native_render_api(uint64_t start,uint64_t get_port)
{
    native_render_start_address=start; native_render_get_port_address=get_port;
}
#ifdef ARLINUX_DARLING
static uint32_t native_framework_render_port;
static int native_framework_render_state,native_framework_render_owner;
static uint32_t native_framework_render_start(void)
{
    int state=__atomic_load_n(&native_framework_render_state,__ATOMIC_ACQUIRE);
    if (state==2) return D_KERN_SUCCESS;
    if (state==3 || !native_render_start_address || !native_render_get_port_address)
        return D_KERN_NOT_SUPPORTED;
    int tid=RAW(SYS_gettid,0,0,0,0,0,0),expected=0;
    if (!__atomic_compare_exchange_n(&native_framework_render_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&native_framework_render_owner,__ATOMIC_ACQUIRE)==tid) return D_KERN_FAILURE;
        while (__atomic_load_n(&native_framework_render_state,__ATOMIC_ACQUIRE)==1)
            RAW(SYS_futex,&native_framework_render_state,FUTEX_WAIT_PRIVATE,1,0,0,0);
        return __atomic_load_n(&native_framework_render_state,__ATOMIC_ACQUIRE)==2 ? D_KERN_SUCCESS : D_KERN_FAILURE;
    }
    __atomic_store_n(&native_framework_render_owner,tid,__ATOMIC_RELEASE);
    /* Called on the original guest lookup thread, with genuine guest TSD.
     * These are real framework calls, not calls from our host-TLS service thread.
     * WebKit QuartzCoreSPI.h declares the bool start and Mach-port getter ABI. */
    const char marker[]="NATIVE_RENDER_FRAMEWORK_START real QuartzCore entry\n";
    RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
    _Bool started=((_Bool (*)(void))(uintptr_t)native_render_start_address)();
    uint32_t port=started ? ((uint32_t (*)(void))(uintptr_t)native_render_get_port_address)() : 0;
    uint32_t status=port ? dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,port,D_PORT_RIGHT_SEND,1) : D_KERN_FAILURE;
    if (!status) __atomic_store_n(&native_framework_render_port,port,__ATOMIC_RELEASE);
    trace_record("NATIVE_RENDER_FRAMEWORK_RESULT started=0x",started,port,status);
    __atomic_store_n(&native_framework_render_state,status ? 3 : 2,__ATOMIC_RELEASE);
    RAW(SYS_futex,&native_framework_render_state,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    return status;
}
#endif

#ifdef ARLINUX_DARLING
static int native_inherit_bootstrap(void)
{
    uint32_t reply=0; uint32_t message[32]={0};
    if (dserver_rpc_mach_reply_port(&reply)) return -1;
    struct ipc_header_bootstrap { uint32_t bits,size,remote,local,voucher; int32_t id; };
    struct ipc_header_bootstrap *header=(void *)message;
    header->bits=D_MACH_TYPE_COPY_SEND|(D_MACH_TYPE_MAKE_SEND_ONCE<<8);
    header->size=36; header->remote=CURRENT_TASK_PORT; header->local=reply;
    header->id=D_MIG_TASK_GET_SPECIAL_PORT; message[7]=1; message[8]=D_TASK_BOOTSTRAP_PORT;
    int error=dserver_rpc_mach_msg_overwrite(message,D_MACH_SEND_MSG|D_MACH_RCV_MSG,36,sizeof(message),reply,0,0,NULL);
    dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,reply,D_PORT_RIGHT_RECEIVE,-1);
    if (error || !(header->bits&D_MACH_MSG_COMPLEX) || header->size!=40 || message[6]!=1 || !message[7]) return -1;
    native_bootstrap_port=message[7]; return 0;
}
#endif
static uint32_t native_display_width,native_display_height;
static uint32_t native_display_refresh;
static uint64_t native_display_period_bits;
static int32_t native_display_pid;
/* Each occupied binding owns a server-side send reference, distinct from
 * the app's send_refs. Local receive objects currently live until exit;
 * timer objects, which can be destroyed, cannot enter this registry. */
static struct native_service_binding {
    char name[D_BOOTSTRAP_NAME_MAX]; uint32_t port; int32_t pid;
} native_services[32];
static int native_registry_mutex;
struct ipc_header { uint32_t bits,size,remote,local,voucher; int32_t id; };
/* XNU message.h: LP64 OOL uses flags before size, unlike the 32-bit form. */
struct ipc_ool_descriptor {
    uint64_t address; uint8_t deallocate,copy,padding,type; uint32_t size;
} __attribute__((packed,aligned(4)));
_Static_assert(sizeof(struct ipc_ool_descriptor)==16,"Darwin LP64 OOL descriptor ABI");
#include "darwin-display.h"
struct ipc_packet { struct darwin_audit_token sender; unsigned char message[IPC_BYTES]; };
struct ipc_timer_message {
    struct ipc_header header; uint64_t unused[3];
} __attribute__((packed,aligned(4)));
_Static_assert(sizeof(struct ipc_timer_message)==48,"Darwin mk_timer_expire message ABI");
static void native_reply_wait(uint32_t);
static void ipc_queue_lock(struct ipc_endpoint *endpoint)
{
    while (__atomic_exchange_n(&endpoint->queue_mutex,1,__ATOMIC_ACQUIRE))
        RAW(SYS_futex,&endpoint->queue_mutex,FUTEX_WAIT_PRIVATE,1,0,0,0);
}
static void ipc_queue_unlock(struct ipc_endpoint *endpoint)
{
    __atomic_store_n(&endpoint->queue_mutex,0,__ATOMIC_RELEASE);
    RAW(SYS_futex,&endpoint->queue_mutex,FUTEX_WAKE_PRIVATE,1,0,0,0);
}
/* Caller owns queue_mutex. timerfd supplies the real expiry; the socket packet
 * keeps an already delivered notification alive across cancel/rearm. No worker
 * or host libc/TLS is needed. Timer messages originate in this compatibility
 * machine's kernel service (zero kernel audit token), not in the app. */
static long ipc_timer_capture(struct ipc_endpoint *endpoint)
{
    if (endpoint->timer_fd<0) return 0;
    uint64_t expirations;
    long bytes=RAW(SYS_read,endpoint->timer_fd,&expirations,sizeof(expirations),0,0,0);
    if (bytes==-EAGAIN) return 0;
    if (bytes!=sizeof(expirations)) return bytes<0 ? bytes : -EIO;
    endpoint->timer_deadline=0;
    if (endpoint->timer_pending) return 0;
    struct ipc_timer_message message={.header={
        .bits=D_MACH_TYPE_MOVE_SEND<<D_MACH_MSG_LOCAL_BITS_SHIFT,
        .size=sizeof(message),.local=endpoint->name}};
    /* Timer notifications are fixed-size: don't reserve an entire maximum
     * message on each nested timer probe's runtime stack. */
    struct { struct darwin_audit_token sender; unsigned char message[sizeof(message)]; } packet;
    for (volatile uint32_t *word=(uint32_t *)&packet.sender;
         word<(uint32_t *)(&packet.sender+1);word++) *word=0;
    __builtin_memcpy(packet.message,&message,sizeof(message));
    unsigned long size=sizeof(packet.sender)+sizeof(message);
    bytes=RAW(SYS_sendto,endpoint->write_fd,&packet,size,MSG_DONTWAIT|MSG_NOSIGNAL,0,0);
    if (bytes!=(long)size) return bytes<0 ? bytes : -EIO;
    endpoint->queued++; endpoint->timer_pending=1;
    const char label[]="DARWIN_TIMER_EXPIRED port follows\n";
    RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
    trace_call(0,0,endpoint->name);
    return 0;
}
static long ipc_enqueue(struct ipc_endpoint *endpoint,const void *message,unsigned long size)
{
    if (size>IPC_BYTES) return -EMSGSIZE;
    struct ipc_packet packet;
    long error=native_current_audit_token(&packet.sender);
    if (error) return error;
    __builtin_memcpy(packet.message,message,size);
    ipc_queue_lock(endpoint);
    if (__atomic_load_n(&endpoint->kind,__ATOMIC_ACQUIRE)) {
        ipc_queue_unlock(endpoint); return -EPIPE;
    }
    long result=-EAGAIN;
    if (endpoint->queued<endpoint->queue_limit) {
        unsigned long bytes=size+sizeof(packet.sender);
        result=RAW(SYS_sendto,endpoint->write_fd,&packet,bytes,MSG_DONTWAIT|MSG_NOSIGNAL,0,0);
        if (result==(long)bytes) { endpoint->queued++; result=size; }
        else if (result>=0) result=-EIO;
    }
    ipc_queue_unlock(endpoint);
    return result;
}
static long ipc_enqueue_wait(struct ipc_endpoint *endpoint,const void *message,unsigned long size,
                             unsigned long options,unsigned long milliseconds)
{
    struct timespec now; uint64_t deadline=0;
    if (options&D_MACH_SEND_TIMEOUT) {
        long error=RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0);
        if (error) return error;
        deadline=(uint64_t)now.tv_sec*1000000000+now.tv_nsec+(uint64_t)(uint32_t)milliseconds*1000000;
    }
    for (;;) {
        uint32_t epoch=__atomic_load_n(&endpoint->space_sequence,__ATOMIC_ACQUIRE);
        long result=ipc_enqueue(endpoint,message,size);
        if (result!=-EAGAIN) return result;
        struct timespec remaining,*timeout=NULL;
        if (options&D_MACH_SEND_TIMEOUT) {
            long error=RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0);
            if (error) return error;
            uint64_t time=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
            if (time>=deadline) return -EAGAIN;
            uint64_t duration=deadline-time;
            remaining=(struct timespec){duration/1000000000,duration%1000000000}; timeout=&remaining;
        }
        result=RAW(SYS_futex,&endpoint->space_sequence,FUTEX_WAIT_PRIVATE,epoch,timeout,0,0);
        if (result && result!=-EAGAIN && result!=-EINTR) return result;
    }
}
/* Caller owns queue_mutex. Packet metadata is captured at the actual send,
 * not reconstructed from the receiving thread's credentials. */
static long ipc_peek(struct ipc_endpoint *endpoint,struct ipc_packet *packet)
{
    long error=ipc_timer_capture(endpoint);
    if (error) return error;
    long bytes=RAW(SYS_recvfrom,endpoint->packet_fd,packet,sizeof(*packet),MSG_PEEK|MSG_DONTWAIT,0,0);
    if (bytes<0) return bytes;
    if (bytes<(long)(sizeof(packet->sender)+sizeof(struct ipc_header))) return -EIO;
    return bytes-sizeof(packet->sender);
}
static long ipc_consume(struct ipc_endpoint *endpoint,struct ipc_packet *packet,long size)
{
    long bytes=RAW(SYS_recvfrom,endpoint->packet_fd,packet,sizeof(*packet),MSG_DONTWAIT,0,0);
    if (bytes!=size+(long)sizeof(packet->sender)) return bytes<0 ? bytes : -EIO;
    endpoint->queued--; endpoint->sequence++;
    if (endpoint->timer_fd>=0) endpoint->timer_pending=0;
    __atomic_fetch_add(&endpoint->space_sequence,1,__ATOMIC_RELEASE);
    RAW(SYS_futex,&endpoint->space_sequence,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    return 0;
}
static int ipc_receive_options(unsigned long options)
{
    unsigned long allowed=D_MACH_RCV_MSG|D_MACH_RCV_LARGE|D_MACH_RCV_LARGE_IDENTITY|
        D_MACH_RCV_VOUCHER|D_MACH_RCV_TRAILER_MASK|D_MACH_RCV_TIMEOUT|D_MACH_RCV_SYNC_WAIT;
    return !(options&~allowed) &&
        ((options&D_MACH_RCV_TRAILER_MASK)>>D_MACH_RCV_TRAILER_SHIFT)<=D_MACH_RCV_TRAILER_MAX;
}
static unsigned long ipc_receive_locked(struct ipc_endpoint *endpoint,uint64_t address,
                                        unsigned long capacity,unsigned long options,
                                        unsigned long *message_size,unsigned long *total_size)
{
    if (__atomic_load_n(&endpoint->kind,__ATOMIC_ACQUIRE)==3) return D_MACH_RCV_PORT_DIED;
    struct ipc_packet packet;
    long size=ipc_peek(endpoint,&packet);
    if (size==-EAGAIN) return D_MACH_RCV_TIMED_OUT;
    if (size<0) return D_MACH_RCV_INTERRUPTED;
    const unsigned int trailer_sizes[5]={8,12,20,52,60};
    unsigned int trailer_kind=(options&D_MACH_RCV_TRAILER_MASK)>>D_MACH_RCV_TRAILER_SHIFT;
    if (!ipc_receive_options(options)) return D_MACH_SEND_INVALID_OPTIONS;
    unsigned int trailer_size=trailer_sizes[trailer_kind];
    unsigned int aligned=((unsigned int)size+3)&~3U;
    *message_size=size; *total_size=aligned+trailer_size;
    if (capacity<*total_size) {
        if (!(options&D_MACH_RCV_LARGE) && ipc_consume(endpoint,&packet,size)) return D_MACH_RCV_INTERRUPTED;
        return D_MACH_RCV_TOO_LARGE;
    }
    unsigned char output[IPC_BYTES+sizeof(struct darwin_context_trailer)];
    __builtin_memcpy(output,packet.message,size);
    for (unsigned int i=size;i<aligned;i++) output[i]=0;
    struct darwin_context_trailer trailer={.size=trailer_size,.sequence=endpoint->sequence,
        .sender={packet.sender.euid,packet.sender.egid},.audit=packet.sender,.context=endpoint->context};
    if (endpoint->timer_fd>=0) trailer.sender[1]=1; /* KERNEL_SECURITY_TOKEN_VALUE */
    __builtin_memcpy(output+aligned,&trailer,trailer_size);
    if (guest_copy(output,address,*total_size,1)) return D_MACH_RCV_INVALID_DATA;
    return ipc_consume(endpoint,&packet,size) ? D_MACH_RCV_INTERRUPTED : D_KERN_SUCCESS;
}
static struct ipc_endpoint *ipc_find(uint32_t name)
{
    /* The selected backend owns the whole port namespace. A numeric name
     * from Darling must never alias an old process-local object. */
    if (darling_active) return NULL;
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++)
        if (__atomic_load_n(&ipc_endpoints[i].used,__ATOMIC_ACQUIRE)==2 && ipc_endpoints[i].name==name) return ipc_endpoints+i;
    return NULL;
}
static int ipc_drop_reference(unsigned long *references)
{
    unsigned long refs=__atomic_load_n(references,__ATOMIC_ACQUIRE);
    while (refs) {
        if (__atomic_compare_exchange_n(references,&refs,refs-1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 1;
    }
    return 0;
}
static int ipc_drop_send(struct ipc_endpoint *endpoint) { return ipc_drop_reference(&endpoint->send_refs); }
/* kind 0 owns a receive queue; kind 2 owns an epoll-backed port set. */
static uint32_t ipc_port_create(int kind)
{
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++) {
        struct ipc_endpoint *endpoint=ipc_endpoints+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&endpoint->used,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        int pair[2]; long error=0;
        if (kind==2) {
            long fd=RAW(SYS_epoll_create1,EPOLL_CLOEXEC,0,0,0,0,0);
            if (fd<0) error=fd;
            else pair[0]=pair[1]=fd;
        } else error=RAW(SYS_socketpair,AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair,0,0);
        if (error) {
            __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE); return 0;
        }
        endpoint->generation=endpoint->generation ?
            (endpoint->generation+D_PORT_GENERATION_STEP)&D_PORT_GENERATION_MASK : D_PORT_GENERATION_TAG;
        endpoint->name=((i+D_REPLY_PORT_INDEX_BASE)<<D_PORT_NAME_INDEX_SHIFT)|endpoint->generation;
        endpoint->read_fd=pair[0]; endpoint->write_fd=pair[1]; endpoint->kind=kind;
        endpoint->packet_fd=pair[0]; endpoint->timer_fd=-1;
        endpoint->timer_pending=0; endpoint->timer_deadline=0; endpoint->sync_destination=0;
        endpoint->send_refs=0; endpoint->dead_refs=0; endpoint->context=0; endpoint->options=0;
        endpoint->queue_mutex=0; endpoint->queued=0; endpoint->sequence=0;
        endpoint->special_tid=0;
        endpoint->space_sequence=0; endpoint->queue_limit=D_PORT_QLIMIT_DEFAULT;
        __atomic_store_n(&endpoint->used,2,__ATOMIC_RELEASE);
        return endpoint->name;
    }
    return 0;
}
static uint32_t ipc_reply_port(void) { return ipc_port_create(0); }
static uint32_t ipc_timer_create(void)
{
    uint32_t name=ipc_reply_port();
    if (!name) return D_MACH_PORT_NULL;
    struct ipc_endpoint *endpoint=ipc_find(name);
    long timer=RAW(SYS_timerfd_create,CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC,0,0,0,0);
    long poller=timer<0 ? -1 : RAW(SYS_epoll_create1,EPOLL_CLOEXEC,0,0,0,0,0);
    long error=timer<0 || poller<0;
    struct epoll_event event={.events=EPOLLIN};
    if (!error) error=RAW(SYS_epoll_ctl,poller,EPOLL_CTL_ADD,endpoint->packet_fd,&event,0,0);
    if (!error) error=RAW(SYS_epoll_ctl,poller,EPOLL_CTL_ADD,timer,&event,0,0);
    if (error) {
        if (timer>=0) RAW(SYS_close,timer,0,0,0,0,0);
        if (poller>=0) RAW(SYS_close,poller,0,0,0,0,0);
        RAW(SYS_close,endpoint->packet_fd,0,0,0,0,0);
        RAW(SYS_close,endpoint->write_fd,0,0,0,0,0);
        __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE);
        return D_MACH_PORT_NULL;
    }
    endpoint->timer_fd=timer; endpoint->read_fd=poller;
    return name;
}
static unsigned long ipc_timer_control(unsigned int number,const unsigned long long *r)
{
    struct ipc_endpoint *endpoint=ipc_find(r[0]);
    if (!endpoint) return D_KERN_INVALID_NAME;
    if (endpoint->kind || endpoint->timer_fd<0) return D_KERN_INVALID_ARGUMENT;
    /* A normal timer may fire at its exact deadline, within any requested
     * leeway. Critical scheduling/other flag policies are not implemented. */
    if (number==D_TRAP_TIMER_ARM_LEEWAY && r[1]) return D_KERN_NOT_SUPPORTED;
    ipc_queue_lock(endpoint);
    if (number==D_TRAP_TIMER_DESTROY) {
        __atomic_store_n(&endpoint->used,1,__ATOMIC_RELEASE);
        RAW(SYS_close,endpoint->timer_fd,0,0,0,0,0);
        RAW(SYS_close,endpoint->packet_fd,0,0,0,0,0);
        RAW(SYS_close,endpoint->write_fd,0,0,0,0,0);
        RAW(SYS_close,endpoint->read_fd,0,0,0,0,0);
        ipc_queue_unlock(endpoint);
        __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE);
        return D_KERN_SUCCESS;
    }
    long error=ipc_timer_capture(endpoint);
    uint64_t previous=endpoint->timer_deadline;
    struct itimerspec spec={0};
    uint64_t deadline=number==D_TRAP_TIMER_CANCEL ? 0 :
        number==D_TRAP_TIMER_ARM_LEEWAY ? r[2] : r[1];
    if (number!=D_TRAP_TIMER_CANCEL) {
        /* timerfd zero disarms; Darwin zero is an already elapsed deadline. */
        uint64_t time=deadline ? deadline : 1;
        spec.it_value=(struct timespec){time/1000000000,time%1000000000};
    }
    if (!error) error=RAW(SYS_timerfd_settime,endpoint->timer_fd,TFD_TIMER_ABSTIME,&spec,0,0,0);
    if (!error) endpoint->timer_deadline=deadline;
    ipc_queue_unlock(endpoint);
    if (error) return D_KERN_FAILURE;
    if (number==D_TRAP_TIMER_CANCEL && r[1] && guest_copy(&previous,r[1],sizeof(previous),1))
        return D_KERN_INVALID_ADDRESS;
    return D_KERN_SUCCESS;
}
static unsigned long ipc_port_allocate(const unsigned long long *r)
{
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    if (r[1]!=D_PORT_RIGHT_RECEIVE && r[1]!=D_PORT_RIGHT_PORT_SET) return D_KERN_NOT_SUPPORTED;
    uint32_t name=ipc_port_create(r[1]==D_PORT_RIGHT_PORT_SET ? 2 : 0);
    if (!name) return D_KERN_RESOURCE_SHORTAGE;
    if (!guest_copy(&name,r[2],sizeof(name),1)) return D_KERN_SUCCESS;
    struct ipc_endpoint *endpoint=ipc_find(name);
    RAW(SYS_close,endpoint->read_fd,0,0,0,0,0);
    if (endpoint->write_fd!=endpoint->read_fd) RAW(SYS_close,endpoint->write_fd,0,0,0,0,0);
    __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE);
    return D_KERN_INVALID_ADDRESS;
}
static unsigned long ipc_port_member(const unsigned long long *r,int insert)
{
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    struct ipc_endpoint *port=ipc_find(r[1]),*set=ipc_find(r[2]);
    if (!port || !set) return D_KERN_INVALID_NAME;
    if (port->kind!=0 || set->kind!=2) return D_KERN_INVALID_RIGHT;
    struct epoll_event event={.events=EPOLLIN,.data.u64=port->name};
    long status=RAW(SYS_epoll_ctl,set->read_fd,insert ? EPOLL_CTL_ADD : EPOLL_CTL_DEL,
                    port->read_fd,&event,0,0);
    if (status==-EEXIST) return D_KERN_ALREADY_IN_SET;
    if (status==-ENOENT) return D_KERN_NOT_IN_SET;
    return status ? D_KERN_RESOURCE_SHORTAGE : D_KERN_SUCCESS;
}
static unsigned long ipc_port_insert_right(const unsigned long long *r)
{
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    struct ipc_endpoint *port=ipc_find(r[1]);
    if (!port) return D_KERN_INVALID_NAME;
    if (port->kind || port->timer_fd>=0) return D_KERN_INVALID_RIGHT;
    /* Only making a send right from this task's own receive right is supported.
     * A rename, cross-task transfer or send-once right needs different ownership. */
    if (r[1]!=r[2] || r[3]!=D_MACH_TYPE_MAKE_SEND) return D_KERN_NOT_SUPPORTED;
    __atomic_fetch_add(&port->send_refs,1,__ATOMIC_ACQ_REL);
    return D_KERN_SUCCESS;
}
static unsigned long ipc_port_mod_refs(const unsigned long long *r)
{
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    if (r[2]!=D_PORT_RIGHT_SEND && r[2]!=D_PORT_RIGHT_DEAD_NAME) return D_KERN_NOT_SUPPORTED;
    struct ipc_endpoint *endpoint=ipc_find(r[1]);
    if (r[2]==D_PORT_RIGHT_DEAD_NAME && (!endpoint || endpoint->kind!=3)) return D_KERN_INVALID_RIGHT;
    unsigned long *references=endpoint ? (r[2]==D_PORT_RIGHT_DEAD_NAME ? &endpoint->dead_refs : &endpoint->send_refs) :
        r[1]==CURRENT_HOST_PORT ? &host_send_refs :
        r[1]==SYSTEM_CLOCK_PORT ? &clock_send_refs[0] :
        r[1]==CALENDAR_CLOCK_PORT ? &clock_send_refs[1] : NULL;
    if (!references) return D_KERN_INVALID_NAME;
    int64_t delta=(int32_t)r[3];
    unsigned long current=__atomic_load_n(references,__ATOMIC_ACQUIRE);
    for (;;) {
        if (!current) return D_KERN_INVALID_RIGHT;
        if (delta<0 && (uint64_t)-delta>current) return D_KERN_INVALID_VALUE;
        if (delta>0 && (uint64_t)delta>D_PORT_UREFS_MAX-current) return D_KERN_UREFS_OVERFLOW;
        unsigned long next=current+delta;
        if (__atomic_compare_exchange_n(references,&current,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
            return D_KERN_SUCCESS;
    }
}
static unsigned long ipc_special_reply_destruct(const unsigned long long *r)
{
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    struct ipc_endpoint *endpoint=ipc_find(r[1]);
    if (!endpoint) return D_KERN_INVALID_NAME;
    ipc_queue_lock(endpoint);
    unsigned long status=D_KERN_NOT_SUPPORTED;
    /* libdispatch disconnects an owned special reply receive right from
     * another thread. No task ownership check against the caller's TID.
     * Broader guarded/queued-message destruction needs resource cleanup. */
    if (endpoint->kind) status=D_KERN_INVALID_RIGHT;
    else if (endpoint->special_tid && !endpoint->options && endpoint->timer_fd<0 && !endpoint->queued) {
        int64_t delta=(int32_t)r[2];
        unsigned long refs=__atomic_load_n(&endpoint->send_refs,__ATOMIC_ACQUIRE);
        if (r[3] || delta>0 || (uint64_t)-delta>refs) status=D_KERN_INVALID_ARGUMENT;
        else {
            long result=RAW(SYS_shutdown,endpoint->packet_fd,SHUT_RDWR,0,0,0,0);
            status=result ? D_KERN_FAILURE : D_KERN_SUCCESS;
            if (!result) {
                /* Keep a tombstone and the shutdown socket until exit: a
                 * receiver/knote can still hold this endpoint or a dup fd.
                 * Reusing its slot/fd now could wake the wrong operation. */
                __atomic_store_n(&endpoint->dead_refs,refs+delta,__ATOMIC_RELEASE);
                __atomic_store_n(&endpoint->send_refs,0,__ATOMIC_RELEASE);
                __atomic_store_n(&endpoint->kind,3,__ATOMIC_RELEASE);
                __atomic_fetch_add(&endpoint->space_sequence,1,__ATOMIC_RELEASE);
                RAW(SYS_futex,&endpoint->space_sequence,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
                const char label[]="DARWIN_REPLY_PORT_DESTROYED name/dead-references follows\n";
                RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
                trace_call(0,0,endpoint->name); trace_call(1,0,refs+delta);
            }
        }
    }
    ipc_queue_unlock(endpoint);
    return status;
}
/* A registry for this standalone compatibility machine, not launchd/XPC.
 * Public APIs are redirected by symbol in the private cache map. */
/* Host-TLS preparation, independent of display ownership. */
int darwin_native_session_join(void)
{
#ifdef ARLINUX_DARLING
    if (!darling_active || native_session_manager) return -1;
    if (native_bootstrap_port) return 0;
    const char *provider=getenv("ARLINUX_SESSION_PID"); char *end=NULL;
    long pid=provider ? strtol(provider,&end,10) : 0; uint64_t ticks=0; uint32_t parent=0;
    if (!provider || !*provider || *end || pid<=0 || pid>INT32_MAX ||
        native_proc_start(pid,&ticks,&parent) || native_inherit_bootstrap()) return -1;
    native_provider_pid=pid; native_provider_incarnation=(uint32_t)ticks;
    return 0;
#else
    return -1;
#endif
}
int darwin_native_display_register(uint32_t width,uint32_t height,uint32_t refresh_millihz,
                                   uint64_t period_bits,int32_t backend_pid)
{
    if (!width || !height || width>8192 || height>8192 || backend_pid<=0 || native_display_port || native_render_port) return -1;
    #ifdef ARLINUX_DARLING
    if (darling_active) {
        if (darwin_native_session_join()) return -1;
        uint32_t *names[]={&native_display_port,&native_render_port};
        for (unsigned int i=0;i<2;i++) {
            if (dserver_rpc_mach_port_allocate(CURRENT_TASK_PORT,D_PORT_RIGHT_RECEIVE,names[i]) ||
                dserver_rpc_mach_port_insert_right(CURRENT_TASK_PORT,*names[i],*names[i],D_MACH_TYPE_MAKE_SEND)) return -1;
        }
    } else
    #endif
    { native_bootstrap_port=ipc_reply_port(); native_display_port=ipc_reply_port(); native_render_port=ipc_reply_port(); }
    if (!native_bootstrap_port || !native_display_port || !native_render_port) return -1;
    if (!darling_active) ipc_find(native_bootstrap_port)->send_refs=1;
    native_display_width=width; native_display_height=height;
    native_display_refresh=refresh_millihz; native_display_period_bits=period_bits;
    native_display_pid=backend_pid;
#ifdef ARLINUX_DARLING
    if (darling_active) {
        uint32_t message[10]={D_MACH_TYPE_COPY_SEND,sizeof(message),native_bootstrap_port,0,0,
            D_SESSION_DISPLAY_REGISTER,width,height,refresh_millihz,(uint32_t)backend_pid};
        int status=darwin_native_host_send(message,sizeof(message));
        if (status) return status;
    }
#endif
    return 0;
}
static int ipc_display_live(void)
{
    siginfo_t status={0};
    return native_display_pid>0 &&
        !RAW(SYS_waitid,P_PID,native_display_pid,&status,WEXITED|WNOHANG|WNOWAIT,0,0) && !status.si_pid;
}
/* Exactly represent a bounded positive pixel count in IEEE binary32 without
 * floating-point instructions in the guest's TLS-independent runtime. */
static uint32_t ipc_display_float(uint32_t value)
{
    unsigned int exponent=31-__builtin_clz(value);
    return ((exponent+127)<<23)|((value<<(23-exponent))&0x7fffffU);
}
#include "darwin-display-service.h"

int darwin_native_services_enabled(void)
{
    return darling_active && (native_display_port || native_session_manager);
}
int darwin_native_session_start(void)
{
#ifdef ARLINUX_DARLING
    if (!darling_active || native_bootstrap_port) return -1;
    uint32_t *names[]={&native_bootstrap_port,&native_session_ports[D_SESSION_PROCESSINFO],
        &native_session_ports[D_SESSION_WORKSPACE],&native_session_ports[D_SESSION_SYSTEMAPP],
        &native_session_ports[D_SESSION_HIDFOCUS]};
    for (unsigned int i=0;i<sizeof(names)/sizeof(names[0]);i++)
        if (dserver_rpc_mach_port_allocate(CURRENT_TASK_PORT,D_PORT_RIGHT_RECEIVE,names[i]) ||
            dserver_rpc_mach_port_insert_right(CURRENT_TASK_PORT,*names[i],*names[i],D_MACH_TYPE_MAKE_SEND)) return -1;
    if (dserver_rpc_task_set_special_port(D_TASK_BOOTSTRAP_PORT,native_bootstrap_port)) return -1;
    native_session_manager=1; return 0;
#else
    return -1;
#endif
}
uint32_t darwin_native_host_task(void) { return CURRENT_TASK_PORT; }
int darwin_native_host_identity(uint32_t *pid,uint32_t *incarnation)
{
    struct darwin_audit_token token;
    long error=native_current_audit_token(&token);
    if (error) return D_KERN_FAILURE;
    *pid=token.pid; *incarnation=token.idversion; return 0;
}
int darwin_native_host_process_matches(uint32_t pid,uint32_t incarnation)
{
    uint64_t ticks=0; uint32_t parent=0;
    return !native_proc_start(pid,&ticks,&parent) && (uint32_t)ticks==incarnation;
}
int darwin_native_host_receive_destroy(uint32_t port)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,port,D_PORT_RIGHT_RECEIVE,-1);
#endif
    (void)port; return D_KERN_NOT_SUPPORTED;
}
int darwin_native_host_receive_create(uint32_t *port)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_port_allocate(CURRENT_TASK_PORT,D_PORT_RIGHT_RECEIVE,port);
#endif
    (void)port; return D_KERN_NOT_SUPPORTED;
}
int darwin_native_host_child(uint32_t pid,uint32_t parent)
{
#ifdef ARLINUX_DARLING
    uint64_t ticks=0; uint32_t actual_parent=0;
    return !native_proc_start(pid,&ticks,&actual_parent) && actual_parent==parent;
#else
    (void)pid; (void)parent; return 0;
#endif
}
int darwin_native_host_send(void *message,uint32_t size)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_msg_overwrite(message,D_MACH_SEND_MSG|D_MACH_SEND_TIMEOUT,size,0,0,100,0,NULL);
#endif
    (void)message; (void)size; return D_KERN_NOT_SUPPORTED;
}
uint32_t darwin_native_bootstrap_peer(uint64_t token_address)
{
    struct darwin_audit_token peer,self;
    if (!native_bootstrap_port || guest_copy(&peer,token_address,sizeof(peer),0) ||
        native_current_audit_token(&self)) return D_KERN_FAILURE;
    /* Authenticate the independent session provider chosen before guest entry.
     * Keep actual audit credentials and its real Linux process incarnation.
     * Original requester-echo checks follow. */
    return peer.pid==native_provider_pid && peer.euid==self.euid && peer.idversion==native_provider_incarnation ?
        D_KERN_SUCCESS : D_KERN_FAILURE;
}
int darwin_native_host_join(uint32_t port,uint32_t set)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_port_insert_member(CURRENT_TASK_PORT,port,set);
#endif
    (void)port; (void)set; return D_KERN_NOT_SUPPORTED;
}
int darwin_native_host_port_refs(uint32_t port,int delta)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,port,D_PORT_RIGHT_SEND,delta);
#endif
    (void)port; (void)delta; return D_KERN_NOT_SUPPORTED;
}
int darwin_native_host_port_deallocate(uint32_t port)
{
#ifdef ARLINUX_DARLING
    if (darling_active) return dserver_rpc_mach_port_deallocate(CURRENT_TASK_PORT,port);
#endif
    (void)port; return D_KERN_NOT_SUPPORTED;
}
void *darwin_native_service_run(void *unused)
{
    (void)unused;
#ifdef ARLINUX_DARLING
    if (!darwin_native_services_enabled()) return NULL;
    uint32_t set=0;
    if (dserver_rpc_mach_port_allocate(CURRENT_TASK_PORT,D_PORT_RIGHT_PORT_SET,&set) ||
        (native_display_port && dserver_rpc_mach_port_insert_member(CURRENT_TASK_PORT,native_display_port,set)) ||
        (native_render_port && dserver_rpc_mach_port_insert_member(CURRENT_TASK_PORT,native_render_port,set)) ||
        (native_session_manager && dserver_rpc_mach_port_insert_member(CURRENT_TASK_PORT,native_bootstrap_port,set)))
        darling_abort("create display service port set");
    if (native_session_manager) {
        for (unsigned int i=0;i<D_SESSION_SERVICE_COUNT;i++)
            if (dserver_rpc_mach_port_insert_member(CURRENT_TASK_PORT,native_session_ports[i],set))
                darling_abort("join session service port set");
        const char ready[]="SESSION_READY shared-bootstrap-process\n";
        RAW(SYS_write,2,ready,sizeof(ready)-1,0,0,0);
    }
    unsigned int traces=0;
    for (;;) {
        if (native_session_manager) arlinux_xpc_session_tick(set);
        unsigned char request[IPC_BYTES+256]; uint32_t response[IPC_BYTES/4];
        int status=dserver_rpc_mach_msg_overwrite(request,D_MACH_RCV_MSG|(3U<<24)|
            (native_session_manager ? D_MACH_RCV_TIMEOUT : 0),0,sizeof(request),set,100,0,NULL);
        if (native_session_manager && status==D_MACH_RCV_TIMED_OUT) continue;
        if (status) { trace_call(0,0,status); darling_abort("receive display service request"); }
        struct ipc_header *header=(void *)request,*out=(void *)response;
        if (header->size<sizeof(*header) || header->size>IPC_BYTES)
            darling_abort("invalid service request size");
        unsigned long trailer_offset=(header->size+3UL)&~3UL;
        struct darwin_context_trailer *trailer=(void *)(request+trailer_offset);
        if (trailer->type || trailer->size<52 || trailer_offset+trailer->size>sizeof(request))
            darling_abort("missing service requester audit trailer");
        uint32_t requester[8]; __builtin_memcpy(requester,&trailer->audit,sizeof(requester));
        if (native_session_manager && header->local==native_bootstrap_port &&
            arlinux_session_display(request,header->size,requester)) continue;
        int processinfo=arlinux_xpc_session(request,header->size,response,sizeof(response),native_session_ports,set,requester);
        if (!processinfo) continue;
        if (processinfo>0) {
            status=dserver_rpc_mach_msg_overwrite(response,D_MACH_SEND_MSG,out->size,0,0,0,0,NULL);
            if (status) darling_abort("send process-info reply");
            continue;
        }
        if (header->local==native_render_port &&
            header->id!=D_MIG_CA_COPY_DISPLAY_LIST && header->id!=D_MIG_CA_COPY_DISPLAY_PROPERTIES) {
            uint32_t render_port=__atomic_load_n(&native_framework_render_port,__ATOMIC_ACQUIRE);
            unsigned int reply_type=header->bits&255;
            if (render_port && !header->voucher &&
                ((!header->remote && !reply_type) || (header->remote && reply_type==D_MACH_TYPE_MOVE_SEND_ONCE))) {
                /* Forward the actual request and move its original reply right.
                 * No reply, client ID, context port or shared memory is invented.
                 * Received port descriptor dispositions already describe the
                 * rights we own. Move received OOL mappings with the message. */
                if (header->bits&D_MACH_MSG_COMPLEX) {
                    uint32_t count; __builtin_memcpy(&count,request+sizeof(*header),4);
                    size_t cursor=sizeof(*header)+4;
                    for (uint32_t i=0;i<count;i++) {
                        if (cursor>header->size || header->size-cursor<12)
                            darling_abort("truncated render descriptor");
                        unsigned int type=request[cursor+11];
                        size_t bytes=type==D_MACH_MSG_PORT_DESCRIPTOR ? 12 : 16;
                        if (type>D_MACH_MSG_OOL_VOLATILE_DESCRIPTOR || bytes>header->size-cursor)
                            darling_abort("unsupported render descriptor");
                        if (type!=D_MACH_MSG_PORT_DESCRIPTOR) request[cursor+8]=1;
                        cursor+=bytes;
                    }
                }
                uint32_t reply=header->remote,id=header->id;
                header->bits=(header->bits&D_MACH_MSG_COMPLEX)|D_MACH_TYPE_COPY_SEND|
                    (reply_type<<D_MACH_MSG_LOCAL_BITS_SHIFT);
                header->remote=render_port; header->local=reply;
                int status=dserver_rpc_mach_msg_overwrite(request,D_MACH_SEND_MSG,header->size,0,0,0,0,NULL);
                trace_record("NATIVE_RENDER_FORWARD id=0x",id,render_port,status);
                if (status) darling_abort("forward real QuartzCore request");
                continue;
            }
        }
        if (
            (header->bits&D_MACH_MSG_COMPLEX) || header->voucher ||
            (header->bits&255)!=D_MACH_TYPE_MOVE_SEND_ONCE || header->id>INT32_MAX-D_MIG_REPLY_ID_OFFSET) {
            const char marker[]="DARLING_UNSUPPORTED_SERVICE port/id/bytes/bits/voucher/descriptors follows\n";
            RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
            trace_call(0,0,header->local); trace_call(1,0,header->id);
            trace_call(2,0,header->size); trace_call(3,0,header->bits); trace_call(4,0,header->voucher);
            uint32_t descriptors=0;
            if ((header->bits&D_MACH_MSG_COMPLEX) && header->size>=sizeof(*header)+sizeof(descriptors))
                __builtin_memcpy(&descriptors,request+sizeof(*header),sizeof(descriptors));
            trace_call(5,0,descriptors);
            /* XNU message.h: descriptor type is byte 11 in both LP64 port
             * (12-byte) and OOL (16-byte) descriptors. Inspect wire metadata,
             * never dereference transferred pointers or consume rights here. */
            size_t cursor=sizeof(*header)+sizeof(descriptors);
            unsigned int decoded=0;
            for (; decoded<descriptors && decoded<D_DIAGNOSTIC_REQUEST_LIMIT;decoded++) {
                if (cursor>header->size || header->size-cursor<12) break;
                unsigned int type=request[cursor+11];
                size_t bytes=type==D_MACH_MSG_PORT_DESCRIPTOR ? 12 : 16;
                if (type>D_MACH_MSG_OOL_VOLATILE_DESCRIPTOR || bytes>header->size-cursor) break;
                uint32_t name_or_size=0; uint64_t address=0;
                if (type==D_MACH_MSG_PORT_DESCRIPTOR) __builtin_memcpy(&name_or_size,request+cursor,4);
                else {
                    __builtin_memcpy(&address,request+cursor,8);
                    __builtin_memcpy(&name_or_size,request+cursor+12,4);
                }
                const char descriptor_marker[]="DARLING_SERVICE_DESCRIPTOR index/type/flags/name-or-size/address follows\n";
                RAW(SYS_write,2,descriptor_marker,sizeof(descriptor_marker)-1,0,0,0);
                trace_call(0,0,decoded); trace_call(1,0,type);
                trace_call(2,0,request[cursor+10]); trace_call(3,0,name_or_size); trace_call(4,0,address);
                cursor+=bytes;
            }
            /* Bound the trailing inline scalars (including the NDR record),
             * retaining exact offsets instead of guessing a method layout. */
            if (decoded==descriptors) {
                const char scalar_marker[]="DARLING_SERVICE_INLINE offset/value pairs follow\n";
                RAW(SYS_write,2,scalar_marker,sizeof(scalar_marker)-1,0,0,0);
                for (unsigned int i=0;i<8 && cursor<=header->size && header->size-cursor>=4;i++,cursor+=4) {
                    uint32_t value; __builtin_memcpy(&value,request+cursor,4);
                    trace_call(cursor,0,value);
                }
            }
            darling_abort("unsupported display request rights/body");
        }
        for (volatile uint32_t *word=response;word<response+IPC_BYTES/4;word++) *word=0;
        out->bits=D_MACH_TYPE_MOVE_SEND_ONCE; out->remote=header->remote;
        out->size=36; out->id=header->id+D_MIG_REPLY_ID_OFFSET;
        response[7]=1; response[8]=(uint32_t)D_MIG_BAD_ID;
        unsigned long ool=0; uint32_t held_port=0;
        if (header->local!=native_bootstrap_port ||
            !arlinux_xpc_bootstrap(request,response,sizeof(response),native_bootstrap_port,&held_port,requester))
            ool=ipc_display_response(header->local==native_render_port,
                header->local==native_display_port,request,header->size,response,0,0);
        if (traces++<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char label[]="DARLING_SERVICE_REPLY port/id/request/reply follows\n";
            RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
            trace_call(0,0,header->local); trace_call(1,0,header->id);
            trace_call(2,0,header->size); trace_call(3,0,out->size);
        }
        status=dserver_rpc_mach_msg_overwrite(response,D_MACH_SEND_MSG,out->size,0,0,0,0,NULL);
        if (held_port) darwin_native_host_port_refs(held_port,-1);
        /* The source mapping remains ours (deallocate=0); XNU owns its snapshot
         * after successful send. No host mapping is exposed as a fake copyout. */
        if (ool) RAW(SYS_munmap,ool,PAGE16,0,0,0,0);
        if (status) { trace_call(0,0,status); darling_abort("send display service reply"); }
    }
#else
    return NULL;
#endif
}

static void native_registry_lock(void)
{
    while (__atomic_exchange_n(&native_registry_mutex,1,__ATOMIC_ACQUIRE))
        RAW(SYS_futex,&native_registry_mutex,FUTEX_WAIT_PRIVATE,1,0,0,0);
}
static void native_registry_unlock(void)
{
    __atomic_store_n(&native_registry_mutex,0,__ATOMIC_RELEASE);
    RAW(SYS_futex,&native_registry_mutex,FUTEX_WAKE_PRIVATE,1,0,0,0);
}
static uint32_t native_service_name(uint64_t address,char *name)
{
    for (unsigned int i=0;i<D_BOOTSTRAP_NAME_MAX;i++) {
        if (guest_copy(name+i,address+i,1,0)) return D_KERN_INVALID_ADDRESS;
        if (!name[i]) return i ? D_KERN_SUCCESS : D_KERN_INVALID_ARGUMENT;
    }
    return D_KERN_INVALID_ARGUMENT;
}
static int native_service_equal(const char *left,const char *right)
{
    for (unsigned int i=0;i<D_BOOTSTRAP_NAME_MAX;i++) {
        if (left[i]!=right[i]) return 0;
        if (!left[i]) return 1;
    }
    return 0;
}
static uint32_t native_service_static(const char *name)
{
    if (native_session_manager)
        for (unsigned int i=D_SESSION_WORKSPACE;i<D_SESSION_SERVICE_COUNT;i++)
            if (native_service_equal(name,native_session_names[i])) return native_session_ports[i];
    return native_service_equal(name,"com.apple.backboard.display.services") ? native_display_port :
        native_service_equal(name,"com.apple.CARenderServer") ? native_render_port : 0;
}
int darwin_native_system_lookup(const char *name,uint64_t type,uint64_t flags,uint32_t *port)
{
    /* The compatibility system namespace contains only our actual provider.
     * A privileged lookup selects this controlled namespace; it does not mint
     * a root audit token or authorize arbitrary application registrations. */
    if (type!=1 || flags!=8) return D_ENOTSUP;
    uint32_t listener=0;
    for (unsigned int i=0;i<D_SESSION_SERVICE_COUNT;i++)
        if (native_service_equal(name,native_session_names[i])) listener=native_session_ports[i];
    if (!listener) return D_ENOTSUP;
    int status=darwin_native_host_port_refs(listener,1);
    if (!status) *port=listener;
    return status;
}
uint32_t darwin_native_service_check_in(uint32_t bootstrap,uint64_t name,uint64_t output,uint64_t flags)
{
    (void)output;
    if (!native_bootstrap_port || bootstrap!=native_bootstrap_port) return D_MACH_SEND_INVALID_DEST;
    if (flags&~(uint64_t)D_BOOTSTRAP_PER_PID_SERVICE) return D_KERN_NOT_SUPPORTED;
    char service[D_BOOTSTRAP_NAME_MAX]; uint32_t error=native_service_name(name,service);
    if (error) return error;
    int32_t pid=flags ? RAW(SYS_getpid,0,0,0,0,0,0) : 0;
    /* No launch-on-demand receive rights are preconfigured. Dynamically
     * registered services already have an owner; check-in cannot steal it. */
    uint32_t status=!pid && native_service_static(service) ?
        D_BOOTSTRAP_SERVICE_ACTIVE : D_BOOTSTRAP_UNKNOWN_SERVICE;
    native_registry_lock();
    for (unsigned int i=0;i<32;i++)
        if (native_services[i].port && native_services[i].pid==pid &&
            native_service_equal(service,native_services[i].name)) status=D_BOOTSTRAP_SERVICE_ACTIVE;
    native_registry_unlock();
    return status;
}
uint32_t darwin_native_service_register(uint32_t bootstrap,uint64_t name,uint32_t port,uint64_t flags)
{
    if (!native_bootstrap_port || bootstrap!=native_bootstrap_port) return D_MACH_SEND_INVALID_DEST;
    if (flags&~(uint64_t)D_BOOTSTRAP_PER_PID_SERVICE) return D_KERN_NOT_SUPPORTED;
    char service[D_BOOTSTRAP_NAME_MAX]; uint32_t error=native_service_name(name,service);
    if (error) return error;
    int32_t pid=flags ? RAW(SYS_getpid,0,0,0,0,0,0) : 0;
    if (!pid && native_service_static(service)) return D_BOOTSTRAP_NAME_IN_USE;
    #ifdef ARLINUX_DARLING
    if (darling_active && port) {
        uint32_t type=0;
        if (dserver_rpc_mach_port_type(CURRENT_TASK_PORT,port,&type) || !(type&D_PORT_TYPE_SEND))
            return D_KERN_INVALID_RIGHT;
        int error=dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,port,D_PORT_RIGHT_SEND,1);
        if (error) return error;
    } else
    #endif
    {
        struct ipc_endpoint *endpoint=port ? ipc_find(port) : NULL;
        if (port && (!endpoint || endpoint->kind || endpoint->timer_fd>=0 || endpoint->special_tid ||
            !__atomic_load_n(&endpoint->send_refs,__ATOMIC_ACQUIRE))) return D_KERN_INVALID_RIGHT;
    }
    native_registry_lock();
    struct native_service_binding *entry=NULL,*empty=NULL;
    for (unsigned int i=0;i<32;i++) {
        struct native_service_binding *candidate=native_services+i;
        if (!candidate->port) { if (!empty) empty=candidate; continue; }
        if (candidate->pid==pid && native_service_equal(candidate->name,service)) entry=candidate;
    }
    uint32_t status=D_KERN_SUCCESS;
    if (!port) {
        if (entry) {
            #ifdef ARLINUX_DARLING
            if (darling_active) dserver_rpc_mach_port_deallocate(CURRENT_TASK_PORT,entry->port);
            #endif
            entry->port=0;
        }
        else status=D_BOOTSTRAP_UNKNOWN_SERVICE;
    } else if (entry) status=D_BOOTSTRAP_NAME_IN_USE;
    else if (!empty) status=D_BOOTSTRAP_NO_MEMORY;
    else {
        unsigned int i=0; do { empty->name[i]=service[i]; } while (service[i++]);
        empty->pid=pid; empty->port=port;
    }
    native_registry_unlock();
    #ifdef ARLINUX_DARLING
    if (darling_active && port && status) dserver_rpc_mach_port_deallocate(CURRENT_TASK_PORT,port);
    #endif
    const char label[]="DARWIN_SERVICE_REGISTER name/port/status follows\n";
    RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
    unsigned int length=0; while (service[length]) length++;
    RAW(SYS_write,2,service,length,0,0,0); RAW(SYS_write,2,"\n",1,0,0,0);
    trace_call(0,0,port); trace_call(1,0,status);
    return status;
}
uint32_t darwin_native_service_lookup(uint32_t bootstrap,uint64_t name,uint64_t output,
                                    int32_t target,uint64_t instance,uint64_t flags)
{
    (void)instance; /* Default lookup has no instance-selection flag. */
    if (!native_bootstrap_port || bootstrap!=native_bootstrap_port) return D_MACH_SEND_INVALID_DEST;
    if (!native_session_manager) {
#ifdef ARLINUX_DARLING
        if (darling_active) {
            uint64_t ticks=0; uint32_t parent=0;
            if (!native_provider_pid || native_proc_start(native_provider_pid,&ticks,&parent) ||
                (uint32_t)ticks!=native_provider_incarnation) return D_KERN_FAILURE;
        } else
#endif
        if (!ipc_display_live()) return D_KERN_FAILURE;
    }
    if (flags || (target && target!=RAW(SYS_getpid,0,0,0,0,0,0))) return D_KERN_NOT_SUPPORTED;
    char service[D_BOOTSTRAP_NAME_MAX]; uint32_t error=native_service_name(name,service);
    if (error) return error;
#ifdef ARLINUX_DARLING
    if (darling_active && !target && native_service_equal(service,"com.apple.CARenderServer")) {
        uint32_t status=native_framework_render_start();
        if (status) return status;
    }
#endif
    native_registry_lock();
    uint32_t port=target ? 0 : native_service_static(service);
    for (unsigned int i=0;!port && i<32;i++)
        if (native_services[i].port && native_services[i].pid==target &&
            native_service_equal(service,native_services[i].name)) port=native_services[i].port;
    uint32_t status=D_BOOTSTRAP_UNKNOWN_SERVICE;
    if (port) {
        #ifdef ARLINUX_DARLING
        if (darling_active) {
            status=dserver_rpc_mach_port_mod_refs(CURRENT_TASK_PORT,port,D_PORT_RIGHT_SEND,1);
            if (!status && guest_copy(&port,output,sizeof(port),1)) {
                dserver_rpc_mach_port_deallocate(CURRENT_TASK_PORT,port); status=D_KERN_INVALID_ADDRESS;
            }
        } else
        #endif
        {
        struct ipc_endpoint *endpoint=ipc_find(port);
        if (endpoint && !endpoint->kind) {
            status=guest_copy(&port,output,sizeof(port),1) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
            if (!status) __atomic_fetch_add(&endpoint->send_refs,1,__ATOMIC_ACQ_REL);
        }
        }
    }
    native_registry_unlock();
    return status;
}
static unsigned long ipc_port_construct(const unsigned long long *r,unsigned long pc)
{
    struct darwin_port_options options;
    if (r[0]!=CURRENT_TASK_PORT) return D_KERN_INVALID_TASK;
    if (guest_copy(&options,r[1],sizeof(options),0)) return D_KERN_INVALID_ADDRESS;
    const char label[]="DARWIN_PORT_CONSTRUCT flags/qlimit/context follows\n";
    RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
    trace_call(0,pc,options.flags); trace_call(1,pc,options.queue_limit); trace_call(2,pc,r[2]);
    if (options.flags&~(D_MPO_CONTEXT_AS_GUARD|D_MPO_INSERT_SEND_RIGHT|D_MPO_STRICT|
                       D_MPO_QLIMIT|D_MPO_IMPORTANCE_RECEIVER|D_MPO_TEMPOWNER) ||
        options.reserved[0] || options.reserved[1]) return D_KERN_NOT_SUPPORTED;
    if ((options.flags&D_MPO_QLIMIT) && options.queue_limit>D_PORT_QLIMIT_MAX)
        return D_KERN_INVALID_ARGUMENT;
    if ((options.flags&D_MPO_STRICT) && !(options.flags&D_MPO_CONTEXT_AS_GUARD))
        return D_KERN_INVALID_ARGUMENT;
    uint32_t name=ipc_reply_port();
    if (!name) return D_KERN_RESOURCE_SHORTAGE;
    struct ipc_endpoint *endpoint=ipc_find(name);
    endpoint->context=r[2]; endpoint->options=options.flags;
    endpoint->send_refs=(options.flags&D_MPO_INSERT_SEND_RIGHT) ? 1 : 0;
    if (options.flags&D_MPO_QLIMIT) endpoint->queue_limit=options.queue_limit;
    /* TEMPOWNER marks the creating task as initial importance owner;
     * IMPORTANCE_RECEIVER records eligibility. All queues here remain in the
     * same task under ordinary Linux scheduling: no App Nap or cross-task
     * importance propagation is claimed. Delivery/queue limits remain real. */
    /* The name owns a genuine Linux message queue and receive right. Inline
     * sends without transferred rights can use it; construction never
     * impersonates a remote XPC service. */
    if (guest_copy(&name,r[3],sizeof(name),1)) {
        RAW(SYS_close,endpoint->read_fd,0,0,0,0,0);
        RAW(SYS_close,endpoint->write_fd,0,0,0,0,0);
        __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE);
        return D_KERN_INVALID_ADDRESS;
    }
    return D_KERN_SUCCESS;
}
static uint32_t ipc_semaphore(unsigned int initial)
{
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++) {
        struct ipc_endpoint *endpoint=ipc_endpoints+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&endpoint->used,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        long fd=RAW(SYS_eventfd2,initial,EFD_SEMAPHORE|EFD_NONBLOCK|EFD_CLOEXEC,0,0,0,0);
        if (fd<0) { __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE); return 0; }
        endpoint->generation=endpoint->generation ?
            (endpoint->generation+D_PORT_GENERATION_STEP)&D_PORT_GENERATION_MASK : D_PORT_GENERATION_TAG;
        endpoint->name=((i+D_REPLY_PORT_INDEX_BASE)<<D_PORT_NAME_INDEX_SHIFT)|endpoint->generation;
        endpoint->read_fd=endpoint->write_fd=fd;
        endpoint->kind=1; endpoint->send_refs=1; __atomic_store_n(&endpoint->used,2,__ATOMIC_RELEASE);
        return endpoint->name;
    }
    return 0;
}
/* Linux eventfd maintains the actual count; poll supplies real timed waits.
 * Contended Mach FIFO policy and concurrent namespace access are unverified. */
static long ipc_semaphore_wait(uint32_t name,const struct timespec *timeout)
{
    struct ipc_endpoint *endpoint=ipc_find(name);
    if (!endpoint || endpoint->kind!=1) return -EINVAL;
    uint64_t token;
    long result=RAW(SYS_read,endpoint->read_fd,&token,8,0,0,0);
    if (result==8) return 0;
    if (result!=-EAGAIN) return result<0 ? result : -EIO;
    struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
    long ready=RAW(SYS_ppoll,&descriptor,1,timeout,0,0,0);
    if (!ready) return -ETIMEDOUT;
    if (ready<0) return ready;
    result=RAW(SYS_read,endpoint->read_fd,&token,8,0,0,0);
    return result==8 ? 0 : (result<0 ? result : -EIO);
}
static unsigned long ipc_receive_complete(struct ipc_endpoint *endpoint,unsigned long status)
{
    __atomic_store_n(&endpoint->sync_destination,0,__ATOMIC_RELEASE);
    if (status==D_MACH_RCV_PORT_DIED) {
        const char label[]="DARWIN_REPLY_RECEIVE_DIED port follows\n";
        RAW(SYS_write,2,label,sizeof(label)-1,0,0,0); trace_call(0,0,endpoint->name);
    }
    return status;
}
static unsigned long ipc_message(const unsigned long long *r,unsigned long pc)
{
    unsigned long options=r[1];
    unsigned long send_options=D_MACH_SEND_MSG|D_MACH_SEND_TIMEOUT|D_MACH_SEND_OVERRIDE|
        D_MACH_SEND_NOIMPORTANCE|D_MACH_SEND_SYNC_OVERRIDE|D_MACH_SEND_PROPAGATE_QOS;
    int priority_argument=(options&D_MACH_SEND_MSG) &&
        (options&(D_MACH_SEND_OVERRIDE|D_MACH_SEND_SYNC_OVERRIDE|D_MACH_SEND_PROPAGATE_QOS));
    /* Supported options: send, receive, receive-large, send/receive timeout.
     * No unsupported complex-message ownership or trailer policy is hidden. */
    if (!(options&(D_MACH_SEND_MSG|D_MACH_RCV_MSG)) ||
        !ipc_receive_options(options&~send_options) ||
        (r[6] && !(options&D_MACH_RCV_SYNC_WAIT) && !priority_argument) ||
        ((options&D_MACH_RCV_SYNC_WAIT) && !(options&D_MACH_RCV_MSG))) {
        static unsigned int rejected;
        if (__atomic_fetch_add(&rejected,1,__ATOMIC_RELAXED)<32) {
            const char label[]="DARWIN_MACH_OPTIONS_REJECTED argument-index/pc/value follows\n";
            RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
            for (unsigned int i=0;i<7;i++) trace_call(i,pc,r[i]);
        }
        return D_MACH_SEND_INVALID_OPTIONS;
    }
    if (options&D_MACH_SEND_MSG) {
        unsigned char request[IPC_BYTES];
        if (r[2]<sizeof(struct ipc_header) || r[2]>sizeof(request) ||
            guest_copy(request,r[0],r[2],0)) {
            static unsigned int rejected;
            if (__atomic_fetch_add(&rejected,1,__ATOMIC_RELAXED)<16) {
                const char label[]="DARWIN_MACH_SEND_DATA_REJECTED argument-index/pc/value follows\n";
                RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
                for (unsigned int i=0;i<7;i++) trace_call(i,pc,r[i]);
            }
            return r[2]>sizeof(request) ? D_MACH_SEND_TOO_LARGE : D_MACH_SEND_INVALID_DATA;
        }
        struct ipc_header *header=(void *)request;
        const char label[]="DARWIN_MACH_REQUEST word-index/pc/value follows\n";
        RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
        for (unsigned int i=0;i<r[2]/4 && i<16;i++) {
            uint32_t word; __builtin_memcpy(&word,request+i*4,4);
            trace_call(i,pc,word);
        }
        int clock=header->remote==SYSTEM_CLOCK_PORT ? 0 :
                  header->remote==CALENDAR_CLOCK_PORT ? 1 : -1;
        int task=header->remote==CURRENT_TASK_PORT;
        int display=native_display_port && header->remote==native_display_port;
        int render=native_render_port && header->remote==native_render_port;
        if (header->remote!=CURRENT_HOST_PORT && clock<0 && !task && !display && !render) {
            struct ipc_endpoint *destination=ipc_find(header->remote);
            if (!destination || destination->kind || !__atomic_load_n(&destination->send_refs,__ATOMIC_ACQUIRE)) return D_MACH_SEND_INVALID_DEST;
            /* First inline process-local transport. No port descriptors,
             * voucher or reply-right transfer is silently discarded. */
            if ((header->bits!=D_MACH_TYPE_COPY_SEND && header->bits!=D_MACH_TYPE_MOVE_SEND) ||
                header->local || header->voucher)
                return D_MACH_SEND_INVALID_HEADER;
            int move=header->bits==D_MACH_TYPE_MOVE_SEND;
            if (move && !ipc_drop_send(destination)) return D_MACH_SEND_INVALID_DEST;
            header->size=r[2]; header->local=header->remote; header->remote=0;
            header->bits=D_MACH_TYPE_MOVE_SEND<<D_MACH_MSG_LOCAL_BITS_SHIFT;
            if (ipc_enqueue_wait(destination,request,r[2],options,r[5])!=(long)r[2]) {
                if (move) __atomic_fetch_add(&destination->send_refs,1,__ATOMIC_RELEASE);
                return D_MACH_SEND_TIMED_OUT;
            }
            goto receive_message;
        }
        if ((!task && !display && !render && clock<0 && !host_send_refs) || (clock>=0 && !clock_send_refs[clock]) ||
            ((display || render) && !__atomic_load_n(&ipc_find(header->remote)->send_refs,__ATOMIC_ACQUIRE))) return D_MACH_SEND_INVALID_DEST;
        struct ipc_endpoint *reply=ipc_find(header->local);
        if (!reply || reply->kind) return D_MACH_SEND_INVALID_REPLY;
        if (header->bits!=(D_MACH_TYPE_COPY_SEND|(D_MACH_TYPE_MAKE_SEND_ONCE<<D_MACH_MSG_LOCAL_BITS_SHIFT)) || header->voucher ||
            header->id>INT32_MAX-D_MIG_REPLY_ID_OFFSET) return D_MACH_SEND_INVALID_HEADER;
        /* MIG host_info: header, NDR, flavor, output count. Unknown routines
         * get a genuine MIG error reply rather than a successful empty body. */
        uint32_t response[IPC_BYTES/4]; unsigned long response_ool=0;
        for (volatile uint32_t *word=response;word<response+IPC_BYTES/4;word++) *word=0;
        struct ipc_header *out=(void *)response;
        out->bits=D_MACH_TYPE_MOVE_SEND_ONCE<<D_MACH_MSG_LOCAL_BITS_SHIFT; out->size=36; out->local=reply->name;
        out->id=header->id+D_MIG_REPLY_ID_OFFSET;
        response[7]=1; /* NDR little-endian integer representation */
        response[8]=(uint32_t)D_MIG_BAD_ID; /* MIG_BAD_ID */
        response_ool=ipc_display_response(render,display,request,r[2],response,pc,1);
        if (!task && !display && !render && clock<0 && header->id==D_MIG_HOST_INFO && r[2]==40) {
            uint32_t flavor,count; __builtin_memcpy(&flavor,request+32,4);
            __builtin_memcpy(&count,request+36,4);
            response[8]=D_KERN_INVALID_ARGUMENT; /* KERN_INVALID_ARGUMENT */
            if (flavor==D_HOST_PRIORITY_INFO && count>=D_HOST_PRIORITY_INFO_COUNT) {
                /* Declared Darwin priority scale (XNU host_priority_info).
                 * These are ABI policy bounds, not a claim of implemented
                 * realtime scheduling or successful priority changes. */
                const uint32_t policy[8]={80,80,64,31,0,0,0,79};
                response[8]=0; response[9]=8; out->size=72;
                for (unsigned int i=0;i<8;i++) response[10+i]=policy[i];
            }
        }
        if (!task && !display && !render && clock<0 && header->id==D_MIG_HOST_GET_CLOCK && r[2]==36) {
            uint32_t clock_id; __builtin_memcpy(&clock_id,request+32,4);
            response[8]=D_KERN_INVALID_ARGUMENT; /* KERN_INVALID_ARGUMENT */
            if (clock_id<=D_CLOCK_CALENDAR) {
                /* MIG complex reply: body + one transferred send right. */
                out->bits|=D_MACH_MSG_COMPLEX; out->size=40;
                response[6]=1; response[7]=clock_id ? CALENDAR_CLOCK_PORT : SYSTEM_CLOCK_PORT;
                response[8]=0; response[9]=D_MACH_TYPE_MOVE_SEND<<D_MACH_DESCRIPTOR_DISPOSITION_SHIFT;
                clock_send_refs[clock_id]++;
            }
        }
        if (task && header->id==D_MIG_PORT_TYPE && r[2]==36) {
            uint32_t name,type=0; __builtin_memcpy(&name,request+32,4);
            struct ipc_endpoint *endpoint=ipc_find(name);
            response[8]=D_KERN_INVALID_NAME;
            if (endpoint) {
                if (endpoint->kind==3) {
                    if (__atomic_load_n(&endpoint->dead_refs,__ATOMIC_ACQUIRE)) type=D_PORT_TYPE_DEAD_NAME;
                } else if (endpoint->kind==2) type=D_PORT_TYPE_PORT_SET;
                else {
                    if (!endpoint->kind) type=D_PORT_TYPE_RECEIVE;
                    if (__atomic_load_n(&endpoint->send_refs,__ATOMIC_ACQUIRE)) type|=D_PORT_TYPE_SEND;
                }
            } else if (name==CURRENT_TASK_PORT ||
                       (name==CURRENT_HOST_PORT && host_send_refs) ||
                       (name==SYSTEM_CLOCK_PORT && clock_send_refs[0]) ||
                       (name==CALENDAR_CLOCK_PORT && clock_send_refs[1])) type=D_PORT_TYPE_SEND;
            else if ((name>>D_PORT_NAME_INDEX_SHIFT)>=D_THREAD_PORT_INDEX_BASE)
                response[8]=D_KERN_NOT_SUPPORTED; /* thread rights use another namespace */
            if (type) { response[8]=D_KERN_SUCCESS; response[9]=type; out->size=40; }
            const char label[]="DARWIN_PORT_TYPE name/type/status follows\n";
            RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
            trace_call(0,pc,name); trace_call(1,pc,type); trace_call(2,pc,response[8]);
        }
        if (task && header->id==D_MIG_TASK_INFO && r[2]==40) {
            uint32_t flavor,count;
            __builtin_memcpy(&flavor,request+32,4); __builtin_memcpy(&count,request+36,4);
            response[8]=D_KERN_INVALID_ARGUMENT;
            if (flavor==D_TASK_AUDIT_TOKEN && count>=D_AUDIT_TOKEN_WORDS) {
                struct darwin_audit_token token;
                long error=native_current_audit_token(&token);
                response[8]=error ? D_KERN_FAILURE : D_KERN_SUCCESS;
                if (!error) {
                    response[9]=D_AUDIT_TOKEN_WORDS;
                    __builtin_memcpy(response+10,&token,sizeof(token));
                    /* Wire size is Head + NDR + return/count + audit token,
                     * independent of the shared reply buffer's capacity. */
                    out->size=sizeof(struct ipc_header)+4*sizeof(uint32_t)+sizeof(token);
                }
            }
        }
        if (task && header->id==D_MIG_TASK_GET_SPECIAL_PORT && r[2]==36) {
            uint32_t which; __builtin_memcpy(&which,request+32,4);
            response[8]=D_KERN_INVALID_ARGUMENT;
            if (which==D_TASK_BOOTSTRAP_PORT || which==D_TASK_DEBUG_CONTROL_PORT) {
                uint32_t port=which==D_TASK_BOOTSTRAP_PORT ? native_bootstrap_port : 0;
                if (port) __atomic_fetch_add(&ipc_find(port)->send_refs,1,__ATOMIC_ACQ_REL);
                out->bits|=D_MACH_MSG_COMPLEX; out->size=40;
                response[6]=1; response[7]=port; response[8]=0; response[9]=D_MACH_TYPE_MOVE_SEND<<D_MACH_DESCRIPTOR_DISPOSITION_SHIFT;
            }
        }
        if (task && header->id==D_MIG_SEMAPHORE_CREATE && r[2]==40) {
            uint32_t policy,value; __builtin_memcpy(&policy,request+32,4);
            __builtin_memcpy(&value,request+36,4);
            response[8]=D_KERN_INVALID_ARGUMENT;
            if (!policy && value<=INT32_MAX) {
                uint32_t semaphore=ipc_semaphore(value);
                response[8]=D_KERN_RESOURCE_SHORTAGE; /* KERN_RESOURCE_SHORTAGE */
                if (semaphore) {
                    out->bits|=D_MACH_MSG_COMPLEX; out->size=40;
                    response[6]=1; response[7]=semaphore;
                    response[8]=0; response[9]=D_MACH_TYPE_MOVE_SEND<<D_MACH_DESCRIPTOR_DISPOSITION_SHIFT;
                }
            }
        }
        if (clock>=0 && header->id==D_MIG_CLOCK_GET_TIME && r[2]==24) {
            struct timespec now;
            long status=RAW(SYS_clock_gettime,clock ? CLOCK_REALTIME : CLOCK_MONOTONIC,&now,0,0,0,0);
            response[8]=status ? D_KERN_FAILURE : D_KERN_SUCCESS; /* real clock, otherwise KERN_FAILURE */
            if (!status) { out->size=44; response[9]=now.tv_sec; response[10]=now.tv_nsec; }
        }
        long sent=ipc_enqueue_wait(reply,response,out->size,options,r[5]);
        if (sent!=(long)out->size) {
            if (response_ool) RAW(SYS_munmap,response_ool,PAGE16,0,0,0,0);
            return D_MACH_SEND_TIMED_OUT;
        }
    }
receive_message:
    if (options&D_MACH_RCV_MSG) {
        struct ipc_endpoint *endpoint=ipc_find(r[4]);
        if (!endpoint || (endpoint->kind!=0 && endpoint->kind!=2)) return D_MACH_RCV_INVALID_NAME;
        /* XNU excludes combined sends from the special-reply linkage path.
         * Send override/propagation/no-importance flags select scheduling
         * metadata, not message delivery. This standalone task uses ordinary
         * Linux scheduling without Apple importance/QoS donation. r[6] may
         * therefore be a priority value rather than a notification port. */
        if ((options&D_MACH_RCV_SYNC_WAIT) && !(options&D_MACH_SEND_MSG)) {
            if (endpoint->kind || endpoint->special_tid!=RAW(SYS_gettid,0,0,0,0,0,0) ||
                !r[6] || (uint32_t)r[6]==UINT32_MAX) return D_MACH_RCV_INVALID_NOTIFY;
            struct ipc_endpoint *destination=ipc_find(r[6]);
            /* XNU permits a vanished destination: it must not fail the
             * receive. Track a live linkage while waiting and clear it on
             * completion. Our ordinary Linux scheduling policy performs no
             * cross-task priority donation. The queue wait itself is real. */
            uint32_t linked=destination && __atomic_load_n(&destination->send_refs,__ATOMIC_ACQUIRE) ? destination->name : 0;
            __atomic_store_n(&endpoint->sync_destination,linked,__ATOMIC_RELEASE);
        }
        struct timespec now; uint64_t deadline=0;
        if (options&D_MACH_RCV_TIMEOUT) {
            if (RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0)) return ipc_receive_complete(endpoint,D_MACH_RCV_INTERRUPTED);
            deadline=(uint64_t)now.tv_sec*1000000000+now.tv_nsec+(uint64_t)(uint32_t)r[5]*1000000;
        }
        for (;;) {
            struct ipc_endpoint *member=endpoint;
            if (endpoint->kind==2) {
                struct epoll_event event;
                long count=RAW(SYS_epoll_pwait,endpoint->read_fd,&event,1,0,0,sizeof(uint64_t));
                if (count<0) return ipc_receive_complete(endpoint,D_MACH_RCV_INTERRUPTED);
                member=count ? ipc_find((uint32_t)event.data.u64) : NULL;
                if (member && member->kind) return ipc_receive_complete(endpoint,D_MACH_RCV_INVALID_NAME);
            }
            unsigned long message_size=0,total_size=0;
            unsigned long status=D_MACH_RCV_TIMED_OUT;
            if (member) {
                ipc_queue_lock(member);
                status=ipc_receive_locked(member,r[0],r[3],
                    options&~send_options,&message_size,&total_size);
            }
            if (status==D_MACH_RCV_TOO_LARGE && (options&D_MACH_RCV_LARGE)) {
                uint32_t size=message_size,identity=member->name;
                if (guest_copy(&size,r[0]+4,4,1) ||
                    ((options&D_MACH_RCV_LARGE_IDENTITY) && guest_copy(&identity,r[0]+12,4,1))) status=D_MACH_RCV_INVALID_DATA;
            }
            if (member) ipc_queue_unlock(member);
            if (status!=D_MACH_RCV_TIMED_OUT) return ipc_receive_complete(endpoint,status);
            struct timespec remaining,*timeout=NULL;
            if (options&D_MACH_RCV_TIMEOUT) {
                if (RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0)) return ipc_receive_complete(endpoint,D_MACH_RCV_INTERRUPTED);
                uint64_t time=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
                if (time>=deadline) return ipc_receive_complete(endpoint,D_MACH_RCV_TIMED_OUT);
                uint64_t duration=deadline-time;
                remaining=(struct timespec){duration/1000000000,duration%1000000000}; timeout=&remaining;
            }
            native_reply_wait(endpoint->name);
            struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
            long ready=RAW(SYS_ppoll,&descriptor,1,timeout,0,0,0);
            if (!ready) return ipc_receive_complete(endpoint,D_MACH_RCV_TIMED_OUT);
            if (ready<0) return ipc_receive_complete(endpoint,D_MACH_RCV_INTERRUPTED);
        }
    }
    return 0;
}

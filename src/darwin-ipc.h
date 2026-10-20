/* Initial process-local Mach service transport, with real kernel queues.
 * Implements observed Host/Clock queries and semaphore creation.
 * Queued replies remain observable by later receive calls; send success never
 * stands in for a fabricated receive. General descriptor/port transfer, port
 * sets, notifications and cross-process namespace semantics are not provided. */
#pragma once
#include <sys/socket.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <linux/futex.h>
static unsigned long host_send_refs,clock_send_refs[2];
#define IPC_ENDPOINTS 64
#define IPC_BYTES 1024
struct ipc_endpoint {
    uint32_t name,generation; int used,kind,read_fd,write_fd;
    unsigned long send_refs; uint64_t context; uint32_t options;
    int queue_mutex; uint32_t queue_limit,queued,sequence,space_sequence;
};
static struct ipc_endpoint ipc_endpoints[IPC_ENDPOINTS];
struct ipc_header { uint32_t bits,size,remote,local,voucher; int32_t id; };
struct ipc_packet { struct darwin_audit_token sender; unsigned char message[IPC_BYTES]; };
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
static long ipc_enqueue(struct ipc_endpoint *endpoint,const void *message,unsigned long size)
{
    if (size>IPC_BYTES) return -EMSGSIZE;
    struct ipc_packet packet;
    long error=native_current_audit_token(&packet.sender);
    if (error) return error;
    __builtin_memcpy(packet.message,message,size);
    ipc_queue_lock(endpoint);
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
    long bytes=RAW(SYS_recvfrom,endpoint->read_fd,packet,sizeof(*packet),MSG_PEEK|MSG_DONTWAIT,0,0);
    if (bytes<0) return bytes;
    if (bytes<(long)(sizeof(packet->sender)+sizeof(struct ipc_header))) return -EIO;
    return bytes-sizeof(packet->sender);
}
static long ipc_consume(struct ipc_endpoint *endpoint,struct ipc_packet *packet,long size)
{
    long bytes=RAW(SYS_recvfrom,endpoint->read_fd,packet,sizeof(*packet),MSG_DONTWAIT,0,0);
    if (bytes!=size+(long)sizeof(packet->sender)) return bytes<0 ? bytes : -EIO;
    endpoint->queued--; endpoint->sequence++;
    __atomic_fetch_add(&endpoint->space_sequence,1,__ATOMIC_RELEASE);
    RAW(SYS_futex,&endpoint->space_sequence,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    return 0;
}
static int ipc_receive_options(unsigned long options)
{
    unsigned long allowed=D_MACH_RCV_MSG|D_MACH_RCV_LARGE|D_MACH_RCV_LARGE_IDENTITY|
        D_MACH_RCV_VOUCHER|D_MACH_RCV_TRAILER_MASK|D_MACH_RCV_TIMEOUT;
    return !(options&~allowed) &&
        ((options&D_MACH_RCV_TRAILER_MASK)>>D_MACH_RCV_TRAILER_SHIFT)<=D_MACH_RCV_TRAILER_MAX;
}
static unsigned long ipc_receive_locked(struct ipc_endpoint *endpoint,uint64_t address,
                                        unsigned long capacity,unsigned long options,
                                        unsigned long *message_size,unsigned long *total_size)
{
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
    __builtin_memcpy(output+aligned,&trailer,trailer_size);
    if (guest_copy(output,address,*total_size,1)) return D_MACH_RCV_INVALID_DATA;
    return ipc_consume(endpoint,&packet,size) ? D_MACH_RCV_INTERRUPTED : D_KERN_SUCCESS;
}
static struct ipc_endpoint *ipc_find(uint32_t name)
{
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++)
        if (__atomic_load_n(&ipc_endpoints[i].used,__ATOMIC_ACQUIRE)==2 && ipc_endpoints[i].name==name) return ipc_endpoints+i;
    return NULL;
}
static uint32_t ipc_reply_port(void)
{
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++) {
        struct ipc_endpoint *endpoint=ipc_endpoints+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&endpoint->used,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        int pair[2];
        if (RAW(SYS_socketpair,AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair,0,0)) {
            __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE); return 0;
        }
        endpoint->generation=endpoint->generation ?
            (endpoint->generation+D_PORT_GENERATION_STEP)&D_PORT_GENERATION_MASK : D_PORT_GENERATION_TAG;
        endpoint->name=((i+D_REPLY_PORT_INDEX_BASE)<<D_PORT_NAME_INDEX_SHIFT)|endpoint->generation;
        endpoint->read_fd=pair[0]; endpoint->write_fd=pair[1]; endpoint->kind=0;
        endpoint->send_refs=0; endpoint->context=0; endpoint->options=0;
        endpoint->queue_mutex=0; endpoint->queued=0; endpoint->sequence=0;
        endpoint->space_sequence=0; endpoint->queue_limit=D_PORT_QLIMIT_DEFAULT;
        __atomic_store_n(&endpoint->used,2,__ATOMIC_RELEASE);
        return endpoint->name;
    }
    return 0;
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
                       D_MPO_QLIMIT|D_MPO_IMPORTANCE_RECEIVER) ||
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
    /* IMPORTANCE_RECEIVER records eligibility under the standalone ordinary
     * Linux scheduling policy. There is no App Nap or cross-task importance
     * propagation here. Message delivery/queue limits remain real effects. */
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
static unsigned long ipc_message(const unsigned long long *r,unsigned long pc)
{
    unsigned long options=r[1];
    /* Supported options: send, receive, receive-large, send/receive timeout.
     * No unsupported complex-message ownership or trailer policy is hidden. */
    if (!(options&(D_MACH_SEND_MSG|D_MACH_RCV_MSG)) ||
        !ipc_receive_options(options&~(unsigned long)(D_MACH_SEND_MSG|D_MACH_SEND_TIMEOUT)) || r[6]) return D_MACH_SEND_INVALID_OPTIONS;
    if (options&D_MACH_SEND_MSG) {
        unsigned char request[IPC_BYTES];
        if (r[2]<sizeof(struct ipc_header) || r[2]>sizeof(request) ||
            guest_copy(request,r[0],r[2],0)) return D_MACH_SEND_INVALID_DATA;
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
        if (header->remote!=CURRENT_HOST_PORT && clock<0 && !task) {
            struct ipc_endpoint *destination=ipc_find(header->remote);
            if (!destination || destination->kind || !destination->send_refs) return D_MACH_SEND_INVALID_DEST;
            /* First inline process-local transport. No port descriptors,
             * voucher or reply-right transfer is silently discarded. */
            if (header->bits!=D_MACH_TYPE_COPY_SEND || header->local || header->voucher)
                return D_MACH_SEND_INVALID_HEADER;
            header->size=r[2]; header->local=header->remote; header->remote=0;
            header->bits=D_MACH_TYPE_MOVE_SEND<<D_MACH_MSG_LOCAL_BITS_SHIFT;
            if (ipc_enqueue_wait(destination,request,r[2],options,r[5])!=(long)r[2]) return D_MACH_SEND_TIMED_OUT;
            goto receive_message;
        }
        if ((!task && clock<0 && !host_send_refs) || (clock>=0 && !clock_send_refs[clock])) return D_MACH_SEND_INVALID_DEST;
        struct ipc_endpoint *reply=ipc_find(header->local);
        if (!reply || reply->kind) return D_MACH_SEND_INVALID_REPLY;
        if (header->bits!=(D_MACH_TYPE_COPY_SEND|(D_MACH_TYPE_MAKE_SEND_ONCE<<D_MACH_MSG_LOCAL_BITS_SHIFT)) || header->voucher ||
            header->id>INT32_MAX-D_MIG_REPLY_ID_OFFSET) return D_MACH_SEND_INVALID_HEADER;
        /* MIG host_info: header, NDR, flavor, output count. Unknown routines
         * get a genuine MIG error reply rather than a successful empty body. */
        uint32_t response[18];
        for (volatile uint32_t *word=response;word<response+18;word++) *word=0;
        struct ipc_header *out=(void *)response;
        out->bits=D_MACH_TYPE_MOVE_SEND_ONCE<<D_MACH_MSG_LOCAL_BITS_SHIFT; out->size=36; out->local=reply->name;
        out->id=header->id+D_MIG_REPLY_ID_OFFSET;
        response[7]=1; /* NDR little-endian integer representation */
        response[8]=(uint32_t)D_MIG_BAD_ID; /* MIG_BAD_ID */
        if (!task && clock<0 && header->id==D_MIG_HOST_INFO && r[2]==40) {
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
        if (!task && clock<0 && header->id==D_MIG_HOST_GET_CLOCK && r[2]==36) {
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
                    out->size=sizeof(response);
                }
            }
        }
        if (task && header->id==D_MIG_TASK_GET_SPECIAL_PORT && r[2]==36) {
            uint32_t which; __builtin_memcpy(&which,request+32,4);
            response[8]=D_KERN_INVALID_ARGUMENT;
            if (which==D_TASK_BOOTSTRAP_PORT || which==D_TASK_DEBUG_CONTROL_PORT) {
                /* No Bootstrap or debug-control server is registered for this
                 * task. Return the actual empty slot, as XNU does. Real
                 * libdispatch explicitly accepts a NULL debug channel. */
                out->bits|=D_MACH_MSG_COMPLEX; out->size=40;
                response[6]=1; response[7]=0; response[8]=0; response[9]=D_MACH_TYPE_MOVE_SEND<<D_MACH_DESCRIPTOR_DISPOSITION_SHIFT;
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
        if (sent!=(long)out->size) return D_MACH_SEND_TIMED_OUT; /* MACH_SEND_TIMED_OUT */
    }
receive_message:
    if (options&D_MACH_RCV_MSG) {
        struct ipc_endpoint *endpoint=ipc_find(r[4]);
        if (!endpoint || endpoint->kind) return D_MACH_RCV_INVALID_NAME;
        if (options&D_MACH_RCV_TIMEOUT) {
            struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
            struct timespec timeout={r[5]/1000,(r[5]%1000)*1000000};
            long ready=RAW(SYS_ppoll,&descriptor,1,&timeout,0,0,0);
            if (!ready) return D_MACH_RCV_TIMED_OUT;
            if (ready<0) return D_MACH_RCV_INTERRUPTED; /* MACH_RCV_INTERRUPTED */
        }
        struct ipc_packet packet;
        long length=RAW(SYS_recvfrom,endpoint->read_fd,&packet,sizeof(packet),MSG_PEEK,0,0);
        if (length<0) return D_MACH_RCV_INTERRUPTED;
        /* Another receiver may consume the packet after the blocking peek.
         * Recheck under the same lock used for enqueue/count updates. Never
         * block a sender while holding this lock. */
        ipc_queue_lock(endpoint);
        unsigned long message_size=0,total_size=0;
        unsigned long status=ipc_receive_locked(endpoint,r[0],r[3],
            options&~(unsigned long)(D_MACH_SEND_MSG|D_MACH_SEND_TIMEOUT),&message_size,&total_size);
        if (status==D_MACH_RCV_TOO_LARGE && (options&D_MACH_RCV_LARGE)) {
            uint32_t size=message_size,identity=endpoint->name;
            if (guest_copy(&size,r[0]+4,4,1) ||
                ((options&D_MACH_RCV_LARGE_IDENTITY) && guest_copy(&identity,r[0]+12,4,1))) status=D_MACH_RCV_INVALID_DATA;
        }
        ipc_queue_unlock(endpoint);
        return status;
    }
    return 0;
}

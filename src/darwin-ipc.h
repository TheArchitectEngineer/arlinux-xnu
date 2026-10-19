/* Initial process-local Mach service transport, with real kernel queues.
 * Implements observed Host/Clock queries and semaphore creation.
 * Queued replies remain observable by later receive calls; send success never
 * stands in for a fabricated receive. General descriptor/port transfer, port
 * sets, notifications and cross-process namespace semantics are not provided. */
#pragma once
#include <sys/socket.h>
#include <poll.h>
#include <sys/eventfd.h>
static unsigned long host_send_refs,clock_send_refs[2];
#define IPC_ENDPOINTS 64
#define IPC_BYTES 1024
struct ipc_endpoint {
    uint32_t name,generation; int used,kind,read_fd,write_fd;
    unsigned long send_refs; uint64_t context; uint32_t options;
};
static struct ipc_endpoint ipc_endpoints[IPC_ENDPOINTS];
struct ipc_header { uint32_t bits,size,remote,local,voucher; int32_t id; };
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
        endpoint->generation=(endpoint->generation+1)&D_PORT_GENERATION_MASK;
        if (!endpoint->generation) endpoint->generation=1;
        endpoint->name=((i+D_REPLY_PORT_INDEX_BASE)<<D_PORT_NAME_INDEX_SHIFT)|endpoint->generation;
        endpoint->read_fd=pair[0]; endpoint->write_fd=pair[1]; endpoint->kind=0;
        endpoint->send_refs=0; endpoint->context=0; endpoint->options=0;
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
    if (options.flags&~(D_MPO_CONTEXT_AS_GUARD|D_MPO_INSERT_SEND_RIGHT|D_MPO_STRICT) ||
        options.reserved[0] || options.reserved[1]) return D_KERN_NOT_SUPPORTED;
    if ((options.flags&D_MPO_STRICT) && !(options.flags&D_MPO_CONTEXT_AS_GUARD))
        return D_KERN_INVALID_ARGUMENT;
    uint32_t name=ipc_reply_port();
    if (!name) return D_KERN_RESOURCE_SHORTAGE;
    struct ipc_endpoint *endpoint=ipc_find(name);
    endpoint->context=r[2]; endpoint->options=options.flags;
    endpoint->send_refs=(options.flags&D_MPO_INSERT_SEND_RIGHT) ? 1 : 0;
    /* The name owns a genuine Linux message queue and receive right. Generic
     * sends are still rejected until their ownership rules are implemented;
     * construction does not impersonate a remote XPC service. */
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
        endpoint->generation=(endpoint->generation+1)&D_PORT_GENERATION_MASK;
        if (!endpoint->generation) endpoint->generation=1;
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
    if (!(options&(D_MACH_SEND_MSG|D_MACH_RCV_MSG)) || (options&~(unsigned long)(D_MACH_SEND_MSG|D_MACH_RCV_MSG|D_MACH_RCV_LARGE|D_MACH_SEND_TIMEOUT|D_MACH_RCV_TIMEOUT)) || r[6]) return D_MACH_SEND_INVALID_OPTIONS;
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
        if (header->remote!=CURRENT_HOST_PORT && clock<0 && !task) return D_MACH_SEND_INVALID_DEST;
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
            if (which==D_TASK_BOOTSTRAP_PORT) {
                /* This task has no registered Bootstrap service. Querying the
                 * empty slot succeeds and transfers MACH_PORT_NULL; it does
                 * not invent an XPC/launchd server or claim any service works. */
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
        long sent=RAW(SYS_sendto,reply->write_fd,response,out->size,MSG_DONTWAIT|MSG_NOSIGNAL,0,0);
        if (sent!=(long)out->size) return D_MACH_SEND_TIMED_OUT; /* MACH_SEND_TIMED_OUT */
    }
    if (options&D_MACH_RCV_MSG) {
        struct ipc_endpoint *endpoint=ipc_find(r[4]);
        if (!endpoint || endpoint->kind) return D_MACH_RCV_INVALID_NAME;
        unsigned char response[IPC_BYTES+8];
        if (options&D_MACH_RCV_TIMEOUT) {
            struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
            struct timespec timeout={r[5]/1000,(r[5]%1000)*1000000};
            long ready=RAW(SYS_ppoll,&descriptor,1,&timeout,0,0,0);
            if (!ready) return D_MACH_RCV_TIMED_OUT;
            if (ready<0) return D_MACH_RCV_INTERRUPTED; /* MACH_RCV_INTERRUPTED */
        }
        long length=RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,MSG_PEEK,0,0);
        if (length<0) return D_MACH_RCV_INTERRUPTED;
        if (r[3]<(unsigned long)length) {
            if (options&D_MACH_RCV_LARGE) {
                uint32_t size=length;
                if (guest_copy(&size,r[0]+4,4,1)) return D_MACH_RCV_INVALID_DATA;
            } else RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,0,0,0);
            return D_MACH_RCV_TOO_LARGE;
        }
        /* Probe destination before consuming its queued message. */
        if (guest_copy(response,r[0],length,1)) return D_MACH_RCV_INVALID_DATA;
        long consumed=RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,0,0,0);
        if (consumed!=length) return D_MACH_RCV_INTERRUPTED;
        uint32_t trailer[2]={0,8};
        if (r[3]>=(unsigned long)length+sizeof(trailer) &&
            guest_copy(trailer,r[0]+length,sizeof(trailer),1)) return D_MACH_RCV_INVALID_DATA;
    }
    return 0;
}

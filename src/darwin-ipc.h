/* Initial process-local Mach service transport, with real kernel queues.
 * Implements observed Host/Clock queries and semaphore creation.
 * Queued replies remain observable by later receive calls; send success never
 * stands in for a fabricated receive. General descriptor/port transfer, port
 * sets, notifications and cross-process namespace semantics are not provided. */
#pragma once
#include <sys/socket.h>
#include <poll.h>
#include <sys/eventfd.h>
#define CURRENT_HOST_PORT 0x203U
#define SYSTEM_CLOCK_PORT 0x10001U
#define CALENDAR_CLOCK_PORT 0x10101U
static unsigned long host_send_refs,clock_send_refs[2];
#define IPC_ENDPOINTS 64
#define IPC_BYTES 1024
struct ipc_endpoint { uint32_t name,generation; int used,kind,read_fd,write_fd; unsigned long send_refs; };
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
        endpoint->generation=(endpoint->generation+1)&0xff;
        if (!endpoint->generation) endpoint->generation=1;
        endpoint->name=((i+3)<<8)|endpoint->generation;
        endpoint->read_fd=pair[0]; endpoint->write_fd=pair[1]; endpoint->kind=0; __atomic_store_n(&endpoint->used,2,__ATOMIC_RELEASE);
        return endpoint->name;
    }
    return 0;
}
static uint32_t ipc_semaphore(unsigned int initial)
{
    for (unsigned int i=0;i<IPC_ENDPOINTS;i++) {
        struct ipc_endpoint *endpoint=ipc_endpoints+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&endpoint->used,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        long fd=RAW(SYS_eventfd2,initial,EFD_SEMAPHORE|EFD_NONBLOCK|EFD_CLOEXEC,0,0,0,0);
        if (fd<0) { __atomic_store_n(&endpoint->used,0,__ATOMIC_RELEASE); return 0; }
        endpoint->generation=(endpoint->generation+1)&0xff;
        if (!endpoint->generation) endpoint->generation=1;
        endpoint->name=((i+3)<<8)|endpoint->generation;
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
    if (!endpoint || endpoint->kind!=1) return -22;
    uint64_t token;
    long result=RAW(SYS_read,endpoint->read_fd,&token,8,0,0,0);
    if (result==8) return 0;
    if (result!=-11) return result<0 ? result : -5;
    struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
    long ready=RAW(SYS_ppoll,&descriptor,1,timeout,0,0,0);
    if (!ready) return -110;
    if (ready<0) return ready;
    result=RAW(SYS_read,endpoint->read_fd,&token,8,0,0,0);
    return result==8 ? 0 : (result<0 ? result : -5);
}
static unsigned long ipc_message(const unsigned long long *r,unsigned long pc)
{
    const uint32_t send_invalid_data=0x10000002U,send_invalid_dest=0x10000003U;
    const uint32_t send_invalid_reply=0x10000009U,send_invalid_options=0x10000013U;
    const uint32_t receive_invalid_name=0x10004002U,receive_timeout=0x10004003U;
    const uint32_t receive_too_large=0x10004004U,receive_invalid_data=0x10004008U;
    unsigned long options=r[1];
    /* Supported options: send, receive, receive-large, send/receive timeout.
     * No unsupported complex-message ownership or trailer policy is hidden. */
    if (!(options&3) || (options&~0x117UL) || r[6]) return send_invalid_options;
    if (options&1) {
        unsigned char request[IPC_BYTES];
        if (r[2]<sizeof(struct ipc_header) || r[2]>sizeof(request) ||
            guest_copy(request,r[0],r[2],0)) return send_invalid_data;
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
        if (header->remote!=CURRENT_HOST_PORT && clock<0 && !task) return send_invalid_dest;
        if ((!task && clock<0 && !host_send_refs) || (clock>=0 && !clock_send_refs[clock])) return send_invalid_dest;
        struct ipc_endpoint *reply=ipc_find(header->local);
        if (!reply || reply->kind) return send_invalid_reply;
        if (header->bits!=0x1513 || header->voucher ||
            header->id>INT32_MAX-100) return 0x10000010U;
        /* MIG host_info: header, NDR, flavor, output count. Unknown routines
         * get a genuine MIG error reply rather than a successful empty body. */
        uint32_t response[18];
        for (volatile uint32_t *word=response;word<response+18;word++) *word=0;
        struct ipc_header *out=(void *)response;
        out->bits=18U<<8; out->size=36; out->local=reply->name;
        out->id=header->id+100;
        response[7]=1; /* NDR little-endian integer representation */
        response[8]=(uint32_t)-303; /* MIG_BAD_ID */
        if (!task && clock<0 && header->id==200 && r[2]==40) {
            uint32_t flavor,count; __builtin_memcpy(&flavor,request+32,4);
            __builtin_memcpy(&count,request+36,4);
            response[8]=4; /* KERN_INVALID_ARGUMENT */
            if (flavor==5 && count>=8) {
                /* Declared Darwin priority scale (XNU host_priority_info).
                 * These are ABI policy bounds, not a claim of implemented
                 * realtime scheduling or successful priority changes. */
                const uint32_t policy[8]={80,80,64,31,0,0,0,79};
                response[8]=0; response[9]=8; out->size=72;
                for (unsigned int i=0;i<8;i++) response[10+i]=policy[i];
            }
        }
        if (!task && clock<0 && header->id==206 && r[2]==36) {
            uint32_t clock_id; __builtin_memcpy(&clock_id,request+32,4);
            response[8]=4; /* KERN_INVALID_ARGUMENT */
            if (clock_id<=1) {
                /* MIG complex reply: body + one transferred send right. */
                out->bits|=0x80000000U; out->size=40;
                response[6]=1; response[7]=clock_id ? CALENDAR_CLOCK_PORT : SYSTEM_CLOCK_PORT;
                response[8]=0; response[9]=17U<<16;
                clock_send_refs[clock_id]++;
            }
        }
        if (task && header->id==3409 && r[2]==36) {
            uint32_t which; __builtin_memcpy(&which,request+32,4);
            response[8]=4;
            if (which==4) {
                /* This task has no registered Bootstrap service. Querying the
                 * empty slot succeeds and transfers MACH_PORT_NULL; it does
                 * not invent an XPC/launchd server or claim any service works. */
                out->bits|=0x80000000U; out->size=40;
                response[6]=1; response[7]=0; response[8]=0; response[9]=17U<<16;
            }
        }
        if (task && header->id==3418 && r[2]==40) {
            uint32_t policy,value; __builtin_memcpy(&policy,request+32,4);
            __builtin_memcpy(&value,request+36,4);
            response[8]=4;
            if (!policy && value<=INT32_MAX) {
                uint32_t semaphore=ipc_semaphore(value);
                response[8]=3; /* KERN_RESOURCE_SHORTAGE */
                if (semaphore) {
                    out->bits|=0x80000000U; out->size=40;
                    response[6]=1; response[7]=semaphore;
                    response[8]=0; response[9]=17U<<16;
                }
            }
        }
        if (clock>=0 && header->id==1000 && r[2]==24) {
            struct timespec now;
            long status=RAW(SYS_clock_gettime,clock ? CLOCK_REALTIME : CLOCK_MONOTONIC,&now,0,0,0,0);
            response[8]=status ? 5 : 0; /* real clock, otherwise KERN_FAILURE */
            if (!status) { out->size=44; response[9]=now.tv_sec; response[10]=now.tv_nsec; }
        }
        long sent=RAW(SYS_sendto,reply->write_fd,response,out->size,MSG_DONTWAIT|MSG_NOSIGNAL,0,0);
        if (sent!=(long)out->size) return 0x10000004U; /* MACH_SEND_TIMED_OUT */
    }
    if (options&2) {
        struct ipc_endpoint *endpoint=ipc_find(r[4]);
        if (!endpoint || endpoint->kind) return receive_invalid_name;
        unsigned char response[IPC_BYTES+8];
        if (options&0x100) {
            struct pollfd descriptor={.fd=endpoint->read_fd,.events=POLLIN};
            struct timespec timeout={r[5]/1000,(r[5]%1000)*1000000};
            long ready=RAW(SYS_ppoll,&descriptor,1,&timeout,0,0,0);
            if (!ready) return receive_timeout;
            if (ready<0) return 0x10004005U; /* MACH_RCV_INTERRUPTED */
        }
        long length=RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,MSG_PEEK,0,0);
        if (length<0) return 0x10004005U;
        if (r[3]<(unsigned long)length) {
            if (options&4) {
                uint32_t size=length;
                if (guest_copy(&size,r[0]+4,4,1)) return receive_invalid_data;
            } else RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,0,0,0);
            return receive_too_large;
        }
        /* Probe destination before consuming its queued message. */
        if (guest_copy(response,r[0],length,1)) return receive_invalid_data;
        long consumed=RAW(SYS_recvfrom,endpoint->read_fd,response,IPC_BYTES,0,0,0);
        if (consumed!=length) return 0x10004005U;
        uint32_t trailer[2]={0,8};
        if (r[3]>=(unsigned long)length+sizeof(trailer) &&
            guest_copy(trailer,r[0]+length,sizeof(trailer),1)) return receive_invalid_data;
    }
    return 0;
}

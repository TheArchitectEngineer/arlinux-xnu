/* Synchronous Darling memory callbacks on a real RPC receive. Guest mappings
 * retain the native 16 KiB/low-address policy; unsupported file/fixed/executable
 * mappings return actual errors, never an address without storage. */
#pragma once
static unsigned int darling_memory_traces;
static int darling_memory_range(uint64_t address,uint64_t length)
{
    return length && length<=D_VM_MAX_ALLOCATION && !(address&(PAGE16-1)) &&
        address<D_GUEST_COMMPAGE && length<=D_GUEST_COMMPAGE-address;
}
static void darling_memory_reply(int fd,const dserver_s2c_call_t *call,long count)
{
    union {
        dserver_s2c_replyhdr_t header;
        dserver_s2c_reply_mmap_t mmap;
        dserver_s2c_reply_munmap_t munmap;
        dserver_s2c_reply_mprotect_t mprotect;
        dserver_s2c_reply_msync_t msync;
    } reply={.header={.call_number=dserver_callnum_s2c,
        .pid=RAW(SYS_getpid,0,0,0,0,0,0),.tid=RAW(SYS_gettid,0,0,0,0,0,0),
        .architecture=dserver_rpc_architecture_arm64,.s2c_number=call->mmap.header.s2c_number}};
    long result=-ENOTSUP; size_t size=0;
    switch (reply.header.s2c_number) {
    case dserver_s2c_msgnum_mmap:
        if (count!=(long)sizeof(call->mmap)) darling_abort("invalid S2C mmap packet");
        if (call->mmap.flags==(MAP_PRIVATE|MAP_ANONYMOUS) && call->mmap.fd==-1 &&
            !call->mmap.address && !call->mmap.offset)
            result=anonymous_aligned(call->mmap.length,call->mmap.protection,PAGE16-1);
        reply.mmap.address=(unsigned long)result>=(unsigned long)-4095 ? UINT64_MAX : (uint64_t)result;
        reply.mmap.errno_result=(unsigned long)result>=(unsigned long)-4095 ? -result : 0;
        size=sizeof(reply.mmap);
        break;
    case dserver_s2c_msgnum_munmap:
        if (count!=(long)sizeof(call->munmap)) darling_abort("invalid S2C munmap packet");
        result=darling_memory_range(call->munmap.address,call->munmap.length) ?
            RAW(SYS_munmap,call->munmap.address,(call->munmap.length+PAGE16-1)&~(PAGE16-1),0,0,0,0) : -EINVAL;
        reply.munmap.return_value=result<0 ? -1 : result;
        reply.munmap.errno_result=result<0 ? -result : 0;
        size=sizeof(reply.munmap);
        break;
    case dserver_s2c_msgnum_mprotect:
        if (count!=(long)sizeof(call->mprotect)) darling_abort("invalid S2C mprotect packet");
        if (darling_memory_range(call->mprotect.address,call->mprotect.length) &&
            !(call->mprotect.protection&~(PROT_READ|PROT_WRITE)))
            result=RAW(SYS_mprotect,call->mprotect.address,(call->mprotect.length+PAGE16-1)&~(PAGE16-1),
                       call->mprotect.protection,0,0,0);
        reply.mprotect.return_value=result<0 ? -1 : result;
        reply.mprotect.errno_result=result<0 ? -result : 0;
        size=sizeof(reply.mprotect);
        break;
    case dserver_s2c_msgnum_msync:
        if (count!=(long)sizeof(call->msync)) darling_abort("invalid S2C msync packet");
        result=darling_memory_range(call->msync.address,call->msync.size) ?
            RAW(SYS_msync,call->msync.address,call->msync.size,call->msync.sync_flags,0,0,0) : -EINVAL;
        reply.msync.return_value=result<0 ? -1 : result;
        reply.msync.errno_result=result<0 ? -result : 0;
        size=sizeof(reply.msync);
        break;
    default: darling_abort("unknown S2C memory opcode");
    }
    if (__atomic_fetch_add(&darling_memory_traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARLING_S2C_MEMORY opcode/address/bytes/protection/flags/result follows\n";
        RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,reply.header.s2c_number); trace_call(1,0,call->mmap.address);
        trace_call(2,0,call->mmap.length);
        trace_call(3,0,reply.header.s2c_number==dserver_s2c_msgnum_mmap ? call->mmap.protection : 0);
        trace_call(4,0,reply.header.s2c_number==dserver_s2c_msgnum_mmap ? call->mmap.flags : 0);
        trace_call(5,0,result);
    }
    struct iovec data={.iov_base=&reply,.iov_len=size};
    struct msghdr message={.msg_name=&darling_address,.msg_namelen=sizeof(darling_address),
                           .msg_iov=&data,.msg_iovlen=1};
    long sent;
    do { sent=RAW(SYS_sendmsg,fd,&message,MSG_NOSIGNAL,0,0,0); } while (sent==-EINTR);
    if (sent!=(long)size) darling_abort("send S2C memory reply");
}

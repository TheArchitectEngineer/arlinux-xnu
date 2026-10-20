/* Real anonymous memory operations requested by the trusted Darling server.
 * Its vm_remap copies already-adapted process bytes. Do not rewrite them again:
 * the Darwin-to-Linux TLS instruction conversion is not idempotent. */
#pragma once
static unsigned int darling_memory_traces;
static int darling_memory_range(uint64_t address,uint64_t length)
{
    return length && length<=D_VM_MAX_ALLOCATION && !(address&(PAGE16-1)) &&
        address<D_GUEST_COMMPAGE && length<=D_GUEST_COMMPAGE-address;
}
enum { DARLING_PENDING_CODE_RANGES=64 };
struct darling_memory_updates {
    struct { uint64_t address,length; } code[DARLING_PENDING_CODE_RANGES];
    unsigned int count;
};
/* Prepare bookkeeping before changing mappings. Removing a subrange preserves
 * both remaining pieces, so later cache maintenance never touches unmapped or
 * inaccessible pages. Capacity failures leave the actual operation unexecuted. */
static int darling_memory_replace(struct darling_memory_updates *updates,
                                  uint64_t address,uint64_t length,int executable)
{
    struct darling_memory_updates next={0};
    uint64_t end=address+length;
    for (unsigned int i=0;i<updates->count;i++) {
        uint64_t start=updates->code[i].address,finish=start+updates->code[i].length;
        if (finish<=address || start>=end) {
            if (next.count==DARLING_PENDING_CODE_RANGES) return -ENOMEM;
            next.code[next.count++]=updates->code[i];
            continue;
        }
        if (start<address) {
            if (next.count==DARLING_PENDING_CODE_RANGES) return -ENOMEM;
            next.code[next.count].address=start;
            next.code[next.count++].length=address-start;
        }
        if (finish>end) {
            if (next.count==DARLING_PENDING_CODE_RANGES) return -ENOMEM;
            next.code[next.count].address=end;
            next.code[next.count++].length=finish-end;
        }
    }
    if (executable) {
        if (next.count==DARLING_PENDING_CODE_RANGES) return -ENOMEM;
        next.code[next.count].address=address;
        next.code[next.count++].length=length;
    }
    *updates=next;
    return 0;
}
static void darling_memory_complete(const struct darling_memory_updates *updates)
{
    /* The final RPC reply follows vm_map_copy_overwrite in the server. At this
     * point its process_vm_writev writes are complete and guest code may resume. */
    for (unsigned int i=0;i<updates->count;i++) {
        char *start=(char *)updates->code[i].address;
        __builtin___clear_cache(start,start+updates->code[i].length);
    }
    if (updates->count) trace_record("DARLING_CODE_COPY_COMPLETE ranges=0x",updates->count,0,0);
}
static long darling_memory_mmap(const dserver_s2c_call_t *call,
                                struct darling_memory_updates *updates)
{
    uint64_t address=call->mmap.address,length=call->mmap.length;
    int protection=call->mmap.protection,flags=call->mmap.flags;
    if (!length || length>D_VM_MAX_ALLOCATION ||
        (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC)) || call->mmap.fd!=-1 || call->mmap.offset ||
        (flags&~(MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|MAP_FIXED_NOREPLACE)) ||
        (flags&(MAP_PRIVATE|MAP_ANONYMOUS))!=(MAP_PRIVATE|MAP_ANONYMOUS) ||
        ((flags&MAP_FIXED) && (flags&MAP_FIXED_NOREPLACE))) return -ENOTSUP;
    length=(length+PAGE16-1)&~(PAGE16-1);
    int fixed=flags&(MAP_FIXED|MAP_FIXED_NOREPLACE);
    if (fixed && (address<PAGE16 || !darling_memory_range(address,length))) return -EINVAL;
    /* Unknown non-fixed hints must not escape the low guest address space. */
    if (!fixed && address) return -ENOTSUP;
    if (!fixed) {
        long allocated=anonymous_aligned(length,PROT_NONE,PAGE16-1);
        if ((unsigned long)allocated>=(unsigned long)-4095) return allocated;
        address=allocated;
    }
    struct darling_memory_updates next=*updates;
    long result=darling_memory_replace(&next,address,length,protection&PROT_EXEC);
    if (!result) result=RAW(SYS_mmap,address,length,protection,
                            fixed ? flags : MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if ((unsigned long)result>=(unsigned long)-4095) {
        if (!fixed) RAW(SYS_munmap,address,length,0,0,0,0);
        return result;
    }
    *updates=next;
    return result;
}
static void darling_memory_reply(int fd,const dserver_s2c_call_t *call,long count,
                                 struct darling_memory_updates *updates)
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
        result=darling_memory_mmap(call,updates);
        reply.mmap.address=(unsigned long)result>=(unsigned long)-4095 ? UINT64_MAX : (uint64_t)result;
        reply.mmap.errno_result=(unsigned long)result>=(unsigned long)-4095 ? -result : 0;
        size=sizeof(reply.mmap);
        break;
    case dserver_s2c_msgnum_munmap:
        if (count!=(long)sizeof(call->munmap)) darling_abort("invalid S2C munmap packet");
        result=-EINVAL;
        if (darling_memory_range(call->munmap.address,call->munmap.length)) {
            uint64_t length=(call->munmap.length+PAGE16-1)&~(PAGE16-1);
            struct darling_memory_updates next=*updates;
            result=darling_memory_replace(&next,call->munmap.address,length,0);
            if (!result) result=RAW(SYS_munmap,call->munmap.address,length,0,0,0,0);
            if (!result) *updates=next;
        }
        reply.munmap.return_value=result<0 ? -1 : result;
        reply.munmap.errno_result=result<0 ? -result : 0;
        size=sizeof(reply.munmap);
        break;
    case dserver_s2c_msgnum_mprotect:
        if (count!=(long)sizeof(call->mprotect)) darling_abort("invalid S2C mprotect packet");
        if (darling_memory_range(call->mprotect.address,call->mprotect.length) &&
            !(call->mprotect.protection&~(PROT_READ|PROT_WRITE|PROT_EXEC))) {
            uint64_t length=(call->mprotect.length+PAGE16-1)&~(PAGE16-1);
            struct darling_memory_updates next=*updates;
            result=darling_memory_replace(&next,call->mprotect.address,length,call->mprotect.protection&PROT_EXEC);
            if (!result) result=RAW(SYS_mprotect,call->mprotect.address,length,call->mprotect.protection,0,0,0);
            if (!result) *updates=next;
        }
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

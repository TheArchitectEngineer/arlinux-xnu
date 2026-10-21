/* Current-task proc_pidinfo. Identity is derived from actual Linux PID/start
 * ticks; executable UUID is the original Mach-O LC_UUID, not a placeholder.
 * Query other processes/flavors only when their behavior is implemented. */
#pragma once
static uint64_t *native_activity_ids;
static unsigned long native_generate_activity_id(const unsigned long long *r)
{
    if (!r[1] || r[1]>D_ACTIVITY_ID_COUNT_MAX) return D_KERN_INVALID_ARGUMENT;
    uint64_t current=__atomic_load_n(native_activity_ids,__ATOMIC_ACQUIRE);
    for (;;) {
        if (current>UINT64_MAX-r[1]) return D_KERN_RESOURCE_SHORTAGE;
        uint64_t next=current+r[1];
        if (__atomic_compare_exchange_n(native_activity_ids,&current,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
            return guest_copy(&current,r[2],sizeof(current),1) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
    }
}
static unsigned char guest_executable_uuid[16];
static long native_proc_start(uint32_t pid,uint64_t *ticks,uint32_t *parent)
{
    char path[40],digits[10],text[2048];
    const char prefix[]="/proc/",suffix[]="/stat";
    unsigned int length=0,count=0;
    for (unsigned int i=0;i<sizeof(prefix)-1;i++) path[length++]=prefix[i];
    uint32_t number=pid;
    do { digits[count++]='0'+number%10; number/=10; } while (number);
    while (count) path[length++]=digits[--count];
    for (unsigned int i=0;i<sizeof(suffix);i++) path[length++]=suffix[i];
    long fd=RAW(SYS_openat,AT_FDCWD,path,O_RDONLY|O_CLOEXEC,0,0,0);
    if (fd<0) return fd;
    long bytes=RAW(SYS_read,fd,text,sizeof(text)-1,0,0,0);
    RAW(SYS_close,fd,0,0,0,0,0);
    if (bytes<=0) return bytes<0 ? bytes : -EIO;
    text[bytes]=0;
    char *cursor=NULL,*end=text+bytes;
    for (char *p=text;p<end;p++) if (*p==')') cursor=p+1;
    if (!cursor) return -EIO;
    /* /proc/PID/stat field 3 follows the parenthesized comm, which may contain
     * spaces/parentheses. Fields 4 and 22 are the parent PID and start ticks. */
    for (unsigned int field=3;field<=22;field++) {
        while (cursor<end && *cursor==' ') cursor++;
        if (cursor==end) return -EIO;
        char *start=cursor;
        while (cursor<end && *cursor!=' ' && *cursor!='\n') cursor++;
        if (field!=4 && field!=22) continue;
        uint64_t value=0;
        for (char *p=start;p<cursor;p++) {
            if (*p<'0' || *p>'9' || value>(UINT64_MAX-(unsigned int)(*p-'0'))/10) return -EIO;
            value=value*10+(*p-'0');
        }
        if (field==4) *parent=value;
        else *ticks=value;
    }
    return 0;
}
static uint64_t native_proc_unique(uint32_t pid,uint64_t ticks)
{
    /* Opaque per-namespace identity, preserving both PID and its incarnation. */
    return ((uint64_t)pid<<32)|(uint32_t)ticks;
}
static long native_proc_info(const unsigned long long *r)
{
    uint32_t pid=RAW(SYS_getpid,0,0,0,0,0,0);
    if (r[0]!=D_PROC_CALL_PIDINFO || (r[1] && r[1]!=pid)) return -ENOTSUP;
    if (r[2]!=D_PROC_PIDUNIQIDENTIFIERINFO || r[3]>1) return -ENOTSUP;
    struct darwin_proc_unique info={0};
    if (r[5]<sizeof(info)) return -EINVAL;
    uint64_t ticks=0,parent_ticks=0;
    uint32_t parent=0,grandparent=0;
    long error=native_proc_start(pid,&ticks,&parent);
    if (error) return error;
    if (parent) {
        error=native_proc_start(parent,&parent_ticks,&grandparent);
        if (error) return error;
    }
    __builtin_memcpy(info.uuid,guest_executable_uuid,sizeof(info.uuid));
    info.uniqueid=native_proc_unique(pid,ticks);
    info.parent_uniqueid=parent ? native_proc_unique(parent,parent_ticks) : 0;
    info.idversion=(uint32_t)ticks;
    error=guest_copy(&info,r[4],sizeof(info),1);
    return error ? error : (long)sizeof(info);
}

/* This compatibility machine is an ordinary production environment, with no
 * AppleInternal/factory content or internal-security policy. Answer this actual
 * read-only kernel node before libSystem's init can re-enter libdispatch via
 * the fallback XPC parser for SystemVersion.plist. Unknown nodes still fail. */
static int native_sysctl_name(const char *name,uint64_t length,const char *expected)
{
    unsigned int i=0;
    while(expected[i]) { if(i>=length || name[i]!=expected[i]) return 0; i++; }
    return length==i || (length==i+1 && !name[i]);
}
/* OID_AUTO nodes get numbers from the kernel's own registry. These two numbers
 * belong to this compatibility machine, not to an assumed Apple OS release. */
enum { NATIVE_SYSCTL_OSVARIANT=0x10000,NATIVE_SYSCTL_BOOTARGS };
static int native_sysctl_node(const char *name,uint64_t length)
{
    if(native_sysctl_name(name,length,"kern.osvariant_status")) return NATIVE_SYSCTL_OSVARIANT;
    if(native_sysctl_name(name,length,"kern.bootargs")) return NATIVE_SYSCTL_BOOTARGS;
    return 0;
}
static long native_sysctl_output(const unsigned long long *r,const void *value,uint64_t required)
{
    uint64_t capacity;
    if (!r[3] || guest_copy(&capacity,r[3],sizeof(capacity),0)) return -EFAULT;
    if (guest_copy(&required,r[3],sizeof(required),1)) return -EFAULT;
    if (!r[2]) return 0;
    uint64_t copied=capacity<required ? capacity : required;
    if (copied && guest_copy((void *)value,r[2],copied,1)) return -EFAULT;
    return capacity<required ? -ENOMEM : 0;
}
static long native_sysctl_value(const unsigned long long *r,int node)
{
    if (r[4] || r[5]) return -EPERM;
    if(node==NATIVE_SYSCTL_BOOTARGS) {
        /* No Darwin kernel boot flags were configured for this machine. */
        const char empty=0;
        return native_sysctl_output(r,&empty,1);
    }
    if(node==NATIVE_SYSCTL_OSVARIANT) {
        /* XNU kern_sysctl.c starts this opaque launchd-owned cache at zero.
         * No Apple launchd initialized it here. Let this userland compute its
         * own checks instead of claiming a partial, version-specific cache.
         * Libc libdarwin/variant.c treats zero as an uncached result. */
        uint64_t value=0;
        return native_sysctl_output(r,&value,sizeof(value));
    }
    return -ENOENT;
}
static long native_sysctlbyname(const unsigned long long *r)
{
    char name[128];
    if(!r[1])return -EINVAL;
    if(r[1]>sizeof(name))return -ENAMETOOLONG;
    if(guest_copy(name,r[0],r[1],0))return -EFAULT;
    return native_sysctl_value(r,native_sysctl_node(name,r[1]));
}
static long native_sysctl(const unsigned long long *r)
{
    int32_t mib[2];
    if(r[1]!=2)return -ENOTSUP;
    if(guest_copy(mib,r[0],sizeof(mib),0))return -EFAULT;
    if(mib[0]==0 && mib[1]==3) { /* XNU sysctl_sysctl_name: name2oid */
        char name[128];
        if(!r[4] || !r[5])return -EINVAL;
        if(r[5]>sizeof(name))return -ENAMETOOLONG;
        if(guest_copy(name,r[4],r[5],0))return -EFAULT;
        int node=native_sysctl_node(name,r[5]);
        if(!node)return -ENOENT;
        const int32_t result[]={1,node}; /* CTL_KERN plus this machine's OID. */
        return native_sysctl_output(r,result,sizeof(result));
    }
    return mib[0]==1 ? native_sysctl_value(r,mib[1]) : -ENOENT;
}

static long native_linux_audit_number(const char *path,uint32_t *value)
{
    long fd=RAW(SYS_openat,AT_FDCWD,path,O_RDONLY|O_CLOEXEC,0,0,0);
    /* No Linux audit facility means no assigned audit identity/session. */
    if (fd==-ENOENT) { *value=UINT32_MAX; return 0; }
    if (fd<0) return fd;
    char text[32];
    long length=RAW(SYS_read,fd,text,sizeof(text),0,0,0);
    RAW(SYS_close,fd,0,0,0,0,0);
    if (length<=0) return length<0 ? length : -EIO;
    uint64_t number=0;
    unsigned int digits=0;
    for (long i=0;i<length;i++) {
        if (text[i]=='\n') break;
        if (text[i]<'0' || text[i]>'9') return -EIO;
        number=number*10+(text[i]-'0');
        if (number>UINT32_MAX) return -EOVERFLOW;
        digits++;
    }
    if (!digits) return -EIO;
    *value=number; return 0;
}
static long native_current_audit_token(struct darwin_audit_token *token)
{
    token->ruid=RAW(SYS_getuid,0,0,0,0,0,0);
    token->euid=RAW(SYS_geteuid,0,0,0,0,0,0);
    token->rgid=RAW(SYS_getgid,0,0,0,0,0,0);
    token->egid=RAW(SYS_getegid,0,0,0,0,0,0);
    token->pid=RAW(SYS_getpid,0,0,0,0,0,0);
    uint64_t ticks=0; uint32_t parent;
    long error=native_proc_start(token->pid,&ticks,&parent);
    if (error) return error;
    token->idversion=(uint32_t)ticks;
    error=native_linux_audit_number("/proc/self/loginuid",&token->audit_uid);
    if (error) return error;
    return native_linux_audit_number("/proc/self/sessionid",&token->session);
}

static uint32_t native_signature_bytes,native_codesign_traces;
static long native_csops(const unsigned long long *r)
{
    uint32_t pid=RAW(SYS_getpid,0,0,0,0,0,0);
    if ((int32_t)r[0] && (int32_t)r[0]!=(int32_t)pid) return -ESRCH;
    uint32_t version=0;
    if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_CSOPS_AUDITTOKEN && r[4]) {
        struct darwin_audit_token token;
        if (guest_copy(&token,r[4],sizeof(token),0)) return -EFAULT;
        uint64_t ticks=0; uint32_t parent=0;
        long error=native_proc_start(pid,&ticks,&parent);
        if (error) return error;
        if (token.pid!=pid || token.idversion!=(uint32_t)ticks) return -ESRCH;
        version=token.idversion;
    }
    long result=-ENOTSUP;
    /* The loader records actual LC_CODE_SIGNATURE presence. No signature means
     * status zero and no entitlements, as XNU reports for unsigned code. An
     * embedded, unvalidated signature must not be silently labelled unsigned. */
    if (!native_signature_bytes) {
        if ((uint32_t)r[1]==D_CS_OPS_STATUS) {
            uint32_t flags=0;
            result=r[2] ? guest_copy(&flags,r[2],sizeof(flags),1) : 0;
        } else if ((uint32_t)r[1]==D_CS_OPS_ENTITLEMENTS_BLOB) result=-EINVAL;
    }
    if (__atomic_fetch_add(&native_codesign_traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARWIN_CODESIGN_QUERY pid/op/bytes/signature_bytes/idversion/result follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,pid); trace_call(1,0,r[1]); trace_call(2,0,r[3]);
        trace_call(3,0,native_signature_bytes); trace_call(4,0,version); trace_call(5,0,result);
    }
    return result;
}

static long native_fcntl(const unsigned long long *r)
{
    long status=RAW(SYS_fcntl,r[0],F_GETFD,0,0,0,0);
    if (status<0) return status;
    status=-ENOTSUP;
    if (r[1]==D_F_GETPATH) {
        char path[1024],root[1024];
        status=native_fd_path(r[0],path);
        if(!status) {
            char *guest=path;
            if(root_descriptor>=0) {
                status=native_fd_path(root_descriptor,root);
                if(status)return status;
                unsigned int n=0;while(root[n] && root[n]==path[n])n++;
                if(!root[n] && (!path[n] || path[n]=='/'))guest=path[n] ? path+n : "/";
            }
            unsigned int length=0;while(guest[length])length++;
            status=guest_copy(guest,r[2],length+1,1);
        }
    } else if (r[1]==D_F_ADDFILESIGS_RETURN) {
        status=native_file_signature_coverage(r[0],r[2]);
    } else if (r[1]==D_F_CHECK_LV) {
        struct darwin_fchecklv request;
        if (guest_copy(&request,r[2],sizeof(request),0)) return -EFAULT;
        if (request.file_start<0) return -EINVAL;
        /* This trusted-input compatibility runtime does not enforce Apple's
         * Library Validation, just as its cache mapper does not authenticate
         * Apple signatures. Allow composition, not a claim that a signature
         * was verified or registered. File/mapping errors remain real. */
        if (request.error_message_size && request.error_message) {
            char empty=0;
            if (guest_copy(&empty,request.error_message,1,1)) return -EFAULT;
        }
        status=0;
    }
    static unsigned int traces;
    if (__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARWIN_FCNTL fd/command/argument/result follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,r[0]); trace_call(1,0,r[1]); trace_call(2,0,r[2]); trace_call(3,0,status);
    }
    return status;
}

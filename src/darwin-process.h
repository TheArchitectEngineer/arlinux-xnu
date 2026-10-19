/* Current-task proc_pidinfo. Identity is derived from actual Linux PID/start
 * ticks; executable UUID is the original Mach-O LC_UUID, not a placeholder.
 * Query other processes/flavors only when their behavior is implemented. */
#pragma once
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
static long native_sysctlbyname(const unsigned long long *r)
{
    const char expected[]="kern.osvariant_status";
    char name[128];
    if (!r[1]) return -EINVAL;
    if (r[1]>sizeof(name)) return -ENAMETOOLONG;
    if (guest_copy(name,r[0],r[1],0)) return -EFAULT;
    if (r[1]!=sizeof(expected)-1 && r[1]!=sizeof(expected)) return -ENOENT;
    for (unsigned int i=0;i<sizeof(expected)-1;i++) if (name[i]!=expected[i]) return -ENOENT;
    if (r[1]==sizeof(expected) && name[sizeof(expected)-1]) return -ENOENT;
    if (r[4] || r[5]) return -EPERM;
    uint64_t value=D_OS_VARIANT_INITIAL_BITS;
    for (unsigned int i=0;i<D_OS_VARIANT_STATUS_FIELDS;i++)
        value|=(uint64_t)D_OS_VARIANT_STATUS_NO<<(i*D_OS_VARIANT_STATUS_WIDTH);
    uint64_t capacity;
    if (!r[3] || guest_copy(&capacity,r[3],sizeof(capacity),0)) return -EFAULT;
    uint64_t required=sizeof(value);
    if (guest_copy(&required,r[3],sizeof(required),1)) return -EFAULT;
    if (!r[2]) return 0;
    if (capacity<sizeof(value)) return -ENOMEM;
    return guest_copy(&value,r[2],sizeof(value),1);
}

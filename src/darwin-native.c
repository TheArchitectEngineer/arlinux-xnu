/* Native Darwin ABI transport, inspired by tawcroot's TLS-independent systrap
 * runtime. Linux supplies these process/VM operations; this is not XNU boot.
 * Trusted non-arm64e inputs only. No security sandbox or general JIT support.
 */
#define _GNU_SOURCE
#include "darwin-native.h"
#include "darwin-abi.h"
#include "darwin-machine.h"
#include "ios-platform-abi.h"
#ifdef ARLINUX_DARLING
#include "rtsig.h"
#endif
#include <errno.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/auxv.h>
#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/openat2.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sched.h>

extern long darwin_raw(long, long, long, long, long, long, long);
extern char darwin_raw_return[];
#define RAW(n,a,b,c,d,e,f) darwin_raw((n),(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),(long)(f))
#define PAGE16 D_GUEST_PAGE_SIZE
#define CARRY D_ARM64_CARRY
#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

static int darwin_error(long result)
{
    switch (-result) {
    case EPERM: return D_EPERM;
    case ENOENT: return D_ENOENT;
    case ESRCH: return D_ESRCH;
    case EINTR: return D_EINTR;
    case EIO: return D_EIO;
    case ENXIO: return D_ENXIO;
    case E2BIG: return D_E2BIG;
    case ENOEXEC: return D_ENOEXEC;
    case EBADF: return D_EBADF;
    case ECHILD: return D_ECHILD;
    case ENOMEM: return D_ENOMEM;
    case EACCES: return D_EACCES;
    case EFAULT: return D_EFAULT;
    case EBUSY: return D_EBUSY;
    case EEXIST: return D_EEXIST;
    case EXDEV: return D_EXDEV;
    case ESTALE: return D_ESTALE;
    case EOWNERDEAD: return D_EOWNERDEAD;
    case ENODEV: return D_ENODEV;
    case ENOTDIR: return D_ENOTDIR;
    case EISDIR: return D_EISDIR;
    case EINVAL: return D_EINVAL;
    case ENFILE: return D_ENFILE;
    case EMFILE: return D_EMFILE;
    case EFBIG: return D_EFBIG;
    case ENOSPC: return D_ENOSPC;
    case ESPIPE: return D_ESPIPE;
    case EROFS: return D_EROFS;
    case EMLINK: return D_EMLINK;
    case EPIPE: return D_EPIPE;
    case EDOM: return D_EDOM;
    case ERANGE: return D_ERANGE;
    case EAGAIN: return D_EAGAIN;
    case ENOTSUP: return D_ENOTSUP;
    case ETIMEDOUT: return D_ETIMEDOUT;
    case ENAMETOOLONG: return D_ENAMETOOLONG;
    case ENOSYS: return D_ENOSYS;
    case EOVERFLOW: return D_EOVERFLOW;
    default: return D_EIO;
    }
}

static int native_trace_fd=2,native_stop_on_fault,native_diagnose_syscalls,native_diagnose_mach_wakeups;
static int native_secure_exec;
static void native_trace_prepare(void)
{
    /* Host-TLS setup only. Keep syscall tracing out of the guest's stderr so
     * dyld's small writes remain readable as one original error message. */
    static int prepared;
    if (prepared) return;
    const char *stop=getenv("ARLINUX_STOP_ON_FAULT");
    native_stop_on_fault=stop && !strcmp(stop,"1");
    const char *diagnose=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    native_diagnose_syscalls=diagnose && !strcmp(diagnose,"1");
    const char *value=getenv("ARLINUX_TRACE_FD");
    if (value) {
        char *end=NULL; long descriptor=strtol(value,&end,10);
        if (!*value || *end || descriptor<=2 || descriptor>INT32_MAX || fcntl(descriptor,F_GETFD)<0) {
            fprintf(stderr,"Invalid inherited ARLinux trace descriptor\n"); exit(2);
        }
        native_trace_fd=(int)descriptor;
        native_diagnose_mach_wakeups=1;
    }
    prepared=1;
}
static void trace_record(const char *tag,unsigned long number,unsigned long pc,unsigned long result)
{
    /* Bounded, TLS/libc-free diagnosis. No guest buffers or credentials. */
    char line[140];
    const char *labels[] = {tag," pc=0x"," result=0x"," tid=0x"};
    unsigned long values[] = {number,pc,result,RAW(SYS_gettid,0,0,0,0,0,0)};
    const char hex[] = "0123456789abcdef";
    unsigned int n=0;
    for (unsigned int i=0;i<4;i++) {
        for (const char *p=labels[i];*p;p++) line[n++]=*p;
        for (int bit=60;bit>=0;bit-=4) line[n++]=hex[(values[i]>>bit)&15];
    }
    line[n++]='\n';
    RAW(SYS_write,native_trace_fd,line,n,0,0,0);
}
static void trace_call(unsigned long number,unsigned long pc,unsigned long result)
{
    trace_record("DARWIN_CALL nr=0x",number,pc,result);
}

/* Legacy ARM64 iOS packs class pointers into a 36-bit user address space.
 * Keep every guest allocation below its commpage. The cursor is only a hint;
 * MAP_FIXED_NOREPLACE atomically owns a range without replacing dyld/cache or
 * another thread's allocation. Read actual holes only after a hint conflict. */
static unsigned long guest_next=D_GUEST_HEAP_START;
static long native_guest_gap(unsigned long length)
{
    if(!length || length>D_GUEST_COMMPAGE_RO-D_GUEST_HEAP_START)return -ENOMEM;
    int fd=RAW(SYS_openat,AT_FDCWD,"/proc/self/maps",O_RDONLY|O_CLOEXEC,0,0,0);
    if(fd<0)return fd;
    char buffer[4096],prefix[64];size_t used=0;unsigned long candidate=D_GUEST_HEAP_START;
    long result=-ENOMEM;
    for (;;) {
        long count=RAW(SYS_read,fd,buffer,sizeof(buffer),0,0,0);
        if(count==-EINTR)continue;
        if(count<0){result=count;break;}
        if(!count) {
            if(candidate<D_GUEST_COMMPAGE_RO && length<=D_GUEST_COMMPAGE_RO-candidate)result=candidate;
            break;
        }
        for(long i=0;i<count;i++) {
            if(buffer[i]!='\n') {if(used<sizeof(prefix))prefix[used++]=buffer[i];continue;}
            unsigned long start=0,end=0;size_t at=0;int valid=1;
            for(unsigned int field=0;field<2;field++) {
                unsigned long value=0;size_t digits=0;
                while(at<used) {
                    unsigned char c=prefix[at];unsigned int digit;
                    if(c>='0' && c<='9')digit=c-'0';
                    else if(c>='a' && c<='f')digit=c-'a'+10;
                    else break;
                    if(++digits>sizeof(value)*2){valid=0;break;}
                    value=(value<<4)|digit;at++;
                }
                char delimiter=field ? ' ' : '-';
                if(!digits || at>=used || prefix[at++]!=delimiter){valid=0;break;}
                if(field)end=value;else start=value;
            }
            used=0;
            if(!valid || start>=end){result=-EIO;goto finished;}
            if(start>=candidate && length<=start-candidate) {result=candidate;goto finished;}
            if(end>candidate)candidate=(end+PAGE16-1)&~(PAGE16-1);
            if(candidate>=D_GUEST_COMMPAGE_RO || length>D_GUEST_COMMPAGE_RO-candidate)goto finished;
        }
    }
finished:
    RAW(SYS_close,fd,0,0,0,0,0);return result;
}
static long anonymous_aligned_mapping(unsigned long size,int protection,unsigned long mask,int shared)
{
    /* Ordinary guest anonymous allocations do not provide adapted code.
     * File mappings and trusted server copy operations have separate callers. */
    if (!size || size > D_VM_MAX_ALLOCATION || (protection & ~(PROT_READ|PROT_WRITE)) ||
        mask>=D_VM_MAX_ALLOCATION || (mask&(mask+1))) return -EINVAL;
    unsigned long alignment=mask+1;
    if (alignment<PAGE16) alignment=PAGE16;
    size = (size + PAGE16 - 1) & ~(PAGE16 - 1);
    long raw=-ENOMEM;
    unsigned long requested=__atomic_load_n(&guest_next,__ATOMIC_RELAXED);
    for (unsigned int attempt=0;attempt<128;attempt++) {
        if(requested>=D_GUEST_COMMPAGE_RO || size+alignment>D_GUEST_COMMPAGE_RO-requested) {
            raw=native_guest_gap(size+alignment);
            if((unsigned long)raw>=(unsigned long)-4095)return raw;
            requested=raw;
        }
        raw=RAW(SYS_mmap,requested,size+alignment,protection,
                (shared ? MAP_SHARED : MAP_PRIVATE|MAP_NORESERVE)|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        if (raw==-EEXIST) {
            long gap=native_guest_gap(size+alignment);
            if((unsigned long)gap>=(unsigned long)-4095)return gap;
            requested=gap;raw=-EAGAIN;continue;
        }
        if ((unsigned long)raw >= (unsigned long)-4095) {
            trace_record("DARWIN_ANON_HOST_REJECTED address=0x",requested,size+alignment,raw);
            trace_record("DARWIN_ANON_HOST_POLICY protection=0x",protection,shared,0);
            return raw;
        }
        if ((unsigned long)raw!=requested) {
            RAW(SYS_munmap,raw,size+alignment,0,0,0,0); return -ENOTSUP;
        }
        unsigned long next=requested+size+alignment,previous=__atomic_load_n(&guest_next,__ATOMIC_RELAXED);
        while(previous<next && !__atomic_compare_exchange_n(&guest_next,&previous,next,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
        break;
    }
    if ((unsigned long)raw >= (unsigned long)-4095) return raw;
    unsigned long base = ((unsigned long)raw + alignment - 1) & ~(alignment - 1);
    unsigned long prefix = base - raw, suffix = alignment - prefix;
    if ((prefix && RAW(SYS_munmap,raw,prefix,0,0,0,0)) ||
        (suffix && RAW(SYS_munmap,base+size,suffix,0,0,0,0))) {
        RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0);
        __builtin_unreachable();
    }
    return (long)base;
}
static long anonymous_aligned(unsigned long size,int protection,unsigned long mask)
{
    return anonymous_aligned_mapping(size,protection,mask,0);
}

long darwin_native_memory(unsigned long size,int protection)
{
    return anonymous_aligned(size,protection,PAGE16-1);
}

static uint64_t native_main_stack_size;
long darwin_native_main_stack(unsigned long size)
{
    if(native_main_stack_size || !size || (size&(PAGE16-1)))return -EINVAL;
    long address=darwin_native_memory(size,PROT_READ|PROT_WRITE);
    if((unsigned long)address<(unsigned long)-4095)native_main_stack_size=size;
    return address;
}

/* Read-only filesystem access for genuine dyld dependencies. Darwin paths
 * resolve inside the selected firmware root; only the explicit executable
 * retains its host path. This is path adaptation, not a security sandbox. */
static int root_descriptor=-1,bundle_descriptor=-1;
static int container_descriptor=-1;
static char container_path[4096];
static size_t container_path_length;
static char bundle_path[4096];
static size_t bundle_path_length;
static uint64_t current_cache_base;
/* Process-local name for this task's VM capability. Only implemented VM traps
 * accept it; other port/IPC operations remain unsupported, never successful. */
static char executable_path[4096];
static long guest_copy(void *local, unsigned long remote, unsigned long length, int write)
{
    struct iovec here={local,length},there={(void *)remote,length};
    long pid=RAW(SYS_getpid,0,0,0,0,0,0);
    long count=RAW(write ? SYS_process_vm_writev : SYS_process_vm_readv,pid,&here,1,&there,1,0);
    return count==(long)length ? 0 : -EFAULT;
}
static long guest_path(unsigned long address,char *path)
{
    for (unsigned int i=0;i<4096;i++) {
        if (guest_copy(path+i,address+i,1,0)) return -EFAULT;
        if (!path[i]) return 0;
    }
    return -ENAMETOOLONG; /* Linux ENAMETOOLONG, translated below */
}
static int path_directory(char **path)
{
    /* Guest paths use one root namespace. Collapse repeated separators before
     * matching explicit bundle/container roots and removing the firmware '/'.
     * Otherwise //System remains absolute after one slash is stripped, and
     * Linux openat silently ignores root_descriptor. Preserve dot components:
     * their meaning depends on actual symlink traversal, not lexical cleanup.
     * All callers supply their own writable, bounded guest_path buffer. */
    char *read=*path,*write=*path;
    while(*read) {
        char value=*read++;
        *write++=value;
        if(value=='/')while(*read=='/')read++;
    }
    *write=0;
    unsigned int i=0;
    while ((*path)[i] && (*path)[i]==executable_path[i]) i++;
    if (!(*path)[i] && !executable_path[i]) return AT_FDCWD;
    size_t matched=0;
    while (matched<bundle_path_length && (*path)[matched]==bundle_path[matched]) matched++;
    if (bundle_descriptor>=0 && matched==bundle_path_length &&
        ((*path)[matched]=='/' || !(*path)[matched])) {
        *path+=matched;
        if (**path=='/') (*path)++;
        if (!**path) *path=".";
        return bundle_descriptor;
    }
    if(container_descriptor>=0) {
        if(**path!='/')return container_descriptor;
        matched=0;
        while(matched<container_path_length && (*path)[matched]==container_path[matched])matched++;
        if(matched==container_path_length && (!(*path)[matched] || (*path)[matched]=='/')) {
            *path+=matched;
            if(**path=='/')(*path)++;
            if(!**path)*path=".";
            return container_descriptor;
        }
    }
    if (root_descriptor>=0) {
        if (**path=='/') { (*path)++; if (!**path) *path="."; }
        return root_descriptor;
    }
    return AT_FDCWD;
}
#include "darwin-storage.h"
static long guest_openat(int dirfd,unsigned long address,unsigned long flags,unsigned long mode)
{
    /* Firmware/bundles are read-only; writes resolve beneath the app container.
     * Unsupported flag semantics are explicit. */
    char buffer[4096],*path=buffer;
    long error=guest_path(address,buffer);
    if (error) return error;
    const char label[]="DARWIN_OPEN ";
    unsigned int length=0; while (buffer[length]) length++;
    RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
    RAW(SYS_write,native_trace_fd,buffer,length,0,0,0);
    RAW(SYS_write,native_trace_fd,"\n",1,0,0,0);
    if ((flags&3)==3 || (flags & ~(D_O_WRONLY|D_O_RDWR|D_O_APPEND|D_O_CREAT|D_O_TRUNC|D_O_EXCL|
            D_O_NONBLOCK|D_O_NOFOLLOW|D_O_DIRECTORY|D_O_CLOEXEC|D_O_NOFOLLOW_ANY))) {
        const char rejected[]="DARWIN_OPEN_UNSUPPORTED_FLAGS follows\n";
        RAW(SYS_write,native_trace_fd,rejected,sizeof(rejected)-1,0,0,0);
        trace_call(0,0,flags);
        return -ENOTSUP;
    }
    /* Absolute guest paths ignore dirfd and use the declared firmware/bundle
     * mapping. Relative paths retain an actual guest-opened directory fd;
     * Darwin AT_FDCWD follows the existing guest-root current-directory policy. */
    if (buffer[0]!='/' && dirfd<0 && dirfd!=D_AT_FDCWD) return -EBADF;
    int directory=(buffer[0]=='/' || dirfd==D_AT_FDCWD) ? path_directory(&path) : dirfd;
    trace_record("DARWIN_OPEN_DIRECTORY fd=0x",(unsigned long)(long)directory,0,flags);
    int options=O_RDONLY;
    if(flags&D_O_WRONLY)options=O_WRONLY;
    if(flags&D_O_RDWR)options=O_RDWR;
    if(flags&D_O_APPEND)options|=O_APPEND;
    if(flags&D_O_CREAT)options|=O_CREAT;
    if(flags&D_O_TRUNC)options|=O_TRUNC;
    if(flags&D_O_EXCL)options|=O_EXCL;
    if (flags&D_O_NONBLOCK) options|=O_NONBLOCK;
    if (flags&D_O_NOFOLLOW) options|=O_NOFOLLOW;
    if (flags&D_O_DIRECTORY) options|=O_DIRECTORY;
    if (flags&D_O_CLOEXEC) options|=O_CLOEXEC;
    if(flags&(D_O_WRONLY|D_O_RDWR|D_O_CREAT|D_O_TRUNC|D_O_APPEND)) {
        char relative[4096];
        error=native_storage_path(directory,path,relative);if(error)return error;
        struct open_how how={.flags=options,.mode=flags&D_O_CREAT ? mode&07777 : 0,
            .resolve=RESOLVE_BENEATH|RESOLVE_NO_MAGICLINKS|
                (flags&D_O_NOFOLLOW_ANY ? RESOLVE_NO_SYMLINKS : 0)};
        return RAW(SYS_openat2,container_descriptor,relative,&how,sizeof(how),0,0);
    }
    if (flags&D_O_NOFOLLOW_ANY) {
        /* XNU fcntl.h requires no symlink in any path component. Linux's
         * O_NOFOLLOW covers only the last component; openat2 owns this effect. */
        struct open_how how={.flags=options,.resolve=RESOLVE_NO_SYMLINKS};
        return RAW(SYS_openat2,directory,path,&how,sizeof(how),0,0);
    }
    return RAW(SYS_openat,directory,path,options,0,0,0);
}
/* XNU bsd/sys/stat.h __DARWIN_STRUCT_STAT64, ARM64 layout. statx avoids
 * depending on the host libc struct-stat ABI inside the TLS-free handler. */
static long guest_stat(unsigned long argument,unsigned long destination,unsigned int kind,int dirfd,unsigned int flags)
{
    char buffer[4096],*path=buffer;
    int directory,options=0;
    if (kind==D_SYS_FSTAT64) { directory=(int)argument; path=""; options=AT_EMPTY_PATH; }
    else {
        long error=guest_path(argument,buffer); if (error) return error;
        if(path[0]!='/' && dirfd<0 && dirfd!=D_AT_FDCWD)return -EBADF;
        directory=(path[0]=='/' || dirfd==D_AT_FDCWD) ? path_directory(&path) : dirfd;
        if(flags&~D_AT_SYMLINK_NOFOLLOW)return -EINVAL;
        if (kind==D_SYS_LSTAT64 || (flags&D_AT_SYMLINK_NOFOLLOW)) options=AT_SYMLINK_NOFOLLOW;
    }
    struct statx actual={0};
    long error=RAW(SYS_statx,directory,path,options,STATX_BASIC_STATS|STATX_BTIME,&actual,0);
    if (error) return error;
    if (actual.stx_nlink>UINT16_MAX || actual.stx_dev_major>D_DEV_MAJOR_MAX || actual.stx_dev_minor>D_DEV_MINOR_MAX ||
        actual.stx_rdev_major>D_DEV_MAJOR_MAX || actual.stx_rdev_minor>D_DEV_MINOR_MAX) return -EOVERFLOW;
    struct darwin_stat64 out={0};
    out.device=(actual.stx_dev_major<<D_DEV_MAJOR_SHIFT)|actual.stx_dev_minor;
    out.rdevice=(actual.stx_rdev_major<<D_DEV_MAJOR_SHIFT)|actual.stx_rdev_minor;
    out.mode=actual.stx_mode; out.links=actual.stx_nlink; out.inode=actual.stx_ino;
    out.uid=actual.stx_uid; out.gid=actual.stx_gid;
    out.access.seconds=actual.stx_atime.tv_sec; out.access.nanoseconds=actual.stx_atime.tv_nsec;
    out.modification.seconds=actual.stx_mtime.tv_sec; out.modification.nanoseconds=actual.stx_mtime.tv_nsec;
    out.change.seconds=actual.stx_ctime.tv_sec; out.change.nanoseconds=actual.stx_ctime.tv_nsec;
    out.birth.seconds=actual.stx_btime.tv_sec; out.birth.nanoseconds=actual.stx_btime.tv_nsec;
    out.size=actual.stx_size; out.blocks=actual.stx_blocks; out.blocksize=actual.stx_blksize;
    return guest_copy(&out,destination,sizeof(out),1);
}

#include "darling-native.h"
#include "darling-suspend.h"
#include "darwin-filesystem.h"
#include "darwin-attributes.h"
#include "darwin-signature.h"
#include "darwin-process.h"
#include "darwin-mmap.h"
#include "darwin-ipc.h"
#include "darwin-threads.h"
#include "darwin-psynch.h"
#include "darwin-kqueue.h"
#include "darwin-workqueue.h"
#include "darwin-http.h"
#include "darwin-ui.h"
#include "darwin-fork.h"

/* Read only the original ARM64 frame records. A corrupt/inaccessible chain
 * stops collection; addresses are evidence, never a synthetic unwind. */
static void trace_guest_frames(const char *tag,const unsigned long long *r)
{
    unsigned long frame=r[29];
    trace_record(tag,0,r[30],frame);
    for (unsigned int i=1;i<16 && frame && !(frame&15);i++) {
        unsigned long pair[2];
        if (guest_copy(pair,frame,sizeof(pair),0) || !pair[1]) break;
        trace_record(tag,i,pair[1],pair[0]);
        if (pair[0]<=frame) break;
        frame=pair[0];
    }
}

/* Opt-in observation only: preserve the failing return and original arguments.
 * Record caller chains once per BSD syscall, rather than waiting for a crash
 * after a framework has converted ENOSYS into a generic NSError. */
static void trace_unimplemented(const unsigned long long *r,unsigned long pc)
{
    static unsigned long seen[16];
    static unsigned int count;
    unsigned long number=r[D_ARM64_SYSCALL_REGISTER];
    if (number>=1024) return;
    unsigned long bit=1UL<<(number%64);
    if (__atomic_fetch_or(&seen[number/64],bit,__ATOMIC_RELAXED)&bit) return;
    unsigned int index=__atomic_fetch_add(&count,1,__ATOMIC_RELAXED);
    if (index>=32) {
        if (index==32) trace_record("DARWIN_UNIMPLEMENTED_TRUNCATED limit=0x",32,pc,number);
        return;
    }
    trace_record("DARWIN_UNIMPLEMENTED nr=0x",number,pc,-ENOSYS);
    for (unsigned int i=0;i<6;i++)
        trace_record("DARWIN_UNIMPLEMENTED_ARGUMENT index=0x",i,number,r[i]);
    trace_guest_frames("DARWIN_UNIMPLEMENTED_FRAME nr=0x",r);
}

static void trace_getattrlist(const unsigned long long *r,unsigned long pc)
{
    static unsigned int count;
    unsigned int index=__atomic_fetch_add(&count,1,__ATOMIC_RELAXED);
    if (index>=32) {
        if (index==32) trace_record("DARWIN_GETATTRLIST_TRUNCATED limit=0x",32,pc,0);
        return;
    }
    char path[4096];
    long error=guest_path(r[0],path);
    trace_record("DARWIN_GETATTRLIST_PATH status=0x",error,pc,r[4]);
    if (!error) {
        const char label[]="DARWIN_GETATTRLIST_PATH ";
        unsigned int length=0; while (path[length]) length++;
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        RAW(SYS_write,native_trace_fd,path,length,0,0,0);
        RAW(SYS_write,native_trace_fd,"\n",1,0,0,0);
    }
    /* XNU attrlist: two uint16_t header fields followed by five uint32_t
     * attribute groups. Copy bounded metadata, never the caller's output. */
    uint32_t attributes[6];
    error=guest_copy(attributes,r[1],sizeof(attributes),0);
    trace_record("DARWIN_GETATTRLIST_ATTRIBUTES status=0x",error,pc,r[3]);
    if (!error) for (unsigned int i=0;i<6;i++)
        trace_record("DARWIN_GETATTRLIST_GROUP index=0x",i,pc,attributes[i]);
}

static void dispatch(int signal, siginfo_t *info, void *context)
{
    /* No errno, malloc, pthread, stdio or other host libc operation here.
     * Guest may have replaced TPIDR_EL0 with its own TSD base. */
    ucontext_t *uc = context;
    unsigned long long *r = uc->uc_mcontext.regs;
    if (signal != SIGSYS || info->si_code != SYS_SECCOMP) {
        RAW(SYS_exit_group,D_RUNTIME_BAD_TRANSPORT_EXIT,0,0,0,0,0);
        __builtin_unreachable();
    }
    int32_t trap=(int32_t)r[D_ARM64_SYSCALL_REGISTER];
    if (trap<0 && (uint32_t)trap!=D_SYS_PLATFORM) {
        /* Mach traps do not use BSD's positive errno/carry convention. Only
         * implemented process-local capabilities are returned; unsupported
         * operations return KERN_NOT_SUPPORTED. */
        unsigned long number=-(int64_t)trap;
#ifdef ARLINUX_DARLING
        if (darling_active && number!=D_TRAP_VM_ALLOCATE && number!=D_TRAP_VM_DEALLOCATE &&
            number!=D_TRAP_VM_MAP && number!=D_TRAP_VM_PROTECT && number!=D_TRAP_ABSOLUTE_TIME &&
            number!=D_TRAP_CONTINUOUS_TIME && number!=D_TRAP_TIMEBASE_INFO && number!=D_TRAP_GENERATE_ACTIVITY_ID &&
            number!=D_TRAP_HOST_CREATE_VOUCHER) {
            r[0]=darling_mach(number,r);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
#endif
        if (number==D_TRAP_SPECIAL_REPLY_PORT) {
            struct native_thread *thread=native_current_thread();
            uint32_t name=thread ? ipc_reply_port() : 0;
            if (name) {
                struct ipc_endpoint *previous=ipc_find(thread->special_reply_port);
                if (previous) previous->special_tid=0;
                struct ipc_endpoint *endpoint=ipc_find(name);
                endpoint->send_refs=1; endpoint->special_tid=thread->tid;
                thread->special_reply_port=name;
            }
            r[0]=name;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_GENERATE_ACTIVITY_ID) {
            r[0]=native_generate_activity_id(r);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number>=D_TRAP_TIMER_CREATE && number<=D_TRAP_TIMER_ARM_LEEWAY) {
            const char label[]="DARWIN_TIMER_OPERATION trap/arguments follows\n";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            trace_call(number,uc->uc_mcontext.pc,0);
            for (unsigned int i=0;i<4;i++) trace_call(i,uc->uc_mcontext.pc,r[i]);
            r[0]=number==D_TRAP_TIMER_CREATE ? ipc_timer_create() : ipc_timer_control(number,r);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_PORT_ALLOCATE || number==D_TRAP_PORT_MOD_REFS || number==D_TRAP_PORT_INSERT_RIGHT ||
            number==D_TRAP_PORT_DESTRUCT ||
            number==D_TRAP_PORT_INSERT_MEMBER || number==D_TRAP_PORT_EXTRACT_MEMBER) {
            const char label[]="DARWIN_PORT_OPERATION trap/arguments follows\n";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            trace_call(number,uc->uc_mcontext.pc,0);
            for (unsigned int i=0;i<4;i++) trace_call(i,uc->uc_mcontext.pc,r[i]);
            r[0]=number==D_TRAP_PORT_ALLOCATE ? ipc_port_allocate(r) :
                 number==D_TRAP_PORT_MOD_REFS ? ipc_port_mod_refs(r) :
                 number==D_TRAP_PORT_DESTRUCT ? ipc_special_reply_destruct(r) :
                 number==D_TRAP_PORT_INSERT_RIGHT ? ipc_port_insert_right(r) :
                 ipc_port_member(r,number==D_TRAP_PORT_INSERT_MEMBER);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_PORT_CONSTRUCT) {
            r[0]=ipc_port_construct(r,uc->uc_mcontext.pc);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_HOST_CREATE_VOUCHER) {
            /* Standalone identity/accounting policy: no extra bank context.
             * This is the empty-base-voucher mode already supported by real
             * libdispatch, not a fabricated identity or resource authority. */
            struct darwin_voucher_recipe recipe;
            uint32_t empty=D_MACH_PORT_NULL;
            if (r[0]!=CURRENT_HOST_PORT) r[0]=D_KERN_INVALID_ARGUMENT;
            else if (r[2]!=sizeof(recipe)) r[0]=D_KERN_NOT_SUPPORTED;
            else if (guest_copy(&recipe,r[1],sizeof(recipe),0)) r[0]=D_KERN_INVALID_ADDRESS;
            else if (recipe.key!=D_VOUCHER_KEY_BANK || recipe.command!=D_VOUCHER_BANK_CREATE ||
                     recipe.previous_voucher || recipe.content_size) r[0]=D_KERN_NOT_SUPPORTED;
            else r[0]=guest_copy(&empty,r[3],sizeof(empty),1) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_TASK_SELF) {
            r[0]=CURRENT_TASK_PORT;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_THREAD_SELF) {
            struct native_thread *thread=native_current_thread();
            r[0]=thread ? thread->port : 0;
            if (thread) __atomic_fetch_add(&thread->refs,1,__ATOMIC_RELAXED);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_VM_PROTECT) {
            if (r[0]!=CURRENT_TASK_PORT) r[0]=D_KERN_INVALID_TASK;
            /* Match integrated Darling duct-tape/src/memory.c: apply the
             * real Linux protection for both forms. Linux has no independent
             * Mach maximum-permission ceiling; trusted guest code can later
             * request a different protection. This is not a sandbox promise. */
            else if (r[4]&~(PROT_READ|PROT_WRITE)) r[0]=D_KERN_NOT_SUPPORTED;
            else if ((r[1]&(D_GUEST_PAGE_SIZE-1)) || !r[2] || r[2]>D_VM_MAX_ALLOCATION ||
                     r[1]<D_GUEST_PAGE_SIZE || r[1]>=D_GUEST_COMMPAGE_RO ||
                     r[2]>D_GUEST_COMMPAGE_RO-r[1]) r[0]=D_KERN_INVALID_ARGUMENT;
            else r[0]=RAW(SYS_mprotect,r[1],(r[2]+(D_GUEST_PAGE_SIZE-1))&~(D_GUEST_PAGE_SIZE-1),r[4],0,0,0) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_HOST_SELF || number==D_TRAP_REPLY_PORT) {
            if (number==D_TRAP_HOST_SELF) host_send_refs++;
            r[0]=number==D_TRAP_HOST_SELF ? CURRENT_HOST_PORT : ipc_reply_port();
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_PORT_DEALLOCATE) { /* release implemented service send-right references */
            unsigned long *refs=r[1]==CURRENT_HOST_PORT ? &host_send_refs :
                r[1]==SYSTEM_CLOCK_PORT ? &clock_send_refs[0] :
                r[1]==CALENDAR_CLOCK_PORT ? &clock_send_refs[1] : NULL;
            if (r[0]!=CURRENT_TASK_PORT) r[0]=D_KERN_INVALID_TASK;
            else if (refs && *refs) { --*refs; r[0]=0; }
            else {
                struct ipc_endpoint *endpoint=ipc_find(r[1]);
                struct native_thread *thread=native_thread_port(r[1]);
                if (endpoint && endpoint->kind==3 && ipc_drop_reference(&endpoint->dead_refs)) {
                    r[0]=0;
                } else if (endpoint && ipc_drop_send(endpoint)) {
                    r[0]=0;
                } else if (thread && __atomic_load_n(&thread->refs,__ATOMIC_ACQUIRE)) {
                    __atomic_fetch_sub(&thread->refs,1,__ATOMIC_ACQ_REL); r[0]=0;
                } else r[0]=D_KERN_INVALID_NAME;
                native_reap_threads();
            }
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_SEMAPHORE_SIGNAL || number==D_TRAP_SEMAPHORE_WAIT) { /* semaphore signal/wait */
            struct ipc_endpoint *endpoint=ipc_find(r[0]);
            long status=-EINVAL;
            if (endpoint && endpoint->kind==1) {
                if (number==D_TRAP_SEMAPHORE_WAIT) status=ipc_semaphore_wait(r[0],NULL);
                else {
                    uint64_t token=1;
                    status=RAW(SYS_write,endpoint->write_fd,&token,8,0,0,0)==8 ? 0 : -EINVAL;
                }
            }
            r[0]=status ? D_KERN_INVALID_ARGUMENT : D_KERN_SUCCESS;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_MSG) {
            r[0]=ipc_message(r,uc->uc_mcontext.pc);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_VM_ALLOCATE || number==D_TRAP_VM_MAP) { /* current-task anonymous allocate/map */
            uint64_t address=0;
            unsigned long status=D_KERN_INVALID_ARGUMENT; /* KERN_INVALID_ARGUMENT */
            if (r[0]!=CURRENT_TASK_PORT) status=D_KERN_INVALID_TASK; /* KERN_INVALID_TASK */
            else if (guest_copy(&address,r[1],sizeof(address),0)) status=D_KERN_INVALID_ADDRESS;
            else if (((number==D_TRAP_VM_ALLOCATE ? r[3] : r[4])&~(unsigned long)(D_VM_FLAGS_TAG_MASK|D_VM_FLAGS_ANYWHERE|D_VM_FLAGS_PURGABLE))==0 &&
                     ((number==D_TRAP_VM_ALLOCATE ? r[3] : r[4])&D_VM_FLAGS_ANYWHERE) && r[2] &&
                     (number==D_TRAP_VM_ALLOCATE || !(r[5]&~(PROT_READ|PROT_WRITE)))) {
                /* VM_FLAGS_PURGABLE begins nonvolatile, with real zero-filled
                 * backing just like ordinary anonymous memory. Accept that
                 * allocation capability; volatile/discard state control is
                 * still unsupported, never reported as a completed purge. */
                long mapped=anonymous_aligned(r[2],number==D_TRAP_VM_ALLOCATE ? PROT_READ|PROT_WRITE : r[5],
                                              number==D_TRAP_VM_ALLOCATE ? PAGE16-1 : r[3]);
                if ((unsigned long)mapped>=(unsigned long)-4095) status=D_KERN_RESOURCE_SHORTAGE;
                else {
                    address=mapped;
                    status=guest_copy(&address,r[1],sizeof(address),1) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
                    if (status) RAW(SYS_munmap,mapped,(r[2]+PAGE16-1)&~(PAGE16-1),0,0,0,0);
                }
            }
            if (status) {
                const char label[]="DARWIN_VM_REJECTED argument-index/pc/value follows\n";
                RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
                for (unsigned int i=0;i<6;i++) trace_call(i,uc->uc_mcontext.pc,r[i]);
            }
            trace_record("DARWIN_VM_ALLOCATE address=0x",address,r[2],status);
            r[0]=status;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_VM_DEALLOCATE) {       /* _kernelrpc_mach_vm_deallocate_trap */
            unsigned long start=r[1]&~(PAGE16-1),offset=r[1]-start;
            unsigned long size=r[2];
            if (r[0]!=CURRENT_TASK_PORT) r[0]=D_KERN_INVALID_TASK;
            else if (size>D_VM_MAX_ALLOCATION || start+size+offset+PAGE16<start) r[0]=D_KERN_INVALID_ARGUMENT;
            else if (!size) r[0]=0;
            else r[0]=RAW(SYS_munmap,start,(size+offset+PAGE16-1)&~(PAGE16-1),0,0,0,0) ? D_KERN_INVALID_ADDRESS : D_KERN_SUCCESS;
            trace_record("DARWIN_VM_DEALLOCATE address=0x",start,r[2],r[0]);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_ABSOLUTE_TIME || number==D_TRAP_CONTINUOUS_TIME) {
            /* ARM fast-time calls return a value, not kern_return_t/errno.
             * Use genuine nanosecond clocks, paired with timebase 1/1. */
            struct timespec now;
            long error=RAW(SYS_clock_gettime,number==D_TRAP_ABSOLUTE_TIME ? CLOCK_MONOTONIC : CLOCK_BOOTTIME,&now,0,0,0,0);
            if (error) {
                trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,error);
                RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0);
                __builtin_unreachable();
            }
            r[0]=(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==D_TRAP_TIMEBASE_INFO) {
            const uint32_t scale[2]={1,1};
            struct iovec local={(void *)scale,sizeof(scale)},remote={(void *)r[0],sizeof(scale)};
            long size=RAW(SYS_process_vm_writev,RAW(SYS_getpid,0,0,0,0,0,0),&local,1,&remote,1,0);
            r[0]=size==(long)sizeof(scale) ? D_KERN_SUCCESS : D_KERN_INVALID_ADDRESS; /* KERN_INVALID_ADDRESS */
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
            return;
        }
        r[0]=(number>=D_TRAP_REPLY_PORT && number<=D_TRAP_HOST_SELF) ? D_MACH_PORT_NULL : D_KERN_NOT_SUPPORTED;
        trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]);
        return;
    }
    long result = -ENOSYS;
    int bsd_error=0;
    switch (r[D_ARM64_SYSCALL_REGISTER]) {
    case D_SYS_BSDTHREAD_CREATE: case D_SYS_BSDTHREAD_REGISTER: {
        const char label[]="DARWIN_THREAD_REQUEST argument-index/pc/value follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        for (unsigned int i=0;i<6;i++) trace_call(i,uc->uc_mcontext.pc,r[i]);
        if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_BSDTHREAD_REGISTER && r[4]>=8 && r[4]<=128) {
            uint64_t metadata[16]={0};
            if (!guest_copy(metadata,r[3],r[4],0)) {
                const char record[]="DARWIN_THREAD_REGISTRATION word-index/pc/value follows\n";
                RAW(SYS_write,native_trace_fd,record,sizeof(record)-1,0,0,0);
                for (unsigned int i=0;i<r[4]/8;i++) trace_call(i,uc->uc_mcontext.pc,metadata[i]);
            }
        }
        result=r[D_ARM64_SYSCALL_REGISTER]==D_SYS_BSDTHREAD_REGISTER ? native_thread_register(r) : native_thread_create(r,&uc->uc_sigmask);
        break;
    }
    case D_SYS_PSYNCH_MUTEXWAIT: case D_SYS_PSYNCH_MUTEXDROP:
    case D_SYS_PSYNCH_CVBROAD: case D_SYS_PSYNCH_CVSIGNAL: case D_SYS_PSYNCH_CVWAIT:
    case D_SYS_PSYNCH_CVCLRPREPOST:
    case D_SYS_PSYNCH_RW_RDLOCK: case D_SYS_PSYNCH_RW_WRLOCK: case D_SYS_PSYNCH_RW_UNLOCK:
        result=native_psynch(r,&bsd_error); break;
    case D_SYS_KQUEUE: result=native_kqueue_create(); break;
    case D_SYS_KEVENT_QOS: case D_SYS_KEVENT_ID: case D_SYS_BSDTHREAD_CTL:
    case D_SYS_WORKQ_OPEN: case D_SYS_WORKQ_KERNRETURN: {
        static unsigned int requests;
        if (__atomic_fetch_add(&requests,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char label[]="DARWIN_SCHEDULER_REQUEST syscall then argument-index/pc/value follows\n";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,0);
            unsigned int count=(r[D_ARM64_SYSCALL_REGISTER]==D_SYS_KEVENT_QOS ||
                                r[D_ARM64_SYSCALL_REGISTER]==D_SYS_KEVENT_ID) ? 8 :
                4;
            for (unsigned int i=0;i<count;i++) trace_call(i,uc->uc_mcontext.pc,r[i]);
            if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_KEVENT_ID && r[2]) {
                struct darwin_kevent_qos event;
                if (!guest_copy(&event,r[1],sizeof(event),0)) {
                    const char event_label[]="DARWIN_KEVENT_ID_CHANGE ident/filter/flags/qos/udata/fflags/xflags/data/ext follows\n";
                    RAW(SYS_write,native_trace_fd,event_label,sizeof(event_label)-1,0,0,0);
                    trace_call(0,uc->uc_mcontext.pc,event.ident);
                    trace_call(1,uc->uc_mcontext.pc,(long)event.filter);
                    trace_call(2,uc->uc_mcontext.pc,event.flags);
                    trace_call(3,uc->uc_mcontext.pc,event.qos);
                    trace_call(4,uc->uc_mcontext.pc,event.udata);
                    trace_call(5,uc->uc_mcontext.pc,event.fflags);
                    trace_call(6,uc->uc_mcontext.pc,event.xflags);
                    trace_call(7,uc->uc_mcontext.pc,event.data);
                    for (unsigned int i=0;i<4;i++) trace_call(8+i,uc->uc_mcontext.pc,event.ext[i]);
                }
            }
        }
        if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_KEVENT_QOS) result=native_kevent_qos(r,&uc->uc_sigmask);
        else if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_KEVENT_ID) result=native_kevent_id(r,&uc->uc_sigmask);
        else if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_BSDTHREAD_CTL) result=native_thread_ctl(r);
        else result=native_workqueue(r,&uc->uc_sigmask);
        if (native_workqueue_resume(uc,result)) return;
        break;
    }
    case D_SYS_BSDTHREAD_TERMINATE: result=native_thread_terminate(r); break;
    case D_SYS_ULOCK_WAIT: case D_SYS_ULOCK_WAKE:
        result=native_ulock(r);
        if (r[0]&D_UL_NO_ERRNO) {
            trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,result);
            r[0]=result<0 ? -(long)darwin_error(result) : result;
            uc->uc_mcontext.pstate&=~CARRY;
            return;
        }
        break;
    case D_SYS_MAC_SYSCALL: {
        char policy[5];
        struct iovec local={policy,sizeof(policy)},remote={(void *)r[0],sizeof(policy)};
        long pid=RAW(SYS_getpid,0,0,0,0,0,0);
        if (RAW(SYS_process_vm_readv,pid,&local,1,&remote,1,0)!=(long)sizeof(policy)) {
            result=-EFAULT; break;
        }
        if(r[1]==D_SANDBOX_CHECK) {
            char name[8],argument;
            if(!guest_copy(name,r[0],sizeof(name),0) && !__builtin_memcmp(name,"Sandbox",sizeof(name))) {
                /* No Apple Seatbelt policy is installed in this Linux
                 * compatibility process. Match Darling's sandbox_check
                 * boundary: a policy query adds no denial. This neither
                 * installs a profile nor grants Unix file/process rights;
                 * actual host operations retain their normal permission checks.
                 * Reference: darlinghq/darling src/sandbox/sandbox.c.
                 * Other Sandbox calls remain unsupported. */
                result=guest_copy(&argument,r[2],sizeof(argument),0);
                if(!result)trace_record("DARWIN_SANDBOX_CHECK policy_installed=0x",0,uc->uc_mcontext.pc,0);
                break;
            }
        }
        if (r[1]!=D_AMFI_DYLD_POLICY || policy[0]!='A' || policy[1]!='M' || policy[2]!='F' ||
            policy[3]!='I' || policy[4]) break;
        /* Confirmed from the actual 650.3.4 wrapper: two uint64_t words,
         * input flags and a POINTER to the uint64_t result, not inline output.
         * This environment permits @paths, path variables, custom caches,
         * fallback paths and diagnostic variables. Required insertions fail
         * closed instead of silently running without the platform library. It does
         * not claim to implement Apple's AMFI authentication or other calls. */
        uint64_t args[2];
        local=(struct iovec){args,sizeof(args)};
        remote=(struct iovec){(void *)r[2],sizeof(args)};
        if (RAW(SYS_process_vm_readv,pid,&local,1,&remote,1,0)!=(long)sizeof(args)) {
            result=-EFAULT; break;
        }
        uint64_t permissions=D_AMFI_ALLOW_AT_PATHS|D_AMFI_ALLOW_PATH_ENV|D_AMFI_ALLOW_CUSTOM_CACHE|
                             D_AMFI_ALLOW_FALLBACK_PATHS|D_AMFI_ALLOW_PRINT_ENV;
        local=(struct iovec){&permissions,sizeof(permissions)};
        remote=(struct iovec){(void *)args[1],sizeof(permissions)};
        result=RAW(SYS_process_vm_writev,pid,&local,1,&remote,1,0)==(long)sizeof(permissions) ? 0 : -EFAULT;
        break;
    }
    case D_SYS_GETENTROPY: {                 /* Darwin getentropy: success returns zero */
        if (r[1]>256) { result=-EIO; break; }
        unsigned long done=0;
        while (done<r[1]) {
            long count=RAW(SYS_getrandom,r[0]+done,r[1]-done,0,0,0,0);
            if (count==-EINTR) continue; /* retry EINTR without host errno/TLS */
            if (count<0) { result=count; break; }
            if (!count) { result=-EIO; break; }
            done+=(unsigned long)count;
        }
        if (done==r[1]) result=0;
        break;
    }
    case D_SYS_ABORT_WITH_PAYLOAD: case D_SYS_TERMINATE_WITH_PAYLOAD: {       /* retain actual dyld fatal reason, no fake success */
        char text[512];
        struct iovec local={text,sizeof(text)};
        struct iovec remote={(void *)r[r[D_ARM64_SYSCALL_REGISTER]==D_SYS_TERMINATE_WITH_PAYLOAD ? 4 : 5],sizeof(text)};
        long size=RAW(SYS_process_vm_readv,RAW(SYS_getpid,0,0,0,0,0,0),&local,1,&remote,1,0);
        if (size>0) {
            unsigned long length=0;
            while (length<(unsigned long)size && text[length]) length++;
            const char label[]="DARWIN_ABORT_REASON ";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            RAW(SYS_write,native_trace_fd,text,length,0,0,0);
            RAW(SYS_write,native_trace_fd,"\n",1,0,0,0);
        }
        break;
    }
    case D_SYS_EXIT:
        trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,r[0]&255);
        /* A framework can terminate the process with status zero before the
         * app's ready callback. Preserve the actual caller chain, not a pass. */
        trace_guest_frames("DARWIN_EXIT_FRAME nr=0x",r);
        RAW(SYS_exit_group,r[0]&255,0,0,0,0,0);
        __builtin_unreachable();
    case D_SYS_READ: case D_SYS_READ_NOCANCEL: result = RAW(SYS_read,r[0],r[1],r[2],0,0,0); break;
    case D_SYS_WRITE: case D_SYS_WRITE_NOCANCEL: result = RAW(SYS_write,r[0],r[1],r[2],0,0,0); break;
    case D_SYS_FCNTL: result=native_fcntl(r); break;
    case D_SYS_ACCESS: {
        char buffer[4096],*path=buffer;
        result=guest_path(r[0],buffer);
        if (result) break;
        if (r[1]&~(R_OK|W_OK|X_OK)) { result=-EINVAL; break; }
        if (r[1]&W_OK) { result=-EROFS; break; }
        int directory=path_directory(&path);
        result=RAW(SYS_faccessat,directory,path,r[1],0,0,0);
        break;
    }
    case D_SYS_GETRLIMIT: result=native_getrlimit(r); break;
    case D_SYS_SYSCTLBYNAME: result=native_sysctlbyname(r); break;
    case D_SYS_SYSCTL: result=native_sysctl(r); break;
    case D_SYS_CSOPS: case D_SYS_CSOPS_AUDITTOKEN: result=native_csops(r); break;
    case D_SYS_PROC_INFO: result=native_proc_info(r); break;
    case D_SYS_OPEN: case D_SYS_OPEN_NOCANCEL: result=guest_openat(D_AT_FDCWD,r[0],r[1],r[2]); break;
    case D_SYS_OPENAT: case D_SYS_OPENAT_NOCANCEL: result=guest_openat((int)r[0],r[1],r[2],r[3]); break;
    case D_SYS_MKDIR: result=native_storage_mkdir(D_AT_FDCWD,r[0],r[1]); break;
    case D_SYS_MKDIRAT: result=native_storage_mkdir((int)r[0],r[1],r[2]); break;
    case D_SYS_UNLINK: case D_SYS_RMDIR:
        result=native_storage_unlink(D_AT_FDCWD,r[0],r[D_ARM64_SYSCALL_REGISTER]==D_SYS_RMDIR ? D_AT_REMOVEDIR : 0); break;
    case D_SYS_UNLINKAT: result=native_storage_unlink((int)r[0],r[1],r[2]); break;
    case D_SYS_RENAME: result=native_storage_rename(D_AT_FDCWD,r[0],D_AT_FDCWD,r[1]); break;
    case D_SYS_RENAMEAT: result=native_storage_rename((int)r[0],r[1],(int)r[2],r[3]); break;
    case D_SYS_FSYNC: case D_SYS_FSYNC_NOCANCEL: result=RAW(SYS_fsync,r[0],0,0,0,0,0); break;
    case D_SYS_FSTATAT64: result=guest_stat(r[1],r[2],D_SYS_FSTATAT64,(int)r[0],r[3]); break;
    case D_SYS_CLOSE: case D_SYS_CLOSE_NOCANCEL:
        result=((int)r[0]==root_descriptor || (int)r[0]==bundle_descriptor || (int)r[0]==container_descriptor ||
                (native_trace_fd>2 && (int)r[0]==native_trace_fd) ||
                (native_http_event>=0 && (int)r[0]==native_http_event)) ? -EBADF : RAW(SYS_close,r[0],0,0,0,0,0); break;
    case D_SYS_PREAD: case D_SYS_PREAD_NOCANCEL: result=RAW(SYS_pread64,r[0],r[1],r[2],r[3],0,0); break;
    case D_SYS_LSEEK: result=RAW(SYS_lseek,r[0],r[1],r[2],0,0,0); break;
    case D_SYS_SHARED_REGION_CHECK: /* Actual process-private cache mappings prepared before entry. */
        result=current_cache_base ? guest_copy(&current_cache_base,r[0],sizeof(current_cache_base),1) : -ENOMEM;
        break;
    case D_SYS_SEMWAIT_SIGNAL: case D_SYS_SEMWAIT_SIGNAL_NOCANCEL: { /* __semwait_signal and nocancel */
        if (r[1] || r[2]>1 || (r[2] && r[3]!=1) || (long)r[4]<0 || r[5]>=1000000000) {
            result=-ENOTSUP; break;
        }
        struct timespec timeout={r[4],r[5]};
        result=ipc_semaphore_wait(r[0],r[2] ? &timeout : NULL);
        break;
    }
    case D_SYS_STAT64: case D_SYS_FSTAT64: case D_SYS_LSTAT64: result=guest_stat(r[0],r[1],r[D_ARM64_SYSCALL_REGISTER],D_AT_FDCWD,0); break;
    case D_SYS_GETATTRLIST:
        if(native_diagnose_syscalls)trace_getattrlist(r,uc->uc_mcontext.pc);
        result=native_getattrlist(r); break;
    case D_SYS_FSTATFS64: result=native_fstatfs(r[0],r[1]); break;
    case D_SYS_GETFSSTAT64: result=native_getfsstat(r); break;
    case D_SYS_FSGETPATH: result=native_fsgetpath(r); break;
    case D_SYS_GETDIRENTRIES64: result=native_getdirentries(r); break;
    case D_SYS_GETUID: result=RAW(SYS_getuid,0,0,0,0,0,0); break;
    case D_SYS_GETEUID: result=RAW(SYS_geteuid,0,0,0,0,0,0); break;
    case D_SYS_GETGID: result=RAW(SYS_getgid,0,0,0,0,0,0); break;
    case D_SYS_GETEGID: result=RAW(SYS_getegid,0,0,0,0,0,0); break;
    case D_SYS_GETTID:
        /* XNU kern_prot.c:gettid returns ESRCH when this thread has no
         * credential override. No override-setting API is implemented. */
        result=-ESRCH; break;
    case D_SYS_GETTIMEOFDAY: {
        struct { int64_t seconds,microseconds; } host;
        struct { int32_t minutes_west,dst; } timezone;
        result=RAW(SYS_gettimeofday,&host,&timezone,0,0,0,0);
        if (result) break;
        struct darwin_timeval value={host.seconds,host.microseconds,0};
        if ((r[0] && guest_copy(&value,r[0],sizeof(value),1)) ||
            (r[1] && guest_copy(&timezone,r[1],sizeof(timezone),1))) { result=-EFAULT; break; }
        if (r[2]) {
            struct timespec now;
            result=RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0);
            if (result) break;
            uint64_t ticks=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
            result=guest_copy(&ticks,r[2],sizeof(ticks),1);
        }
        break;
    }
    case D_SYS_GETPID: result = RAW(SYS_getpid,0,0,0,0,0,0); break;
    case D_SYS_ISSETUGID:
        /* XNU kern_prot.c: credential-tainted execution, not an errno.
         * Honor Linux's kernel-supplied secure-exec state and current real/
         * effective identities. Guest credential-changing calls are unsupported.
         * In particular ENOSYS (-1) must not masquerade as a true query result
         * and make CoreFoundation discard its explicit application home. */
        result=native_secure_exec || RAW(SYS_getuid,0,0,0,0,0,0)!=RAW(SYS_geteuid,0,0,0,0,0,0) ||
            RAW(SYS_getgid,0,0,0,0,0,0)!=RAW(SYS_getegid,0,0,0,0,0,0);
        break;
    case D_SYS_THREAD_SELFID: result = RAW(SYS_gettid,0,0,0,0,0,0); break; /* thread_selfid */
    case D_SYS_FORK: {
        int child=0;result=native_fork(&child);
        r[1]=child; /* Darwin raw fork child discriminator. */
        break;
    }
    case D_SYS_WAIT4:
        if (r[3] || (r[2] & ~(WNOHANG|WUNTRACED))) { result = -ENOTSUP; break; }
        result = RAW(SYS_wait4,r[0],r[1],r[2],0,0,0);
        break;
    case D_SYS_MUNMAP: case D_SYS_MPROTECT:
        if ((r[0] & (PAGE16-1)) || !r[1] || r[1] > D_VM_MAX_ALLOCATION ||
            r[0] + ((r[1]+PAGE16-1)&~(PAGE16-1)) < r[0]) {
            result = -EINVAL; break;
        }
        if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_MPROTECT && (r[2] & ~(PROT_READ|PROT_WRITE))) { result = -ENOTSUP; break; }
        result = RAW(r[D_ARM64_SYSCALL_REGISTER]==D_SYS_MUNMAP ? SYS_munmap : SYS_mprotect,
                     r[0],(r[1]+PAGE16-1)&~(PAGE16-1),r[2],0,0,0);
        if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_MUNMAP)
            trace_record("DARWIN_MUNMAP address=0x",r[0],r[1],result);
        break;
    case D_SYS_MMAP: result=native_mmap(r); break;
    case D_SYS_MREMAP_ENCRYPTED:
        /* XNU kern_mman.c: CRYPTID_NO_ENCRYPTION is an empty load command.
         * Validate the guest-page/range contract, then leave real plaintext
         * mappings unchanged. Encrypted images are not decrypted or accepted.
         */
        if ((r[0]&(D_GUEST_PAGE_SIZE-1)) || r[1]>UINT64_MAX-r[0]) result=-EINVAL;
        else result=r[2]==0 ? 0 : -ENOTSUP;
        trace_record("DARWIN_MREMAP_ENCRYPTED cryptid=0x",r[2],r[0],result);
        break;
    case D_SYS_PLATFORM:
        if (r[3] == ARLINUX_IOS_PLATFORM_QUERY) {
            result=r[0]==ARLINUX_IOS_PLATFORM_ABI_VERSION && ui.pixels && ui.control ?
                (long)(uintptr_t)&native_ios_platform_api : -ENOTSUP;
        } else if (r[3] == D_PLATFORM_SET_TSD) {
            __asm__ volatile("msr tpidr_el0,%0" :: "r"(r[0]) : "memory");
            result = 0;
        } else if (r[3] == D_PLATFORM_GET_TSD) {
            __asm__ volatile("mrs %0,tpidr_el0" : "=r"(result));
        } else result = -ENOSYS;
        break;
    }
    if (native_diagnose_syscalls && !bsd_error && result==-ENOSYS) {
        trace_unimplemented(r,uc->uc_mcontext.pc);
    }
    trace_call(r[D_ARM64_SYSCALL_REGISTER],uc->uc_mcontext.pc,bsd_error ? -result : result);
    if (bsd_error) {
        r[0]=result;
        uc->uc_mcontext.pstate |= CARRY;
    } else if ((unsigned long)result >= (unsigned long)-4095) {
        r[0] = darwin_error(result);
        uc->uc_mcontext.pstate |= CARRY;
    } else {
        r[0] = result;
        uc->uc_mcontext.pstate &= ~CARRY;
    }
}

static void trace_fault_maps(void)
{
    /* The failed original process is still alive here. Preserve kernel VM
     * ranges/permissions/backing paths, never executable bytes or a replay.
     * One bounded write keeps mapping lines together in the inherited log. */
    char maps[32768];unsigned long used=0;
    long fd=RAW(SYS_openat,AT_FDCWD,"/proc/self/maps",O_RDONLY|O_CLOEXEC,0,0,0);
    if (fd<0) { trace_record("DARWIN_FAULT_MAPS_UNAVAILABLE status=0x",0,0,fd);return; }
    long result=0;
    while (used<sizeof(maps)) {
        result=RAW(SYS_read,fd,maps+used,sizeof(maps)-used,0,0,0);
        if (result==-EINTR) continue;
        if (result<=0) break;
        used+=result;
    }
    RAW(SYS_close,fd,0,0,0,0,0);
    trace_record("DARWIN_FAULT_MAPS_BEGIN bytes=0x",used,0,result);
    if (used) RAW(SYS_write,native_trace_fd,maps,used,0,0,0);
    trace_record("DARWIN_FAULT_MAPS_END truncated=0x",used==sizeof(maps),0,result);
}
static void fault(int signal,siginfo_t *info,void *context)
{
    const char label[]="DARWIN_NATIVE_FAULT signal/pc/address follows\n";
    RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
    ucontext_t *uc=context;
    trace_record("DARWIN_FAULT signal=0x",signal,uc->uc_mcontext.pc,(uintptr_t)info->si_addr);
    const char registers[]="DARWIN_FAULT_REGISTERS x0..x30\n";
    RAW(SYS_write,native_trace_fd,registers,sizeof(registers)-1,0,0,0);
    for (unsigned int i=0;i<31;i++)
        trace_record("DARWIN_FAULT_REGISTER index=0x",i,uc->uc_mcontext.pc,uc->uc_mcontext.regs[i]);
    trace_guest_frames("DARWIN_FAULT_FRAME nr=0x",uc->uc_mcontext.regs);
    trace_fault_maps();
    /* Explicit runner diagnosis only: the journey records the fault as a
     * failure before attaching its existing debugger to this original process.
     * Resume/detach continues to the real fatal exit below. */
    if (native_stop_on_fault) RAW(SYS_kill,RAW(SYS_getpid,0,0,0,0,0,0),SIGSTOP,0,0,0,0);
    RAW(SYS_exit_group,128+signal,0,0,0,0,0);
    __builtin_unreachable();
}


static void session_fault(int signal,siginfo_t *info,void *context)
{
    const char label[]="SESSION_NATIVE_FAULT original session caller retained\n";
    RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
    fault(signal,info,context);
}
void darwin_native_session_diagnostics(void)
{
    /* The existing runner fault collector must cover its owned Mach core too.
     * No guest TLS/filter/alternate stack is installed in native core threads. */
    if(!native_stop_on_fault)return;
    struct sigaction action={.sa_sigaction=session_fault,.sa_flags=SA_SIGINFO};
    sigfillset(&action.sa_mask);
    int failures[]={SIGSEGV,SIGILL,SIGBUS,SIGABRT,SIGTRAP,SIGFPE};
    for(unsigned int i=0;i<sizeof(failures)/sizeof(failures[0]);i++)
        if(sigaction(failures[i],&action,NULL)) {perror("session fault handler");exit(1);}
}

int darwin_native_dyld_info(uint64_t address,uint64_t length)
{
    if(!address || !length || length>D_VM_MAX_ALLOCATION || address>=D_GUEST_COMMPAGE_RO ||
       length>D_GUEST_COMMPAGE_RO-address) return -EINVAL;
#ifdef ARLINUX_DARLING
    if(darling_active) {
        int result=dserver_rpc_set_dyld_info(address,length);
        if(!result) {darling_dyld_address=address;darling_dyld_length=length;}
        trace_record("DARLING_DYLD_INFO bytes=0x",length,address,result);
        return result;
    }
#endif
    return -ENOTSUP;
}
void darwin_native_connect(void)
{
    native_trace_prepare();
    /* Select affinity before starting either native services or guest threads. */
    cpu_set_t available,one;
    if (sched_getaffinity(0,sizeof(available),&available)) { perror("guest CPU affinity"); exit(1); }
    CPU_ZERO(&one);
    for (int cpu=0;cpu<CPU_SETSIZE;cpu++) if (CPU_ISSET(cpu,&available)) { CPU_SET(cpu,&one); break; }
    if (sched_setaffinity(0,sizeof(one),&one)) { perror("single guest CPU"); exit(1); }
    darling_prepare();
}

void darwin_native_prepare(const char *root,const char *program,const char *container,uint64_t cache_base,const unsigned char uuid[16],const struct darwin_code_metadata *codesign)
{
    /* Host libc/TLS is still active before entering the guest. AT_SECURE also
     * covers executable file capabilities and LSM secure-execution policy. */
    errno=0;native_secure_exec=getauxval(AT_SECURE)!=0;
    if(errno) {perror("Linux secure execution state");exit(1);}
    darwin_native_connect();
    native_codesign=*codesign;
    native_threads[0].tid=RAW(SYS_gettid,0,0,0,0,0,0);
    native_threads[0].state=2; native_threads[0].port=(D_THREAD_PORT_INDEX_BASE<<D_PORT_NAME_INDEX_SHIFT)|D_PORT_GENERATION_TAG; native_threads[0].refs=1;
#ifdef ARLINUX_DARLING
    if (darling_active) native_threads[0].port=darling_thread_self();
#endif
    /* The activity namespace belongs to this standalone compatibility machine.
     * MAP_SHARED preserves a single allocator across real fork/COW children. */
    native_activity_ids=mmap(NULL,D_GUEST_PAGE_SIZE,PROT_READ|PROT_WRITE,
                             MAP_SHARED|MAP_ANONYMOUS,-1,0);
    if (native_activity_ids==MAP_FAILED) { perror("activity ID namespace"); exit(1); }
    *native_activity_ids=1;
    current_cache_base=cache_base;
    memcpy(guest_executable_uuid,uuid,sizeof(guest_executable_uuid));
    char *absolute=realpath(program,NULL);
    if (!absolute || strlen(absolute)>=sizeof(executable_path)) {
        fprintf(stderr,"Invalid guest executable path\n"); exit(1);
    }
    memcpy(executable_path,absolute,strlen(absolute)+1);
    memcpy(bundle_path,absolute,strlen(absolute)+1);
    char *slash=strrchr(bundle_path,'/');
    if (!slash) { fprintf(stderr,"Missing application bundle parent\n"); exit(1); }
    *slash=0;
    bundle_path_length=slash-bundle_path;
    bundle_descriptor=open(*bundle_path ? bundle_path : "/",O_PATH|O_DIRECTORY|O_CLOEXEC);
    if (bundle_descriptor<0) { perror("application bundle directory"); exit(1); }
    free(absolute);
    if(container) {
        container_path_length=strlen(container);
        if(!container_path_length || container_path_length>=sizeof(container_path)) {
            fprintf(stderr,"Invalid application container path\n");exit(1);
        }
        memcpy(container_path,container,container_path_length+1);
        container_descriptor=open(container_path,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(container_descriptor<0) {perror("application container");exit(1);}
    }
    if (root) {
        root_descriptor=open(root,O_PATH|O_DIRECTORY|O_CLOEXEC);
        if (root_descriptor<0) { perror("Darwin filesystem root"); exit(1); }
    }
    /* XNU arm/cpu_capabilities.h defines both legacy and immutable commpages.
     * Publish actual configured machine properties at both ABI locations;
     * optional acceleration/timebase features remain disabled. */
    unsigned char *page=mmap((void *)D_GUEST_COMMPAGE,PAGE16,PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if (page==MAP_FAILED) { perror("Darwin commpage"); exit(1); }
    memcpy(page,"commpage 64-bit",14);
    *(uint16_t *)(page+D_COMM_VERSION)=D_COMM_COMPAT_VERSION;
    *(uint64_t *)(page+D_COMM_CPU_CAPABILITIES64)=UINT64_C(1)<<D_COMM_CPU_COUNT_SHIFT;
    *(uint32_t *)(page+D_COMM_CPU_CAPABILITIES)=1U<<D_COMM_CPU_COUNT_SHIFT; /* one configured CPU, conservative features */
    page[D_COMM_USER_PAGE_SHIFT_32]=page[D_COMM_USER_PAGE_SHIFT_64]=D_GUEST_PAGE_SHIFT;
    long cache=sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (cache<=0 || cache>65535) { fprintf(stderr,"Unknown native cache line size\n"); exit(1); }
    *(uint16_t *)(page+D_COMM_CACHE_LINE_SIZE)=(uint16_t)cache;
    page[D_COMM_ACTIVE_CPUS]=page[D_COMM_PHYSICAL_CPUS]=page[D_COMM_LOGICAL_CPUS]=1;
    page[D_COMM_KERNEL_PAGE_SHIFT]=D_GUEST_PAGE_SHIFT;
    long physical=sysconf(_SC_PHYS_PAGES),host_page=sysconf(_SC_PAGESIZE);
    if (physical<=0 || host_page<=0) { fprintf(stderr,"Unknown native memory size\n"); exit(1); }
    *(uint64_t *)(page+D_COMM_MEMORY_SIZE)=(uint64_t)physical*(uint64_t)host_page;
    if (mprotect(page,PAGE16,PROT_READ)) { perror("read-only commpage"); exit(1); }
    unsigned char *immutable=mmap((void *)D_GUEST_COMMPAGE_RO,PAGE16,PROT_READ|PROT_WRITE,
                                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if (immutable==MAP_FAILED) { perror("immutable Darwin commpage"); exit(1); }
    /* Only fields moved to the RO ABI belong here. It is not an alias of the
     * mutable/legacy page; reserved fields and dev-firmware status stay zero. */
    immutable[D_COMM_USER_PAGE_SHIFT_32]=immutable[D_COMM_USER_PAGE_SHIFT_64]=D_GUEST_PAGE_SHIFT;
    immutable[D_COMM_KERNEL_PAGE_SHIFT]=D_GUEST_PAGE_SHIFT;
    if (mprotect(immutable,PAGE16,PROT_READ)) { perror("protect immutable commpage"); exit(1); }
    /* Initialize before the filter is live, on host TLS. The signal handler
     * thereafter uses only the exact allowed raw SVC site. Like tawcroot,
     * compare the address AFTER SVC, as Linux records it in seccomp_data. */
    void *stack = mmap(NULL,D_SIGNAL_STACK_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (stack == MAP_FAILED) { perror("signal stack"); exit(1); }
    stack_t alt = {.ss_sp=stack,.ss_size=D_SIGNAL_STACK_SIZE};
    struct sigaction action = {.sa_sigaction=dispatch,.sa_flags=SA_SIGINFO|SA_ONSTACK};
    sigemptyset(&action.sa_mask);
    if (sigaltstack(&alt,NULL) || sigaction(SIGSYS,&action,NULL)) {
        perror("SIGSYS installation"); exit(1);
    }
    action.sa_sigaction=fault;
    int failures[]={SIGSEGV,SIGILL,SIGBUS,SIGABRT,SIGTRAP,SIGFPE};
    for (unsigned int i=0;i<sizeof(failures)/sizeof(failures[0]);i++) {
        if (sigaction(failures[i],&action,NULL)) { perror("fault handler"); exit(1); }
    }
#ifdef ARLINUX_DARLING
    action.sa_sigaction=darling_suspend;action.sa_flags|=SA_RESTART;
    sigfillset(&action.sa_mask);
    if(sigaction(LINUX_SIGRTMIN,&action,NULL)) {
        perror("Darling suspension handler");exit(1);
    }
#endif
    void *vdso = dlopen("linux-vdso.so.1",RTLD_NOW|RTLD_LOCAL);
    uint32_t *sigreturn = vdso ? dlsym(vdso,"__kernel_rt_sigreturn") : NULL;
    /* Linux ARM64's VDSO ABI is mov x8,#139; svc #0. Allow that exact
     * return PC, not syscall number 139 at arbitrary guest PCs: Darwin x8 is
     * not its syscall selector and may coincidentally contain that value. */
    if (!sigreturn || sigreturn[0] != (D_ARM64_INSN_MOVZ_X8 | (SYS_rt_sigreturn << D_ARM64_MOVZ_IMMEDIATE_SHIFT)) ||
        sigreturn[1] != D_ARM64_INSN_SVC_0) {
        fprintf(stderr,"Unsupported ARM64 signal-return trampoline\n"); exit(1);
    }
    uintptr_t sites[]={(uintptr_t)darwin_raw_return,(uintptr_t)(sigreturn+2),(uintptr_t)darwin_clone_return,
        (uintptr_t)darwin_clone_callback_return};
    struct sock_filter code[3+5*(sizeof(sites)/sizeof(*sites))+1];
    size_t count=0;
    code[count++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,arch));
    code[count++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_AARCH64,1,0);
    code[count++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS);
    for (unsigned int i=0;i<sizeof(sites)/sizeof(*sites);i++) {
        code[count++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer));
        code[count++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)sites[i],0,3);
        code[count++]=(struct sock_filter)BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer)+4);
        code[count++]=(struct sock_filter)BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)(sites[i]>>32),0,1);
        code[count++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    }
    code[count++]=(struct sock_filter)BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_TRAP);
    struct sock_fprog filter_program = {.len=count,.filter=code};
    if (prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) ||
        RAW(SYS_seccomp,SECCOMP_SET_MODE_FILTER,0,&filter_program,0,0,0)) {
        perror("native syscall filter"); exit(1);
    }
}

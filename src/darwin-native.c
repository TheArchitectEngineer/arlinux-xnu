/* Native Darwin ABI transport, inspired by tawcroot's TLS-independent systrap
 * runtime. Linux supplies these process/VM operations; this is not XNU boot.
 * Trusted non-arm64e inputs only. No security sandbox or general JIT support.
 */
#define _GNU_SOURCE
#include "darwin-native.h"
#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
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

extern long darwin_raw(long, long, long, long, long, long, long);
extern char darwin_raw_return[];
#define RAW(n,a,b,c,d,e,f) darwin_raw((n),(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),(long)(f))
#define PAGE16 16384UL
#define CARRY (1UL << 29)
#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

static int darwin_error(long result)
{
    switch (-result) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
    case 10: case 12: case 13: case 14: case 16: case 17: case 19: case 20:
    case 21: case 22: case 23: case 24: case 27: case 28: case 29: case 30:
    case 31: case 32: case 33: case 34: return (int)-result;
    case 11: return 35;           /* EAGAIN */
    case 38: return 78;           /* ENOSYS */
    case 36: return 63;           /* ENAMETOOLONG */
    case 75: return 84;           /* EOVERFLOW */
    case 95: return 45;           /* ENOTSUP */
    default: return 5;            /* unclassified Linux failure -> EIO */
    }
}

static void trace_call(unsigned long number,unsigned long pc,unsigned long result)
{
    /* Bounded, TLS/libc-free diagnosis. No guest buffers or credentials. */
    char line[100];
    const char *labels[] = {"DARWIN_CALL nr=0x"," pc=0x"," result=0x"};
    unsigned long values[] = {number,pc,result};
    const char hex[] = "0123456789abcdef";
    unsigned int n=0;
    for (unsigned int i=0;i<3;i++) {
        for (const char *p=labels[i];*p;p++) line[n++]=*p;
        for (int bit=60;bit>=0;bit-=4) line[n++]=hex[(values[i]>>bit)&15];
    }
    line[n++]='\n';
    RAW(SYS_write,2,line,n,0,0,0);
}

static long anonymous16(unsigned long size, int protection)
{
    /* Until executable mapping rewrite is implemented, never grant unadapted
     * new executable pages. Unknown/file/fixed map modes fail at the caller. */
    if (!size || size > (1UL << 32) || (protection & ~3)) return -22;
    size = (size + PAGE16 - 1) & ~(PAGE16 - 1);
    long raw = RAW(SYS_mmap,0,size+PAGE16,protection,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if ((unsigned long)raw >= (unsigned long)-4095) return raw;
    unsigned long base = ((unsigned long)raw + PAGE16 - 1) & ~(PAGE16 - 1);
    unsigned long prefix = base - raw, suffix = PAGE16 - prefix;
    if ((prefix && RAW(SYS_munmap,raw,prefix,0,0,0,0)) ||
        (suffix && RAW(SYS_munmap,base+size,suffix,0,0,0,0))) {
        RAW(SYS_exit_group,125,0,0,0,0,0);
        __builtin_unreachable();
    }
    return (long)base;
}

/* Read-only filesystem access for genuine dyld dependencies. Darwin paths
 * resolve inside the selected firmware root; only the explicit executable
 * retains its host path. This is path adaptation, not a security sandbox. */
static int root_descriptor=-1;
static uint64_t current_cache_base;
/* Process-local name for this task's VM capability. Only implemented VM traps
 * accept it; other port/IPC operations remain unsupported, never successful. */
#define CURRENT_TASK_PORT 0x103U
static char executable_path[4096];
static long guest_copy(void *local, unsigned long remote, unsigned long length, int write)
{
    struct iovec here={local,length},there={(void *)remote,length};
    long pid=RAW(SYS_getpid,0,0,0,0,0,0);
    long count=RAW(write ? SYS_process_vm_writev : SYS_process_vm_readv,pid,&here,1,&there,1,0);
    return count==(long)length ? 0 : -14;
}
static long guest_path(unsigned long address,char *path)
{
    for (unsigned int i=0;i<4096;i++) {
        if (guest_copy(path+i,address+i,1,0)) return -14;
        if (!path[i]) return 0;
    }
    return -36; /* Linux ENAMETOOLONG, translated below */
}
static int path_directory(char **path)
{
    unsigned int i=0;
    while ((*path)[i] && (*path)[i]==executable_path[i]) i++;
    if (!(*path)[i] && !executable_path[i]) return AT_FDCWD;
    if (root_descriptor>=0) {
        if (**path=='/') (*path)++;
        return root_descriptor;
    }
    return AT_FDCWD;
}
static long guest_open(unsigned long address,unsigned long flags)
{
    /* The firmware is read-only. Unsupported flag semantics are explicit. */
    if (flags & ~(0x100UL|0x100000UL|0x1000000UL)) return -95;
    char buffer[4096],*path=buffer;
    long error=guest_path(address,buffer);
    if (error) return error;
    const char label[]="DARWIN_OPEN ";
    unsigned int length=0; while (buffer[length]) length++;
    RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
    RAW(SYS_write,2,buffer,length,0,0,0);
    RAW(SYS_write,2,"\n",1,0,0,0);
    int directory=path_directory(&path);
    int options=O_RDONLY;
    if (flags&0x100) options|=O_NOFOLLOW;
    if (flags&0x100000) options|=O_DIRECTORY;
    if (flags&0x1000000) options|=O_CLOEXEC;
    return RAW(SYS_openat,directory,path,options,0,0,0);
}
/* XNU bsd/sys/stat.h __DARWIN_STRUCT_STAT64, ARM64 layout. statx avoids
 * depending on the host libc struct-stat ABI inside the TLS-free handler. */
struct darwin_stat64 {
    int32_t device; uint16_t mode,links; uint64_t inode;
    uint32_t uid,gid; int32_t rdevice,padding;
    int64_t times[8];
    int64_t size,blocks; int32_t blocksize; uint32_t flags,generation;
    int32_t spare; int64_t qspare[2];
};
static long guest_stat(unsigned long argument,unsigned long destination,unsigned int kind)
{
    char buffer[4096],*path=buffer;
    int directory,options=0;
    if (kind==339) { directory=(int)argument; path=""; options=AT_EMPTY_PATH; }
    else {
        long error=guest_path(argument,buffer); if (error) return error;
        directory=path_directory(&path);
        if (kind==340) options=AT_SYMLINK_NOFOLLOW;
    }
    struct statx actual={0};
    long error=RAW(SYS_statx,directory,path,options,STATX_BASIC_STATS|STATX_BTIME,&actual,0);
    if (error) return error;
    if (actual.stx_nlink>65535 || actual.stx_dev_major>255 || actual.stx_dev_minor>0xffffff ||
        actual.stx_rdev_major>255 || actual.stx_rdev_minor>0xffffff) return -75;
    struct darwin_stat64 out={0};
    out.device=(actual.stx_dev_major<<24)|actual.stx_dev_minor;
    out.rdevice=(actual.stx_rdev_major<<24)|actual.stx_rdev_minor;
    out.mode=actual.stx_mode; out.links=actual.stx_nlink; out.inode=actual.stx_ino;
    out.uid=actual.stx_uid; out.gid=actual.stx_gid;
    out.times[0]=actual.stx_atime.tv_sec; out.times[1]=actual.stx_atime.tv_nsec;
    out.times[2]=actual.stx_mtime.tv_sec; out.times[3]=actual.stx_mtime.tv_nsec;
    out.times[4]=actual.stx_ctime.tv_sec; out.times[5]=actual.stx_ctime.tv_nsec;
    out.times[6]=actual.stx_btime.tv_sec; out.times[7]=actual.stx_btime.tv_nsec;
    out.size=actual.stx_size; out.blocks=actual.stx_blocks; out.blocksize=actual.stx_blksize;
    return guest_copy(&out,destination,sizeof(out),1);
}

static void dispatch(int signal, siginfo_t *info, void *context)
{
    /* No errno, malloc, pthread, stdio or other host libc operation here.
     * Guest may have replaced TPIDR_EL0 with its own TSD base. */
    ucontext_t *uc = context;
    unsigned long long *r = uc->uc_mcontext.regs;
    if (signal != SIGSYS || info->si_code != SYS_SECCOMP) {
        RAW(SYS_exit_group,126,0,0,0,0,0);
        __builtin_unreachable();
    }
    int32_t trap=(int32_t)r[16];
    if (trap<0 && (uint32_t)trap!=0x80000000U) {
        /* Mach traps do not use BSD's positive errno/carry convention. Until
         * a real port namespace exists, port-returning traps fail with NULL;
         * other unsupported Mach operations return KERN_NOT_SUPPORTED. */
        unsigned long number=-(int64_t)trap;
        if (number==28) {
            r[0]=CURRENT_TASK_PORT;
            trace_call(r[16],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==10 || number==15) { /* current-task anonymous allocate/map */
            uint64_t address=0;
            unsigned long status=4; /* KERN_INVALID_ARGUMENT */
            if (r[0]!=CURRENT_TASK_PORT) status=16; /* KERN_INVALID_TASK */
            else if (guest_copy(&address,r[1],sizeof(address),0)) status=1;
            else if (((number==10 ? r[3] : r[4])&~0xff000001UL)==0 &&
                     ((number==10 ? r[3] : r[4])&1) && r[2] &&
                     (number==10 || (!r[3] && !(r[5]&~3UL)))) {
                long mapped=anonymous16(r[2],number==10 ? PROT_READ|PROT_WRITE : r[5]);
                if ((unsigned long)mapped>=(unsigned long)-4095) status=3;
                else {
                    address=mapped;
                    status=guest_copy(&address,r[1],sizeof(address),1) ? 1 : 0;
                    if (status) RAW(SYS_munmap,mapped,(r[2]+PAGE16-1)&~(PAGE16-1),0,0,0,0);
                }
            }
            r[0]=status;
            trace_call(r[16],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==12) {       /* _kernelrpc_mach_vm_deallocate_trap */
            unsigned long start=r[1]&~(PAGE16-1),offset=r[1]-start;
            unsigned long size=r[2];
            if (r[0]!=CURRENT_TASK_PORT) r[0]=16;
            else if (size>(1UL<<32) || start+size+offset+PAGE16<start) r[0]=4;
            else if (!size) r[0]=0;
            else r[0]=RAW(SYS_munmap,start,(size+offset+PAGE16-1)&~(PAGE16-1),0,0,0,0) ? 1 : 0;
            trace_call(r[16],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==3 || number==4) {
            /* ARM fast-time calls return a value, not kern_return_t/errno.
             * Use genuine nanosecond clocks, paired with timebase 1/1. */
            struct timespec now;
            long error=RAW(SYS_clock_gettime,number==3 ? CLOCK_MONOTONIC : CLOCK_BOOTTIME,&now,0,0,0,0);
            if (error) {
                trace_call(r[16],uc->uc_mcontext.pc,error);
                RAW(SYS_exit_group,125,0,0,0,0,0);
                __builtin_unreachable();
            }
            r[0]=(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;
            trace_call(r[16],uc->uc_mcontext.pc,r[0]);
            return;
        }
        if (number==89) {
            const uint32_t scale[2]={1,1};
            struct iovec local={(void *)scale,sizeof(scale)},remote={(void *)r[0],sizeof(scale)};
            long size=RAW(SYS_process_vm_writev,RAW(SYS_getpid,0,0,0,0,0,0),&local,1,&remote,1,0);
            r[0]=size==(long)sizeof(scale) ? 0 : 1; /* KERN_INVALID_ADDRESS */
            trace_call(r[16],uc->uc_mcontext.pc,r[0]);
            return;
        }
        r[0]=(number>=26 && number<=29) ? 0 : 46;
        trace_call(r[16],uc->uc_mcontext.pc,r[0]);
        return;
    }
    long result = -38;
    switch (r[16]) {
    case 381: {                /* __mac_syscall: compatibility dyld policy only */
        char policy[5];
        struct iovec local={policy,sizeof(policy)},remote={(void *)r[0],sizeof(policy)};
        long pid=RAW(SYS_getpid,0,0,0,0,0,0);
        if (RAW(SYS_process_vm_readv,pid,&local,1,&remote,1,0)!=(long)sizeof(policy)) {
            result=-14; break;
        }
        if (r[1]!=90 || policy[0]!='A' || policy[1]!='M' || policy[2]!='F' ||
            policy[3]!='I' || policy[4]) break;
        /* Confirmed from the actual 650.3.4 wrapper: two uint64_t words,
         * input flags and a POINTER to the uint64_t result, not inline output.
         * This environment permits @paths, path variables, custom caches,
         * fallback paths, diagnostic variables and failed insertions. It does
         * not claim to implement Apple's AMFI authentication or other calls. */
        uint64_t args[2];
        local=(struct iovec){args,sizeof(args)};
        remote=(struct iovec){(void *)r[2],sizeof(args)};
        if (RAW(SYS_process_vm_readv,pid,&local,1,&remote,1,0)!=(long)sizeof(args)) {
            result=-14; break;
        }
        uint64_t permissions=0x3f;
        local=(struct iovec){&permissions,sizeof(permissions)};
        remote=(struct iovec){(void *)args[1],sizeof(permissions)};
        result=RAW(SYS_process_vm_writev,pid,&local,1,&remote,1,0)==(long)sizeof(permissions) ? 0 : -14;
        break;
    }
    case 500: {                 /* Darwin getentropy: success returns zero */
        if (r[1]>256) { result=-5; break; }
        unsigned long done=0;
        while (done<r[1]) {
            long count=RAW(SYS_getrandom,r[0]+done,r[1]-done,0,0,0,0);
            if (count==-4) continue; /* retry EINTR without host errno/TLS */
            if (count<0) { result=count; break; }
            if (!count) { result=-5; break; }
            done+=(unsigned long)count;
        }
        if (done==r[1]) result=0;
        break;
    }
    case 520: case 521: {       /* retain actual dyld fatal reason, no fake success */
        char text[512];
        struct iovec local={text,sizeof(text)};
        struct iovec remote={(void *)r[r[16]==521 ? 4 : 5],sizeof(text)};
        long size=RAW(SYS_process_vm_readv,RAW(SYS_getpid,0,0,0,0,0,0),&local,1,&remote,1,0);
        if (size>0) {
            unsigned long length=0;
            while (length<(unsigned long)size && text[length]) length++;
            const char label[]="DARWIN_ABORT_REASON ";
            RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
            RAW(SYS_write,2,text,length,0,0,0);
            RAW(SYS_write,2,"\n",1,0,0,0);
        }
        break;
    }
    case 1:
        trace_call(r[16],uc->uc_mcontext.pc,r[0]&255);
        RAW(SYS_exit_group,r[0]&255,0,0,0,0,0);
        __builtin_unreachable();
    case 3: result = RAW(SYS_read,r[0],r[1],r[2],0,0,0); break;
    case 4: result = RAW(SYS_write,r[0],r[1],r[2],0,0,0); break;
    case 5: case 398: result=guest_open(r[0],r[1]); break;
    case 6: case 399:
        result=(int)r[0]==root_descriptor ? -9 : RAW(SYS_close,r[0],0,0,0,0,0); break;
    case 153: case 414: result=RAW(SYS_pread64,r[0],r[1],r[2],r[3],0,0); break;
    case 199: result=RAW(SYS_lseek,r[0],r[1],r[2],0,0,0); break;
    case 294: /* Actual process-private cache mappings prepared before entry. */
        result=current_cache_base ? guest_copy(&current_cache_base,r[0],sizeof(current_cache_base),1) : -12;
        break;
    case 338: case 339: case 340: result=guest_stat(r[0],r[1],r[16]); break;
    case 20: result = RAW(SYS_getpid,0,0,0,0,0,0); break;
    case 372: result = RAW(SYS_gettid,0,0,0,0,0,0); break; /* thread_selfid */
    case 2:
        result = RAW(SYS_clone,SIGCHLD,0,0,0,0,0);
        if (!result) {
            r[1] = 1;           /* Darwin raw fork child discriminator */
            result = RAW(SYS_getpid,0,0,0,0,0,0);
        } else r[1] = 0;
        break;
    case 7:
        if (r[3] || (r[2] & ~3UL)) { result = -95; break; }
        result = RAW(SYS_wait4,r[0],r[1],r[2],0,0,0);
        break;
    case 73: case 74:
        if ((r[0] & (PAGE16-1)) || !r[1] || r[1] > (1UL << 32) ||
            r[0] + ((r[1]+PAGE16-1)&~(PAGE16-1)) < r[0]) {
            result = -22; break;
        }
        if (r[16] == 74 && (r[2] & ~3UL)) { result = -95; break; }
        result = RAW(r[16] == 73 ? SYS_munmap : SYS_mprotect,
                     r[0],(r[1]+PAGE16-1)&~(PAGE16-1),r[2],0,0,0);
        break;
    case 197:
        if (r[0] || r[3] != 0x1002 || r[4] != (unsigned long)-1 || r[5]) {
            result = -95; break;
        }
        result = anonymous16(r[1],r[2]);
        break;
    case 0x80000000:
        if (r[3] == 2) {
            __asm__ volatile("msr tpidr_el0,%0" :: "r"(r[0]) : "memory");
            result = 0;
        } else if (r[3] == 3) {
            __asm__ volatile("mrs %0,tpidr_el0" : "=r"(result));
        } else result = -38;
        break;
    }
    trace_call(r[16],uc->uc_mcontext.pc,result);
    if ((unsigned long)result >= (unsigned long)-4095) {
        r[0] = darwin_error(result);
        uc->uc_mcontext.pstate |= CARRY;
    } else {
        r[0] = result;
        uc->uc_mcontext.pstate &= ~CARRY;
    }
}

static void fault(int signal,siginfo_t *info,void *context)
{
    const char label[]="DARWIN_NATIVE_FAULT signal/pc/address follows\n";
    RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
    ucontext_t *uc=context;
    trace_call(signal,uc->uc_mcontext.pc,(uintptr_t)info->si_addr);
    const char registers[]="DARWIN_FAULT_REGISTERS x0..x30\n";
    RAW(SYS_write,2,registers,sizeof(registers)-1,0,0,0);
    for (unsigned int i=0;i<31;i++)
        trace_call(i,uc->uc_mcontext.pc,uc->uc_mcontext.regs[i]);
    RAW(SYS_exit_group,128+signal,0,0,0,0,0);
    __builtin_unreachable();
}

void darwin_native_prepare(const char *root,const char *program,uint64_t cache_base)
{
    current_cache_base=cache_base;
    char *absolute=realpath(program,NULL);
    if (!absolute || strlen(absolute)>=sizeof(executable_path)) {
        fprintf(stderr,"Invalid guest executable path\n"); exit(1);
    }
    memcpy(executable_path,absolute,strlen(absolute)+1); free(absolute);
    if (root) {
        root_descriptor=open(root,O_PATH|O_DIRECTORY|O_CLOEXEC);
        if (root_descriptor<0) { perror("Darwin filesystem root"); exit(1); }
    }
    /* Legacy ARM64 Darwin commpage ABI. These are real properties of the
     * declared compatibility machine, not fabricated hardware capabilities.
     * Leave optional acceleration/timebase features disabled. XNU's ARM
     * cpu_capabilities.h defines the offsets; iOS 12 accesses kernel shift
     * at +0x37 during dyld mach_init, as observed in the failed actual run. */
    unsigned char *page=mmap((void *)0xfffffc000ULL,PAGE16,PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if (page==MAP_FAILED) { perror("Darwin commpage"); exit(1); }
    memcpy(page,"commpage 64-bit",14);
    *(uint16_t *)(page+0x1e)=3;
    *(uint32_t *)(page+0x20)=1U<<16; /* one configured CPU, conservative features */
    page[0x24]=page[0x25]=14;
    long cache=sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (cache<=0 || cache>65535) { fprintf(stderr,"Unknown native cache line size\n"); exit(1); }
    *(uint16_t *)(page+0x26)=(uint16_t)cache;
    page[0x34]=page[0x35]=page[0x36]=1;
    page[0x37]=14;
    long physical=sysconf(_SC_PHYS_PAGES),host_page=sysconf(_SC_PAGESIZE);
    if (physical<=0 || host_page<=0) { fprintf(stderr,"Unknown native memory size\n"); exit(1); }
    *(uint64_t *)(page+0x38)=(uint64_t)physical*(uint64_t)host_page;
    if (mprotect(page,PAGE16,PROT_READ)) { perror("read-only commpage"); exit(1); }
    /* Initialize before the filter is live, on host TLS. The signal handler
     * thereafter uses only the exact allowed raw SVC site. Like tawcroot,
     * compare the address AFTER SVC, as Linux records it in seccomp_data. */
    void *stack = mmap(NULL,128*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (stack == MAP_FAILED) { perror("signal stack"); exit(1); }
    stack_t alt = {.ss_sp=stack,.ss_size=128*1024};
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
    uintptr_t allowed = (uintptr_t)darwin_raw_return;
    void *vdso = dlopen("linux-vdso.so.1",RTLD_NOW|RTLD_LOCAL);
    uint32_t *sigreturn = vdso ? dlsym(vdso,"__kernel_rt_sigreturn") : NULL;
    /* Linux ARM64's VDSO ABI is mov x8,#139; svc #0. Allow that exact
     * return PC, not syscall number 139 at arbitrary guest PCs: Darwin x8 is
     * not its syscall selector and may coincidentally contain that value. */
    if (!sigreturn || sigreturn[0] != (0xd2800008U | (SYS_rt_sigreturn << 5)) ||
        sigreturn[1] != 0xd4000001U) {
        fprintf(stderr,"Unsupported ARM64 signal-return trampoline\n"); exit(1);
    }
    uintptr_t restored = (uintptr_t)(sigreturn+2);
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,arch)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_AARCH64,1,0),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)allowed,0,2),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer)+4),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)(allowed>>32),5,0),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)restored,0,2),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,instruction_pointer)+4),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(uint32_t)(restored>>32),1,0),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_TRAP),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
    };
    struct sock_fprog filter_program = {.len=sizeof(code)/sizeof(code[0]),.filter=code};
    if (prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) ||
        RAW(SYS_seccomp,SECCOMP_SET_MODE_FILTER,0,&filter_program,0,0,0)) {
        perror("native syscall filter"); exit(1);
    }
}

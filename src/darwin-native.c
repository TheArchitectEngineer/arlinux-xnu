/* Native Darwin ABI transport, inspired by tawcroot's TLS-independent systrap
 * runtime. Linux supplies these process/VM operations; this is not XNU boot.
 * Trusted static non-arm64e programs only. No security sandbox, dyld or JIT yet.
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
#include <ucontext.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>

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
    if ((int64_t)r[16] < 0) {
        /* Mach traps do not use BSD's positive errno/carry convention. Until
         * a real port namespace exists, port-returning traps fail with NULL;
         * other unsupported Mach operations return KERN_NOT_SUPPORTED. */
        unsigned long number=-(int64_t)r[16];
        r[0]=(number>=26 && number<=29) ? 0 : 46;
        trace_call(r[16],uc->uc_mcontext.pc,r[0]);
        return;
    }
    long result = -38;
    switch (r[16]) {
    case 1:
        trace_call(r[16],uc->uc_mcontext.pc,r[0]&255);
        RAW(SYS_exit_group,r[0]&255,0,0,0,0,0);
        __builtin_unreachable();
    case 3: result = RAW(SYS_read,r[0],r[1],r[2],0,0,0); break;
    case 4: result = RAW(SYS_write,r[0],r[1],r[2],0,0,0); break;
    case 20: result = RAW(SYS_getpid,0,0,0,0,0,0); break;
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
    RAW(SYS_exit_group,128+signal,0,0,0,0,0);
    __builtin_unreachable();
}

void darwin_native_prepare(void)
{
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
    int failures[]={SIGSEGV,SIGILL,SIGBUS,SIGABRT};
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
    struct sock_fprog program = {.len=sizeof(code)/sizeof(code[0]),.filter=code};
    if (prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) ||
        RAW(SYS_seccomp,SECCOMP_SET_MODE_FILTER,0,&program,0,0,0)) {
        perror("native syscall filter"); exit(1);
    }
}

#define _GNU_SOURCE
#include <asm/ptrace.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

/* Diagnostic for the future XNU host backend, not a kernel implementation. */
#ifndef NT_ARM_SYSTEM_CALL
#define NT_ARM_SYSTEM_CALL 0x404
#endif
#define GUEST_PAGE 16384
#define DARWIN_GETPID 20
#define GUEST_PID 424242
static void fail(const char *where) { perror(where); exit(1); }
static void require(int ok, const char *where) { if (!ok) { fprintf(stderr,"FAIL %s\n",where); exit(1); } }
static void registers(pid_t pid, struct user_pt_regs *r, int write) {
    struct iovec io={r,sizeof(*r)};
    if (ptrace(write ? PTRACE_SETREGSET : PTRACE_GETREGSET,pid,(void *)NT_PRSTATUS,&io)<0) fail("registers");
}
static void page_probe(void) {
    long host=sysconf(_SC_PAGESIZE);
    require(host>0 && host<=GUEST_PAGE && GUEST_PAGE%host==0,"host pages must divide guest 16K");
    int fd=memfd_create("xnu-16k-probe",MFD_CLOEXEC);
    if(fd<0 || ftruncate(fd,GUEST_PAGE)<0) fail("guest page backing");
    unsigned char *a=mmap(NULL,GUEST_PAGE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    unsigned char *b=mmap(NULL,GUEST_PAGE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(a==MAP_FAILED || b==MAP_FAILED) fail("guest page mapping");
    for(size_t i=0;i<GUEST_PAGE;i++) a[i]=(unsigned char)(i*17+3);
    require(!memcmp(a,b,GUEST_PAGE),"shared 16K page contents");
    if(mprotect(b,GUEST_PAGE,PROT_READ)<0) fail("16K protection");
    pid_t p=fork(); if(p<0) fail("fork page probe");
    if(!p) { b[GUEST_PAGE-1]=1; _exit(1); }
    int status; if(waitpid(p,&status,0)<0) fail("wait page probe");
    require(WIFSIGNALED(status) && WTERMSIG(status)==SIGSEGV,"16K write protection fault");
    munmap(a,GUEST_PAGE); munmap(b,GUEST_PAGE); close(fd);
    printf("PASS guest_page=16384 host_page=%ld shared_mapping_and_protection\n",host);
}
static void trap_probe(void) {
    pid_t child=fork(); if(child<0) fail("fork trap probe");
    if(!child) {
        int guard=memfd_create("syscall-side-effect-guard",MFD_CLOEXEC);
        if(guard<0 || ptrace(PTRACE_TRACEME,0,0,0)<0) _exit(2);
        raise(SIGSTOP);
        register uint64_t x0 __asm__("x0")=(uint64_t)guard;
        register uint64_t x8 __asm__("x8")=SYS_close;
        register uint64_t x16 __asm__("x16")=DARWIN_GETPID;
        uint64_t flags;
        __asm__ volatile("cmp xzr,xzr\n\tsvc #0x80\n\tmrs %3,nzcv" : "+r"(x0), "+r"(x8), "+r"(x16), "=r"(flags) :: "memory","cc");
        int returned=(x0==GUEST_PID && !(flags&(1UL<<29)));
        int suppressed=(fcntl(guard,F_GETFD)!=-1);
        _exit(returned && suppressed ? 0 : 3);
    }
    int status; if(waitpid(child,&status,0)<0) fail("initial wait");
    require(WIFSTOPPED(status),"initial child stop");
    if(ptrace(PTRACE_SETOPTIONS,child,0,PTRACE_O_TRACESYSGOOD|PTRACE_O_EXITKILL)<0) fail("ptrace options");
    int entering=1,pending=0,handled=0;
    while(1) {
        if(ptrace(PTRACE_SYSCALL,child,0,0)<0) fail("ptrace resume");
        if(waitpid(child,&status,0)<0) fail("ptrace wait");
        if(WIFEXITED(status)) { require(WEXITSTATUS(status)==0 && handled==1,"Darwin child return"); break; }
        require(WIFSTOPPED(status),"unexpected child termination");
        int sig=WSTOPSIG(status);
        require(sig==(SIGTRAP|0x80),"unexpected child signal");
        struct user_pt_regs r; registers(child,&r,0);
        if(entering) {
            errno=0;
            long instruction=ptrace(PTRACE_PEEKTEXT,child,(void *)(r.pc-4),0);
            if(instruction==-1 && errno) fail("read svc instruction");
            if((uint32_t)instruction==0xd4001001 && r.regs[16]==DARWIN_GETPID) {
                /* Linux reads x8, Darwin reads x16. Suppress Linux execution. */
                int nr=-1; struct iovec io={&nr,sizeof(nr)};
                if(ptrace(PTRACE_SETREGSET,child,(void *)NT_ARM_SYSTEM_CALL,&io)<0) fail("cancel Linux syscall");
                pending=1;
            }
        } else if(pending) {
            /* ARM64 syscall=-1 may retain x0 instead of returning ENOSYS.
             * The child verifies suppression by checking close(fd) had no effect. */
            printf("TRACE skipped syscall x0=%lld\n",(long long)r.regs[0]);
            r.regs[0]=GUEST_PID; r.pstate &= ~(1UL<<29);
            registers(child,&r,1); pending=0; handled++;
        }
        entering=!entering;
    }
    printf("PASS Darwin svc80 x16_dispatch Linux_suppression x0_return carry_flag\n");
}
int main(void) { setbuf(stdout,NULL); page_probe(); trap_probe(); puts("HOST_SUBSTRATE_OK (not userspace XNU)"); }

#define _GNU_SOURCE
#include <asm/ptrace.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

/* Experimental freestanding Mach-O host; no XNU kernel is linked yet.
 * Not a security sandbox. Run only the repository's own diagnostic guest. */
static pid_t tracee=-1;
static void die(const char *message) {
    fprintf(stderr,"ERROR %s (errno=%d %s)\n",message,errno,strerror(errno));
    if(tracee>0) { kill(tracee,SIGKILL); waitpid(tracee,NULL,0); }
    exit(1);
}
static void need(int condition,const char *message) { if(!condition) { errno=0; die(message); } }
#include "macho-image.h"
__attribute__((noreturn)) void enter_guest(uint64_t stack,uint64_t entry);
__asm__(".text\n.global enter_guest\n.type enter_guest,%function\nenter_guest:\nmov sp,x0\nmov x29,xzr\nmov x30,xzr\nbr x1\n");
static void regs(pid_t p,struct user_pt_regs *r,int write) {
    struct iovec io={r,sizeof(*r)};
    if(ptrace(write?PTRACE_SETREGSET:PTRACE_GETREGSET,p,(void *)NT_PRSTATUS,&io)<0) die("trace register access");
}
static int guest_range(const struct image *im,uint64_t addr,uint64_t n,int prot) {
    for(size_t j=0;j<im->count;j++) {
        const struct segment *s=&im->segments[j];
        if((s->prot&prot)==(uint32_t)prot && addr>=s->address && range(addr-s->address,n,s->size)) return 1;
    }
    return 0;
}
static int read_guest(pid_t child,uint64_t address,void *buffer,size_t size) {
    struct iovec local={buffer,size},remote={(void *)address,size};
    return process_vm_readv(child,&local,1,&remote,1,0)==(ssize_t)size;
}
static int run_guest(struct image *im) {
    pid_t child=fork(); if(child<0) die("fork guest");
    if(!child) {
        map_guest(im);
        size_t n=256*1024;
        void *raw=mmap(NULL,n+PAGE16,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(raw==MAP_FAILED) die("guest stack");
        uint64_t bottom=((uintptr_t)raw+PAGE16-1)&~(PAGE16-1);
        uint64_t top=bottom+n-16; *(uint64_t *)top=0; // argc=0, otherwise zeroed stack.
        if(ptrace(PTRACE_TRACEME,0,0,0)<0) die("guest trace permission");
        raise(SIGSTOP);
        enter_guest(top,im->entry);
    }
    tracee=child;
    int status; if(waitpid(child,&status,0)<0) die("initial guest wait");
    need(WIFSTOPPED(status) && WSTOPSIG(status)==SIGSTOP,"guest initialization failed");
    if(ptrace(PTRACE_SETOPTIONS,child,0,PTRACE_O_TRACESYSGOOD|PTRACE_O_EXITKILL)<0) die("trace options");
    int entering=1,pending=0,in_guest=0; uint64_t result=0; int error=0; size_t calls=0;
    while(1) {
        if(ptrace(PTRACE_SYSCALL,child,0,0)<0) die("resume guest");
        if(waitpid(child,&status,0)<0) die("wait guest");
        if(WIFSIGNALED(status)) { fprintf(stderr,"guest signal=%d\n",WTERMSIG(status)); die("guest died without Darwin exit"); }
        need(!WIFEXITED(status),"unexpected Linux exit");
        need(WIFSTOPPED(status) && WSTOPSIG(status)==(SIGTRAP|0x80),"unexpected guest fault/signal");
        struct user_pt_regs r; regs(child,&r,0);
        if(entering) {
            uint32_t instruction=0;
            if(r.pc>=4 && guest_range(im,r.pc-4,4,4)) {
                need(read_guest(child,r.pc-4,&instruction,4),"read guest instruction");
                need(instruction==0xd4001001,"only Darwin svc #0x80 is supported");
                in_guest=1; pending=1; calls++;
                int nr=-1; struct iovec io={&nr,sizeof(nr)};
                if(ptrace(PTRACE_SETREGSET,child,(void *)NT_ARM_SYSTEM_CALL,&io)<0) die("suppress Linux syscall");
                result=0; error=0;
                fprintf(stderr,"Darwin syscall=%" PRIu64 " x0=%" PRIu64 "\n",(uint64_t)r.regs[16],(uint64_t)r.regs[0]);
                switch(r.regs[16]) {
                case 1: {
                    int code=(int)(r.regs[0]&255);
                    kill(child,SIGKILL); waitpid(child,&status,0); tracee=-1;
                    fprintf(stderr,"DARWIN_GUEST_EXIT status=%d syscalls=%zu (host shim, not XNU)\n",code,calls);
                    return code;
                }
                case 4: {
                    if(r.regs[0]!=1 && r.regs[0]!=2) { result=9; error=1; break; }
                    size_t n=r.regs[2];
                    if(n>1024*1024 || !guest_range(im,r.regs[1],n,1)) { result=14; error=1; break; }
                    unsigned char *buffer=malloc(n?n:1); if(!buffer) die("write buffer");
                    if(!read_guest(child,r.regs[1],buffer,n)) { free(buffer); result=14; error=1; break; }
                    ssize_t wrote=write((int)r.regs[0],buffer,n); free(buffer);
                    if(wrote<0) { result=5; error=1; } else result=(uint64_t)wrote;
                    break;
                }
                case 20: result=(uint64_t)child; break;
                default: result=78; error=1; break; // Darwin ENOSYS, not Linux ENOSYS.
                }
            } else need(!in_guest,"guest attempted syscall outside executable Mach-O mapping");
        } else if(pending) {
            r.regs[0]=result;
            if(error) r.pstate|=CARRY; else r.pstate&=~CARRY;
            regs(child,&r,1); pending=0;
        }
        entering=!entering;
    }
}
int main(int argc,char **argv) {
    if(argc!=2) { fprintf(stderr,"usage: macho-host FREESTANDING_ARM64_EXECUTABLE\n"); return 2; }
    long host=sysconf(_SC_PAGESIZE);
    need(host>0 && (unsigned long)host<=PAGE16 && PAGE16%(unsigned long)host==0,"host pages must divide 16K");
    struct image im={0}; load_image(argv[1],&im,0); return run_guest(&im);
}

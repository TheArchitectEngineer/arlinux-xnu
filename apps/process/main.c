/* Darwin runtime capability, not a claim that App Store apps normally fork. */
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <mach/mach.h>
static int own_task(void) {
    audit_token_t token={{0}};mach_msg_type_number_t count=TASK_AUDIT_TOKEN_COUNT;
    kern_return_t error=task_info(mach_task_self(),TASK_AUDIT_TOKEN,(task_info_t)&token,&count);
    if(error || count!=TASK_AUDIT_TOKEN_COUNT || token.val[5]!=(unsigned int)getpid())return 0;
    mach_port_t port=MACH_PORT_NULL;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port))return 0;
    return mach_port_mod_refs(mach_task_self(),port,MACH_PORT_RIGHT_RECEIVE,-1)==KERN_SUCCESS;
}
int main(void) {
    volatile int *memory=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (memory==MAP_FAILED) return 31;
    volatile int *shared=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
    if (shared==MAP_FAILED || !own_task())return 35;
    *memory=11;*shared=17;pid_t child=fork();
    if (!child) {
        if(!own_task())_exit(36);
        *memory=99;*shared=42;_exit(37);
    }
    int status=0;
    if (child<0 || waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=37 ||
        *memory!=11 || *shared!=42 || !own_task())return 32;
    if (munmap((void *)memory,16384) || munmap((void *)shared,16384)) return 33;
    puts("Process COW: 11 shared: 42 child: 37"); fflush(stdout);
    return getchar()=='q' ? 0 : 34;
}

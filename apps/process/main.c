/* Darwin runtime capability, not a claim that App Store apps normally fork. */
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <mach/mach.h>
#include <mach/memory_object_types.h>
static int own_task(void) {
    audit_token_t token={{0}};mach_msg_type_number_t count=TASK_AUDIT_TOKEN_COUNT;
    kern_return_t error=task_info(mach_task_self(),TASK_AUDIT_TOKEN,(task_info_t)&token,&count);
    if(error || count!=TASK_AUDIT_TOKEN_COUNT || token.val[5]!=(unsigned int)getpid())return 0;
    mach_port_t port=MACH_PORT_NULL;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port))return 0;
    return mach_port_mod_refs(mach_task_self(),port,MACH_PORT_RIGHT_RECEIVE,-1)==KERN_SUCCESS;
}
/* The user-requested memory command exercises the same exported Mach backing
 * used by framework IPC. It does not certify unrelated processes' port transfer. */
static int named_memory_command(void) {
    vm_address_t source=0,alias=0;mach_port_t entry=MACH_PORT_NULL;
    memory_object_size_t size=16384;int result=40,initial=0,status=0;
    kern_return_t error=vm_allocate(mach_task_self(),&source,size,VM_FLAGS_ANYWHERE);
    if(error)goto done;
    *(volatile int *)(uintptr_t)source=23;
    error=mach_make_memory_entry_64(mach_task_self(),&size,source,
        VM_PROT_READ|VM_PROT_WRITE|VM_PROT_IS_MASK|MAP_MEM_VM_SHARE|MAP_MEM_USE_DATA_ADDR,
        &entry,MACH_PORT_NULL);
    if(error){result=41;goto done;}
    error=vm_map(mach_task_self(),&alias,size,0,VM_FLAGS_ANYWHERE|VM_FLAGS_RETURN_DATA_ADDR,entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_SHARE);
    if(error){result=42;goto done;}
    initial=*(volatile int *)(uintptr_t)alias;
    if(initial!=23){result=43;goto done;}
    *(volatile int *)(uintptr_t)alias=31;
    if(*(volatile int *)(uintptr_t)source!=31){result=44;goto done;}
    error=mach_port_deallocate(mach_task_self(),entry);
    if(error){result=45;goto done;}entry=MACH_PORT_NULL;
    pid_t child=fork();
    if(!child) {
        if(!own_task() || *(volatile int *)(uintptr_t)alias!=31)_exit(38);
        *(volatile int *)(uintptr_t)alias=57;_exit(39);
    }
    if(child<0 || waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=39 ||
       *(volatile int *)(uintptr_t)source!=57 || *(volatile int *)(uintptr_t)alias!=57){result=46;goto done;}
    *(volatile int *)(uintptr_t)source=61;
    if(*(volatile int *)(uintptr_t)alias!=61){result=47;goto done;}
    printf("Process Mach shared: initial %d source %d alias %d child %d\n",initial,
        *(volatile int *)(uintptr_t)source,*(volatile int *)(uintptr_t)alias,WEXITSTATUS(status));
    fflush(stdout);result=0;
done:
    if(result)fprintf(stderr,"Process Mach failure stage=%d kernel=%d\n",result,error);
    if(entry!=MACH_PORT_NULL && mach_port_deallocate(mach_task_self(),entry) && !result)result=48;
    if(alias && vm_deallocate(mach_task_self(),alias,size) && !result)result=49;
    if(source && vm_deallocate(mach_task_self(),source,size) && !result)result=50;
    return result;
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
    for(;;) {
        int command=getchar();
        if(command=='q')return 0;
        if(command!='m')return 34;
        int result=named_memory_command();if(result)return result;
    }
}

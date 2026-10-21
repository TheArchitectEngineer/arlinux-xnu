#define _GNU_SOURCE
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include "darwin-native.h"
#include "ios-platform-abi.h"
#ifdef ARLINUX_DARLING
#include "../components/darling/session-core.h"
#endif

static void die(const char *message)
{
    fprintf(stderr,"ERROR %s: %s\n",message,strerror(errno));
    exit(1);
}
static void need(int ok,const char *message)
{
    if (!ok) { fprintf(stderr,"ERROR %s\n",message);exit(1); }
}
#include "native-display.h"
#include "macho-image.h"

/* Mach-O section_64. Rewrite only sections explicitly marked instructions,
 * never cstrings, linkedit or other data. Initial scope is our static ARM64
 * program; arbitrary literal pools, dylibs, runtime code and arm64e are not
 * established. No guest file/signature is changed on disk. */
#include "arm64-adapt.h"
static void rewrite_tls(struct image *im)
{
    struct header h; memcpy(&h,im->bytes,sizeof(h));
    size_t pos=sizeof(h),rewritten=0;
    for (uint32_t i=0;i<h.ncmds;i++) {
        struct command cmd; memcpy(&cmd,im->bytes+pos,sizeof(cmd));
        if (cmd.cmd==LC_SEGMENT_64) {
            struct segment s; memcpy(&s,im->bytes+pos,sizeof(s));
            s.address+=im->slide;
            need(s.nsects<=128 && sizeof(s)+(uint64_t)s.nsects*sizeof(struct section64)<=cmd.size,
                 "section table bounds");
            if (s.prot&4) {
                need(mprotect((void *)s.address,round16(s.size),PROT_READ|PROT_WRITE)==0,
                     "make code writable for TLS adaptation");
                for (uint32_t j=0;j<s.nsects;j++) {
                    struct section64 section;
                    memcpy(&section,im->bytes+pos+sizeof(s)+j*sizeof(section),sizeof(section));
                    if (!(section.flags&0x80000400U)) continue;
                    section.address+=im->slide;
                    need(!(section.address&3) && !(section.size&3) &&
                         section.address>=s.address && range(section.address-s.address,section.size,s.filesize) &&
                         section.offset==s.offset+section.address-s.address,
                         "instruction section bounds/alignment");
                    uint32_t *instructions=(void *)section.address;
                    rewritten+=rewrite_instruction_words(instructions,section.size/4);
                }
                __builtin___clear_cache((void *)s.address,(void *)(s.address+s.size));
                need(mprotect((void *)s.address,round16(s.size),s.prot)==0,"restore code protection");
            }
        }
        pos+=cmd.size;
    }
    fprintf(stderr,"NATIVE_TLS_REWRITE instructions=%zu guest_page=16384 cpu_count=1\n",rewritten);
}

#include "dyld-cache.h"
#include "native-launch.h"

int main(int argc,char **argv)
{
    if (argc==3 && !strcmp(argv[1],"--session")) {
#ifdef ARLINUX_DARLING
        /* Set CPU affinity before constructing any Mach worker threads. The
         * first connect has no external service configured and only sets CPU. */
        if (getenv("ARLINUX_DARLING_PREFIX") || getenv("ARLINUX_DARLING_PID")) {
            fprintf(stderr,"session owns its Mach core; external service environment is invalid\n");
            return 2;
        }
        darwin_native_connect();
        int status=arlinux_session_core_start(argv[2]);
        if (status) { errno=-status; die("start session Mach core"); }
        char pid[32]; snprintf(pid,sizeof(pid),"%ld",(long)getpid());
        if (setenv("ARLINUX_DARLING_PREFIX",argv[2],1) || setenv("ARLINUX_DARLING_PID",pid,1))
            die("configure session Mach core");
#else
        need(0,"session requires integrated Mach core");
#endif
        darwin_native_connect();
        need(!darwin_native_session_start(),"initialize independent session service");
        darwin_native_service_run(NULL);
        return 1;
    }
    struct native_launch_options options=native_launch_parse(argc,argv);
    char *container=native_launch_container(options.container);
    int dynamic=options.dyld!=NULL;
    const char *root=options.root;
    long host=sysconf(_SC_PAGESIZE);
    need(host>0 && (unsigned long)host<=PAGE16 && PAGE16%(unsigned long)host==0,"host page geometry");
    const char *program=options.program;
    struct image im={0},dyld={0};
    load_image(program,&im,dynamic); map_guest(&im); rewrite_tls(&im);
    if (dynamic) {
        load_image(options.dyld,&dyld,1);
        need(dyld.filetype==7,"interpreter must be MH_DYLINKER");
        map_guest(&dyld); rewrite_tls(&dyld);
        fprintf(stderr,"NATIVE_DYLD_ENTRY entry=%#" PRIx64 " header=%#" PRIx64 " slide=%#" PRIx64 "\n",
                dyld.entry,dyld.header_address,dyld.slide);
    }
    darwin_native_connect();
    if(dynamic) {
        need(dyld.dyld_info_address && dyld.dyld_info_size,"interpreter lacks actual dyld image-info section");
        need(!darwin_native_dyld_info(dyld.dyld_info_address,dyld.dyld_info_size),"register actual interpreter image info");
    }
    native_framework_services_enabled=options.role!=NATIVE_CONSOLE;
    if (native_framework_services_enabled)
        need(!darwin_native_session_join(),"join authenticated compatibility session");
    if (options.role==NATIVE_APPLICATION) {
        native_display_open();
        need(native_display_enabled,"application requires a configured Wayland display");
    }
    fprintf(stderr,"NATIVE_PROCESS_ROLE role=%s pid=%ld\n",
        options.role==NATIVE_APPLICATION ? "application" : options.role==NATIVE_SERVICE ? "service" : "console",(long)getpid());
    if (darwin_native_services_enabled()) {
        pthread_t service;
        need(!pthread_create(&service,NULL,darwin_native_service_run,NULL),"native Mach service thread");
        need(!pthread_detach(service),"detach native Mach service thread");
    }
    uint64_t cache_base=root ? load_native_cache(root) : 0;
    long stack_address=darwin_native_memory(1024*1024,PROT_READ|PROT_WRITE);
    if ((unsigned long)stack_address>=(unsigned long)-4095) { errno=-stack_address; die("guest stack"); }
    void *stack=(void *)stack_address;
    uint64_t top=((uintptr_t)stack+1024*1024-128)&~15UL;
    if (dynamic) {
        size_t length=strlen(program)+1;
        need(length<4096,"program path too long");
        top=(top-length)&~15UL;
        memcpy((void *)top,program,length);
        uint64_t name=top;
        char *absolute=realpath(program,NULL);
        if (!absolute) die("canonical executable path");
        size_t apple_length=strlen(absolute)+sizeof("executable_path=");
        need(apple_length<8192,"Apple executable path too long");
        top=(top-apple_length)&~15UL;
        memcpy((void *)top,"executable_path=",16);
        memcpy((void *)(top+16),absolute,strlen(absolute)+1);
        free(absolute);
        uint64_t apple=top;
        /* XNU/libpthread's exec contract: describe the actual mapped main
         * stack, and seed pointer signing from real kernel entropy. pthread
         * consumes/erases these Apple-vector values during initialization. */
        char stack_parameter[192],token_parameter[64];
        int stack_count=snprintf(stack_parameter,sizeof(stack_parameter),
            "main_stack=%#" PRIx64 ",%#x,%#" PRIx64 ",%#x",
            (uint64_t)stack_address+1024*1024,1024*1024,(uint64_t)stack_address,1024*1024);
        need(stack_count>0 && (size_t)stack_count<sizeof(stack_parameter),"main stack parameter length");
        top=(top-stack_count-1)&~15UL;
        memcpy((void *)top,stack_parameter,stack_count+1);uint64_t stack_parameter_address=top;
        uint64_t token;
        do { if(getentropy(&token,sizeof(token)))die("pthread pointer entropy"); } while(!token);
        int token_count=snprintf(token_parameter,sizeof(token_parameter),"ptr_munge=%#" PRIx64,token);
        need(token_count>0 && (size_t)token_count<sizeof(token_parameter),"pthread token parameter length");
        top=(top-token_count-1)&~15UL;
        memcpy((void *)top,token_parameter,token_count+1);uint64_t token_parameter_address=top;
        token=0;memset(token_parameter,0,sizeof(token_parameter));
        const char *container_names[]={"HOME","CFFIXED_USER_HOME","TMPDIR"};
        const char *diagnostic_names[]={"DYLD_PRINT_LIBRARIES","DYLD_PRINT_SEGMENTS","ARLINUX_DIAGNOSE_LIFECYCLE"};
        uint64_t environments[1+sizeof(container_names)/sizeof(*container_names)+sizeof(diagnostic_names)/sizeof(*diagnostic_names)];
        size_t environment_count=0;
        if (options.role==NATIVE_APPLICATION) {
            /* Only the declared compatibility library enters guest envp;
             * host credentials/build environment are never copied wholesale. */
            char *library=realpath(program,NULL);
            need(library!=NULL,"canonical application path for platform library");
            char *separator=strrchr(library,'/'); need(separator!=NULL,"application bundle path");
            *separator=0;
            char injected[8192];
            int count=snprintf(injected,sizeof(injected),"DYLD_INSERT_LIBRARIES=%s/%s",library,ARLINUX_IOS_PLATFORM_LIBRARY);
            free(library);
            need(count>0 && (size_t)count<sizeof(injected),"platform library environment length");
            top=(top-count-1)&~15UL;
            memcpy((void *)top,injected,count+1); environments[environment_count++]=top;
        }
        /* Explicit application storage, not the workstation's HOME. The real
         * CoreFoundation home-directory implementation honors CFFIXED_USER_HOME;
         * no passwd identity or Apple container service is invented. */
        if(container)for(size_t i=0;i<sizeof(container_names)/sizeof(*container_names);i++) {
            char setting[8192];int count=snprintf(setting,sizeof(setting),"%s=%s%s",container_names[i],container,
                !strcmp(container_names[i],"TMPDIR") ? "/tmp/" : "");
            need(count>0 && (size_t)count<sizeof(setting),"container environment length");
            top=(top-count-1)&~15UL;memcpy((void *)top,setting,count+1);
            environments[environment_count++]=top;
        }
        /* Explicit read-only dyld diagnostics may enter the guest. Never copy
         * arbitrary host environment or credentials into the Apple process. */
        for(size_t i=0;i<sizeof(diagnostic_names)/sizeof(diagnostic_names[0]);i++) {
            const char *value=getenv(diagnostic_names[i]);
            if(!value || strcmp(value,"1")) continue;
            char setting[64];int size=snprintf(setting,sizeof(setting),"%s=1",diagnostic_names[i]);
            need(size>0 && (size_t)size<sizeof(setting),"dyld diagnostic environment length");
            top=(top-size-1)&~15UL;memcpy((void *)top,setting,size+1);
            environments[environment_count++]=top;
        }
        /* Header/argc/argv, optional env, and three Apple strings. Count the
         * actual entries; stack alignment does not depend on magic slot totals. */
        uint64_t initial[4+sizeof(environments)/sizeof(*environments)+1+4];size_t count=0;
        initial[count++]=im.header_address;initial[count++]=1;
        initial[count++]=name;initial[count++]=0;
        for(size_t i=0;i<environment_count;i++)initial[count++]=environments[i];
        initial[count++]=0;
        initial[count++]=apple;initial[count++]=stack_parameter_address;
        initial[count++]=token_parameter_address;initial[count++]=0;
        top=(top-count*sizeof(initial[0]))&~15UL;
        memcpy((void *)top,initial,count*sizeof(initial[0]));
    }
    /* Zeroed argc/argv/env/apple terminators for this freestanding entry. */
    fprintf(stderr,"NATIVE_DARWIN_ENTRY entry=%#" PRIx64 " host_page=%ld (Linux services, not XNU boot)\n",im.entry,host);
    darwin_native_prepare(root,program,container,cache_base,im.uuid,im.signature_bytes);
    darwin_native_enter(top,dynamic ? dyld.entry : im.entry);
}

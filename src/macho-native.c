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
        uint64_t environment=0;
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
            memcpy((void *)top,injected,count+1); environment=top;
        }
        top-=(environment ? 10 : 8)*8;
        uint64_t *words=(void *)top;
        words[0]=im.header_address;
        words[1]=1;
        words[2]=name;
        words[3]=0;             /* argv terminator */
        if (environment) {
            words[4]=environment; words[5]=0;
            words[6]=apple; words[7]=words[8]=words[9]=0;
        } else {
            words[4]=0; words[5]=apple; words[6]=words[7]=0;
        }
    }
    /* Zeroed argc/argv/env/apple terminators for this freestanding entry. */
    fprintf(stderr,"NATIVE_DARWIN_ENTRY entry=%#" PRIx64 " host_page=%ld (Linux services, not XNU boot)\n",im.entry,host);
    darwin_native_prepare(root,program,cache_base,im.uuid,im.signature_bytes);
    darwin_native_enter(top,dynamic ? dyld.entry : im.entry);
}

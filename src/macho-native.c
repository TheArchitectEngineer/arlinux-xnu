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

static void die(const char *message)
{
    fprintf(stderr,"ERROR %s: %s\n",message,strerror(errno));
    exit(1);
}
static void need(int ok,const char *message)
{
    if (!ok) { errno=0; die(message); }
}
#include "native-display.h"
#include "macho-image.h"

/* Mach-O section_64. Rewrite only sections explicitly marked instructions,
 * never cstrings, linkedit or other data. Initial scope is our static ARM64
 * program; arbitrary literal pools, dylibs, runtime code and arm64e are not
 * established. No guest file/signature is changed on disk. */
struct section64 {
    char name[16],segment[16];
    uint64_t address,size;
    uint32_t offset,align,reloff,nreloc,flags,reserved1,reserved2,reserved3;
};
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

int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--session")) {
        darwin_native_connect();
        need(!darwin_native_session_start(),"initialize independent session service");
        darwin_native_service_run(NULL);
        return 1;
    }
    int dynamic=(argc==4 || argc==6) && !strcmp(argv[1],"--dyld");
    const char *root=argc==6 && !strcmp(argv[3],"--root") ? argv[4] : NULL;
    need(argc!=6 || root,"expected --root directory");
    if (argc!=2 && !dynamic) { fprintf(stderr,"usage: macho-native [--dyld DYLD [--root DIRECTORY]] ARM64_MACHO\n"); return 2; }
    long host=sysconf(_SC_PAGESIZE);
    need(host>0 && (unsigned long)host<=PAGE16 && PAGE16%(unsigned long)host==0,"host page geometry");
    const char *program=argv[argc-1];
    struct image im={0},dyld={0};
    load_image(program,&im,dynamic); map_guest(&im); rewrite_tls(&im);
    if (dynamic) {
        load_image(argv[2],&dyld,1);
        need(dyld.filetype==7,"interpreter must be MH_DYLINKER");
        map_guest(&dyld); rewrite_tls(&dyld);
        fprintf(stderr,"NATIVE_DYLD_ENTRY entry=%#" PRIx64 " header=%#" PRIx64 " slide=%#" PRIx64 "\n",
                dyld.entry,dyld.header_address,dyld.slide);
    }
    darwin_native_connect();
    if (root) native_display_open();
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
        top-=8*8;
        uint64_t *words=(void *)top;
        words[0]=im.header_address;
        words[1]=1;
        words[2]=name;
        words[3]=0;             /* argv terminator */
        words[4]=0;             /* envp terminator */
        words[5]=apple;
        words[6]=words[7]=0;
    }
    /* Zeroed argc/argv/env/apple terminators for this freestanding entry. */
    fprintf(stderr,"NATIVE_DARWIN_ENTRY entry=%#" PRIx64 " host_page=%ld (Linux services, not XNU boot)\n",im.entry,host);
    darwin_native_prepare(root,program,cache_base,im.uuid,im.signature_bytes);
    darwin_native_enter(top,dynamic ? dyld.entry : im.entry);
}

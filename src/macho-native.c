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
static void rewrite_tls(struct image *im)
{
    struct header h; memcpy(&h,im->bytes,sizeof(h));
    size_t pos=sizeof(h),rewritten=0;
    for (uint32_t i=0;i<h.ncmds;i++) {
        struct command cmd; memcpy(&cmd,im->bytes+pos,sizeof(cmd));
        if (cmd.cmd==LC_SEGMENT_64) {
            struct segment s; memcpy(&s,im->bytes+pos,sizeof(s));
            need(s.nsects<=128 && sizeof(s)+(uint64_t)s.nsects*sizeof(struct section64)<=cmd.size,
                 "section table bounds");
            if (s.prot&4) {
                need(mprotect((void *)s.address,round16(s.size),PROT_READ|PROT_WRITE)==0,
                     "make code writable for TLS adaptation");
                for (uint32_t j=0;j<s.nsects;j++) {
                    struct section64 section;
                    memcpy(&section,im->bytes+pos+sizeof(s)+j*sizeof(section),sizeof(section));
                    if (!(section.flags&0x80000400U)) continue;
                    need(!(section.address&3) && !(section.size&3) &&
                         section.address>=s.address && range(section.address-s.address,section.size,s.filesize) &&
                         section.offset==s.offset+section.address-s.address,
                         "instruction section bounds/alignment");
                    uint32_t *instructions=(void *)section.address;
                    for (uint64_t k=0;k<section.size/4;k++) {
                        uint32_t op=instructions[k];
                        if ((op&~31U)==0xd53bd060U) { /* mrs Xt,TPIDRRO_EL0 */
                            instructions[k]=0xd53bd040U|(op&31); /* Linux guest TLS */
                            rewritten++;
                        } else if ((op&~31U)==0xd53bd040U) {
                            /* Original Darwin TPIDR_EL0 holds CPU metadata.
                             * Our declared machine has a single CPU, index 0. */
                            instructions[k]=0xd2800000U|(op&31);
                            rewritten++;
                        }
                    }
                }
                __builtin___clear_cache((void *)s.address,(void *)(s.address+s.size));
                need(mprotect((void *)s.address,round16(s.size),s.prot)==0,"restore code protection");
            }
        }
        pos+=cmd.size;
    }
    fprintf(stderr,"NATIVE_TLS_REWRITE instructions=%zu guest_page=16384 cpu_count=1\n",rewritten);
}

int main(int argc,char **argv)
{
    if (argc!=2) { fprintf(stderr,"usage: macho-native STATIC_ARM64_MACHO\n"); return 2; }
    long host=sysconf(_SC_PAGESIZE);
    need(host>0 && host<=PAGE16 && PAGE16%(unsigned long)host==0,"host page geometry");
    struct image im={0}; load_image(argv[1],&im); map_guest(&im); rewrite_tls(&im);
    void *stack=mmap(NULL,1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (stack==MAP_FAILED) die("guest stack");
    uint64_t top=((uintptr_t)stack+1024*1024-128)&~15UL;
    /* Zeroed argc/argv/env/apple terminators for this freestanding entry. */
    fprintf(stderr,"NATIVE_DARWIN_ENTRY entry=%#" PRIx64 " host_page=%ld (Linux services, not XNU boot)\n",im.entry,host);
    darwin_native_prepare();
    darwin_native_enter(top,im.entry);
}

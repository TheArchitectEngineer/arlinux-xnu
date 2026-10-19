/* Shared bounded ARM64 Mach-O image loader; consumers supply die/need. */
#pragma once
#define PAGE16 16384UL
#define LC_SEGMENT_64 0x19
#define LC_MAIN 0x80000028U
#define LC_UNIXTHREAD 5
#define NT_ARM_SYSTEM_CALL 0x404
#define CARRY (1UL<<29)
#define MAX_SEGMENTS 32
struct header { uint32_t magic,cpu,subtype,type,ncmds,sizeofcmds,flags,reserved; };
struct command { uint32_t cmd,size; };
struct segment {
    struct command lc; char name[16];
    uint64_t address,size,offset,filesize;
    uint32_t maxprot,prot,nsects,flags;
};
struct entry_command { struct command lc; uint64_t entryoff,stacksize; };
struct image {
    unsigned char *bytes; size_t size;
    struct segment segments[MAX_SEGMENTS]; size_t count;
    uint64_t entry;
};
static int range(uint64_t offset,uint64_t length,uint64_t size) { return offset<=size && length<=size-offset; }
static uint64_t round16(uint64_t size) { need(size<=UINT64_MAX-(PAGE16-1),"size overflow"); return (size+PAGE16-1)&~(PAGE16-1); }
static void load_image(const char *path,struct image *im) {
    int fd=open(path,O_RDONLY); if(fd<0) die("open Mach-O");
    struct stat st; if(fstat(fd,&st)<0) die("stat Mach-O");
    need(st.st_size>=(off_t)sizeof(struct header) && st.st_size<64*1024*1024,"invalid Mach-O size");
    im->size=(size_t)st.st_size;
    im->bytes=mmap(NULL,im->size,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if(im->bytes==MAP_FAILED) die("map input");
    struct header h; memcpy(&h,im->bytes,sizeof(h));
    need(h.magic==0xfeedfacf && h.cpu==0x0100000c && (h.subtype&0xffffff)==0 && h.type==2,
         "only little-endian ARM64 non-arm64e MH_EXECUTE is supported");
    need(h.ncmds<=256 && range(sizeof(h),h.sizeofcmds,im->size),"invalid load commands");
    uint64_t pos=sizeof(h),commands_end=pos+h.sizeofcmds,entryoff=UINT64_MAX;
    for(uint32_t i=0;i<h.ncmds;i++) {
        need(range(pos,sizeof(struct command),commands_end),"truncated command");
        struct command cmd; memcpy(&cmd,im->bytes+pos,sizeof(cmd));
        need(cmd.size>=sizeof(cmd) && !(cmd.size%8) && range(pos,cmd.size,commands_end),"invalid command bounds");
        if(cmd.cmd==LC_SEGMENT_64) {
            need(cmd.size>=sizeof(struct segment) && im->count<MAX_SEGMENTS,"invalid segment command");
            struct segment s; memcpy(&s,im->bytes+pos,sizeof(s));
            need(s.filesize<=s.size && range(s.offset,s.filesize,im->size),"invalid segment file bounds");
            if(!s.prot && !s.filesize) { pos+=cmd.size; continue; } // __PAGEZERO stays unmapped.
            need(s.size && s.size<=64*1024*1024 && s.address>=0x10000 && !(s.address%PAGE16),"segment is not a supported 16K mapping");
            need(s.address<0x0000800000000000ULL && range(s.address,round16(s.size),0x0000800000000000ULL),"segment address overflow");
            need(!(s.prot&~7U) && !((s.prot&2) && (s.prot&4)),"unsupported or writable executable segment");
            for(size_t j=0;j<im->count;j++) {
                struct segment *old=&im->segments[j];
                need(s.address+round16(s.size)<=old->address || old->address+round16(old->size)<=s.address,"overlapping guest segments");
            }
            im->segments[im->count++]=s;
        } else if(cmd.cmd==LC_MAIN) {
            need(cmd.size>=sizeof(struct entry_command) && entryoff==UINT64_MAX,"invalid or duplicate main entry");
            struct entry_command e; memcpy(&e,im->bytes+pos,sizeof(e)); entryoff=e.entryoff;
        } else if(cmd.cmd==LC_UNIXTHREAD) {
            die("LC_UNIXTHREAD entry not implemented; use the LC_MAIN freestanding fixture");
        } else if(cmd.cmd==0xe) {
            // LLD emits /usr/lib/dyld metadata even without dependencies. The
            // freestanding diagnostic enters LC_MAIN directly, without dyld.
            fprintf(stderr,"LC_LOAD_DYLINKER metadata: no dyld execution in this freestanding host\n");
        } else if(cmd.cmd==0xb) {
            need(cmd.size>=80,"truncated dynamic symbol table");
            uint32_t undefined; memcpy(&undefined,im->bytes+pos+28,sizeof(undefined));
            need(!undefined,"undefined dynamic symbols require dyld");
        } else if(cmd.cmd==0xc || cmd.cmd==0x80000018U || cmd.cmd==0x8000001fU || cmd.cmd==0x80000023U) {
            die("dynamic loading is not implemented");
        } else if(cmd.cmd==0x80000022U) {
            // dyld_info: reject streams requiring a dynamic loader.
            need(cmd.size>=48,"invalid dyld_info");
            uint32_t words[10]; memcpy(words,im->bytes+pos+8,sizeof(words));
            need(!words[1] && !words[3] && !words[5] && !words[7],"dyld rebasing or binding required");
        } else if(cmd.cmd==0x80000034U) {
            die("chained fixups require dyld");
        }
        pos+=cmd.size;
    }
    need(pos==commands_end && im->count && entryoff!=UINT64_MAX,"missing or inconsistent entry/segments");
    for(size_t j=0;j<im->count;j++) {
        struct segment *s=&im->segments[j];
        if((s->prot&4) && entryoff>=s->offset && entryoff-s->offset<s->filesize)
            im->entry=s->address+entryoff-s->offset;
    }
    need(im->entry,"entry is not in executable file data");
    fprintf(stderr,"Mach-O mapped plan: segments=%zu entry=0x%" PRIx64 " guest_page=16384\n",im->count,im->entry);
}
static void map_guest(struct image *im) {
    for(size_t j=0;j<im->count;j++) {
        struct segment *s=&im->segments[j];
        void *p=mmap((void *)s->address,round16(s->size),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        if(p==MAP_FAILED || (uintptr_t)p!=s->address) die("map guest segment without replacing host memory");
        memcpy(p,im->bytes+s->offset,s->filesize);
        __builtin___clear_cache(p,(char *)p+s->filesize);
        if(mprotect(p,round16(s->size),s->prot)<0) die("protect guest segment");
    }
}

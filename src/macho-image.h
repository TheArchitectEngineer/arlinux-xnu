/* Shared bounded ARM64 Mach-O image loader; consumers supply die/need. */
#pragma once
#define PAGE16 16384UL
#define LC_SEGMENT_64 0x19
#define LC_MAIN 0x80000028U
#define LC_UNIXTHREAD 5
#define LC_UUID 0x1b
#define LC_CODE_SIGNATURE 0x1d
#define MH_DYLIB 0x6
#define MH_DYLINKER 0x7
#define NT_ARM_SYSTEM_CALL 0x404
#define CARRY (1UL<<29)
#define MAX_SEGMENTS 32
/* The canonical cache spans 6-10 GiB; keep its interpreter above that and
 * the runtime low-address heap. Mapping collisions still fail via NOREPLACE. */
#define NATIVE_INTERPRETER_BASE 0x400000000ULL
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
    uint64_t header_address,slide;
    uint32_t filetype;
    uint32_t signature_bytes;
    unsigned char uuid[16];
};
static int range(uint64_t offset,uint64_t length,uint64_t size) { return offset<=size && length<=size-offset; }
static uint64_t round16(uint64_t size) { need(size<=UINT64_MAX-(PAGE16-1),"size overflow"); return (size+PAGE16-1)&~(PAGE16-1); }
static void load_image(const char *path,struct image *im,int dynamic) {
    int fd=open(path,O_RDONLY); if(fd<0) die("open Mach-O");
    struct stat st; if(fstat(fd,&st)<0) die("stat Mach-O");
    need(st.st_size>=(off_t)sizeof(struct header) && st.st_size<64*1024*1024,"invalid Mach-O size");
    im->size=(size_t)st.st_size;
    im->bytes=mmap(NULL,im->size,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    if(im->bytes==MAP_FAILED) die("map input");
    struct header h; memcpy(&h,im->bytes,sizeof(h));
    need(h.magic==0xfeedfacf && h.cpu==0x0100000c && (h.subtype&0xffffff)==0 &&
         (h.type==2 || (dynamic && h.type==7)),
         "only little-endian ARM64 non-arm64e executable/dylinker is supported");
    im->filetype=h.type;
    im->slide=h.type==7 ? NATIVE_INTERPRETER_BASE : 0;
    need(h.ncmds<=256 && range(sizeof(h),h.sizeofcmds,im->size),"invalid load commands");
    uint64_t pos=sizeof(h),commands_end=pos+h.sizeofcmds,entryoff=UINT64_MAX,thread_entry=0;
    int signature_seen=0;
    for(uint32_t i=0;i<h.ncmds;i++) {
        need(range(pos,sizeof(struct command),commands_end),"truncated command");
        struct command cmd; memcpy(&cmd,im->bytes+pos,sizeof(cmd));
        need(cmd.size>=sizeof(cmd) && !(cmd.size%8) && range(pos,cmd.size,commands_end),"invalid command bounds");
        if(cmd.cmd==LC_CODE_SIGNATURE) {
            need(!signature_seen,"duplicate code signature command"); signature_seen=1;
            uint32_t data[2];
            need(cmd.size==sizeof(cmd)+sizeof(data),"invalid code signature command");
            memcpy(data,im->bytes+pos+sizeof(cmd),sizeof(data));
            need(range(data[0],data[1],im->size),"code signature bounds");
            im->signature_bytes=data[1];
        } else if(cmd.cmd==LC_SEGMENT_64) {
            need(cmd.size>=sizeof(struct segment) && im->count<MAX_SEGMENTS,"invalid segment command");
            struct segment s; memcpy(&s,im->bytes+pos,sizeof(s));
            need(s.filesize<=s.size && range(s.offset,s.filesize,im->size),"invalid segment file bounds");
            if(!s.prot && !s.filesize) { pos+=cmd.size; continue; } // __PAGEZERO stays unmapped.
            need(s.address<=UINT64_MAX-im->slide,"segment slide overflow");
            s.address+=im->slide;
            need(s.size && s.size<=64*1024*1024 && s.address>=0x10000 && !(s.address%PAGE16),"segment is not a supported 16K mapping");
            need(s.address<0x0000800000000000ULL && range(s.address,round16(s.size),0x0000800000000000ULL),"segment address overflow");
            need(!(s.prot&~7U) && !((s.prot&2) && (s.prot&4)),"unsupported or writable executable segment");
            for(size_t j=0;j<im->count;j++) {
                struct segment *old=&im->segments[j];
                need(s.address+round16(s.size)<=old->address || old->address+round16(old->size)<=s.address,"overlapping guest segments");
            }
            im->segments[im->count++]=s;
            if(!s.offset && s.filesize>=sizeof(h)) im->header_address=s.address;
        } else if(cmd.cmd==LC_MAIN) {
            need(cmd.size>=sizeof(struct entry_command) && entryoff==UINT64_MAX,"invalid or duplicate main entry");
            struct entry_command e; memcpy(&e,im->bytes+pos,sizeof(e)); entryoff=e.entryoff;
        } else if(cmd.cmd==LC_UNIXTHREAD) {
            need(dynamic && cmd.size>=288 && !thread_entry,"unsupported/duplicate UNIXTHREAD entry");
            uint32_t flavor,count;
            memcpy(&flavor,im->bytes+pos+8,4); memcpy(&count,im->bytes+pos+12,4);
            need(flavor==6 && count==68,"expected ARM_THREAD_STATE64 entry");
            memcpy(&thread_entry,im->bytes+pos+272,8);
            need(thread_entry<=UINT64_MAX-im->slide,"thread entry overflow");
            thread_entry+=im->slide;
        } else if(cmd.cmd==LC_UUID) {
            need(cmd.size==24,"invalid executable UUID command");
            memcpy(im->uuid,im->bytes+pos+sizeof(cmd),sizeof(im->uuid));
        } else if(cmd.cmd==0xe) {
            // LLD emits /usr/lib/dyld metadata even without dependencies. The
            // freestanding diagnostic enters LC_MAIN directly, without dyld.
            fprintf(stderr,dynamic ? "LC_LOAD_DYLINKER: external dyld requested\n" :
                    "LC_LOAD_DYLINKER metadata: no dyld execution in this freestanding host\n");
        } else if(cmd.cmd==0xb) {
            need(cmd.size>=80,"truncated dynamic symbol table");
            uint32_t undefined; memcpy(&undefined,im->bytes+pos+28,sizeof(undefined));
            need(dynamic || !undefined,"undefined dynamic symbols require dyld");
        } else if(cmd.cmd==0xc || cmd.cmd==0x80000018U || cmd.cmd==0x8000001fU || cmd.cmd==0x80000023U) {
            need(dynamic,"dynamic loading requires an explicit dyld");
        } else if(cmd.cmd==0x80000022U) {
            // dyld_info: reject streams requiring a dynamic loader.
            need(cmd.size>=48,"invalid dyld_info");
            uint32_t words[10]; memcpy(words,im->bytes+pos+8,sizeof(words));
            need(dynamic || (!words[1] && !words[3] && !words[5] && !words[7]),"dyld rebasing or binding required");
        } else if(cmd.cmd==0x80000034U) {
            need(dynamic,"chained fixups require dyld");
        }
        pos+=cmd.size;
    }
    need(pos==commands_end && im->count && (entryoff!=UINT64_MAX || thread_entry),"missing or inconsistent entry/segments");
    for(size_t j=0;j<im->count;j++) {
        struct segment *s=&im->segments[j];
        if((s->prot&4) && entryoff>=s->offset && entryoff-s->offset<s->filesize)
            im->entry=s->address+entryoff-s->offset;
        if((s->prot&4) && thread_entry>=s->address && thread_entry-s->address<s->filesize)
            im->entry=thread_entry;
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

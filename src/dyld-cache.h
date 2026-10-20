/* Native, process-private loader for the legacy non-arm64e iOS cache.
 * Format reference: apple-oss-distributions/dyld, dyld-655.1,
 * dyld3/shared-cache/dyld_cache_format.h and SharedCacheRuntime.cpp.
 * This implements mapping/relocation, not Apple's signature authentication.
 * The selected firmware's SHA-256 is verified by runner input preparation.
 * Consumers supply need(), range(), PAGE16 and rewrite_instruction_words(). */
#pragma once
struct cache_map { uint64_t address,size,offset; uint32_t maxprot,prot; };
struct cache_prefix {
    char magic[16]; uint32_t mappings_offset,mappings_count,images_offset,images_count;
    uint64_t dyld_base,signature_offset,signature_size,slide_offset,slide_size;
};
struct cache_image { uint64_t address,mtime,inode; uint32_t path_offset,padding; };
struct cache_slide2 {
    uint32_t version,page_size,starts_offset,starts_count,extras_offset,extras_count;
    uint64_t delta_mask,value_add;
};
static struct cache_map cache_maps[3];
static unsigned int cache_service_hooks;
static int cache_contains(uint64_t address,uint64_t length,uint32_t protection)
{
    for (unsigned int i=0;i<3;i++) {
        const struct cache_map *m=cache_maps+i;
        if ((m->prot&protection)==protection && address>=m->address &&
            range(address-m->address,length,m->size)) return 1;
    }
    return 0;
}
static uint16_t cache_u16(const unsigned char *bytes)
{
    uint16_t value; memcpy(&value,bytes,sizeof(value)); return value;
}
#include "cache-bootstrap-policy.h"
static void cache_relocate_chain(unsigned char *page,uint32_t offset,
                                 const struct cache_slide2 *info)
{
    unsigned int shift=__builtin_ctzll(info->delta_mask)-2;
    for (uint32_t step=0;step<info->page_size/4;step++) {
        need(!(offset&3) && range(offset,8,info->page_size),"cache pointer chain bounds");
        uint64_t encoded; memcpy(&encoded,page+offset,8);
        uint64_t advance=(encoded&info->delta_mask)>>shift;
        uint64_t pointer=encoded&~info->delta_mask;
        if (pointer) {
            need(pointer<=UINT64_MAX-info->value_add,"cache pointer overflow");
            pointer+=info->value_add; /* Fixed unslid placement: slide is zero. */
        }
        memcpy(page+offset,&pointer,8);
        if (!advance) return;
        need(advance<=info->page_size-offset,"cache chain escapes page");
        offset+=(uint32_t)advance;
    }
    need(0,"unterminated cache pointer chain");
}
static uint64_t load_native_cache(const char *root)
{
    char path[8192];
    int length=snprintf(path,sizeof(path),"%s/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",root);
    need(length>0 && (size_t)length<sizeof(path),"cache path length");
    int fd=open(path,O_RDONLY|O_CLOEXEC); if (fd<0) die("open real iOS shared cache");
    struct stat status; if (fstat(fd,&status)) die("stat real iOS shared cache");
    need(status.st_size>=16384 && (uint64_t)status.st_size<=(1ULL<<32),"cache file size");
    uint64_t size=status.st_size;
    const unsigned char *file=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);
    if (file==MAP_FAILED) die("read cache metadata");
    struct cache_prefix header; memcpy(&header,file,sizeof(header));
    need(!memcmp(header.magic,"dyld_v1   arm64",sizeof("dyld_v1   arm64")),
         "only legacy non-arm64e ARM64 shared cache is supported");
    need(header.mappings_count==3 && header.images_count>0 && header.images_count<=16384 &&
         range(header.mappings_offset,sizeof(cache_maps),size) &&
         range(header.images_offset,(uint64_t)header.images_count*sizeof(struct cache_image),size) &&
         range(header.slide_offset,header.slide_size,size) &&
         header.slide_size>=sizeof(struct cache_slide2),"cache table bounds");
    memcpy(cache_maps,file+header.mappings_offset,sizeof(cache_maps));
    for (unsigned int i=0;i<3;i++) {
        struct cache_map *m=cache_maps+i;
        need(m->size && !(m->size&(PAGE16-1)) && !(m->address&(PAGE16-1)) &&
             !(m->offset&(PAGE16-1)) && range(m->offset,m->size,size) &&
             m->address>=0x10000 && range(m->address,m->size,0x0000800000000000ULL) &&
             !(m->prot&~7U) && !(m->prot&2 && m->prot&4),"cache mapping geometry/protection");
        if (i) need(cache_maps[i-1].address+cache_maps[i-1].size<=m->address,
                    "overlapping cache mappings");
        /* Never execute code while rewriting it. Every region is private. */
        int initial=(m->prot&4) ? PROT_READ|PROT_WRITE : m->prot;
        void *mapped=mmap((void *)m->address,m->size,initial,MAP_PRIVATE|MAP_FIXED_NOREPLACE,fd,m->offset);
        if (mapped==MAP_FAILED) die("map actual shared cache");
        fprintf(stderr,"NATIVE_CACHE_MAP address=%#" PRIx64 " bytes=%#" PRIx64 " prot=%u\n",
                m->address,m->size,m->prot);
    }
    close(fd);
    need(cache_maps[0].prot==5 && cache_maps[1].prot==3 && cache_maps[2].prot==1,
         "expected legacy text/data/linkedit cache");
    struct cache_slide2 slide; memcpy(&slide,file+header.slide_offset,sizeof(slide));
    need(slide.version==2 && (slide.page_size==4096 || slide.page_size==16384) &&
         slide.delta_mask && __builtin_ctzll(slide.delta_mask)>=2 &&
         range(slide.starts_offset,(uint64_t)slide.starts_count*2,header.slide_size) &&
         range(slide.extras_offset,(uint64_t)slide.extras_count*2,header.slide_size) &&
         (uint64_t)slide.starts_count*slide.page_size<=cache_maps[1].size,
         "unsupported or invalid cache slide info");
    uint64_t mask=slide.delta_mask>>__builtin_ctzll(slide.delta_mask);
    need(!(mask&(mask+1)),"noncontiguous cache delta mask");
    const unsigned char *starts=file+header.slide_offset+slide.starts_offset;
    const unsigned char *extras=file+header.slide_offset+slide.extras_offset;
    for (uint32_t i=0;i<slide.starts_count;i++) {
        uint16_t entry=cache_u16(starts+i*2);
        if (entry==0x4000) continue;
        unsigned char *page=(void *)(cache_maps[1].address+(uint64_t)i*slide.page_size);
        if (!(entry&0x8000)) cache_relocate_chain(page,(uint32_t)entry*4,&slide);
        else {
            uint32_t index=entry&0x3fff;
            for (;;) {
                need(index<slide.extras_count,"cache extra chain bounds");
                uint16_t chain=cache_u16(extras+index*2);
                cache_relocate_chain(page,(chain&0x3fff)*4,&slide);
                index++;
                if (chain&0x8000) break;
            }
        }
    }
    /* Cache images may alias the same Mach-O. Adapt each image only once so a
     * rewritten TSD read cannot be mistaken for an original CPU metadata read. */
    uint64_t *seen=calloc(header.images_count,sizeof(uint64_t));
    need(seen!=NULL,"cache image bookkeeping");
    size_t images=0,rewritten=0;
    for (uint32_t i=0;i<header.images_count;i++) {
        struct cache_image image;
        memcpy(&image,file+header.images_offset+(uint64_t)i*sizeof(image),sizeof(image));
        size_t j=0; while (j<images && seen[j]!=image.address) j++;
        if (j<images) continue;
        seen[images++]=image.address;
        need(image.path_offset<size && memchr(file+image.path_offset,0,size-image.path_offset),"cache image path bounds");
        int bootstrap_policy=native_display_enabled &&
            !strcmp((const char *)file+image.path_offset,"/usr/lib/system/libxpc.dylib");
        need(cache_contains(image.address,sizeof(struct header),1),"cached Mach-O header bounds");
        const unsigned char *bytes=(void *)image.address;
        struct header mh; memcpy(&mh,bytes,sizeof(mh));
        need(mh.magic==0xfeedfacf && mh.cpu==0x0100000c && (mh.subtype&0xffffff)==0 &&
             mh.type==6 && mh.ncmds<=256 && cache_contains(image.address,sizeof(mh)+mh.sizeofcmds,1),
             "unsupported cached Mach-O");
        uint64_t pos=sizeof(mh),end=pos+mh.sizeofcmds;
        for (uint32_t n=0;n<mh.ncmds;n++) {
            need(range(pos,sizeof(struct command),end),"cache command bounds");
            struct command command; memcpy(&command,bytes+pos,sizeof(command));
            need(command.size>=sizeof(command) && !(command.size&7) && range(pos,command.size,end),
                 "cache command size");
            if (command.cmd==LC_SEGMENT_64) {
                need(command.size>=sizeof(struct segment),"cache segment command");
                struct segment segment; memcpy(&segment,bytes+pos,sizeof(segment));
                need(segment.nsects<=128 && range(sizeof(segment),(uint64_t)segment.nsects*sizeof(struct section64),command.size),
                     "cache section table bounds");
                if (segment.prot&4) for (uint32_t k=0;k<segment.nsects;k++) {
                    struct section64 section;
                    memcpy(&section,bytes+pos+sizeof(segment)+(uint64_t)k*sizeof(section),sizeof(section));
                    if (!(section.flags&0x80000400U) || !section.size) continue;
                    need(!(section.address&3) && !(section.size&3) &&
                         cache_contains(section.address,section.size,4),"cache instruction section bounds");
                    rewritten+=rewrite_instruction_words((void *)section.address,section.size/4);
                    if (bootstrap_policy) cache_bootstrap_policy(section.address,section.size);
                }
            }
            if (native_display_enabled && command.cmd==2) { /* LC_SYMTAB */
                need(command.size==24 && image.path_offset<size &&
                     memchr(file+image.path_offset,0,size-image.path_offset),"cache symbol/image path bounds");
                if (!strcmp((const char *)file+image.path_offset,"/usr/lib/system/libxpc.dylib")) {
                    uint32_t table[4]; memcpy(table,bytes+pos+8,sizeof(table));
                    need(range(table[0],(uint64_t)table[1]*16,size) && range(table[2],table[3],size),
                         "cache service symbol bounds");
                    for (uint32_t k=0;k<table[1];k++) {
                        const unsigned char *symbol=file+table[0]+(uint64_t)k*16;
                        uint32_t name; uint64_t address;
                        memcpy(&name,symbol,4); memcpy(&address,symbol+8,8);
                        if (name>=table[3] || symbol[4]&0xe0 || (symbol[4]&0xe)!=0xe || !symbol[5]) continue;
                        const char *text=(const char *)file+table[2]+name;
                        need(memchr(text,0,table[3]-name)!=NULL,"cache symbol name termination");
                        uint64_t target=!strcmp(text,"_bootstrap_look_up3") ? (uintptr_t)darwin_native_service_lookup :
                            !strcmp(text,"_bootstrap_register2") ? (uintptr_t)darwin_native_service_register :
                            !strcmp(text,"_bootstrap_check_in2") ? (uintptr_t)darwin_native_service_check_in : 0;
                        if (!target) continue;
                        need(!(address&3) && cache_contains(address,16,4),"service adapter code bounds");
                        /* LP64 ARM64 absolute tail jump; preserve the original
                         * public API arguments and guest LR. Map is private. */
                        uint32_t code[2]={0x58000050U,0xd61f0200U};
                        memcpy((void *)address,code,sizeof(code));
                        memcpy((void *)(address+8),&target,sizeof(target));
                        cache_service_hooks++;
                        fprintf(stderr,"NATIVE_SERVICE_ADAPTER symbol=%s address=%#" PRIx64 "\n",text,address);
                    }
                }
            }
            pos+=command.size;
        }
        need(pos==end,"cached command extent");

    }
    free(seen);
    need(!native_display_enabled || cache_service_hooks==3,"exact service registry adapter symbols unavailable");
    need(!native_display_enabled || cache_bootstrap_policy_hooks==1,"unsupported bootstrap trust predicate layout");
    for (unsigned int i=0;i<3;i++) {
        struct cache_map *m=cache_maps+i;
        if (m->prot&4) __builtin___clear_cache((void *)m->address,(void *)(m->address+m->size));
        if (mprotect((void *)m->address,m->size,m->prot)) die("final cache protection");
    }
    munmap((void *)file,size);
    fprintf(stderr,"NATIVE_CACHE_READY base=%#" PRIx64 " images=%zu tls_instructions=%zu slide=0 signature_authentication=disabled\n",
            cache_maps[0].address,images,rewritten);
    return cache_maps[0].address;
}

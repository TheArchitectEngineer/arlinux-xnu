/* Native, process-private loader for non-arm64e ARM64 split shared caches.
 * Format reference: apple-oss-distributions/dyld include/mach-o/dyld_cache_format.h
 * and SharedCacheRuntime.cpp. Membership, UUIDs and VM/slide tables come from data.
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
enum { CACHE_MAX_FILES=128,CACHE_MAX_MAPS=512 };
static struct cache_map cache_maps[CACHE_MAX_MAPS];
static unsigned int cache_map_count;
static unsigned int cache_service_hooks;
static int native_framework_services_enabled;
static int cache_contains(uint64_t address,uint64_t length,uint32_t protection)
{
    for (unsigned int i=0;i<cache_map_count;i++) {
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
#include "ios-framework-bindings.h"
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
#include "dyld-cache-storage.h"
static uint64_t load_native_cache(const char *root)
{
    char path[8192];
    int length=snprintf(path,sizeof(path),"%s/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",root);
    need(length>0 && (size_t)length<sizeof(path),"cache path length");
    cache_open_members(path);cache_map_members();cache_rebase_members();cache_configure_dynamic_data();
    const struct cache_file *primary=cache_files;
    const unsigned char *file=primary->bytes;uint64_t size=primary->size;
    struct cache_prefix header=primary->header.common;
    header.images_offset=primary->header.images_offset;header.images_count=primary->header.images_count;
    need(header.images_count && header.images_count<=16384 && range(header.images_offset,(uint64_t)header.images_count*sizeof(struct cache_image),size),"cache image index bounds");
    /* Cache images may alias the same Mach-O. Adapt each image only once so a
     * rewritten TSD read cannot be mistaken for an original CPU metadata read. */
    struct darwin_ui_api ui_api={0};
    int diagnostic_exports=getenv("ARLINUX_DIAGNOSTICS")!=NULL;
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
        int bootstrap_policy=native_framework_services_enabled &&
            !strcmp((const char *)file+image.path_offset,"/usr/lib/system/libxpc.dylib");
        need(cache_contains(image.address,sizeof(struct header),1),"cached Mach-O header bounds");
        const unsigned char *bytes=(void *)image.address;
        struct header mh; memcpy(&mh,bytes,sizeof(mh));
        /* Modern caches also index their own dynamic linker. Its identity is
         * the primary header's dyldInCacheMH, not a guessed path or OS version. */
        int supported_kind=mh.type==MH_DYLIB ||
            (mh.type==MH_DYLINKER && image.address==primary->header.dyld_header);
        int supported=mh.magic==0xfeedfacf && mh.cpu==0x0100000c && (mh.subtype&0xffffff)==0 &&
             supported_kind && mh.ncmds<=256 && cache_contains(image.address,sizeof(mh)+mh.sizeofcmds,1);
        if (!supported) fprintf(stderr,"NATIVE_CACHE_IMAGE_REJECT path=%s address=%#" PRIx64
            " magic=%#x cpu=%#x subtype=%#x type=%u commands=%u command_bytes=%u mapped=%d\n",
            (const char *)file+image.path_offset,image.address,mh.magic,mh.cpu,mh.subtype,
            mh.type,mh.ncmds,mh.sizeofcmds,cache_contains(image.address,sizeof(mh)+mh.sizeofcmds,1));
        need(supported,"unsupported cached Mach-O");
        uint64_t pos=sizeof(mh),end=pos+mh.sizeofcmds;
        uint64_t linkedit_address=0,linkedit_offset=0,linkedit_size=0;
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
                if (!memcmp(segment.name,"__LINKEDIT",10)) { linkedit_address=segment.address;linkedit_offset=segment.offset;linkedit_size=segment.filesize; }
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
            if (native_framework_services_enabled && command.cmd==2) { /* LC_SYMTAB */
                need(command.size==24 && image.path_offset<size &&
                     memchr(file+image.path_offset,0,size-image.path_offset),"cache symbol/image path bounds");
                int xpc_image=!strcmp((const char *)file+image.path_offset,"/usr/lib/system/libxpc.dylib");
                int render_image=!strcmp((const char *)file+image.path_offset,"/System/Library/Frameworks/QuartzCore.framework/QuartzCore");
                int ui_image=ios_framework_image((const char *)file+image.path_offset);
                if (xpc_image || render_image || ui_image) {
                    uint32_t table[4]; memcpy(table,bytes+pos+8,sizeof(table));
                    need(linkedit_address && table[0]>=linkedit_offset && table[2]>=linkedit_offset &&
                         range(table[0]-linkedit_offset,(uint64_t)table[1]*16,linkedit_size) &&
                         range(table[2]-linkedit_offset,table[3],linkedit_size),"cache image linkedit symbol bounds");
                    const unsigned char *symbols=(void *)(linkedit_address+table[0]-linkedit_offset);
                    const unsigned char *strings=(void *)(linkedit_address+table[2]-linkedit_offset);
                    need(cache_contains((uintptr_t)symbols,(uint64_t)table[1]*16,1) && cache_contains((uintptr_t)strings,table[3],1),"cache mapped symbol bounds");
                    for (uint32_t k=0;k<table[1];k++) {
                        const unsigned char *symbol=symbols+(uint64_t)k*16;
                        uint32_t name; uint64_t address;
                        memcpy(&name,symbol,4); memcpy(&address,symbol+8,8);
                        if (name>=table[3] || symbol[4]&0xe0 || (symbol[4]&0xe)!=0xe || !symbol[5]) continue;
                        const char *text=(const char *)strings+name;
                        need(memchr(text,0,table[3]-name)!=NULL,"cache symbol name termination");
                        ios_framework_bind(&ui_api,(const char *)file+image.path_offset,text,address);
                        if (render_image) {
                            if (diagnostic_exports && !strncmp(text,"_CARender",9) && cache_contains(address,4,4))
                                fprintf(stderr,"NATIVE_RENDER_EXPORT symbol=%s address=%#" PRIx64 "\n",text,address);
                            continue;
                        }
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
    darwin_native_ui_api(&ui_api);

    need(!native_framework_services_enabled || cache_service_hooks==3,"exact service registry adapter symbols unavailable");
    need(!native_framework_services_enabled || cache_bootstrap_policy_hooks==1,"unsupported bootstrap trust predicate layout");
    for (unsigned int i=0;i<cache_map_count;i++) {
        struct cache_map *m=cache_maps+i;
        if (m->prot&4) __builtin___clear_cache((void *)m->address,(void *)(m->address+m->size));
        if (mprotect((void *)m->address,m->size,m->prot)) die("final cache protection");
    }
    for (unsigned int i=0;i<cache_file_count;i++) { munmap((void *)cache_files[i].bytes,cache_files[i].size);close(cache_files[i].fd); }
    fprintf(stderr,"NATIVE_CACHE_READY base=%#" PRIx64 " images=%zu tls_instructions=%zu slide=0 signature_authentication=disabled\n",
            cache_maps[0].address,images,rewritten);
    return cache_maps[0].address;
}

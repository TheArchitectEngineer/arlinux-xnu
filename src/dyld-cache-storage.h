/* Actual split-cache storage. Layout reference: Apple's dyld_cache_format.h.
 * Header geometry selects structures. No marketing-version or filename glob
 * determines which bytes are executable. The primary UUID table owns members.
 */
#pragma once
#include <stddef.h>
struct cache_header {
    struct cache_prefix common;
    uint64_t locals_offset,locals_size;
    unsigned char uuid[16];
    uint64_t cache_type;
    uint32_t branch_offset,branch_count;
    uint64_t dyld_header,dyld_entry,text_offset,text_count,patch_address,patch_size;
    uint64_t old_group_address,old_group_size,closures_address,closures_size,closures_trie,closures_trie_size;
    uint32_t platform,format;
    uint64_t region_start,region_size,max_slide;
    uint64_t image_array,image_array_size,dylibs_trie,dylibs_trie_size;
    uint64_t other_array,other_array_size,other_trie,other_trie_size;
    uint32_t slide_maps_offset,slide_maps_count;
    uint64_t old_pbl_states,pbl_set,program_pool,program_pool_size,program_trie;
    uint32_t program_trie_size,os_version,alt_platform,alt_version;
    uint64_t swift_options,swift_options_size;
    uint32_t subcaches_offset,subcaches_count;
    unsigned char symbol_uuid[16];
    uint64_t rosetta_ro,rosetta_ro_size,rosetta_rw,rosetta_rw_size;
    uint32_t images_offset,images_count,cache_subtype;
};
struct cache_slide_map {
    uint64_t address,size,offset,slide_offset,slide_size,flags;
    uint32_t maxprot,prot;
};
struct cache_subcache { unsigned char uuid[16]; uint64_t vm_offset; char suffix[32]; };
struct cache_file { const unsigned char *bytes; uint64_t size; int fd; struct cache_header header; };
_Static_assert(offsetof(struct cache_header,uuid)==88,"cache UUID ABI");
_Static_assert(offsetof(struct cache_header,slide_maps_offset)==312,"cache slide mappings ABI");
_Static_assert(offsetof(struct cache_header,subcaches_offset)==392,"cache subcache table ABI");
_Static_assert(offsetof(struct cache_header,images_offset)==448,"cache image table ABI");
_Static_assert(sizeof(struct cache_subcache)==56,"cache member ABI");
_Static_assert(sizeof(struct cache_slide_map)==56,"cache mapping and slide ABI");
static struct cache_file cache_files[CACHE_MAX_FILES];
static unsigned int cache_file_count;
static void cache_open_file(const char *path,unsigned int index)
{
    need(index<CACHE_MAX_FILES,"too many declared cache members");
    struct cache_file *file=cache_files+index;
    file->fd=open(path,O_RDONLY|O_CLOEXEC);if(file->fd<0)die("open declared cache member");
    struct stat st;if(fstat(file->fd,&st))die("stat cache member");
    need(st.st_size>=16384,"truncated cache member");file->size=st.st_size;
    file->bytes=mmap(NULL,file->size,PROT_READ,MAP_PRIVATE,file->fd,0);
    if(file->bytes==MAP_FAILED)die("map cache metadata");
    struct cache_prefix prefix;memcpy(&prefix,file->bytes,sizeof(prefix));
    need(!memcmp(prefix.magic,"dyld_v1   arm64",sizeof("dyld_v1   arm64")),"only non-arm64e ARM64 cache members are supported");
    need(prefix.mappings_offset>=sizeof(struct cache_header) && prefix.mappings_offset<=file->size,"unsupported cache header geometry");
    memcpy(&file->header,file->bytes,sizeof(file->header));
    need(prefix.mappings_count && prefix.mappings_count<=CACHE_MAX_MAPS &&
         range(prefix.mappings_offset,(uint64_t)prefix.mappings_count*sizeof(struct cache_map),file->size),"cache member mapping table bounds");
    cache_file_count=index+1;
}
static void cache_open_members(const char *path)
{
    cache_open_file(path,0);
    struct cache_file *primary=cache_files;
    const struct cache_header *header=&primary->header;
    need(header->subcaches_count<CACHE_MAX_FILES &&
         range(header->subcaches_offset,(uint64_t)header->subcaches_count*sizeof(struct cache_subcache),primary->size),"cache member declarations");
    for(unsigned int i=0;i<header->subcaches_count;i++) {
        struct cache_subcache member;
        memcpy(&member,primary->bytes+header->subcaches_offset+(uint64_t)i*sizeof(member),sizeof(member));
        need(member.suffix[0]=='.' && memchr(member.suffix,0,sizeof(member.suffix)) &&
             !strchr(member.suffix,'/') && !strchr(member.suffix,'\\'),"unsafe cache member suffix");
        char name[8192];int size=snprintf(name,sizeof(name),"%s%s",path,member.suffix);
        need(size>0 && (size_t)size<sizeof(name),"cache member path overflow");cache_open_file(name,i+1);
        const struct cache_file *file=cache_files+i+1;
        need(!memcmp(file->header.uuid,member.uuid,sizeof(member.uuid)),"cache member UUID mismatch");
        struct cache_map first;memcpy(&first,file->bytes+file->header.common.mappings_offset,sizeof(first));
        need(member.vm_offset<=UINT64_MAX-header->region_start && first.address==header->region_start+member.vm_offset,"cache member VM identity mismatch");
    }
}
static void cache_map_members(void)
{
    for(unsigned int f=0;f<cache_file_count;f++) {
        const struct cache_file *file=cache_files+f;
        const struct cache_header *header=&file->header;
        for(unsigned int i=0;i<header->common.mappings_count;i++) {
            need(cache_map_count<CACHE_MAX_MAPS,"too many cache VM mappings");
            struct cache_map mapping;
            memcpy(&mapping,file->bytes+header->common.mappings_offset+(uint64_t)i*sizeof(mapping),sizeof(mapping));
            need(mapping.size && !(mapping.size&(PAGE16-1)) && !(mapping.address&(PAGE16-1)) && !(mapping.offset&(PAGE16-1)) &&
                 range(mapping.offset,mapping.size,file->size) && mapping.address>=PAGE16 &&
                 range(mapping.address,mapping.size,0x0000800000000000ULL) && !(mapping.prot&~7U) &&
                 !(mapping.prot&2 && mapping.prot&4),"invalid cache member VM geometry");
            for(unsigned int j=0;j<cache_map_count;j++)
                need(mapping.address>=cache_maps[j].address+cache_maps[j].size || cache_maps[j].address>=mapping.address+mapping.size,"overlapping declared cache regions");
            void *mapped=mmap((void *)mapping.address,mapping.size,PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_FIXED_NOREPLACE,file->fd,mapping.offset);
            if(mapped==MAP_FAILED)die("map declared cache VM region");
            cache_maps[cache_map_count++]=mapping;
            fprintf(stderr,"NATIVE_CACHE_MAP member=%u address=%#" PRIx64 " bytes=%#" PRIx64 " prot=%u\n",f,mapping.address,mapping.size,mapping.prot);
        }
    }
}
static void cache_rebase_members(void)
{
    for(unsigned int f=0;f<cache_file_count;f++) {
        const struct cache_file *file=cache_files+f;
        const struct cache_header *header=&file->header;
        need(header->slide_maps_count==header->common.mappings_count &&
             range(header->slide_maps_offset,(uint64_t)header->slide_maps_count*sizeof(struct cache_slide_map),file->size),"cache slide mapping table bounds");
        for(unsigned int i=0;i<header->slide_maps_count;i++) {
            struct cache_slide_map mapping;
            memcpy(&mapping,file->bytes+header->slide_maps_offset+(uint64_t)i*sizeof(mapping),sizeof(mapping));
            struct cache_map base;memcpy(&base,file->bytes+header->common.mappings_offset+(uint64_t)i*sizeof(base),sizeof(base));
            need(mapping.address==base.address && mapping.size==base.size && mapping.offset==base.offset && mapping.prot==base.prot,"cache slide/VM mapping mismatch");
            if(!mapping.slide_size)continue;
            need(range(mapping.slide_offset,mapping.slide_size,file->size) && mapping.slide_size>=sizeof(struct cache_slide2),"cache slide info bounds");
            struct cache_slide2 slide;memcpy(&slide,file->bytes+mapping.slide_offset,sizeof(slide));
            need(slide.version==2 && (slide.page_size==4096 || slide.page_size==16384) && slide.delta_mask &&
                 __builtin_ctzll(slide.delta_mask)>=2 && range(slide.starts_offset,(uint64_t)slide.starts_count*2,mapping.slide_size) &&
                 range(slide.extras_offset,(uint64_t)slide.extras_count*2,mapping.slide_size) &&
                 (uint64_t)slide.starts_count*slide.page_size<=mapping.size,"unsupported or invalid cache pointer-chain format");
            uint64_t mask=slide.delta_mask>>__builtin_ctzll(slide.delta_mask);need(!(mask&(mask+1)),"noncontiguous cache delta mask");
            const unsigned char *starts=file->bytes+mapping.slide_offset+slide.starts_offset;
            const unsigned char *extras=file->bytes+mapping.slide_offset+slide.extras_offset;
            for(uint32_t page=0;page<slide.starts_count;page++) {
                uint16_t entry=cache_u16(starts+page*2);if(entry==0x4000)continue;
                unsigned char *bytes=(void *)(mapping.address+(uint64_t)page*slide.page_size);
                if(!(entry&0x8000))cache_relocate_chain(bytes,entry*4,&slide);
                else for(uint32_t index=entry&0x3fff;;index++) {
                    need(index<slide.extras_count,"cache extra chain bounds");uint16_t chain=cache_u16(extras+index*2);
                    cache_relocate_chain(bytes,(chain&0x3fff)*4,&slide);if(chain&0x8000)break;
                }
            }
        }
    }
}

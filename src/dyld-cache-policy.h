/* Explicit native-adapter policy for genuine libdispatch, resolved by symbols
 * in the selected cache. No firmware-specific function/data addresses. The
 * ordinary pthread workqueue backend is real; kevent delivery is not offered. */
#pragma once
struct cache_nlist64 { uint32_t string; uint8_t type,section; uint16_t desc; uint64_t value; };
static uint64_t cache_table_symbol(const unsigned char *file,uint64_t size,
                                  uint64_t offset,uint32_t count,uint64_t strings,
                                  uint32_t string_bytes,const char *name)
{
    need(range(offset,(uint64_t)count*sizeof(struct cache_nlist64),size) &&
         range(strings,string_bytes,size),"cache symbol table bounds");
    for (uint32_t i=0;i<count;i++) {
        struct cache_nlist64 item;
        memcpy(&item,file+offset+(uint64_t)i*sizeof(item),sizeof(item));
        if ((item.type&0xe0) || (item.type&0x0e)!=0x0e || !item.section || item.string>=string_bytes) continue;
        const char *text=(void *)(file+strings+item.string);
        need(memchr(text,0,string_bytes-item.string)!=NULL,"cache symbol name termination");
        if (!strcmp(text,name)) return item.value;
    }
    return 0;
}
static void cache_dispatch_policy(const unsigned char *file,uint64_t size,uint64_t image)
{
    const char *name="__dispatch_kevent_workqueue_enabled";
    uint64_t address=0;
    const unsigned char *bytes=(void *)image;
    struct header mh; memcpy(&mh,bytes,sizeof(mh));
    uint64_t pos=sizeof(mh),end=pos+mh.sizeofcmds;
    for (uint32_t i=0;i<mh.ncmds;i++) {
        struct command command; memcpy(&command,bytes+pos,sizeof(command));
        need(command.size>=sizeof(command) && range(pos,command.size,end),"dispatch symbol command bounds");
        if (command.cmd==2) { /* Mach-O LC_SYMTAB */
            uint32_t table[4];
            need(command.size>=sizeof(command)+sizeof(table),"dispatch symtab command");
            memcpy(table,bytes+pos+sizeof(command),sizeof(table));
            address=cache_table_symbol(file,size,table[0],table[1],table[2],table[3],name);
        }
        pos+=command.size;
    }
    if (!address) {
        /* Legacy cache local-symbol entries are keyed by the image's file offset. */
        uint64_t image_offset=UINT64_MAX,locals[2];
        for (unsigned int i=0;i<3;i++) if (image>=cache_maps[i].address &&
            image-cache_maps[i].address<cache_maps[i].size)
            image_offset=cache_maps[i].offset+image-cache_maps[i].address;
        need(range(72,sizeof(locals),size),"cache local-symbol header");
        memcpy(locals,file+72,sizeof(locals));
        need(locals[1]>=24 && range(locals[0],locals[1],size),"cache local-symbol extent");
        uint32_t info[6]; memcpy(info,file+locals[0],sizeof(info));
        need(range(info[4],(uint64_t)info[5]*12,locals[1]) &&
             range(info[0],(uint64_t)info[1]*16,locals[1]) &&
             range(info[2],info[3],locals[1]),"cache local-symbol tables");
        for (uint32_t i=0;i<info[5] && !address;i++) {
            uint32_t entry[3]; memcpy(entry,file+locals[0]+info[4]+(uint64_t)i*sizeof(entry),sizeof(entry));
            if (entry[0]!=image_offset) continue;
            need(range(entry[1],entry[2],info[1]),"cache local-symbol image range");
            address=cache_table_symbol(file,size,locals[0]+info[0]+(uint64_t)entry[1]*16,
                                       entry[2],locals[0]+info[2],info[3],name);
        }
    }
    need(address && cache_contains(address,1,PROT_WRITE),"dispatch ordinary-workqueue policy symbol");
    *(uint8_t *)address=0;
    fprintf(stderr,"NATIVE_CACHE_POLICY symbol=%s address=%#" PRIx64 " value=0 ordinary_linux_workers=enabled kevent=unsupported\n",
            name,address);
}

/* Real file mappings for dyld. Only Mach-O instruction sections are adapted;
 * original files and non-instruction bytes are never rewritten. */
#pragma once
#include "arm64-adapt.h"
enum {
    N_MH_MAGIC_64=0xfeedfacfU, N_FAT_CIGAM=0xbebafecaU,
    N_CPU_ARM64=0x0100000c, N_LC_SEGMENT_64=0x19,
    N_S_INSTRUCTIONS=0x80000400U,
    N_MAX_LOAD_COMMAND_BYTES=1024*1024, N_MAX_CODE_RANGES=512
};
struct native_mach_header { uint32_t magic,cpu,subtype,type,ncmds,sizeofcmds,flags,reserved; };
struct native_mach_command { uint32_t type,size; };
struct native_mach_segment {
    struct native_mach_command command; char name[16];
    uint64_t address,size,offset,filesize; int32_t maxprot,prot;
    uint32_t nsects,flags;
};
struct native_mach_section {
    char name[16],segment[16]; uint64_t address,size;
    uint32_t offset,align,reloff,nreloc,flags,reserved1,reserved2,reserved3;
};
struct native_code_range { uint64_t offset,size; };
_Static_assert(sizeof(struct native_mach_segment)==72,"Mach-O LP64 segment ABI");
_Static_assert(sizeof(struct native_mach_section)==80,"Mach-O LP64 section ABI");
static int native_mach_range(uint64_t start,uint64_t size,uint64_t limit)
{ return start<=limit && size<=limit-start; }
static long native_file_code_ranges(int fd,uint64_t mapped_offset,uint64_t mapped_size,
                                    struct native_code_range *ranges,unsigned int *count)
{
    struct stat info;
    long status=RAW(SYS_fstat,fd,&info,0,0,0,0);
    if (status<0) return status;
    if ((info.st_mode&S_IFMT)!=S_IFREG || info.st_size<0) return -ENOEXEC;
    uint64_t base=0,limit=info.st_size;
    struct native_mach_header header;
    status=native_file_read(fd,&header,sizeof(header),0);
    if (status) return status;
    if (header.magic==N_FAT_CIGAM) {
        /* FAT32 big-endian architecture records, including old universal IPAs. */
        uint32_t nfat=__builtin_bswap32(header.cpu);
        if (!nfat || nfat>64 || !native_mach_range(8,(uint64_t)nfat*20,limit)) return -ENOEXEC;
        int found=0;
        for (uint32_t i=0;i<nfat;i++) {
            uint32_t arch[5];
            status=native_file_read(fd,arch,sizeof(arch),8+(uint64_t)i*sizeof(arch));
            if (status) return status;
            uint32_t cpu=__builtin_bswap32(arch[0]),subtype=__builtin_bswap32(arch[1])&0xffffff;
            if (cpu!=N_CPU_ARM64 || subtype>1) continue;
            base=__builtin_bswap32(arch[2]); uint64_t size=__builtin_bswap32(arch[3]);
            if (!native_mach_range(base,size,limit) || size<sizeof(header)) return -ENOEXEC;
            limit=base+size; found=1; break;
        }
        if (!found) return -ENOEXEC;
        status=native_file_read(fd,&header,sizeof(header),base);
        if (status) return status;
    }
    if (header.magic!=N_MH_MAGIC_64 || header.cpu!=N_CPU_ARM64 || (header.subtype&0xffffff)>1 ||
        header.sizeofcmds>N_MAX_LOAD_COMMAND_BYTES || header.ncmds>header.sizeofcmds/sizeof(struct native_mach_command) ||
        !native_mach_range(base+sizeof(header),header.sizeofcmds,limit)) return -ENOEXEC;
    uint64_t cursor=base+sizeof(header),end=cursor+header.sizeofcmds;
    *count=0;
    for (uint32_t i=0;i<header.ncmds;i++) {
        struct native_mach_command command;
        if (!native_mach_range(cursor,sizeof(command),end)) return -ENOEXEC;
        status=native_file_read(fd,&command,sizeof(command),cursor);
        if (status) return status;
        if (command.size<sizeof(command) || !native_mach_range(cursor,command.size,end)) return -ENOEXEC;
        if (command.type==N_LC_SEGMENT_64) {
            struct native_mach_segment segment;
            if (command.size<sizeof(segment)) return -ENOEXEC;
            status=native_file_read(fd,&segment,sizeof(segment),cursor);
            if (status) return status;
            if (segment.nsects>(command.size-sizeof(segment))/sizeof(struct native_mach_section) ||
                !native_mach_range(segment.offset,segment.filesize,limit-base)) return -ENOEXEC;
            for (uint32_t j=0;j<segment.nsects;j++) {
                struct native_mach_section section;
                status=native_file_read(fd,&section,sizeof(section),cursor+sizeof(segment)+(uint64_t)j*sizeof(section));
                if (status) return status;
                if (!(section.flags&N_S_INSTRUCTIONS) || !section.size) continue;
                if (!(segment.prot&PROT_EXEC) || (section.offset&3) || (section.size&3) ||
                    section.offset<segment.offset || section.address<segment.address ||
                    section.offset-segment.offset!=section.address-segment.address ||
                    !native_mach_range(section.offset-segment.offset,section.size,segment.filesize)) return -ENOEXEC;
                uint64_t start=base+section.offset,finish=start+section.size;
                if (start<mapped_offset) start=mapped_offset;
                if (finish>mapped_offset+mapped_size) finish=mapped_offset+mapped_size;
                if (finish<=start) continue;
                if (((start-mapped_offset)&3) || ((finish-start)&3)) return -ENOEXEC;
                if (*count==N_MAX_CODE_RANGES) return -ENOTSUP;
                struct native_code_range range={start-mapped_offset,finish-start};
                for (unsigned int k=0;k<*count;k++)
                    if (range.offset<ranges[k].offset+ranges[k].size && ranges[k].offset<range.offset+range.size)
                        return -ENOEXEC;
                ranges[(*count)++]=range;
            }
        }
        cursor+=command.size;
    }
    return 0;
}
static long native_resilient_mapping(unsigned long address,unsigned long length,int protection,int fd,unsigned long offset)
{
    /* A private eager snapshot preserves actual available file bytes and
     * zero-fills unavailable media/EOF, without a later Linux SIGBUS. Private
     * mappings do not promise visibility of subsequent backing-file changes.
     * Executable resilient mappings still require a separate implementation. */
    long status=RAW(SYS_fcntl,fd,F_GETFL,0,0,0,0);
    if (status<0) return status;
    if ((status&O_ACCMODE)==O_WRONLY) return -EACCES;
    struct stat info;
    status=RAW(SYS_fstat,fd,&info,0,0,0,0);
    if (status<0) return status;
    if ((info.st_mode&S_IFMT)!=S_IFREG) return -ENODEV;
    status=RAW(SYS_mmap,address,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if ((unsigned long)status>=(unsigned long)-4095) return status;
    unsigned long copied=0,missing=0;
    int eof=0;
    for (unsigned long page=0;page<length && !eof;page+=PAGE16) {
        unsigned long done=0;
        while (done<PAGE16) {
            long count=RAW(SYS_pread64,fd,address+page+done,PAGE16-done,offset+page+done,0,0);
            if (count==-EINTR) continue;
            if (!count) { copied+=done; eof=1; break; }
            if (count<0) {
                if (count!=-EIO && count!=-ENXIO && count!=-ENODEV && count!=-ESTALE &&
                    count!=-ENETDOWN && count!=-ENETUNREACH && count!=-ETIMEDOUT && count!=-EREMOTEIO) {
                    RAW(SYS_munmap,address,length,0,0,0,0); return count;
                }
                volatile unsigned char *bytes=(void *)(address+page);
                for (unsigned long i=0;i<PAGE16;i++) bytes[i]=0;
                missing++; break;
            }
            done+=count;
            if (done==PAGE16) copied+=done;
        }
    }
    status=RAW(SYS_mprotect,address,length,protection,0,0,0);
    if (status) { RAW(SYS_munmap,address,length,0,0,0,0);return status; }
    trace_record("DARWIN_RESILIENT_MMAP copied=0x",copied,length,missing);
    return address;
}
static long native_file_mmap(const unsigned long long *r)
{
    /* MAP_UNIX03 selects POSIX validation, not a Linux mapping flag. The
     * checks below and Linux mmap enforce our supported POSIX mapping subset. */
    unsigned long address=r[0],length=r[1],protection=r[2],flags=r[3]&~(unsigned long)D_MAP_UNIX03,offset=r[5];
    /* XNU ignores MEDIA for shared mappings: zero replacement must never
     * modify the real shared file. Preserve that existing shared behavior. */
    if (flags&D_MAP_SHARED) flags&=~(unsigned long)D_MAP_RESILIENT_MEDIA;
    int fd=(int)r[4];
    if (!length || length>D_VM_MAX_ALLOCATION || (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC)) ||
        flags&~(unsigned long)(D_MAP_SHARED|D_MAP_PRIVATE|D_MAP_FIXED|D_MAP_RESILIENT_MEDIA) ||
        ((flags&(D_MAP_SHARED|D_MAP_PRIVATE))!=D_MAP_SHARED && (flags&(D_MAP_SHARED|D_MAP_PRIVATE))!=D_MAP_PRIVATE) ||
        (offset&(PAGE16-1)) || offset>INT64_MAX || length>INT64_MAX-offset) return -EINVAL;
    length=(length+PAGE16-1)&~(PAGE16-1);
    if (length>INT64_MAX-offset) return -EINVAL;
    if ((flags&D_MAP_FIXED) && ((address&(PAGE16-1)) || address<PAGE16 ||
        address>=D_GUEST_COMMPAGE || length>D_GUEST_COMMPAGE-address)) return -EINVAL;
    if ((protection&PROT_EXEC) && !(flags&D_MAP_PRIVATE)) return -ENOTSUP;
    if ((flags&D_MAP_RESILIENT_MEDIA) && (protection&PROT_EXEC)) return -ENOTSUP;
    struct native_code_range ranges[N_MAX_CODE_RANGES]; unsigned int count=0;
    long status=0;
    if (protection&PROT_EXEC) {
        status=native_file_code_ranges(fd,offset,length,ranges,&count);
        if (status) return status;
    }
    int reserved=!(flags&D_MAP_FIXED);
    if (reserved) {
        status=anonymous_aligned(length,PROT_NONE,PAGE16-1);
        if ((unsigned long)status>=(unsigned long)-4095) return status;
        address=status;
    }
    if (flags&D_MAP_RESILIENT_MEDIA) {
        status=native_resilient_mapping(address,length,protection,fd,offset);
        if ((unsigned long)status>=(unsigned long)-4095 && reserved) RAW(SYS_munmap,address,length,0,0,0,0);
        return status;
    }
    int linux_flags=(flags&D_MAP_SHARED ? MAP_SHARED : MAP_PRIVATE)|MAP_FIXED;
    int initial_protection=protection&PROT_EXEC ? PROT_READ|PROT_WRITE : protection;
    status=RAW(SYS_mmap,address,length,initial_protection,linux_flags,fd,offset);
    if ((unsigned long)status>=(unsigned long)-4095) {
        if (reserved) RAW(SYS_munmap,address,length,0,0,0,0);
        return status;
    }
    if (protection&PROT_EXEC) {
        size_t rewritten=0;
        for (unsigned int i=0;i<count;i++)
            rewritten+=rewrite_instruction_words((uint32_t *)(address+ranges[i].offset),ranges[i].size/4);
        __builtin___clear_cache((char *)address,(char *)(address+length));
        status=RAW(SYS_mprotect,address,length,protection,0,0,0);
        if (status) { RAW(SYS_munmap,address,length,0,0,0,0); return status; }
        const char label[]="NATIVE_DYNAMIC_TLS_REWRITE ranges/instructions/address/bytes follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,count); trace_call(1,0,rewritten); trace_call(2,0,address); trace_call(3,0,length);
    }
    return address;
}
static long native_mmap(const unsigned long long *r)
{
    /* XNU kern_mman.c uses MAP_ANON's fd as VM allocation metadata, not
     * a file descriptor. -1 and alias/tag-only values both request ordinary
     * real anonymous memory. Purgeable/superpage/chunk semantics are not
     * silently treated as ordinary mappings. Darwin's fd is signed 32-bit. */
    uint32_t allocation_flags=(uint32_t)r[4];
    if ((r[3]&~(unsigned long)D_MAP_UNIX03)==(D_MAP_PRIVATE|D_MAP_ANON) &&
        ((int32_t)allocation_flags==-1 || !(allocation_flags&~D_VM_FLAGS_TAG_MASK)) && !r[5] && !r[0]) {
        long result=darwin_native_memory(r[1],r[2]);
        static unsigned int anonymous_traces;
        if (__atomic_fetch_add(&anonymous_traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT)
            trace_record("DARWIN_ANON_MMAP tag=0x",allocation_flags,r[1],result);
        return result;
    }
    long result=native_file_mmap(r);
    static unsigned int traces;
    if (__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARWIN_FILE_MMAP address/bytes/protection/flags/fd/offset/result follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        for (unsigned int i=0;i<6;i++) trace_call(i,0,r[i]);
        trace_call(6,0,result);
    }
    return result;
}

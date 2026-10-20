/* XNU cs_blobs.h / kern_descrip.c F_ADDFILESIGS_RETURN coverage contract.
 * This trusted-input Linux runtime inspects real on-file metadata but does
 * not authenticate Apple signatures or mark a process CS_VALID/platform. */
#pragma once
enum {
    N_CS_EMBEDDED_SIGNATURE=0xfade0cc0U,N_CS_CODEDIRECTORY=0xfade0c02U,
    N_CS_PRIMARY_DIRECTORY=0,N_CS_MAX_BLOB=16*1024*1024,N_CS_MAX_INDEX=128,
    N_CS_SCATTER_VERSION=0x20100,N_CS_LIMIT64_VERSION=0x20300
};
struct native_code_directory {
    uint32_t magic,length,version,flags,hash_offset,identifier_offset;
    uint32_t special_slots,code_slots,code_limit;
    uint8_t hash_size,hash_type,platform,page_shift;
    uint32_t spare;
};
_Static_assert(sizeof(struct native_code_directory)==44,"CodeDirectory prefix ABI");
static long native_file_signature_coverage(int fd,uint64_t argument)
{
    struct darwin_fsignatures request;
    long error=guest_copy(&request,argument,sizeof(request),0);if(error) return error;
    struct stat file;
    error=RAW(SYS_fstat,fd,&file,0,0,0,0);if(error) return error;
    if ((file.st_mode&S_IFMT)!=S_IFREG || file.st_size<0 || request.file_start<0 ||
        (uint64_t)request.file_start>(uint64_t)file.st_size) return -EINVAL;
    uint64_t slice_size=file.st_size-request.file_start;
    if (request.blob_start>slice_size || request.blob_size>slice_size-request.blob_start ||
        request.blob_size<8 || request.blob_size>N_CS_MAX_BLOB) return -EINVAL;
    uint64_t blob=request.file_start+request.blob_start;
    uint32_t prefix[3];
    error=native_file_read(fd,prefix,8,blob);if(error) return error;
    uint32_t magic=__builtin_bswap32(prefix[0]),length=__builtin_bswap32(prefix[1]);
    if (length>request.blob_size || length<8) return -EINVAL;
    uint64_t directory=0,available=length;
    if (magic==N_CS_EMBEDDED_SIGNATURE) {
        if (length<sizeof(prefix)) return -EINVAL;
        error=native_file_read(fd,prefix,sizeof(prefix),blob);if(error) return error;
        uint32_t count=__builtin_bswap32(prefix[2]);
        if (!count || count>N_CS_MAX_INDEX || count>(length-sizeof(prefix))/8) return -EINVAL;
        int found=0;
        for (uint32_t i=0;i<count;i++) {
            uint32_t index[2];
            error=native_file_read(fd,index,sizeof(index),blob+sizeof(prefix)+(uint64_t)i*8);
            if(error) return error;
            if (__builtin_bswap32(index[0])!=N_CS_PRIMARY_DIRECTORY) continue;
            if (found) return -EINVAL;
            directory=__builtin_bswap32(index[1]);
            if (directory<sizeof(prefix)+(uint64_t)count*8 || directory>length) return -EINVAL;
            available=length-directory;found=1;
        }
        if (!found) return -ENOTSUP;
    } else if (magic!=N_CS_CODEDIRECTORY) return -ENOTSUP;
    struct native_code_directory cd;
    if (available<sizeof(cd)) return -EINVAL;
    error=native_file_read(fd,&cd,sizeof(cd),blob+directory);if(error) return error;
    uint32_t size=__builtin_bswap32(cd.length),version=__builtin_bswap32(cd.version);
    uint64_t limit=__builtin_bswap32(cd.code_limit);
    if (__builtin_bswap32(cd.magic)!=N_CS_CODEDIRECTORY || size<sizeof(cd) || size>available ||
        !cd.hash_size || cd.page_shift>30) return -EINVAL;
    if (version>=N_CS_SCATTER_VERSION) {
        uint32_t scatter;
        if (size<sizeof(cd)+sizeof(scatter)) return -EINVAL;
        error=native_file_read(fd,&scatter,sizeof(scatter),blob+directory+sizeof(cd));if(error) return error;
        if (scatter) return -ENOTSUP; /* Contiguous file coverage only. */
    }
    if (version>=N_CS_LIMIT64_VERSION) {
        uint64_t extended;
        if (size<64) return -EINVAL;
        error=native_file_read(fd,&extended,sizeof(extended),blob+directory+56);if(error) return error;
        extended=__builtin_bswap64(extended);if(extended) limit=extended;
    }
    uint64_t hash_offset=__builtin_bswap32(cd.hash_offset);
    uint64_t slots=__builtin_bswap32(cd.code_slots),special=__builtin_bswap32(cd.special_slots);
    if (hash_offset>size || slots*cd.hash_size>size-hash_offset ||
        special*cd.hash_size>hash_offset || limit>slice_size) return -EINVAL;
    uint64_t page=cd.page_shift ? 1ULL<<cd.page_shift : limit;
    if (!page || slots<(limit+page-1)/page) return -EINVAL;
    uint64_t coverage=cd.page_shift ? (limit+page-1)&~(page-1) : limit;
    if (coverage>INT64_MAX) return -EOVERFLOW;
    error=guest_copy(&coverage,argument,sizeof(coverage),1);
    trace_record("DARWIN_FILE_SIGNATURE_COVERAGE bytes=0x",coverage,request.blob_start,error);
    return error;
}

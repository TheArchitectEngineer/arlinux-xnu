/* Linux-backed directory ABI adapter. Reference: VibeDarling/darling-xnu
 * 40bc7ec, libsystem_kernel's stat/fstatfs.c and dirent/getdirentries.c.
 * Use real descriptors, mount metadata and seek cookies; no synthetic bundle
 * entries. All operations remain TLS-independent under guest libSystem. */
#pragma once
static long native_file_read(int fd,void *output,size_t size,uint64_t offset)
{
    size_t done=0;
    while (done<size) {
        long result=RAW(SYS_pread64,fd,(char *)output+done,size-done,offset+done,0,0);
        if (result==-EINTR) continue;
        if (result<0) return result;
        if (!result) return -ENOEXEC;
        done+=result;
    }
    return 0;
}
static long native_fd_path(int fd,char path[1024])
{
    char link[64]="/proc/self/fd/",digits[12];
    unsigned int n=14,k=0,value=(unsigned int)fd;
    do { digits[k++]='0'+value%10; value/=10; } while(value);
    while(k) link[n++]=digits[--k];
    link[n]=0;
    long bytes=RAW(SYS_readlinkat,AT_FDCWD,link,path,1023,0,0);
    if (bytes<0) return bytes;
    if (bytes==1023) return -ENAMETOOLONG;
    path[bytes]=0; return 0;
}
static int native_mount_token(char **cursor,char *out,unsigned int capacity)
{
    while (**cursor==' ') (*cursor)++;
    unsigned int n=0;
    while (**cursor && **cursor!=' ') {
        unsigned char c=*(*cursor)++;
        if (c=='\\' && (*cursor)[0]>='0' && (*cursor)[0]<='7' &&
            (*cursor)[1]>='0' && (*cursor)[1]<='7' && (*cursor)[2]>='0' && (*cursor)[2]<='7') {
            c=((*cursor)[0]-'0')*64+((*cursor)[1]-'0')*8+(*cursor)[2]-'0';
            *cursor+=3;
        }
        if (n+1>=capacity) return 0;
        out[n++]=c;
    }
    out[n]=0; return n!=0;
}
static long native_mapped_identity_path(char *line,uint32_t device,uint64_t inode,
                                       unsigned long destination,unsigned long capacity)
{
    char token[128],*cursor=line;
    for(unsigned int i=0;i<5;i++)
        if(!native_mount_token(&cursor,token,sizeof(token))) return 0;
    uint64_t object=0;
    for(unsigned int i=0;token[i];i++) {
        if(token[i]<'0' || token[i]>'9' || object>(UINT64_MAX-(token[i]-'0'))/10) return 0;
        object=object*10+token[i]-'0';
    }
    if(object!=inode) return 0;
    while(*cursor==' ') cursor++;
    if(*cursor!='/') return 0;
    char path[4096];unsigned int length=0;
    while(*cursor) {
        unsigned char c=*cursor++;
        if(c=='\\' && cursor[0]>='0' && cursor[0]<='7' &&
           cursor[1]>='0' && cursor[1]<='7' && cursor[2]>='0' && cursor[2]<='7') {
            c=(cursor[0]-'0')*64+(cursor[1]-'0')*8+cursor[2]-'0';cursor+=3;
        }
        if(!c || length+1>=sizeof(path)) return 0;
        path[length++]=c;
    }
    path[length]=0;
    struct stat actual;
    if(RAW(SYS_newfstatat,AT_FDCWD,path,&actual,0,0,0) ||
       actual.st_ino!=inode || (uint32_t)actual.st_dev!=device) return 0;
    char root[1024];
    long error=native_fd_path(root_descriptor,root);if(error) return error;
    unsigned int prefix=0;while(root[prefix] && root[prefix]==path[prefix]) prefix++;
    const char *guest=path;
    if(root[0]=='/' && !root[1]) { /* Host root is already the guest namespace. */ }
    else if(!root[prefix] && path[prefix]=='/') guest=path+prefix;
    else {
        unsigned int matched=0;
        while(matched<bundle_path_length && path[matched]==bundle_path[matched]) matched++;
        if(bundle_descriptor<0 || matched!=bundle_path_length ||
           (path[matched] && path[matched]!='/')) return 0;
    }
    unsigned int size=0;while(guest[size]) size++;size++;
    if(size>capacity) return -ERANGE;
    error=guest_copy((void *)guest,destination,size,1);
    return error ? error : (long)size;
}
static long native_fsgetpath(const unsigned long long *r)
{
    /* XNU fsgetpath_extended uses fsid.val[0] and the real object ID. Linux
     * has no unprivileged reverse-inode syscall. Resolve currently mapped
     * files from the kernel's maps, then re-stat and translate their actual
     * path into the declared guest root/bundle. Unknown identities fail;
     * this is not a fabricated path or a persistent inode catalogue. */
    uint32_t fsid[2];
    if(!r[1] || r[1]>D_GUEST_PAGE_SIZE) return -EINVAL;
    long error=guest_copy(fsid,r[2],sizeof(fsid),0);if(error) return error;
    long fd=RAW(SYS_openat,AT_FDCWD,"/proc/self/maps",O_RDONLY|O_CLOEXEC,0,0,0);
    if(fd<0) return fd;
    char input[4096],line[8192];unsigned int used=0;int overflow=0;
    long result=-ENOENT;
    for(;;) {
        long size=RAW(SYS_read,fd,input,sizeof(input),0,0,0);
        if(size==-EINTR) continue;
        if(size<0) { result=size;break; }
        if(!size) break;
        for(long i=0;i<size;i++) {
            if(input[i]!='\n') {
                if(used+1<sizeof(line)) line[used++]=input[i];else overflow=1;
                continue;
            }
            line[used]=0;
            long match=overflow ? 0 : native_mapped_identity_path(line,fsid[0],r[3],r[0],r[1]);
            used=0;overflow=0;
            if(match) { result=match;goto done; }
        }
    }
done:
    RAW(SYS_close,fd,0,0,0,0,0);
    trace_record("DARWIN_FSGETPATH object=0x",r[3],fsid[0],result);
    return result;
}
static long native_statfs_value(int fd,struct darwin_statfs64 *value)
{
    struct statfs actual={0};
    long error=RAW(SYS_fstatfs,fd,&actual,0,0,0,0);
    if(error) return error;
    if (actual.f_bsize<=0 || (uint64_t)actual.f_bsize>INT32_MAX) return -EOVERFLOW;
    struct darwin_statfs64 out={0};
    out.block_size=out.io_size=actual.f_bsize;
    out.blocks=actual.f_blocks; out.free_blocks=actual.f_bfree;
    out.available_blocks=actual.f_bavail; out.files=actual.f_files; out.free_files=actual.f_ffree;
    __builtin_memcpy(out.fsid,&actual.f_fsid,sizeof(out.fsid));
    out.type=actual.f_type; /* Opaque Linux filesystem type, as in Darling. */
    /* Linux ST_* -> Darwin MNT_*; unrepresented flags are not claimed. */
    if(actual.f_flags&ST_RDONLY) out.flags|=D_MNT_RDONLY;
    if(actual.f_flags&ST_NOSUID) out.flags|=D_MNT_NOSUID;
    if(actual.f_flags&ST_NODEV) out.flags|=D_MNT_NODEV;
    if(actual.f_flags&ST_NOEXEC) out.flags|=D_MNT_NOEXEC;
    if(actual.f_flags&ST_SYNCHRONOUS) out.flags|=D_MNT_SYNCHRONOUS;
    if(actual.f_flags&ST_NOATIME) out.flags|=D_MNT_NOATIME;
    char path[1024]; error=native_fd_path(fd,path); if(error) return error;
    long mounts=RAW(SYS_openat,AT_FDCWD,"/proc/self/mounts",O_RDONLY|O_CLOEXEC,0,0,0);
    if(mounts<0) return mounts;
    char buffer[1024],line[4096]; unsigned int used=0,best=0; int overflow=0;
    for (;;) {
        long bytes=RAW(SYS_read,mounts,buffer,sizeof(buffer),0,0,0);
        if(bytes<0) { error=bytes; break; }
        if(!bytes) break;
        for(long i=0;i<bytes;i++) {
            if(buffer[i]!='\n') {
                if(used+1<sizeof(line)) line[used++]=buffer[i]; else overflow=1;
                continue;
            }
            line[used]=0;
            if(!overflow) {
                char source[1024],point[1024],type[16],*cursor=line;
                if(native_mount_token(&cursor,source,sizeof(source)) &&
                   native_mount_token(&cursor,point,sizeof(point)) &&
                   native_mount_token(&cursor,type,sizeof(type))) {
                    unsigned int n=0; while(point[n] && path[n]==point[n]) n++;
                    if(!point[n] && n>=best && (n==1 || !path[n] || path[n]=='/')) {
                        best=n;
                        __builtin_memcpy(out.mount_point,point,n+1);
                        unsigned int s=0; while(source[s]) s++;
                        __builtin_memcpy(out.mount_source,source,s+1);
                        s=0; while(type[s]) s++;
                        __builtin_memcpy(out.type_name,type,s+1);
                    }
                }
            }
            used=0; overflow=0;
        }
    }
    RAW(SYS_close,mounts,0,0,0,0,0);
    if(error) return error;
    if(!best) return -ENOENT;
    *value=out;
    return 0;
}
static long native_fstatfs(int fd,unsigned long destination)
{
    struct darwin_statfs64 value;
    long error=native_statfs_value(fd,&value);
    return error ? error : guest_copy(&value,destination,sizeof(value),1);
}
static long native_statfs(unsigned long address,unsigned long destination)
{
    char buffer[4096],*path=buffer;
    long error=guest_path(address,buffer);if(error)return error;
    int directory=path_directory(&path);
    /* statfs follows the named object, including symlinks. Reuse the actual
     * mount/space metadata and Darwin packing used by fstatfs64. O_PATH avoids
     * imposing file-read permission on this metadata query. */
    long fd=RAW(SYS_openat,directory,path,O_PATH|O_CLOEXEC,0,0,0);
    if(fd<0)return fd;
    struct darwin_statfs64 value;
    error=native_statfs_value(fd,&value);
    RAW(SYS_close,fd,0,0,0,0,0);
    return error ? error : guest_copy(&value,destination,sizeof(value),1);
}
static long native_getfsstat(const unsigned long long *r)
{
    /* XNU vfs_syscalls.c:getfsstat64: NULL asks for the count; otherwise
     * return at most the number of complete statfs64 records that fit.
     * Enumerate only filesystems exposed by our declared root/bundle mapping,
     * using the same real fd-backed metadata as fstatfs/getattrlist. */
    if((int)r[1]<0 || (r[2]!=0 && r[2]!=1 && r[2]!=2))return -EINVAL;
    int descriptors[]={root_descriptor,bundle_descriptor};
    struct darwin_statfs64 values[2];
    unsigned int count=0;
    for(unsigned int i=0;i<2;i++) {
        if(descriptors[i]<0)continue;
        struct darwin_statfs64 value;
        long error=native_statfs_value(descriptors[i],&value);
        if(error)return error;
        int duplicate=0;
        for(unsigned int j=0;j<count;j++)
            if(value.type==values[j].type && value.fsid[0]==values[j].fsid[0] && value.fsid[1]==values[j].fsid[1])duplicate=1;
        if(duplicate)continue;
        __builtin_memset(value.mount_point,0,sizeof(value.mount_point));
        if(i==0)value.mount_point[0]='/';
        else {
            if(bundle_path_length>=sizeof(value.mount_point))return -ENAMETOOLONG;
            __builtin_memcpy(value.mount_point,bundle_path,bundle_path_length+1);
        }
        values[count++]=value;
    }
    if(!r[0])return count;
    unsigned int copied=r[1]/sizeof(values[0]);
    if(copied>count)copied=count;
    long error=guest_copy(values,r[0],copied*sizeof(values[0]),1);
    trace_record("DARWIN_GETFSSTAT visible=0x",count,copied,error);
    return error ? error : copied;
}
static long native_getdirentries(const unsigned long long *r)
{
    /* Linux d_name starts at byte 19; Darwin's inode64 name starts at 21.
     * Repack variable records, retaining real inode/type and opaque cookies. */
    char input[4096],output[8192] __attribute__((aligned(8)));
    unsigned long capacity=r[2];
    if(capacity<24) return -EINVAL;
    long start=RAW(SYS_lseek,r[0],0,SEEK_CUR,0,0,0);
    if(start<0) return start;
    if(r[3] && guest_copy(&start,r[3],sizeof(start),1)) return -EFAULT;
    long bytes=RAW(SYS_getdents64,r[0],input,sizeof(input),0,0,0);
    if(bytes<=0) return bytes;
    unsigned int position=0,used=0; uint64_t cookie=start;
    while(position<(unsigned int)bytes) {
        uint16_t length; uint64_t inode,next;
        if(bytes-position<20) return -EIO;
        __builtin_memcpy(&inode,input+position,8);
        __builtin_memcpy(&next,input+position+8,8);
        __builtin_memcpy(&length,input+position+16,2);
        if(length<20 || length>(unsigned long)bytes-position) return -EIO;
        unsigned int name=0;
        while(19+name<length && input[position+19+name]) name++;
        if(19+name==length || name>UINT16_MAX) return -EIO;
        unsigned int size=(__builtin_offsetof(struct darwin_dirent64,name)+name+1+7)&~7U;
        if(size>capacity-used || size>sizeof(output)-used) {
            long error=RAW(SYS_lseek,r[0],cookie,SEEK_SET,0,0,0);
            if(error<0) return error;
            if(!used) return -EINVAL;
            break;
        }
        struct darwin_dirent64 *entry=(void *)(output+used);
        __builtin_memset(entry,0,size);
        entry->inode=inode; entry->seek_offset=next;
        entry->record_length=size; entry->name_length=name; entry->type=input[position+18];
        __builtin_memcpy(entry->name,input+position+19,name+1);
        used+=size; position+=length; cookie=next;
    }
    long error=guest_copy(output,r[1],used,1);
    return error ? error : (long)used;
}

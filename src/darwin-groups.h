/* TLS-free access to explicit, real application-group storage. The launcher
 * registers only groups declared by the original executable's metadata. */
#pragma once
struct native_group {
    char identifier[D_NATIVE_GROUP_IDENTIFIER_LIMIT],path[4096];
    unsigned int path_length;
    int descriptor;
};
static struct native_group native_groups[D_NATIVE_GROUP_LIMIT];
static unsigned int native_group_count;
static int native_vendor_root=-1;
int darwin_native_vendor_root_register(const char *path)
{
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return -errno;
    if(native_vendor_root>=0)close(native_vendor_root);
    native_vendor_root=fd;
    return 0;
}
static long native_vendor_uuid(const char *vendor,unsigned char *output,unsigned long size)
{
    if(size!=16)return -EINVAL;
    if(native_vendor_root<0)return -ENOTSUP;
    char name[4096];
    long error=guest_path((unsigned long)vendor,name);if(error)return error;
    unsigned int length=0;
    for(;name[length];length++) {
        unsigned char c=name[length];
        if(length>=240 || !((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='.' || c=='-' || c=='_'))return -EINVAL;
    }
    if(!length || name[0]=='.' || name[length-1]=='.')return -EINVAL;
    char file[256]=".vendor-";
    __builtin_memcpy(file+8,name,length+1);
    long fd=RAW(SYS_openat,native_vendor_root,file,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600,0,0);
    if(fd<0)return fd;
    do {error=RAW(SYS_flock,fd,2,0,0,0,0);}while(error==-EINTR); /* LOCK_EX */
    unsigned char bytes[16];struct stat metadata;
    if(!error)error=RAW(SYS_fstat,fd,&metadata,0,0,0,0);
    if(!error && (!S_ISREG(metadata.st_mode) || (metadata.st_size && metadata.st_size!=16)))error=-EIO;
    if(!error && !metadata.st_size) {
        unsigned int done=0;
        while(done<16) {
            long n=RAW(SYS_getrandom,bytes+done,16-done,0,0,0,0);
            if(n==-EINTR)continue;
            if(n<=0) {error=n ? n : -EIO;break;}
            done+=n;
        }
        if(!error) {
            bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
            long n;
            do {n=RAW(SYS_pwrite64,fd,bytes,16,0,0,0);}while(n==-EINTR);
            error=n==16 ? 0 : n<0 ? n : -EIO;
            if(!error)error=RAW(SYS_fsync,fd,0,0,0,0,0);
            if(!error)error=RAW(SYS_fsync,native_vendor_root,0,0,0,0,0);
        }
    } else if(!error) {
        long n;
        do {n=RAW(SYS_pread64,fd,bytes,16,0,0,0);}while(n==-EINTR);
        error=n==16 ? 0 : n<0 ? n : -EIO;
    }
    RAW(SYS_close,fd,0,0,0,0,0);
    return error ? error : guest_copy(bytes,(unsigned long)output,16,1);
}
static int native_group_descriptor(int descriptor)
{
    for(unsigned int i=0;i<native_group_count;i++)
        if(native_groups[i].descriptor==descriptor)return 1;
    return 0;
}
/* Host libc/TLS is still active in this preparation API. */
int darwin_native_group_register(const char *identifier,const char *path)
{
    size_t id_length=strlen(identifier),path_length=strlen(path);
    if(!id_length || id_length>=D_NATIVE_GROUP_IDENTIFIER_LIMIT || !path_length || path_length>=4096)
        return -ENAMETOOLONG;
    for(unsigned int i=0;i<native_group_count;i++)
        if(!strcmp(native_groups[i].identifier,identifier))return -EEXIST;
    if(native_group_count==D_NATIVE_GROUP_LIMIT)return -E2BIG;
    int descriptor=open(path,O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(descriptor<0)return -errno;
    struct native_group *group=&native_groups[native_group_count++];
    memcpy(group->identifier,identifier,id_length+1);memcpy(group->path,path,path_length+1);
    group->path_length=path_length;group->descriptor=descriptor;
    return 0;
}
static long native_group_path(const char *identifier,char *output,unsigned long capacity)
{
    char name[4096];
    long error=guest_path((unsigned long)identifier,name);if(error)return error;
    for(unsigned int i=0;i<native_group_count;i++) {
        struct native_group *group=&native_groups[i];
        unsigned int n=0;while(name[n] && name[n]==group->identifier[n])n++;
        if(name[n] || group->identifier[n])continue;
        if(capacity<=group->path_length)return -ERANGE;
        return guest_copy(group->path,(unsigned long)output,group->path_length+1,1);
    }
    return -ENOENT;
}

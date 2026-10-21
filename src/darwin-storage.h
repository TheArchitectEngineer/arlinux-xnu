/* Writable application storage on actual Linux files. XNU syscalls.master and
 * fcntl.h define the guest ABI; Linux openat2 owns traversal and real errors.
 * Firmware/bundles remain read-only through this adapter. This is not an iOS
 * sandbox or an implementation of Apple data-protection/encryption policy. */
#pragma once
static long native_fd_path(int fd,char path[1024]);

static long native_storage_path(int directory,const char *path,char relative[4096])
{
    int base=container_descriptor;
    const char *base_path=container_path;
    size_t base_length=container_path_length;
    unsigned int n=0;
    char actual[1024];
    long error=native_fd_path(directory,actual);if(error)return error;
    size_t i=0;
    if(base>=0)while(i<base_length && actual[i]==base_path[i])i++;
    if(base<0 || i!=base_length || (actual[i] && actual[i]!='/')) {
        base=-1;
        for(unsigned int g=0;g<native_group_count;g++) {
            struct native_group *group=&native_groups[g];
            i=0;while(i<group->path_length && actual[i]==group->path[i])i++;
            if(i==group->path_length && (!actual[i] || actual[i]=='/')) {
                base=group->descriptor;break;
            }
        }
        if(base<0)return -EROFS;
    }
    {
        if(actual[i]=='/')i++;
        while(actual[i])relative[n++]=actual[i++];
        if(n)relative[n++]='/';
    }
    for(unsigned int i=0;path[i];i++) {
        if(n+1>=4096)return -ENAMETOOLONG;
        relative[n++]=path[i];
    }
    relative[n]=0;
    return base;
}

static long native_storage_parent(int fd,unsigned long address,char relative[4096],char **name,int trim_directory_separator)
{
    char buffer[4096],*path=buffer;
    long error=guest_path(address,buffer);if(error)return error;
    if(buffer[0]!='/' && fd<0 && fd!=D_AT_FDCWD)return -EBADF;
    int directory=(buffer[0]=='/' || fd==D_AT_FDCWD) ? path_directory(&path) : fd;
    long base=native_storage_path(directory,path,relative);if(base<0)return base;
    if(trim_directory_separator) {
        unsigned int n=0;while(relative[n])n++;
        while(n && relative[n-1]=='/')relative[--n]=0;
    }
    char *last=NULL;
    for(char *p=relative;*p;p++)if(*p=='/')last=p;
    const char *parent=".";
    *name=relative;
    if(last) {*last=0;*name=last+1;parent=relative;}
    struct open_how how={.flags=O_PATH|O_DIRECTORY|O_CLOEXEC,
        .resolve=RESOLVE_BENEATH|RESOLVE_NO_MAGICLINKS};
    return RAW(SYS_openat2,base,parent,&how,sizeof(how),0,0);
}

static long native_storage_mkdir(int fd,unsigned long address,unsigned long mode)
{
    char relative[4096],*name;
    long parent=native_storage_parent(fd,address,relative,&name,1);if(parent<0)return parent;
    long result=RAW(SYS_mkdirat,parent,name,mode,0,0,0);
    RAW(SYS_close,parent,0,0,0,0,0);return result;
}
static long native_storage_unlink(int fd,unsigned long address,unsigned long flags)
{
    if(flags&~D_AT_REMOVEDIR)return -EINVAL;
    char relative[4096],*name;
    long parent=native_storage_parent(fd,address,relative,&name,0);if(parent<0)return parent;
    long result=RAW(SYS_unlinkat,parent,name,flags&D_AT_REMOVEDIR ? AT_REMOVEDIR : 0,0,0,0);
    RAW(SYS_close,parent,0,0,0,0,0);return result;
}
static long native_storage_rename(int fromfd,unsigned long from,int tofd,unsigned long to)
{
    char first[4096],second[4096],*source,*target;
    long left=native_storage_parent(fromfd,from,first,&source,0);if(left<0)return left;
    long right=native_storage_parent(tofd,to,second,&target,0);
    long result=right<0 ? right : RAW(SYS_renameat,left,source,right,target,0,0);
    if(right>=0)RAW(SYS_close,right,0,0,0,0,0);
    RAW(SYS_close,left,0,0,0,0,0);return result;
}

/* Read-only Linux object metadata -> XNU getattrlist wire ABI.
 * References in the imported Darling tree: bsd/sys/attr.h,
 * bsd/vfs/vfs_attrlist.c and emulation's helper/xattr/getattrlist_generic.c.
 * Preserve their returned-mask and four-byte packing rules. Apple-only
 * metadata is absent, never fabricated or reported as a supported zero. */
#pragma once
enum darwin_attribute_options {
    D_FSOPT_NOFOLLOW=1, D_FSOPT_NOINMEMUPDATE=2, D_FSOPT_REPORT_FULLSIZE=4,
    D_FSOPT_ATTR_CMN_EXTENDED=0x20
};
enum darwin_common_attributes {
    D_ATTR_NAME=1, D_ATTR_DEVID=2, D_ATTR_FSID=4, D_ATTR_OBJTYPE=8,
    D_ATTR_CRTIME=0x200, D_ATTR_MODTIME=0x400, D_ATTR_CHGTIME=0x800,
    D_ATTR_ACCTIME=0x1000, D_ATTR_OWNERID=0x8000, D_ATTR_GRPID=0x10000,
    D_ATTR_ACCESSMASK=0x20000, D_ATTR_USERACCESS=0x200000,
    D_ATTR_FILEID=0x2000000, D_ATTR_FULLPATH=0x8000000,
    D_ATTR_ERROR=0x20000000, D_ATTR_RETURNED=0x80000000
};
enum darwin_object_attributes {
    D_ATTR_DIR_LINKCOUNT=1, D_ATTR_FILE_LINKCOUNT=1,
    D_ATTR_FILE_TOTALSIZE=2, D_ATTR_FILE_ALLOCSIZE=4,
    D_ATTR_FILE_IOBLOCKSIZE=8, D_ATTR_FILE_DATALENGTH=0x200,
    D_ATTR_FILE_DATAALLOCSIZE=0x400, D_ATTR_EXT_REALDEVID=0x40
};
struct darwin_attrlist {
    uint16_t count,reserved;
    uint32_t common,volume,directory,file,fork;
};
struct darwin_attribute_set { uint32_t common,volume,directory,file,fork; };
struct darwin_attrreference { int32_t offset; uint32_t length; };
_Static_assert(sizeof(struct darwin_attrlist)==24,"XNU attrlist ABI");
_Static_assert(sizeof(struct darwin_attribute_set)==20,"XNU attribute_set ABI");

static unsigned int native_attr_width(uint32_t bit)
{
    switch(bit) {
    case D_ATTR_NAME: case D_ATTR_FULLPATH: case D_ATTR_FSID: case D_ATTR_FILEID:return 8;
    case D_ATTR_CRTIME: case D_ATTR_MODTIME: case D_ATTR_CHGTIME: case D_ATTR_ACCTIME:return 16;
    default:return 4;
    }
}
static void native_attr_pack(char *out,unsigned int *position,const void *value,unsigned int size)
{
    __builtin_memcpy(out+*position,value,size);*position+=size;
}
static long native_attr_guest_path(int fd,char output[1024])
{
    char root[1024];
    long error=native_fd_path(fd,output);if(error)return error;
    error=native_fd_path(root_descriptor,root);if(error)return error;
    unsigned int n=0;while(root[n] && root[n]==output[n])n++;
    if(!root[n] && n>1 && output[n]=='/') {
        unsigned int i=0;do {output[i]=output[n+i];}while(output[i++]);
    } else if(!root[n] && n>1 && !output[n]) {output[0]='/';output[1]=0;}
    /* Bundle files intentionally keep their declared host-side bundle path,
     * as does the existing loader/executable_path interface. */
    return 0;
}
static uint32_t native_attr_object_type(unsigned int mode)
{
    /* XNU vtype, matching Darling's linux_mode_to_vtype. */
    if(S_ISREG(mode))return 1;if(S_ISDIR(mode))return 2;
    if(S_ISBLK(mode))return 3;if(S_ISCHR(mode))return 4;
    if(S_ISLNK(mode))return 5;if(S_ISSOCK(mode))return 6;
    if(S_ISFIFO(mode))return 7;return 0;
}
static long native_attr_object(int fd,const struct darwin_attrlist *request,
                               unsigned long destination,unsigned long capacity,unsigned long options)
{
    struct statx actual={0};
    long error=RAW(SYS_statx,fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,
                   STATX_BASIC_STATS|STATX_BTIME,&actual,0);
    if(error)return error;
    if(actual.stx_dev_major>D_DEV_MAJOR_MAX || actual.stx_dev_minor>D_DEV_MINOR_MAX)return -EOVERFLOW;
    uint32_t device=(actual.stx_dev_major<<D_DEV_MAJOR_SHIFT)|actual.stx_dev_minor;
    struct darwin_attribute_set returned={0};
    uint32_t available=D_ATTR_NAME|D_ATTR_DEVID|D_ATTR_ERROR;
    if(actual.stx_mask&STATX_TYPE)available|=D_ATTR_OBJTYPE;
    if(actual.stx_mask&STATX_INO)available|=D_ATTR_FILEID;
    if(actual.stx_mask&STATX_BTIME)available|=D_ATTR_CRTIME;
    if(actual.stx_mask&STATX_MTIME)available|=D_ATTR_MODTIME;
    if(actual.stx_mask&STATX_CTIME)available|=D_ATTR_CHGTIME;
    if(actual.stx_mask&STATX_ATIME)available|=D_ATTR_ACCTIME;
    if(actual.stx_mask&STATX_UID)available|=D_ATTR_OWNERID;
    if(actual.stx_mask&STATX_GID)available|=D_ATTR_GRPID;
    if(actual.stx_mask&STATX_MODE)available|=D_ATTR_ACCESSMASK;
    struct statfs filesystem={0};
    if((request->common&D_ATTR_FSID) && !RAW(SYS_fstatfs,fd,&filesystem,0,0,0,0))available|=D_ATTR_FSID;
    char fullpath[1024];
    error=native_attr_guest_path(fd,fullpath);
    if(!error)available|=D_ATTR_FULLPATH;
    else available&=~D_ATTR_NAME;
    uint32_t access=0;
    if(request->common&D_ATTR_USERACCESS) {
        available|=D_ATTR_USERACCESS;
        /* The adapter exposes read-only opens. Query actual effective read/
         * execute permissions, and do not advertise an unavailable write path. */
        for(unsigned int mode=1;mode<=R_OK;mode<<=1) {
            if(mode==W_OK)continue;
            error=RAW(SYS_faccessat2,fd,"",mode,AT_EMPTY_PATH|AT_EACCESS,0,0);
            if(!error)access|=mode;
            else if(error!=-EACCES && error!=-EPERM && error!=-EROFS)available&=~D_ATTR_USERACCESS;
        }
    }
    returned.common=request->common&(available|D_ATTR_RETURNED);
    if(S_ISDIR(actual.stx_mode) && (actual.stx_mask&STATX_NLINK))returned.directory=request->directory&D_ATTR_DIR_LINKCOUNT;
    else if(S_ISREG(actual.stx_mode))returned.file=request->file&
        (D_ATTR_FILE_LINKCOUNT|D_ATTR_FILE_TOTALSIZE|D_ATTR_FILE_ALLOCSIZE|
         D_ATTR_FILE_IOBLOCKSIZE|D_ATTR_FILE_DATALENGTH|D_ATTR_FILE_DATAALLOCSIZE);
    if(!(actual.stx_mask&STATX_NLINK))returned.file&=~D_ATTR_FILE_LINKCOUNT;
    if(!(actual.stx_mask&STATX_SIZE))returned.file&=~(D_ATTR_FILE_TOTALSIZE|D_ATTR_FILE_DATALENGTH);
    if(!(actual.stx_mask&STATX_BLOCKS))returned.file&=~(D_ATTR_FILE_ALLOCSIZE|D_ATTR_FILE_DATAALLOCSIZE);
    if(options&D_FSOPT_ATTR_CMN_EXTENDED)returned.fork=request->fork&D_ATTR_EXT_REALDEVID;
    int report=(request->common&D_ATTR_RETURNED)!=0;
    if(!report && (returned.common!=request->common || returned.volume!=request->volume ||
        returned.directory!=request->directory || returned.file!=request->file || returned.fork!=request->fork))return -ENOTSUP;
    char output[4096]={0};
    unsigned int fixed=4+(report ? sizeof(returned) : 0);
    for(unsigned int i=0;i<31;i++)if(returned.common&(1U<<i))fixed+=native_attr_width(1U<<i);
    if(returned.directory)fixed+=4;
    for(unsigned int i=0;i<32;i++)if(returned.file&(1U<<i))fixed+=
        ((1U<<i)==D_ATTR_FILE_LINKCOUNT || (1U<<i)==D_ATTR_FILE_IOBLOCKSIZE) ? 4 : 8;
    if(returned.fork)fixed+=4;
    unsigned int position=4+(report ? sizeof(returned) : 0),variable=fixed;
    uint32_t zero=0;
    /* XNU packs ATTR_CMN_ERROR before name, irrespective of its bit number. */
    if(returned.common&D_ATTR_ERROR)native_attr_pack(output,&position,&zero,4);
    for(unsigned int i=0;i<31;i++) {
        uint32_t bit=1U<<i;
        if(!(returned.common&bit) || bit==D_ATTR_ERROR)continue;
        uint64_t value=0;
        struct darwin_timespec time={0};
        const struct statx_timestamp *stamp=NULL;
        if(bit==D_ATTR_NAME || bit==D_ATTR_FULLPATH) {
            const char *text=fullpath;
            if(bit==D_ATTR_NAME)for(unsigned int j=0;fullpath[j];j++)
                if(fullpath[j]=='/' && fullpath[j+1])text=fullpath+j+1;
            unsigned int size=0;while(text[size])size++;size++;
            unsigned int padded=(size+3)&~3U;
            if(variable+padded>sizeof(output))return -EOVERFLOW;
            struct darwin_attrreference reference={(int32_t)(variable-position),size};
            native_attr_pack(output,&position,&reference,sizeof(reference));
            __builtin_memcpy(output+variable,text,size);variable+=padded;
            continue;
        }
        switch(bit) {
        case D_ATTR_DEVID:value=device;break;
        case D_ATTR_FSID:native_attr_pack(output,&position,&filesystem.f_fsid,8);continue;
        case D_ATTR_OBJTYPE:value=native_attr_object_type(actual.stx_mode);break;
        case D_ATTR_FILEID:value=actual.stx_ino;break;
        case D_ATTR_CRTIME:stamp=&actual.stx_btime;break;
        case D_ATTR_MODTIME:stamp=&actual.stx_mtime;break;
        case D_ATTR_CHGTIME:stamp=&actual.stx_ctime;break;
        case D_ATTR_ACCTIME:stamp=&actual.stx_atime;break;
        case D_ATTR_OWNERID:value=actual.stx_uid;break;
        case D_ATTR_GRPID:value=actual.stx_gid;break;
        case D_ATTR_ACCESSMASK:value=actual.stx_mode&07777;break;
        case D_ATTR_USERACCESS:value=access;break;
        }
        if(stamp) {
            time.seconds=stamp->tv_sec;time.nanoseconds=stamp->tv_nsec;
            native_attr_pack(output,&position,&time,sizeof(time));
        } else native_attr_pack(output,&position,&value,native_attr_width(bit));
    }
    if(returned.directory)native_attr_pack(output,&position,&actual.stx_nlink,4);
    for(unsigned int i=0;i<32;i++) {
        uint32_t bit=1U<<i;if(!(returned.file&bit))continue;
        uint64_t value;
        switch(bit) {
        case D_ATTR_FILE_LINKCOUNT:value=actual.stx_nlink;break;
        case D_ATTR_FILE_IOBLOCKSIZE:value=actual.stx_blksize;break;
        case D_ATTR_FILE_TOTALSIZE:case D_ATTR_FILE_DATALENGTH:value=actual.stx_size;break;
        default:value=actual.stx_blocks*512;break; /* POSIX stat blocks are 512 bytes. */
        }
        native_attr_pack(output,&position,&value,
            bit==D_ATTR_FILE_LINKCOUNT || bit==D_ATTR_FILE_IOBLOCKSIZE ? 4 : 8);
    }
    if(returned.fork)native_attr_pack(output,&position,&device,4);
    if(position!=fixed)return -EIO;
    uint32_t copied=capacity<variable ? capacity : variable;
    uint32_t length=(options&D_FSOPT_REPORT_FULLSIZE) ? variable : copied;
    __builtin_memcpy(output,&length,4);
    if(report)__builtin_memcpy(output+4,&returned,sizeof(returned));
    trace_record("DARWIN_GETATTRLIST_RETURN common=0x",returned.common,returned.file,variable);
    return guest_copy(output,destination,copied,1);
}
static long native_getattrlist(const unsigned long long *r)
{
    struct darwin_attrlist request;
    long error=guest_copy(&request,r[1],sizeof(request),0);if(error)return error;
    if(request.count!=5 || request.reserved || r[3]<4)return -EINVAL;
    if(r[4]&~(D_FSOPT_NOFOLLOW|D_FSOPT_NOINMEMUPDATE|D_FSOPT_REPORT_FULLSIZE|D_FSOPT_ATTR_CMN_EXTENDED))return -ENOTSUP;
    char buffer[4096],*path=buffer;
    error=guest_path(r[0],buffer);if(error)return error;
    int directory=path_directory(&path);
    long fd=RAW(SYS_openat,directory,path,O_PATH|O_CLOEXEC|
        ((r[4]&D_FSOPT_NOFOLLOW) ? O_NOFOLLOW : 0),0,0,0);
    if(fd<0)return fd;
    error=native_attr_object(fd,&request,r[2],r[3],r[4]);
    RAW(SYS_close,fd,0,0,0,0,0);
    return error;
}

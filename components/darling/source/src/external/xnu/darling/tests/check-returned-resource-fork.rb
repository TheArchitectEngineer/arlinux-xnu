# Native Linux regression for both instantiations of the actual shared helper.
# Exercises legacy and returned-attribute packing in the selected integration.
require 'tmpdir'
require 'open3'
root=File.join(File.realpath(ARGV.fetch(0)),'darling/src/libsystem_kernel/emulation')
source=File.read(File.join(root,'include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'))
if ENV['ATTRIBUTE_SOURCE_REF']
  path='darling/src/libsystem_kernel/emulation/include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'
  source,status=Open3.capture2('git','-C',File.realpath(ARGV.fetch(0)),'show',"#{ENV.fetch('ATTRIBUTE_SOURCE_REF')}:#{path}")
  abort 'attribute source revision lookup failed' unless status.success?
end
mount_dir=File.join(root,'include/xnu_syscall/bsd/helper/xattr')
source=source.sub('#include "mountroot.h"') { File.read(File.join(mount_dir,'mountpoint.h')) + File.read(File.join(mount_dir,'mountroot.h')) }
source=source.lines.reject { |line| line.start_with?('#include') }.join
header=File.read(File.join(root,'include/conversion/xattr/getattrlist.h'))
fdpath=File.read(File.join(root,'src/linux_premigration/vchroot_userspace.c'))
fdpath=fdpath[fdpath.index('int vchroot_fdpath(')...fdpath.index('int vchroot_unexpand(')]
fdpath=fdpath.sub('int vchroot_fdpath(', 'static int actual_vchroot_fdpath(')
program=<<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <dirent.h>
  #include <stdint.h>
  #include <stddef.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <sys/stat.h>
  #include <sys/syscall.h>
  #include <sys/xattr.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #{header}
  #define linux_stat stat
  #define st_ctime_nsec st_ctim.tv_nsec
  #define st_mtime_nsec st_mtim.tv_nsec
  #define st_atime_nsec st_atim.tv_nsec
  #define LINUX_AT_SYMLINK_NOFOLLOW AT_SYMLINK_NOFOLLOW
  #define LINUX_O_RDONLY O_RDONLY
  #define LINUX_O_DIRECTORY O_DIRECTORY
  #define LINUX_O_NOFOLLOW O_NOFOLLOW
  #define LINUX_O_CLOEXEC O_CLOEXEC
  #define __simple_sprintf sprintf
  #define VCHROOT_FOLLOW 1
  #define atfd(fd) (fd)
  #define sys_dup dup
  #define close_internal close
  #define errno_linux_to_bsd(error) (error)
  static int birthUnavailable;
  static int accessUnavailable;
  enum { HOST_STATX = SYS_statx };
  #if OMIT_STATX
  #undef __NR_statx
  #endif
  #define LINUX_SYSCALL(number, ...) ({ long r; if ((number)==439 && accessUnavailable) { errno=ENOSYS; r=-1; } else if ((number)==HOST_STATX && birthUnavailable) { errno=ENOSYS; r=birthUnavailable==1 ? -1 : 0; } else r=syscall(number, __VA_ARGS__); r<0 ? -errno : r; })
  #define FUNC_NAME test_getattrlist
  struct linux_dirent64 { uint64_t ino; int64_t off; unsigned short d_reclen; unsigned char type; char d_name[]; };
  struct vchroot_expand_args { int flags,dfd; char path[4096]; };
  struct vchroot_fdpath_args { int fd; char *path; unsigned maxlen; };
  #define TEST 1
  #define __simple_sprintf sprintf
  #define LINUX_ENAMETOOLONG ENAMETOOLONG
  #define LINUX_AT_FDCWD AT_FDCWD
  static char prefix_path[4096];
  static int prefix_path_len;
  static const char EXIT_PATH[]="/Volumes/SystemRoot";
  #{fdpath}
  static int pathConversionFails, lastPathFd=-1;
  static int vchroot_fdpath(struct vchroot_fdpath_args *args) {
    lastPathFd=args->fd;
  #if HAS_PATH
    assert(fcntl(args->fd,F_GETFD)&FD_CLOEXEC);
  #endif
    if(pathConversionFails) return -EIO;
    return actual_vchroot_fdpath(args);
  }
  static inline int vchroot_expand(struct vchroot_expand_args *v) {
    if(strcmp(v->path,"/")==0 && prefix_path[0]) strcpy(v->path,prefix_path);
    else if(v->path[0]!='/' && v->dfd!=AT_FDCWD) {
      char input[8192],resolved[4096];
      snprintf(input,sizeof input,"/proc/self/fd/%d/%s",v->dfd,v->path);
      if(!realpath(input,resolved)) return -errno;
      strcpy(v->path,resolved);
    }
    return 0;
  }
  #{source}
  static long get(int fd,const char *path,struct xnu_attrlist *attrs,void *out,size_t size,unsigned long options) {
  #if HAS_PATH
    return test_getattrlist(fd,path,attrs,out,size,options);
  #else
    (void)path;
    return test_getattrlist(fd,attrs,out,size,options);
  #endif
  }
  static unsigned open_count(void) {
    DIR *directory=opendir("/proc/self/fd"); assert(directory);
    unsigned count=0; while(readdir(directory)) ++count;
    closedir(directory); return count;
  }
  int main(int argc,char **argv) {
    if(argc==3 && strcmp(argv[2],"--credentials")==0) {
      assert(geteuid()==0 && CREDENTIAL_UID!=0);
      int fd=open(argv[1],O_CREAT|O_EXCL|O_RDONLY,0400); assert(fd>=0);
      assert(fchown(fd,0,0)==0);
      for(unsigned effectiveRoot=0;effectiveRoot<2;++effectiveRoot) {
        pid_t child=fork(); assert(child>=0);
        if(child==0) {
          assert(setresuid(effectiveRoot ? CREDENTIAL_UID : 0,effectiveRoot ? 0 : CREDENTIAL_UID,0)==0);
          assert(getuid()!=geteuid());
          // The fixture is root-owned and readable only by its owner.
          long realResult=syscall(SYS_faccessat2,fd,"",R_OK,AT_EMPTY_PATH);
          long effectiveResult=syscall(SYS_faccessat2,fd,"",R_OK,AT_EMPTY_PATH|AT_EACCESS);
          assert((realResult==0)!= (effectiveResult==0));
          struct xnu_attrlist attrs={.bitmapcount=5,.commonattr=0x80200000};
          unsigned char out[32]; uint32_t mask,rights,length;
          assert(get(fd,argv[1],&attrs,out,sizeof out,0)==0);
          memcpy(&length,out,4); memcpy(&mask,out+4,4); memcpy(&rights,out+24,4);
          assert(length==28 && mask==0x80200000);
          assert(!!(rights&R_OK)==(effectiveResult==0));
          _exit(0);
        }
        int status; assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
      }
      close(fd); assert(unlink(argv[1])==0);
      puts("PASS: effective credentials win over differing real credentials in both directions");
      return 0;
    }
    assert(argc==2);
    const char *path=argv[1];
    strcpy(prefix_path,path); char *slash=strrchr(prefix_path,'/'); assert(slash); *slash=0;
    prefix_path_len=strlen(prefix_path);
    char childPath[4096]; snprintf(childPath,sizeof childPath,"%s/mount-child",prefix_path);
    assert(mkdir(childPath,0700)==0);
    const char *mountPaths[]={prefix_path,childPath,"/proc"};
    unsigned beforeMounts=open_count();
    for(unsigned which=0;which<3;++which) {
      int directory=open(mountPaths[which],O_RDONLY|O_DIRECTORY); assert(directory>=0);
      uint64_t mountID; int supported=attribute_fd_mount_id(directory,&mountID)==0;
      for(unsigned invalid=0;invalid<2;++invalid) {
        struct xnu_attrlist attrs={.bitmapcount=5,.commonattr=0x80000000,.dirattr=4};
        unsigned char result[40]; memset(result,0xa5,sizeof result);
        assert(get(directory,mountPaths[which],&attrs,result,sizeof result,invalid ? 8 : 0)==0);
        uint32_t length,mask,value=0; memcpy(&length,result,4); memcpy(&mask,result+12,4);
        assert(mask==(supported ? 4 : 0));
        assert(length==((supported || invalid) ? 28 : 24));
        if(supported || invalid) memcpy(&value,result+24,4);
        assert(value==(supported && which!=1 ? 1 : 0));
        assert(result[length]==0xa5 && fcntl(directory,F_GETFD)>=0);
      }
      close(directory);
    }
    assert(open_count()==beforeMounts && rmdir(childPath)==0);
    int fd=open(path,O_CREAT|O_RDWR,0600); assert(fd>=0);
    const char payload[]="resource payload";
    char finder[32]={0};
    for(unsigned i=0;i<sizeof finder;++i) finder[i]=(char)(i+1);
    assert(fsetxattr(fd,"user.com.apple.ResourceFork",payload,sizeof payload,0)==0);
    assert(fsetxattr(fd,"user.com.apple.FinderInfo",finder,sizeof finder,0)==0);
    {
      unsigned char rootFinder[32]; memset(rootFinder,0x71,sizeof rootFinder);
      assert(setxattr(prefix_path,"user.com.apple.FinderInfo",rootFinder,sizeof rootFinder,0)==0);
      for(unsigned returned=0;returned<2;++returned) {
        struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x4000|(returned ? 0x80000000 : 0),.volattr=0x80020000};
        unsigned char result[96]; memset(result,0xa5,sizeof result);
        long status=get(fd,path,&volume,result,sizeof result,0);
        if(OMIT_STATX) {
          assert(status==-ENOTSUP);
          for(unsigned i=0;i<sizeof result;++i) assert(result[i]==0xa5);
        } else {
          unsigned offset=returned ? 24 : 4;
          assert(status==0 && memcmp(result+offset,rootFinder,32)==0);
          uint32_t length; memcpy(&length,result,4);
          assert(length==offset+64 && result[length]==0xa5);
        }
      }
      assert(removexattr(prefix_path,"user.com.apple.FinderInfo")==0);
      assert(fcntl(fd,F_GETFD)>=0);
    }
    for(unsigned returned=0;returned<2;++returned) {
      struct xnu_attrlist info={.bitmapcount=5,.commonattr=0x4000|(returned ? 0x80000000 : 0)};
      unsigned char result[64]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&info,result,sizeof result,0)==0);
      unsigned offset=returned ? 24 : 4;
      uint32_t size; memcpy(&size,result,4);
      assert(size==offset+32 && memcmp(result+offset,finder,32)==0 && result[size]==0xa5);
    }
    assert(write(fd,payload,sizeof payload)==sizeof payload);
    for(unsigned invalid=0;invalid<2;++invalid) {
      struct xnu_attrlist ids={.bitmapcount=5,.commonattr=0x80180008};
      unsigned char result[48]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&ids,result,sizeof result,0x20|(invalid ? 8 : 0))==0);
      uint32_t length,common,type;
      memcpy(&length,result,4); memcpy(&common,result+4,4); memcpy(&type,result+24,4);
      assert(common==0x80000008 && type==1 && length==(invalid ? 36 : 28));
      if(invalid) for(unsigned i=28;i<36;++i) assert(result[i]==0);
      assert(result[length]==0xa5);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
      struct xnu_attrlist unknown={.bitmapcount=5,.commonattr=0xc0000008,.fileattr=0x2000};
      unsigned char result[48]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&unknown,result,sizeof result,invalid ? 8 : 0)==0);
      uint32_t length,common,file,type;
      memcpy(&length,result,4); memcpy(&common,result+4,4);
      memcpy(&file,result+16,4); memcpy(&type,result+24,4);
      assert(common==0x80000008 && file==0 && type==1);
      assert(length==(invalid ? 40 : 28));
      if(invalid) for(unsigned i=28;i<40;++i) assert(result[i]==0);
      assert(result[length]==0xa5);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
      struct xnu_attrlist flags={.bitmapcount=5,.commonattr=0x80000008,.forkattr=0x200};
      unsigned char result[48]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&flags,result,sizeof result,0x20|(invalid ? 8 : 0))==0);
      uint32_t length,extended; memcpy(&length,result,4); memcpy(&extended,result+20,4);
      assert(extended==0 && length==(invalid ? 36 : 28));
      if(invalid) for(unsigned i=28;i<36;++i) assert(result[i]==0);
      assert(result[length]==0xa5);
    }
    for(unsigned returned=0;returned<2;++returned) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=returned ? 0x80000000 : 0,.volattr=0xc0000000};
      unsigned char result[80]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,0)==0);
      uint32_t masks[10],length; unsigned base=returned ? 24 : 4;
      memcpy(masks,result+base,sizeof masks); memcpy(&length,result,4);
      assert(length==base+40 && result[length]==0xa5);
      for(unsigned group=0;group<5;++group) assert((masks[group+5]&~masks[group])==0);
      assert((masks[2]&2)!=0 && masks[7]==0); // Enumeration is not a native count.
      assert(masks[6]==0); // Generated capability/support structures.
      assert((masks[5]&(0x08000000|0x10|0x200000))==0); // Path/tag/access computation.
      assert((masks[5]&(0x400|0x800|0x1000|0x8000|0x10000|0x20000|0x2000000))==
        (0x400|0x800|0x1000|0x8000|0x10000|0x20000|0x2000000));
      assert(masks[8]==0x601 && masks[9]==0x40);
      assert((masks[4]&0x200)==0); // No extended-flag query is implemented.
    }
    for(unsigned invalid=0;invalid<3;++invalid) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x80020000,.volattr=0x80020000};
      if(invalid==0) volume.bitmapcount=4;
      if(invalid==1) volume.fileattr=0x1000;
      if(invalid==2) volume.volattr|=0x00800000;
      unsigned char result[96]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,0)==-EINVAL);
      for(unsigned i=0;i<sizeof result;++i) assert(result[i]==0xa5);
    }
    {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x80020000,.volattr=0x80020000};
      unsigned char result[96]; memset(result,0xa5,sizeof result);
      long status=get(fd,path,&volume,result,sizeof result,0);
      if(OMIT_STATX) {
        assert(status==-ENOTSUP);
        for(unsigned i=0;i<sizeof result;++i) assert(result[i]==0xa5);
      } else {
        uint32_t type,length; assert(status==0);
        memcpy(&type,result+24,4); memcpy(&length,result,4);
        struct stat rootStat; assert(stat(prefix_path,&rootStat)==0);
        assert(type==rootStat.st_mode && S_ISDIR(type) && length==60 && result[60]==0xa5);
        unsigned before=open_count();
        for(unsigned repeat=0;repeat<50;++repeat) {
          assert(get(fd,path,&volume,result,sizeof result,0)==0);
          assert(get(fd,path,&volume,result,4,0)==-ERANGE);
        }
        assert(open_count()==before);
      }
      assert(fcntl(fd,F_GETFD)>=0);
      volume.commonattr=0x80000008;
      for(unsigned invalid=0;invalid<2;++invalid) {
        assert(get(fd,path,&volume,result,sizeof result,invalid ? 8 : 0)==0);
        uint32_t common,length; memcpy(&common,result+4,4); memcpy(&length,result,4);
        assert(common==0x80000000 && length==(invalid ? 60 : 56));
        if(invalid) { uint32_t value; memcpy(&value,result+24,4); assert(value==0); }
      }
    }
    {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x88000000,.volattr=0x80020000};
      unsigned char result[96]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,0)==-EINVAL); // FULLPATH is object-only.
      volume.commonattr=0x02000000;
      assert(get(fd,path,&volume,result,sizeof result,0)==-EINVAL); // Legacy volume FILEID.
      for(unsigned i=0;i<sizeof result;++i) assert(result[i]==0xa5);
      volume.commonattr=0x82000000;
      for(unsigned invalid=0;invalid<2;++invalid) {
        memset(result,0xa5,sizeof result);
        assert(get(fd,path,&volume,result,sizeof result,invalid ? 8 : 0)==0);
        uint32_t length,common; memcpy(&length,result,4); memcpy(&common,result+4,4);
        assert(common==0x80000000 && length==(invalid ? 64 : 56));
        for(unsigned i=24;i<length;++i) assert(result[i]==0);
        assert(result[length]==0xa5);
      }
    }
    for(unsigned invalid=0;invalid<4;++invalid) for(unsigned packing=0;packing<2;++packing) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x80000000,.volattr=0x80020000};
      if(invalid==0) volume.fileattr=0x1000;
      if(invalid==1) volume.dirattr=2;
      if(invalid==2) volume.forkattr=1;
      if(invalid==3) volume.volattr|=0x00800000;
      unsigned char result[128]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,packing ? 8 : 0)==-EINVAL);
      for(unsigned j=0;j<sizeof result;++j) assert(result[j]==0xa5);
    }
    // The bridge has not queried backing-filesystem capabilities. Unknown
    // capability bits (including reserved words) must not be marked valid.
    for(unsigned returned=0;returned<2;++returned) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=returned ? 0x80000000 : 0,.volattr=0x80020000};
      unsigned char result[64]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,0)==0);
      uint32_t length; memcpy(&length,result,4);
      unsigned base=returned ? 24 : 4;
      assert(length==base+32 && result[length]==0xa5);
      for(unsigned j=base;j<base+32;++j) assert(result[j]==0);
      if(returned) { uint32_t mask; memcpy(&mask,result+8,4); assert(mask==0x80020000); }
    }
    struct timespec requestedTimes[2]={{123456789,123456789},{-12345,987654321}};
    // Unsupported scalar/reference fields retain their Darwin slot widths.
    const uint32_t volumeBits[]={1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,0x40000,0x10000000,0x20000000};
    const unsigned volumeWidths[]={4,4,8,8,8,8,8,4,4,4,4,4,8,8,4,8,8,16,8,8};
    for(unsigned field=0;field<sizeof(volumeBits)/sizeof(volumeBits[0]);++field) for(unsigned invalid=0;invalid<2;++invalid) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x80000000,.volattr=0xc0020000|volumeBits[field]};
      unsigned char result[128]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,invalid ? 8 : 0)==0);
      uint32_t length,mask; memcpy(&length,result,4); memcpy(&mask,result+8,4);
      unsigned attributesOffset=56+(invalid ? volumeWidths[field] : 0);
      assert(mask==0xc0020000 && length==attributesOffset+40);
      for(unsigned j=24;j<attributesOffset;++j) assert(result[j]==0);
      uint32_t supportedVolume; memcpy(&supportedVolume,result+attributesOffset+4,4);
      assert(supportedVolume==VOLUME_SUPPORTED);
      assert(result[length]==0xa5);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
      struct xnu_attrlist volume={.bitmapcount=5,.commonattr=0x80000000,.volattr=0x80060000};
      unsigned char result[80]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,invalid ? 8 : 0)==0);
      uint32_t length,mask; memcpy(&length,result,4); memcpy(&mask,result+8,4);
      assert(mask==0x80020000 && length==(invalid ? 72 : 56));
      for(unsigned j=24;j<length;++j) assert(result[j]==0);
      assert(result[length]==0xa5);
      volume.commonattr=0;
      memset(result,0xa5,sizeof result);
      assert(get(fd,path,&volume,result,sizeof result,0)==-ENOTSUP);
      for(unsigned j=0;j<sizeof result;++j) assert(result[j]==0xa5);
    }
    assert(futimens(fd,requestedTimes)==0);
    struct xnu_attrlist attrs={.bitmapcount=5,.fileattr=0x1000};
    unsigned char out[64]; int64_t length; uint32_t total;
    memset(out,0xa5,sizeof out);
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&total,out,4); memcpy(&length,out+4,8);
    assert(total==12 && length==sizeof payload && out[12]==0xa5);
    for(unsigned kind=0;kind<3;++kind) {
      struct xnu_attrlist invalid={.bitmapcount=5,.commonattr=0x80000000};
      if(kind==0) invalid.forkattr=0x40;
      if(kind==1) invalid.forkattr=1;
      if(kind==2) invalid.commonattr|=0x80000;
      memset(out,0xa5,sizeof out);
      assert(get(fd,path,&invalid,out,sizeof out,kind==1 ? 0x20 : 0)==-EINVAL);
      for(unsigned b=0;b<sizeof out;++b) assert(out[b]==0xa5);
    }
    attrs.commonattr=0x80000008; // Returned masks, object type, resource off_t.
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+28,8);
    assert(length==sizeof payload);
    uint32_t returnedCommon,returnedFile;
    memcpy(&returnedCommon,out+4,4); memcpy(&returnedFile,out+16,4);
    assert(returnedCommon==attrs.commonattr && returnedFile==attrs.fileattr);
    attrs.commonattr=0xa0000008; // Returned attrs, error, object type.
    assert(get(fd,path,&attrs,out,sizeof out,8)==0);
    uint32_t errorCode,errorType;
    memcpy(&returnedCommon,out+4,4); memcpy(&errorCode,out+24,4);
    memcpy(&errorType,out+28,4); memcpy(&length,out+32,8);
    assert(returnedCommon==attrs.commonattr && errorCode==0 && errorType==1 && length==sizeof payload);
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&returnedCommon,out+4,4); memcpy(&errorType,out+24,4); memcpy(&length,out+28,8);
    assert(returnedCommon==0x80000008 && errorType==1 && length==sizeof payload);
    struct stat st; assert(fstat(fd,&st)==0);
    const uint32_t timeBits[]={0x400,0x800,0x1000};
    const int64_t seconds[]={st.st_mtime,st.st_ctime,st.st_atime};
    const uint64_t nanos[]={st.st_mtim.tv_nsec,st.st_ctim.tv_nsec,st.st_atim.tv_nsec};
    attrs.fileattr=0;
    for(unsigned i=0;i<3;++i) {
      attrs.commonattr=0x80000008|timeBits[i];
      assert(get(fd,path,&attrs,out,sizeof out,0)==0);
      int64_t sec; uint64_t nsec;
      memcpy(&sec,out+28,8); memcpy(&nsec,out+36,8);
      assert(sec==seconds[i] && nsec==nanos[i]);
    }
    attrs.commonattr=0x82000008;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    uint64_t inode; memcpy(&inode,out+28,8); assert(inode==st.st_ino);
    // stat ctime cannot supply time of arrival in the containing directory.
    // Unsupported ADDEDTIME must be absent, or a zero placeholder with no bit.
    for(unsigned invalid=0;invalid<2;++invalid) {
      attrs.commonattr=0x90000008;
      memset(out,0xa5,sizeof out);
      assert(get(fd,path,&attrs,out,sizeof out,invalid ? 8 : 0)==0);
      memcpy(&total,out,4); memcpy(&returnedCommon,out+4,4);
      assert(returnedCommon==0x80000008 && total==(invalid ? 44 : 28));
      if(invalid) for(unsigned j=28;j<44;++j) assert(out[j]==0);
      assert(out[total]==0xa5);
    }
    const uint32_t fileBits[]={0x200,0x400};
    struct statx nativeBirth;
    assert(sizeof(nativeBirth)==sizeof(struct attribute_linux_statx));
    assert(offsetof(struct statx,stx_btime)==offsetof(struct attribute_linux_statx,birth_seconds));
    int birthResult=syscall(HOST_STATX,fd,"",AT_EMPTY_PATH,STATX_BTIME,&nativeBirth);
    printf("birth-time host support: %s; candidate syscall compiled: %s\\n",
      birthResult==0 && (nativeBirth.stx_mask & STATX_BTIME) ? "yes" : "no", OMIT_STATX ? "no" : "yes");
    // Mode 1: missing syscall. Mode 2: success without a birth-time mask.
    for(unsigned unavailable=0;unavailable<3;++unavailable) {
      birthUnavailable=unavailable;
      int supported=!OMIT_STATX && !unavailable && birthResult==0 && (nativeBirth.stx_mask & STATX_BTIME);
      for(unsigned invalid=0;invalid<2;++invalid) {
        attrs.commonattr=0x80000208;
        memset(out,0xa5,sizeof out);
        assert(get(fd,path,&attrs,out,sizeof out,invalid ? 8 : 0)==0);
        memcpy(&total,out,4); memcpy(&returnedCommon,out+4,4);
        assert(returnedCommon==(supported ? 0x80000208 : 0x80000008));
        assert(total==((supported || invalid) ? 44 : 28));
        if(supported) {
          int64_t sec; uint64_t nsec;
          memcpy(&sec,out+28,8); memcpy(&nsec,out+36,8);
          assert(sec==nativeBirth.stx_btime.tv_sec && nsec==nativeBirth.stx_btime.tv_nsec);
        } else if(invalid) for(unsigned j=28;j<44;++j) assert(out[j]==0);
        assert(out[total]==0xa5);
      }
    }
    birthUnavailable=0;
    assert(fchmod(fd,0640)==0);
    if(!OMIT_STATX && birthResult==0 && (nativeBirth.stx_mask & STATX_BTIME)) {
      attrs.commonattr=0x80000200;
      assert(get(fd,path,&attrs,out,sizeof out,0)==0);
      int64_t sec; uint64_t nsec;
      memcpy(&sec,out+24,8); memcpy(&nsec,out+32,8);
      assert(sec==nativeBirth.stx_btime.tv_sec && nsec==nativeBirth.stx_btime.tv_nsec);
    }
    const int64_t fileValues[]={st.st_size,st.st_blocks*512};
    const mode_t permissions[]={0600,0700,0400,0000};
    for(unsigned p=0;p<4;++p) {
      assert(fchmod(fd,permissions[p])==0);
      uint32_t expectedAccess=0;
      for(unsigned mode=1;mode<=4;mode<<=1) {
        long r=syscall(SYS_faccessat2,fd,"",mode,AT_EACCESS|AT_EMPTY_PATH);
        assert(r==0 || errno==EACCES || errno==EPERM || errno==EROFS);
        if(r==0) expectedAccess|=mode;
      }
      for(unsigned unavailable=0;unavailable<2;++unavailable) for(unsigned invalid=0;invalid<2;++invalid) {
        accessUnavailable=unavailable;
        attrs.commonattr=0x80200008;
        memset(out,0xa5,sizeof out);
        assert(get(fd,path,&attrs,out,sizeof out,invalid ? 8 : 0)==0);
        memcpy(&returnedCommon,out+4,4); memcpy(&total,out,4);
        assert(returnedCommon==(unavailable ? 0x80000008 : 0x80200008));
        assert(total==((!unavailable || invalid) ? 32 : 28));
        if(!unavailable || invalid) {
          uint32_t access; memcpy(&access,out+28,4);
          assert(access==(unavailable ? 0 : expectedAccess));
        }
        assert(out[total]==0xa5);
      }
    }
    accessUnavailable=0;
    assert(fchmod(fd,0600)==0);
    for(unsigned i=0;i<2;++i) {
      attrs.commonattr=0x80000008; attrs.fileattr=fileBits[i];
      assert(get(fd,path,&attrs,out,sizeof out,0)==0);
      memcpy(&length,out+28,8); assert(length==fileValues[i]);
    }
    attrs.fileattr=0x1000;
  #if !HAS_PATH
    // NAME remains unsupported; an unavailable FULLPATH must also omit its slot.
    pathConversionFails=1;
    attrs.commonattr=0x88000009; attrs.fileattr=0;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    uint32_t fdType;
    memcpy(&total,out,4); memcpy(&returnedCommon,out+4,4); memcpy(&fdType,out+24,4);
    assert(total==28 && returnedCommon==0x80000008 && fdType==1);
    assert(get(fd,path,&attrs,out,sizeof out,8)==0);
    memcpy(&total,out,4); memcpy(&returnedCommon,out+4,4); memcpy(&fdType,out+32,4);
    assert(total==44 && returnedCommon==0x80000008 && fdType==1);
    assert(fcntl(fd,F_GETFD)>=0);
    pathConversionFails=0;
    attrs.commonattr=0x88000000;
    unsigned char fdPath[8192];
    assert(get(fd,path,&attrs,fdPath,sizeof fdPath,0)==0);
    uint32_t pathMask,pathLength; int32_t pathOffset;
    memcpy(&pathMask,fdPath+4,4); memcpy(&pathOffset,fdPath+24,4); memcpy(&pathLength,fdPath+28,4);
    assert(pathMask==0x88000000 && pathLength==6);
    assert(strcmp((char*)fdPath+24+pathOffset,"/file")==0);
    assert(lastPathFd==fd && fcntl(fd,F_GETFD)>=0);
    attrs.fileattr=0x1000;
  #endif
    attrs.commonattr=0x18; /* OBJTYPE then OBJTAG then resource off_t */
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    uint32_t type,tag;
    memcpy(&type,out+4,4); memcpy(&tag,out+8,4); memcpy(&length,out+12,8);
    assert(type==1 && tag==16 && length==sizeof payload);
    memset(out,0xa5,sizeof out);
    assert(get(fd,path,&attrs,out,4,0)==-ERANGE && out[4]==0xa5);
    assert(get(fd,path,&attrs,out,4,4)==0 && out[4]==0xa5);
    memcpy(&total,out,4); assert(total==20);
  #if HAS_PATH
    attrs.commonattr=0x19; /* Also preserve upstream's variable-length name. */
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    int32_t offset; memcpy(&offset,out+4,4);
    assert(strcmp((char *)out+4+offset,"file")==0);
    memcpy(&length,out+20,8); assert(length==sizeof payload);
    char linkpath[4096]; snprintf(linkpath,sizeof linkpath,"%s.link",path);
    assert(symlink(path,linkpath)==0);
    {
      // A followed symlink must return the resolved object path, not the
      // caller's spelling. Also verify temporary-fd cleanup on conversion error.
      attrs.commonattr=0x88000000; attrs.fileattr=0;
      unsigned char full[8192];
      assert(get(fd,linkpath,&attrs,full,sizeof full,0)==0);
      uint32_t fullMask,fullLength; int32_t fullOffset;
      memcpy(&fullMask,full+4,4); memcpy(&fullOffset,full+24,4); memcpy(&fullLength,full+28,4);
      assert(fullMask==0x88000000 && fullLength==6);
      assert(strcmp((char*)full+24+fullOffset,"/file")==0);
      assert(fcntl(lastPathFd,F_GETFD)==-1 && errno==EBADF);
      assert(get(fd,linkpath,&attrs,full,sizeof full,1)==0);
      memcpy(&fullOffset,full+24,4); memcpy(&fullLength,full+28,4);
      assert(fullLength==11 && strcmp((char*)full+24+fullOffset,"/file.link")==0);
      assert(fcntl(lastPathFd,F_GETFD)==-1 && errno==EBADF);
      pathConversionFails=1;
      for(unsigned invalid=0;invalid<2;++invalid) {
        memset(full,0xa5,sizeof full);
        assert(get(fd,linkpath,&attrs,full,sizeof full,invalid ? 8 : 0)==0);
        memcpy(&fullMask,full+4,4); memcpy(&total,full,4);
        assert(fullMask==0x80000000 && total==(invalid ? 32 : 24));
        if(invalid) for(unsigned i=24;i<32;++i) assert(full[i]==0);
        assert(full[total]==0xa5);
        assert(fcntl(lastPathFd,F_GETFD)==-1 && errno==EBADF);
      }
      pathConversionFails=0;
      attrs.fileattr=0x1000;
    }
    attrs.commonattr=0;
    assert(get(fd,linkpath,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+4,8); assert(length==sizeof payload);
    assert(get(fd,linkpath,&attrs,out,sizeof out,1)==0);
    memcpy(&length,out+4,8); assert(length==0);
    attrs.commonattr=0x80000000;
    assert(get(fd,linkpath,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+24,8); assert(length==sizeof payload);
    assert(get(fd,linkpath,&attrs,out,sizeof out,1)==0);
    memcpy(&length,out+24,8); assert(length==0);
    attrs.commonattr=0x80000208; attrs.fileattr=0;
    for(unsigned nofollow=0;nofollow<2;++nofollow) {
      struct statx linkBirth;
      int r=syscall(HOST_STATX,AT_FDCWD,linkpath,nofollow ? AT_SYMLINK_NOFOLLOW : 0,STATX_BTIME,&linkBirth);
      int supported=!OMIT_STATX && r==0 && (linkBirth.stx_mask & STATX_BTIME);
      for(unsigned invalid=0;invalid<2;++invalid) {
        assert(get(fd,linkpath,&attrs,out,sizeof out,nofollow | (invalid ? 8 : 0))==0);
        uint32_t objectType;
        memcpy(&returnedCommon,out+4,4); memcpy(&objectType,out+24,4); memcpy(&total,out,4);
        assert(objectType==(nofollow ? 5 : 1));
        assert(returnedCommon==(supported ? 0x80000208 : 0x80000008));
        assert(total==((supported || invalid) ? 44 : 28));
        if(supported) {
          int64_t sec; uint64_t nsec;
          memcpy(&sec,out+28,8); memcpy(&nsec,out+36,8);
          assert(sec==linkBirth.stx_btime.tv_sec && nsec==linkBirth.stx_btime.tv_nsec);
        } else if(invalid) for(unsigned j=28;j<44;++j) assert(out[j]==0);
      }
    }
    attrs.fileattr=0x1000;
    unlink(linkpath);
  #endif
    assert(fremovexattr(fd,"user.com.apple.ResourceFork")==0);
    attrs.commonattr=0;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+4,8); assert(length==0);
    // Legacy requests also select only the concrete object's attribute group.
    attrs.dirattr=2;
    memset(out,0xa5,sizeof out);
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&total,out,4); memcpy(&length,out+4,8);
    assert(total==12 && length==0 && out[12]==0xa5);
    attrs.dirattr=0;
    {
      attrs.commonattr=0x88000000; attrs.fileattr=0;
      unsigned char full[8192]; int32_t offset; uint32_t fullLength;
      char savedPrefix[4096]; strcpy(savedPrefix,prefix_path);
      strcpy(prefix_path,"/unrelated-guest-root"); prefix_path_len=strlen(prefix_path);
      assert(get(fd,path,&attrs,full,sizeof full,0)==0);
      memcpy(&offset,full+24,4); memcpy(&fullLength,full+28,4);
      char expected[8192]; snprintf(expected,sizeof expected,"/Volumes/SystemRoot%s",path);
      assert(fullLength==strlen(expected)+1 && strcmp((char*)full+24+offset,expected)==0);
      strcpy(prefix_path,savedPrefix); prefix_path_len=strlen(prefix_path);
      int directory=open(prefix_path,O_RDONLY|O_DIRECTORY); assert(directory>=0);
      assert(get(directory,prefix_path,&attrs,full,sizeof full,0)==0);
      memcpy(&offset,full+24,4); memcpy(&fullLength,full+28,4);
      assert(fullLength==2 && strcmp((char*)full+24+offset,"/")==0);
  #if HAS_PATH
      assert(get(directory,"file",&attrs,full,sizeof full,0)==0);
      memcpy(&offset,full+24,4); memcpy(&fullLength,full+28,4);
      assert(fullLength==6 && strcmp((char*)full+24+offset,"/file")==0);
  #endif
      assert(fcntl(directory,F_GETFD)>=0); close(directory);
      attrs.commonattr=0; attrs.fileattr=0x1000;
    }
    close(fd); unlink(path);
    assert(mkdir(path,0700)==0);
    fd=open(path,O_RDONLY|O_DIRECTORY); assert(fd>=0);
    int child=openat(fd,"child",O_CREAT|O_WRONLY,0600); assert(child>=0); close(child);
    off_t before=lseek(fd,0,SEEK_CUR); assert(before>=0);
    assert(fsetxattr(fd,"user.com.apple.ResourceFork",payload,sizeof payload,0)==0);
    attrs.dirattr=2;
    memset(out,0xa5,sizeof out);
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    uint32_t entries; memcpy(&entries,out+4,4); memcpy(&total,out,4);
    assert(entries==1 && total==8 && out[8]==0xa5);
    assert(lseek(fd,0,SEEK_CUR)==before);
    attrs.commonattr=0x80000000;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&entries,out+24,4); memcpy(&total,out,4); memcpy(&returnedFile,out+16,4);
    assert(entries==1 && total==28 && returnedFile==0);
    assert(lseek(fd,0,SEEK_CUR)==before);
    // Darwin directory hard-link count excludes the synthetic dot links.
    // Adding a subdirectory must change ENTRYCOUNT, not LINKCOUNT.
    attrs.dirattr=3;
    for (unsigned populated=0; populated<2; ++populated) {
      if (populated) assert(mkdirat(fd,"subdirectory",0700)==0);
      for (unsigned invalid=0; invalid<2; ++invalid) {
        memset(out,0xa5,sizeof out);
        assert(get(fd,path,&attrs,out,sizeof out,invalid ? 8 : 0)==0);
        uint32_t links,returnedDir;
        memcpy(&links,out+24,4); memcpy(&entries,out+28,4);
        memcpy(&total,out,4); memcpy(&returnedDir,out+12,4);
        memcpy(&returnedFile,out+16,4);
        assert(links==1 && entries==1+populated && total==32);
        assert(returnedDir==3 && returnedFile==0 && out[32]==0xa5);
        assert(lseek(fd,0,SEEK_CUR)==before);
      }
    }
    assert(unlinkat(fd,"subdirectory",AT_REMOVEDIR)==0);
    assert(unlinkat(fd,"child",0)==0);
    close(fd); rmdir(path);
    puts("PASS: resource forks, returned masks, four-byte-packed times/IDs/sizes, legacy ordering and symlinks");
  }
C
Dir.mktmpdir('resource-fork') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  [0,1].product([0,1]).each do |has_path,omit_statx|
    abort 'compile failed' unless system(ENV.fetch('CC','clang'),'-Wall','-Wextra','-O2','-fsanitize=address,undefined',"-DCREDENTIAL_UID=#{Process.uid}","-DHAS_PATH=#{has_path}","-DOMIT_STATX=#{omit_statx}",input,'-o',output)
    puts "HAS_PATH=#{has_path} OMIT_STATX=#{omit_statx}"
    abort 'regression failed' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},output,File.join(dir,'file'),rlimit_core:0)
    if ENV['PROBE_DIFFERING_CREDENTIALS']=='1'
      abort 'credential regression failed' unless system('sudo','-n',output,File.join(dir,'credential-file'),'--credentials',rlimit_core:0)
    end
  end
end

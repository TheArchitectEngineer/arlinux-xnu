# Compose the actual bulk wrapper with the actual attribute helper.
# Native Linux filesystem and syscalls; virtual-root expansion uses /proc fd paths.
require 'tmpdir'
root=File.join(File.realpath(ARGV.fetch(0)),'darling/src/libsystem_kernel/emulation')
source=File.read(File.join(root,'include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'))
mount_dir=File.join(root,'include/xnu_syscall/bsd/helper/xattr')
source=source.sub('#include "mountroot.h"') { File.read(File.join(mount_dir,'mountpoint.h')) + File.read(File.join(mount_dir,'mountroot.h')) }
source=source.lines.reject { |line| line.start_with?('#include') }.join
header=File.read(File.join(root,'include/conversion/xattr/getattrlist.h'))
bulk=File.read(File.join(root,'src/xnu_syscall/bsd/impl/xattr/getattrlistbulk.c'))
bulk=bulk[bulk.index('#define ATTR_CMN_NAME')..]
packing=File.read(File.join(root,'src/xnu_syscall/bsd/impl/xattr/getattrlistat.c'))
packing=packing[packing.index('// Shared with bulk enumeration')..]
program=<<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <stdint.h>
  #include <stddef.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <sys/stat.h>
  #include <sys/syscall.h>
  #include <sys/xattr.h>
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
  #define LINUX_SYSCALL(...) ({ long r=syscall(__VA_ARGS__); r<0 ? -errno : r; })
  #define FUNC_NAME sys_getattrlistat
  struct linux_dirent64 { uint64_t d_ino; int64_t d_off; unsigned short d_reclen; unsigned char d_type; char d_name[]; };
  struct vchroot_expand_args { int flags,dfd; char path[4096]; };
  struct vchroot_fdpath_args { int fd; char *path; unsigned maxlen; };
  static int vchroot_fdpath(struct vchroot_fdpath_args *args) {
    char procpath[64]; snprintf(procpath,sizeof procpath,"/proc/self/fd/%d",args->fd);
    ssize_t length=readlink(procpath,args->path,args->maxlen);
    if(length<0) return -errno;
    if((size_t)length>=args->maxlen) return -ENAMETOOLONG;
    args->path[length]=0;
    return 0;
  }
  #if HAS_PATH
  static int injectFailure;
  static inline int vchroot_expand(struct vchroot_expand_args *v) {
    if(injectFailure && strcmp(v->path,injectFailure==3 ? "a7" : "a2")==0) return -EACCES;
    if(v->path[0]!='/' && v->dfd!=AT_FDCWD) {
      char relative[4096]; strcpy(relative,v->path);
      int length=snprintf(v->path,sizeof(v->path),"/proc/self/fd/%d/%s",v->dfd,relative);
      assert(length>0 && (size_t)length<sizeof(v->path));
    }
    return 0;
  }
  #endif
  #{source}
  #{packing}
  static int unknownTypes;
  static long get_unknown_entries(int fd,void *buffer,size_t size) {
    long result=syscall(__NR_getdents64,fd,buffer,size);
    if(result>0 && unknownTypes) {
      for(char *p=buffer;p<(char *)buffer+result;) {
        struct linux_dirent64 *entry=(void *)p;
        entry->d_type=0; p+=entry->d_reclen;
      }
    }
    return result;
  }
  #undef LINUX_SYSCALL
  #define LINUX_SYSCALL(number,fd,arg,last) ({ \
    long r=(number)==__NR_getdents64 ? get_unknown_entries(fd,(void *)(uintptr_t)(arg),last) : syscall(number,fd,arg,last); \
    r<0 ? -errno : r; })
  #{bulk}
  int main(int argc,char **argv) {
    assert(argc==5 && mkdir(argv[1],0700)==0);
    unsigned long options=strtoul(argv[2],NULL,10);
    injectFailure=atoi(argv[3]);
    unknownTypes=atoi(argv[4]);
    int fd=open(argv[1],O_RDONLY|O_DIRECTORY); assert(fd>=0);
    for(unsigned i=0;i<5;++i) {
      char name[8]; snprintf(name,sizeof(name),"a%u",i);
      int f=openat(fd,name,O_CREAT|O_WRONLY,0600); assert(f>=0);
      for(unsigned n=0;n<=i;++n) assert(write(f,"x",1)==1);
      close(f);
    }
    struct xnu_attrlist attrs={.bitmapcount=5,.commonattr=0x82000009,.dirattr=2,.fileattr=0x200};
    if(injectFailure==1 || injectFailure==3) attrs.commonattr|=0x20000000;
    assert(symlinkat("a0",fd,"a5")==0);
    assert(symlinkat("missing-target",fd,"a6")==0);
    assert(mkdirat(fd,"a7",0700)==0);
    for(unsigned bitmapcount=4;bitmapcount<=6;bitmapcount+=2) {
      struct xnu_attrlist invalid=attrs; invalid.bitmapcount=bitmapcount;
      unsigned char untouched[80]; memset(untouched,0xa5,sizeof(untouched));
      off_t position=lseek(fd,0,SEEK_CUR); assert(position>=0);
      assert(sys_getattrlistbulk(fd,&invalid,untouched,sizeof(untouched),options)==-EINVAL);
      assert(lseek(fd,0,SEEK_CUR)==position);
      for(unsigned b=0;b<sizeof(untouched);++b) assert(untouched[b]==0xa5);
    }
    unsigned char shortStorage[34]; memset(shortStorage,0xa5,sizeof(shortStorage));
    {
      unsigned char untouched[80]; memset(untouched,0xa5,sizeof(untouched));
      off_t position=lseek(fd,0,SEEK_CUR);
      assert(sys_getattrlistbulk(fd,&attrs,untouched,sizeof(untouched),options|0x40)==-ENOTSUP);
      assert(lseek(fd,0,SEEK_CUR)==position);
      for(unsigned b=0;b<sizeof(untouched);++b) assert(untouched[b]==0xa5);
    }
    for(unsigned invalidBit=0;invalidBit<4;++invalidBit) {
      struct xnu_attrlist invalid=attrs;
      if(invalidBit==0) invalid.dirattr|=0x40;
      if(invalidBit==1) invalid.fileattr|=0x4000;
      if(invalidBit==2) invalid.forkattr=1;
      if(invalidBit==3) invalid.forkattr=0x800;
      unsigned char untouched[80]; memset(untouched,0xa5,sizeof(untouched));
      off_t position=lseek(fd,0,SEEK_CUR);
      assert(sys_getattrlistbulk(fd,&invalid,untouched,sizeof(untouched),options)==-EINVAL);
      assert(lseek(fd,0,SEEK_CUR)==position);
      for(unsigned b=0;b<sizeof(untouched);++b) assert(untouched[b]==0xa5);
    }
    assert(sys_getattrlistbulk(fd,&attrs,shortStorage+1,32,options)==-ERANGE);
    assert(shortStorage[0]==0xa5 && shortStorage[33]==0xa5);
    unsigned seen=0,calls=0;
    for(;;) {
      unsigned char storage[82]; memset(storage,0xa5,sizeof(storage));
      unsigned char *out=storage+1;
      long count=sys_getattrlistbulk(fd,&attrs,out,80,options);
      assert(count>=0 && count<=1 && ++calls<=9);
      assert(storage[0]==0xa5 && storage[81]==0xa5);
      if(count==0) break;
      uint32_t length,common,file,dir,type,nameLength;
      int32_t nameOffset; uint64_t inode; int64_t size;
      memcpy(&length,out,4); memcpy(&common,out+4,4); memcpy(&file,out+16,4);
      memcpy(&dir,out+12,4);
      unsigned reference=24+((common&0x20000000) ? 4 : 0);
      uint32_t error=0; if(reference==28) memcpy(&error,out+24,4);
      memcpy(&nameOffset,out+reference,4); memcpy(&nameLength,out+reference+4,4);
      memcpy(&type,out+reference+8,4); memcpy(&inode,out+reference+12,8); memcpy(&size,out+reference+20,8);
      assert(nameLength==3);
      const char *name=(const char *)out+reference+nameOffset;
      assert(name[0]=='a' && name[1]>='0' && name[1]<='7' && name[2]==0);
      unsigned i=name[1]-'0'; assert(!(seen&(1U<<i))); seen|=1U<<i;
      if(injectFailure && i==(injectFailure==3 ? 7U : 2U)) {
        assert(common==(injectFailure!=2 ? 0xa0000001U : 0x80000001U));
        assert(dir==0 && file==0 && error==(injectFailure!=2 ? EACCES : 0));
        assert(length==(options ? (injectFailure==1 ? 64U : 56U) : 40U));
        continue;
      }
      uint32_t expectedCommon=attrs.commonattr;
      if(!options) expectedCommon&=~0x20000000U;
      assert(common==expectedCommon && error==0);
      assert(length==((reference==28 && type!=2) ? 64U : 56U));
      assert(nameOffset==(type==2 ? 24 : 28));
      struct stat st; assert(fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW)==0);
      assert(inode==st.st_ino);
      assert(type==(i<5 ? 1U : i<7 ? 5U : 2U));
      if(i==7) {
        uint32_t entries; memcpy(&entries,out+44,4);
        assert(dir==2 && file==0 && entries==0);
      } else assert(dir==0 && file==0x200 && size==st.st_size);
    }
    assert(seen==255 && calls==9);
    for(unsigned i=0;i<8;++i) {
      char name[8]; snprintf(name,sizeof(name),"a%u",i);
      assert(unlinkat(fd,name,i==7 ? AT_REMOVEDIR : 0)==0);
    }
    close(fd); assert(rmdir(argv[1])==0);
    puts("PASS: bulk/helper composition, unaligned buffers, exact fields and live/dangling symlink entries");
  }
C
Dir.mktmpdir('bulk-packed-') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  abort 'compile failed' unless system('clang','-Wall','-Wextra','-O2',
    '-fsanitize=address,undefined','-DHAS_PATH=1',input,'-o',output)
  [0,8].each do |options|
    [0,1,2,3].each do |failure|
      [0,1].each do |unknown|
        abort 'combined regression failed' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},
          output,File.join(dir,'files'),options.to_s,failure.to_s,unknown.to_s,rlimit_core:0)
      end
    end
  end
end

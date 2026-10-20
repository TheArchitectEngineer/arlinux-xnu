# Compile the actual failed-entry packing helper with its shared width tables.
# Checks controlled error records, not enumeration or real failing lookups.
require 'tmpdir'
root=File.join(File.realpath(ARGV.fetch(0)),'darling/src/libsystem_kernel/emulation')
source=File.read(File.join(root,'include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'))
mount_dir=File.join(root,'include/xnu_syscall/bsd/helper/xattr')
source=source.sub('#include "mountroot.h"') { File.read(File.join(mount_dir,'mountpoint.h')) + File.read(File.join(mount_dir,'mountroot.h')) }
source=source.lines.reject { |line| line.start_with?('#include') }.join
header=File.read(File.join(root,'include/conversion/xattr/getattrlist.h'))
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
  struct linux_dirent64 { uint64_t d_ino; int64_t d_off; unsigned short d_reclen; unsigned char type; char d_name[]; };
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
  static inline int vchroot_expand(struct vchroot_expand_args *v) {
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
  int main(void) {
    struct xnu_attrlist attrs={.bitmapcount=5,.commonattr=0xa0000009,.dirattr=2,.fileattr=0x200};
    for(unsigned invalid=0;invalid<2;++invalid) for(unsigned directory=0;directory<2;++directory) {
      unsigned char storage[82]; memset(storage,0xa5,sizeof(storage));
      unsigned char *out=storage+1;
      uint32_t expected=invalid ? (directory ? 48 : 56) : 40;
      assert(darling_pack_attribute_error("bad",directory,&attrs,out,expected-1,invalid ? 8 : 0,EACCES)==-ERANGE);
      for(unsigned i=0;i<sizeof(storage);++i) assert(storage[i]==0xa5);
      assert(darling_pack_attribute_error("bad",directory,&attrs,out,80,invalid ? 8 : 0,EACCES)==0);
      uint32_t length,common,error,nameLength; int32_t offset;
      memcpy(&length,out,4); memcpy(&common,out+4,4); memcpy(&error,out+24,4);
      memcpy(&offset,out+28,4); memcpy(&nameLength,out+32,4);
      assert(length==expected && common==0xa0000001 && error==EACCES && nameLength==4);
      assert(strcmp((char *)out+28+offset,"bad")==0);
      for(unsigned i=8;i<24;++i) assert(out[i]==0);
      for(unsigned i=36;i<28+(unsigned)offset;++i) assert(out[i]==0);
      assert(storage[0]==0xa5 && out[length]==0xa5);
    }
    puts("PASS: failed-entry name/error masks, file/directory placeholder layouts and short-buffer preservation");
  }
C
Dir.mktmpdir('bulk-error-packing-') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  abort 'compile failed' unless system('clang','-Wall','-Wextra','-O2',
    '-fsanitize=address,undefined','-DHAS_PATH=1',input,'-o',output)
  abort 'error packing failed' unless system({'UBSAN_OPTIONS'=>'halt_on_error=1'},output)
end

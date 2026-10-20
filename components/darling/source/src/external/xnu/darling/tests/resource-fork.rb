# Native Linux regression for both instantiations of the actual shared helper.
# Optional argument selects a baseline getattrlist_generic.c.
require 'tmpdir'
root=File.expand_path('../src/libsystem_kernel/emulation',__dir__)
source=File.read(ARGV[0] || File.join(root,'include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'))
source=source.lines.reject { |line| line.start_with?('#include') }.join
header=File.read(File.join(root,'include/conversion/xattr/getattrlist.h'))
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
  #define LINUX_AT_SYMLINK_NOFOLLOW AT_SYMLINK_NOFOLLOW
  #define LINUX_O_RDONLY O_RDONLY
  #define LINUX_O_DIRECTORY O_DIRECTORY
  #define VCHROOT_FOLLOW 1
  #define atfd(fd) (fd)
  #define sys_dup dup
  #define close_internal close
  #define errno_linux_to_bsd(error) (error)
  #define LINUX_SYSCALL(...) ({ long r=syscall(__VA_ARGS__); r<0 ? -errno : r; })
  #define FUNC_NAME test_getattrlist
  struct linux_dirent64 { uint64_t ino; int64_t off; unsigned short d_reclen; unsigned char type; char name[]; };
  struct vchroot_expand_args { int flags,dfd; char path[4096]; };
  #if HAS_PATH
  static inline int vchroot_expand(struct vchroot_expand_args *v) { (void)v; return 0; }
  #endif
  #{source}
  static long get(int fd,const char *path,struct xnu_attrlist *attrs,void *out,size_t size,unsigned long options) {
  #if HAS_PATH
    return test_getattrlist(fd,path,attrs,out,size,options);
  #else
    (void)path;
    return test_getattrlist(fd,attrs,out,size,options);
  #endif
  }
  int main(int argc,char **argv) {
    assert(argc==2);
    const char *path=argv[1];
    int fd=open(path,O_CREAT|O_RDWR,0600); assert(fd>=0);
    const char payload[]="resource payload";
    char finder[32]={0};
    for(unsigned i=0;i<sizeof finder;++i) finder[i]=(char)(i+1);
    assert(fsetxattr(fd,"user.com.apple.ResourceFork",payload,sizeof payload,0)==0);
    assert(fsetxattr(fd,"user.com.apple.FinderInfo",finder,sizeof finder,0)==0);
    {
      struct xnu_attrlist info={.bitmapcount=5,.commonattr=0x4000};
      unsigned char result[40]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&info,result,sizeof result,0)==0);
      uint32_t size; memcpy(&size,result,4);
      assert(size==36 && memcmp(result+4,finder,32)==0 && result[36]==0xa5);
    }
    struct xnu_attrlist attrs={.bitmapcount=5,.fileattr=0x1000};
    unsigned char out[64]; int64_t length; uint32_t total;
    memset(out,0xa5,sizeof out);
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&total,out,4); memcpy(&length,out+4,8);
    assert(total==12 && length==sizeof payload && out[12]==0xa5);
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
      struct xnu_attrlist info={.bitmapcount=5,.commonattr=0x4000};
      unsigned char result[40];
      assert(get(fd,linkpath,&info,result,sizeof result,0)==0);
      assert(memcmp(result+4,finder,32)==0);
      assert(get(fd,linkpath,&info,result,sizeof result,1)==0);
      for(unsigned i=4;i<36;++i) assert(result[i]==0);
    }
    attrs.commonattr=0;
    assert(get(fd,linkpath,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+4,8); assert(length==sizeof payload);
    assert(get(fd,linkpath,&attrs,out,sizeof out,1)==0);
    memcpy(&length,out+4,8); assert(length==0);
    unlink(linkpath);
  #endif
    assert(fremovexattr(fd,"user.com.apple.ResourceFork")==0);
    assert(fremovexattr(fd,"user.com.apple.FinderInfo")==0);
    {
      struct xnu_attrlist info={.bitmapcount=5,.commonattr=0x4000};
      unsigned char result[40]; memset(result,0xa5,sizeof result);
      assert(get(fd,path,&info,result,sizeof result,0)==0);
      for(unsigned i=4;i<36;++i) assert(result[i]==0);
      assert(result[36]==0xa5);
    }
    attrs.commonattr=0;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    memcpy(&length,out+4,8); assert(length==0);
    close(fd); unlink(path);
    assert(mkdir(path,0700)==0);
    fd=open(path,O_RDONLY|O_DIRECTORY); assert(fd>=0);
    assert(fsetxattr(fd,"user.com.apple.ResourceFork",payload,sizeof payload,0)==0);
    attrs.dirattr=2;
    assert(get(fd,path,&attrs,out,sizeof out,0)==0);
    uint32_t entries; memcpy(&entries,out+4,4); memcpy(&length,out+8,8);
    assert(entries==2 && length==sizeof payload);
    close(fd); rmdir(path);
    puts("PASS: resource key/length, combined field order, short buffers, absent fork");
  }
C
Dir.mktmpdir('resource-fork') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  [0,1].each do |has_path|
    abort 'compile failed' unless system(ENV.fetch('CC','clang'),'-Wall','-Wextra','-O2','-fsanitize=address,undefined',"-DHAS_PATH=#{has_path}",input,'-o',output)
    puts "HAS_PATH=#{has_path}"
    abort 'regression failed' unless system(output,File.join(dir,'file'),rlimit_core:0)
  end
end

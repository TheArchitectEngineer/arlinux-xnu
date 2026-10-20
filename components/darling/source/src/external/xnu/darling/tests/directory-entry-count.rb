# Native Linux regression compiling both instantiations of the actual helper.
# Optional argument selects an unmodified baseline helper for a negative control.
require 'tmpdir'
root = File.expand_path('../src/libsystem_kernel/emulation', __dir__)
source = File.read(ARGV[0] || File.join(root, 'include/xnu_syscall/bsd/helper/xattr/getattrlist_generic.c'))
source = source.lines.reject { |line| line.start_with?('#include') }.join
header = File.read(File.join(root, 'include/conversion/xattr/getattrlist.h'))
program = <<~C
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
  struct linux_dirent64 { uint64_t ino; int64_t off; unsigned short d_reclen; unsigned char type; char d_name[]; };
  struct vchroot_expand_args { int flags,dfd; char path[4096]; };
  #if HAS_PATH
  static int vchroot_expand(struct vchroot_expand_args *v) { (void)v; return 0; }
  #endif
  #{source}
  static void check(int fd, const char *path, uint32_t expected) {
    struct xnu_attrlist attrs = {.bitmapcount=5, .dirattr=2};
    unsigned char out[16]; memset(out, 0xa5, sizeof out);
    off_t before=lseek(fd,0,SEEK_CUR); assert(before>=0);
  #if HAS_PATH
    assert(test_getattrlist(fd,path,&attrs,out,sizeof out,0)==0);
  #else
    (void)path;
    assert(test_getattrlist(fd,&attrs,out,sizeof out,0)==0);
  #endif
    uint32_t size,count; memcpy(&size,out,4); memcpy(&count,out+4,4);
    assert(size==8 && count==expected && out[8]==0xa5);
    assert(lseek(fd,0,SEEK_CUR)==before);
  }
  int main(int argc, char **argv) {
    assert(argc==2); const char *path=argv[1];
    int fd=open(path,O_RDONLY|O_DIRECTORY); assert(fd>=0);
    check(fd,path,0);
    const char *names[]={"file", ".hidden", "..prefix"};
    for (unsigned i=0;i<3;i++) {
      int child=openat(fd,names[i],O_CREAT|O_EXCL|O_WRONLY,0600);
      assert(child>=0); close(child);
    }
    assert(mkdirat(fd,"subdirectory",0700)==0);
    assert(symlinkat("missing",fd,"dangling")==0);
    assert(lseek(fd,0,SEEK_SET)==0); check(fd,path,5);
    /* One record, then a count from the middle of enumeration. */
    char entries[32]; assert(syscall(SYS_getdents64,fd,entries,sizeof entries)>0);
    check(fd,path,5); check(fd,path,5);
    char batch[1024]; long n;
    do { n=syscall(SYS_getdents64,fd,batch,sizeof batch); assert(n>=0); } while(n);
    check(fd,path,5); /* An exhausted descriptor still reports the total. */
    for(unsigned i=0;i<3;i++) assert(unlinkat(fd,names[i],0)==0);
    assert(unlinkat(fd,"dangling",0)==0);
    assert(unlinkat(fd,"subdirectory",AT_REMOVEDIR)==0);
    close(fd);
    puts("PASS: counts exclude dots, include hidden objects, and preserve start/middle/EOF offsets");
  }
C
Dir.mktmpdir('directory-entry-count') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  Dir.mkdir(File.join(dir,'objects'))
  [0,1].each do |has_path|
    abort 'compile failed' unless system(ENV.fetch('CC','clang'), '-Wall', '-Wextra', '-O2', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', "-DHAS_PATH=#{has_path}", input, '-o', output)
    puts "HAS_PATH=#{has_path}"
    abort 'regression failed' unless system(output,File.join(dir,'objects'),rlimit_core:0)
  end
end

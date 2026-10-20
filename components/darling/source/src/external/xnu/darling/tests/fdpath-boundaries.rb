# Exercise the actual fd-to-guest-path implementation with controlled readlink.
require 'tmpdir'
path = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/src/linux_premigration/vchroot_userspace.c', __dir__)
source = File.read(path)
source = source[source.index('int vchroot_fdpath(')...source.index('int vchroot_unexpand(')]
program = <<~C
  #include <assert.h>
  #include <errno.h>
  #include <stdarg.h>
  #include <stdio.h>
  #include <string.h>
  #define TEST 1
  #define LINUX_ENAMETOOLONG ENAMETOOLONG
  #define LINUX_AT_FDCWD -100
  #define __NR_readlinkat 2
  #if DIRECT_READLINK
  #define __NR_readlink 1
  #endif
  #define __simple_sprintf sprintf
  static const char prefix_path[]="/guest";
  static int prefix_path_len=6;
  static const char EXIT_PATH[]="/Volumes/SystemRoot";
  static const char *target;
  static int error;
  struct vchroot_fdpath_args { int fd; char *path; unsigned maxlen; };
  static long fake_syscall(int number, ...) {
    va_list ap; va_start(ap,number);
    if(number==2) assert(va_arg(ap,int)==LINUX_AT_FDCWD);
    const char *path=va_arg(ap,const char*);
    char *out=va_arg(ap,char*); size_t cap=va_arg(ap,size_t); va_end(ap);
    assert(strcmp(path,"/proc/self/fd/7")==0);
    if(error) return -error;
    size_t length=strlen(target); if(length>cap) length=cap;
    memcpy(out,target,length); return length;
  }
  #define LINUX_SYSCALL fake_syscall
  #{source}
  static void check(const char *input, const char *expected) {
    target=input;
    unsigned length=strlen(expected)+1;
    for(unsigned cap=0;cap<=length+1;++cap) {
      char out[128]; memset(out,0x5a,sizeof out);
      struct vchroot_fdpath_args args={7,out,cap};
      int result=vchroot_fdpath(&args);
      if(cap<length) {
        assert(result==-ENAMETOOLONG);
        for(unsigned i=0;i<sizeof out;++i) assert(out[i]==0x5a);
      } else {
        assert(result==0 && strcmp(out,expected)==0 && out[length]==0x5a);
      }
    }
  }
  int main(void) {
    check("/guest","/"); check("/guest/file","/file");
    check("/guest-other/file","/Volumes/SystemRoot/guest-other/file");
    check("/outside","/Volumes/SystemRoot/outside");
    char longpath[5000]; memset(longpath,'a',sizeof longpath); longpath[0]='/'; longpath[4999]=0;
    target=longpath; char out[6000]; memset(out,0x5a,sizeof out);
    struct vchroot_fdpath_args args={7,out,sizeof out};
    assert(vchroot_fdpath(&args)==-ENAMETOOLONG);
    for(unsigned i=0;i<sizeof out;++i) assert(out[i]==0x5a);
    error=EBADF; assert(vchroot_fdpath(&args)==-EBADF);
    puts("PASS: prefix boundary, root and exact output capacity, truncation and readlink errors");
  }
C
Dir.mktmpdir('fdpath-boundaries') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  [0,1].each do |direct|
    abort 'compile failed' unless system('clang','-Wall','-Wextra','-O2','-fsanitize=address,undefined','-fno-sanitize-recover=all',"-DDIRECT_READLINK=#{direct}",input,'-o',output)
    abort 'regression failed' unless system(output,rlimit_core:0)
  end
end

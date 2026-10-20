# Focused production-function test; procfs parsing/I/O is mocked.
require 'tmpdir'
root=File.expand_path('../..',__dir__)
source=File.read(ARGV[0] || File.join(root,'darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc/proc_info.c'))
function=source[/^static long _proc_pidonfo_uniqinfo\([^\n]*\)\n\{.*?^\}/m] or abort 'function missing'
record=File.read(File.join(root,'bsd/sys/proc_info.h'))[/struct proc_uniqidentifierinfo \{.*?^\};/m] or abort 'record missing'
program=<<~C
  #include <stdint.h>
  #include <string.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <errno.h>
  #include <assert.h>
  #{record}
  static char fakeuuid[16];
  static int reads,fail_read,element,no_parent;
  #define __simple_sprintf sprintf
  #define __simple_atoi(text,end) strtoull(text,(char **)end,10)
  static int read_string(const char *path,char *out,size_t size) {
    (void)path;(void)size;out[0]=0;
    return ++reads!=fail_read;
  }
  static void skip_stat_elems(char **p,int n) {(void)p;(void)n;}
  static const char *next_stat_elem(char **p) {
    (void)p; const char *values[]={no_parent ? "0" : "2","10","20"};
    assert(element<3);return values[element++];
  }
  #{function}
  int main(void) {
    struct proc_uniqidentifierinfo info;
    memset(&info,0xa5,sizeof info);
  #ifndef VARIANT_DYLD
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info-1)==-ENOSPC && reads==0);
    for (size_t i=0;i<sizeof info;++i) assert(((unsigned char *)&info)[i]==0xa5);
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info)==sizeof info);
    assert(info.p_uniqueid==((10ULL<<16)|3) && info.p_puniqueid==((20ULL<<16)|2));
    reads=element=0;fail_read=1;
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info)==-ESRCH);
    reads=element=0;fail_read=2;no_parent=1;
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info)==sizeof info);
    assert(reads==1 && info.p_puniqueid==0 && info.p_uniqueid==((10ULL<<16)|3));
    no_parent=0;
    reads=element=0;fail_read=2;
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info)==-ESRCH);
  #else
    assert(_proc_pidonfo_uniqinfo(3,&info,sizeof info)==-ENOTSUP);
    for (size_t i=0;i<sizeof info;++i) assert(((unsigned char *)&info)[i]==0xa5);
    assert(reads==0);
  #endif
    puts("PASS: result-size/error contract");
  }
C
Dir.mktmpdir('proc-unique') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  [[],['-DVARIANT_DYLD']].each do |flags|
    abort 'compile failed' unless system('clang','-fsanitize=address,undefined',*flags,input,'-o',output)
    abort 'test failed' unless system(output,rlimit_core:0)
  end
end

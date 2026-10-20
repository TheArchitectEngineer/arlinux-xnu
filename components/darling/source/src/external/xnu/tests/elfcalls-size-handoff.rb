require 'tmpdir'
require 'open3'
source=File.read(File.expand_path('../darling/src/libsystem_kernel/emulation/src/other/mach/lkm.c',__dir__))
block=source[/\tvoid\* \(\*p2\)\(void\) = NULL;.*?(?=\n#if defined\(__aarch64__\))/m] or abort 'handoff missing'
Dir.mktmpdir('elfcalls-handoff-') do |dir|
  code=<<~C
    #include <stddef.h>
    #include <string.h>
    #include <assert.h>
    #include <stdio.h>
    static void *_elfcalls;
    static size_t _elfcalls_size;
    static int mode, token;
    static void *pointer(void) { return mode==2 ? NULL : &token; }
    static size_t size(void) { return 123; }
    static void lookup(const char *name,void **out) {
      if(!strcmp(name,"__dyld_get_elfcalls")) *out=pointer;
      if(!strcmp(name,"__dyld_get_elfcalls_size") && mode!=1) *out=size;
    }
    static struct { void (*dyld_func_lookup)(const char *,void **); } funcs={lookup}, *_libkernel_functions=&funcs;
    static void handoff(void) { #{block} }
    int main(void) {
      for(mode=0;mode<3;++mode) {
        _elfcalls=NULL; _elfcalls_size=999; handoff();
        assert(_elfcalls==(mode==2 ? NULL : &token));
        assert(_elfcalls_size==(mode==0 ? 123 : 0));
      }
      mode=0; _elfcalls=&token; _elfcalls_size=456; handoff();
      assert(_elfcalls==&token && _elfcalls_size==123);
      mode=1; handoff();
      assert(_elfcalls==&token && _elfcalls_size==0);
      puts("PASS: sized dyld, old dyld, null pointer and repeated handoff reset");
    }
  C
  File.write("#{dir}/probe.c",code)
  out,status=Open3.capture2e('clang','-fsanitize=address,undefined',"#{dir}/probe.c",'-o',"#{dir}/probe")
  abort out unless status.success?
  out,status=Open3.capture2e("#{dir}/probe")
  puts out
  abort 'handoff failed' unless status.success?
end

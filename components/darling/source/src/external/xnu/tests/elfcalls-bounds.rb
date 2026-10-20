require 'tmpdir'
require 'open3'
root=File.expand_path('..',__dir__)
header=File.realpath(ARGV.fetch(0))
source=File.read("#{root}/darling/src/libsystem_kernel/emulation/src/linux_premigration/elfcalls_wrapper.c")
macro=source[/^#define ELFCALLS_HAS_FIELD.*\n.*\n/] or abort 'bounds macro missing'
fork=source[/^int native_fork\(void\)\n\{.*?^\}/m] or abort 'fork missing'
complete=source[/^void __darling_arm64_thread_bridge_postfork_complete\(void\)\n\{.*?^\}/m] or abort 'completion missing'
Dir.mktmpdir('elfcalls-bounds-') do |dir|
  code=<<~C
    #include <assert.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include "#{header}"
    #define LINUX_ENOSYS 38
    static struct elf_calls *_elfcalls;
    static size_t _elfcalls_size;
    static int forks, completions, setup;
    static int fork_callback(void) { ++forks; return 42; }
    static void complete_callback(void) { ++completions; }
    static void sys_fork_postfork_child(void) { ++setup; }
    #{macro}
    #{fork}
    #{complete}
    int main(void) {
      assert(native_fork()==-LINUX_ENOSYS);
      size_t end=offsetof(struct elf_calls,native_fork)+sizeof(_elfcalls->native_fork);
      for(size_t n=1;n<end;++n) {
        _elfcalls=calloc(1,n); _elfcalls_size=n;
        assert(native_fork()==-LINUX_ENOSYS);
        __darling_arm64_thread_bridge_postfork_complete();
        free(_elfcalls);
      }
      _elfcalls=calloc(1,sizeof(*_elfcalls)); _elfcalls_size=sizeof(*_elfcalls);
      assert(native_fork()==-LINUX_ENOSYS);
      _elfcalls->native_fork=fork_callback;
      _elfcalls->arm64_thread_bridge_postfork_complete=complete_callback;
      _elfcalls_size=end;
      assert(native_fork()==42 && forks==1);
      __darling_arm64_thread_bridge_postfork_complete(); assert(completions==0);
      _elfcalls_size=sizeof(*_elfcalls);
      __darling_arm64_thread_bridge_postfork_complete(); assert(completions==1);
      _elfcalls_size=0; assert(native_fork()==-LINUX_ENOSYS);
      free(_elfcalls);
      puts("PASS: truncated/unknown/null callbacks and exact-boundary extended dispatch");
    }
  C
  File.write("#{dir}/probe.c",code)
  out,status=Open3.capture2e('clang','-O1','-fsanitize=address,undefined',"#{dir}/probe.c",'-o',"#{dir}/probe")
  abort out unless status.success?
  out,status=Open3.capture2e("#{dir}/probe")
  puts out
  abort 'bounds failed' unless status.success?
end

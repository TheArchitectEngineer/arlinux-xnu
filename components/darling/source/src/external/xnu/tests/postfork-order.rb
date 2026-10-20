# Host control-flow check of the actual private XNU fork implementation.
# Controlled callbacks, not real process creation or guest malloc validation.
require 'tmpdir'
require 'open3'
root=ARGV.fetch(0, File.expand_path('..', __dir__))
source=File.read("#{root}/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/process/fork.c")
source=source.gsub(/^#include.*\n/,'').sub(/^extern _libkernel_functions_t.*\n/,'')
Dir.mktmpdir('postfork-order-') do |dir|
  code=<<~C
    #include <assert.h>
    #include <stdio.h>
    #include <stdbool.h>
    #define __arm64__ 1
    static int result, phase, guards, checked, restored, closed;
    static void reenter_completion(void);
    typedef struct { void (*close)(int); } guard_entry_options_t;
    enum { guard_flag_prevent_close=1, guard_flag_close_on_fork=2 };
    static int get_perthread_wd(void) { return 17; }
    static int native_fork(void) { return result; }
    static int errno_linux_to_bsd(int v) { return v; }
    static void guard_table_postfork_child(void) { assert(phase==0); phase=1; }
    static void __dserver_per_thread_socket_refresh(void) { assert(phase==1); phase=2; }
    static int __dserver_process_lifetime_pipe_refresh(void) { assert(phase==2); phase=3; return 19; }
    static int __dserver_per_thread_socket(void) { return 20; }
    static int __dserver_get_process_lifetime_pipe(void) { return 21; }
    static void __dserver_close_socket(int fd) { assert(0); }
    static void __dserver_close_process_lifetime_pipe(int fd) { assert(phase==4 && fd==19); ++closed; }
    static void guard_table_add(int fd,int flags,guard_entry_options_t *options) {
      assert(phase==4 && (fd==20 || fd==21)); ++guards;
    }
    static int dserver_rpc_checkin(int child,void *stack,int fd) {
      assert(phase==4 && guards==2 && child && stack && fd==19);
      ++checked; reenter_completion(); return 0;
    }
    static void sys_fchdir(int fd) { assert(checked==1 && fd==17); ++restored; }
    static void __simple_printf(const char *s) { assert(0); }
    static void __simple_abort(void) { assert(0); }
    #{source}
    static void reenter_completion(void) { sys_fork_postfork_child(); }
    int main(void) {
      sys_fork_postfork_child();
      assert(phase==0 && guards==0 && checked==0 && restored==0 && closed==0);
      result=-12; assert(sys_fork()==-12 && phase==0 && guards==0);
      result=42; assert(sys_fork()==42 && phase==0 && guards==0);
      result=0; assert(sys_fork()==0 && phase==3);
      assert(guards==0 && checked==0 && restored==0 && closed==0);
      // Model completion of libSystem's malloc child callback.
      phase=4; sys_fork_postfork_child();
      assert(guards==2 && checked==1 && restored==1 && closed==1);
      assert(postfork_child_wdfd==-1 && postfork_child_lifetime_read_fd==-1);
      sys_fork_postfork_child();
      assert(guards==2 && checked==1 && restored==1 && closed==1);
      puts("PASS: child setup defers guarding/check-in until completion; parent/error paths unchanged");
    }
  C
  File.write("#{dir}/probe.c",code)
  out,status=Open3.capture2e('clang','-O1','-fsanitize=address,undefined',"#{dir}/probe.c",'-o',"#{dir}/probe")
  abort out unless status.success?
  out,status=Open3.capture2e("#{dir}/probe",rlimit_core:0)
  puts out
  abort 'postfork control flow failed' unless status.success?
end

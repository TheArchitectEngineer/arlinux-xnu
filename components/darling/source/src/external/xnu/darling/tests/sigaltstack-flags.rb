# Linux-native test of the production wrapper with a real sigaltstack syscall.
# Optional argument selects an upstream/control sigaltstack.c.
require 'tmpdir'
root = File.expand_path('../src/libsystem_kernel/emulation', __dir__)
source = File.read(ARGV[0] || File.join(root, 'src/xnu_syscall/bsd/impl/signal/sigaltstack.c'))
source = source.lines.reject { |line| line.start_with?('#include') }.join
header = File.read(File.join(root, 'include/xnu_syscall/bsd/impl/signal/sigaltstack.h'))
program = <<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <signal.h>
  #include <stddef.h>
  #include <stdlib.h>
  #include <stdio.h>
  #include <sys/syscall.h>
  #include <unistd.h>
  #{header}
  _Static_assert(sizeof(struct linux_stack)==sizeof(stack_t), "stack layout");
  _Static_assert(offsetof(struct linux_stack,ss_flags)==offsetof(stack_t,ss_flags), "flags layout");
  _Static_assert(offsetof(struct linux_stack,ss_size)==offsetof(stack_t,ss_size), "size layout");
  static long host_sigaltstack(const struct linux_stack *ss, struct linux_stack *oss) {
    long result=syscall(SYS_sigaltstack,ss,oss);
    return result < 0 ? -errno : result;
  }
  /* EINVAL, EPERM, and ENOMEM have equal values on the two tested ABIs. */
  #define errno_linux_to_bsd(error) (error)
  #define LINUX_SYSCALL(number,ss,oss) host_sigaltstack(ss,oss)
  #{source}
  static volatile sig_atomic_t onstack;
  static void handler(int signo) {
    struct bsd_stack state;
    (void)signo;
    onstack = sys_sigaltstack(NULL,&state)==0 && state.ss_flags==1;
  }
  int main(void) {
    struct bsd_stack state;
    assert(sys_sigaltstack(NULL,&state)==0 && state.ss_flags==4);
    void *memory=malloc(128*1024); assert(memory);
    struct bsd_stack install={memory,128*1024,0};
    assert(sys_sigaltstack(&install,&state)==0 && state.ss_flags==4);
    assert(sys_sigaltstack(NULL,&state)==0 && state.ss_flags==0);
    assert(state.ss_sp==memory && state.ss_size==128*1024);
    struct sigaction action={0};
    action.sa_handler=handler; action.sa_flags=SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGUSR1,&action,NULL)==0);
    assert(raise(SIGUSR1)==0 && onstack);
    for (unsigned flag=1;flag<=8;++flag) {
      if (flag==4) continue;
      install.ss_flags=flag;
      state.ss_sp=(void*)0x1230; state.ss_size=42; state.ss_flags=99;
      assert(sys_sigaltstack(&install,&state)==-EINVAL);
      assert(state.ss_sp==(void*)0x1230 && state.ss_size==42 && state.ss_flags==99);
      assert(sys_sigaltstack(NULL,&state)==0 && state.ss_flags==0);
      assert(state.ss_sp==memory && state.ss_size==128*1024);
    }
    struct bsd_stack disable={NULL,0,4};
    assert(sys_sigaltstack(&disable,&state)==0 && state.ss_flags==0);
    assert(sys_sigaltstack(NULL,&state)==0 && state.ss_flags==4);
    free(memory);
    puts("PASS: disabled/enabled/on-stack flags, invalid flags, real disable syscall");
  }
C
Dir.mktmpdir('sigaltstack-flags') do |dir|
  input=File.join(dir,'test.c'); output=File.join(dir,'test')
  File.write(input,program)
  abort 'compile failed' unless system(ENV.fetch('CC','cc'),'-Wall','-Wextra','-O2',input,'-o',output)
  abort 'test failed' unless system(output,rlimit_core:0)
end

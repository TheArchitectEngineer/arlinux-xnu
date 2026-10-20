# Linux/AArch64 ABI and real signal-delivery probe. Extracts the candidate's
# actual Linux context declarations without importing Darwin signal types.
# Usage: ruby darling/tests/arm64-signal-context.rb [path/to/emulation]
require 'tmpdir'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation', __dir__)
header = File.read(File.join(root, 'include/conversion/signal/sigaction.h'))
arm = header[/#elif defined\(__aarch64__\) \|\| defined\(__arm64__\)\n(.*?)\n#endif/m, 1]
abort 'ARM context declarations not found' unless arm
structures = %w[linux_mcontext linux_ucontext].map do |name|
  header[/^struct #{name}\n\{.*?^\};/m] || abort("missing #{name}")
end.join("\n")
stack_header = File.read(File.join(root, 'include/xnu_syscall/bsd/impl/signal/sigaltstack.h'))
stack = stack_header[/^struct linux_stack\n\{.*?^\};/m]
signals = File.read(File.join(root, 'include/conversion/signal/duct_signals.h'))
sigset = signals[/^typedef .* linux_sigset_t;/]
abort 'missing stack/mask declarations' unless stack && sigset
program = <<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <stddef.h>
  #include <stdint.h>
  #include <signal.h>
  #include <stdio.h>
  #include <string.h>
  #include <ucontext.h>
  #if !defined(__aarch64__)
  #error Run this probe on Linux/AArch64
  #endif
  #{sigset}
  #{stack}
  #{arm}
  #{structures}
  #define UC(field) _Static_assert(offsetof(struct linux_ucontext, field) == offsetof(ucontext_t, field), #field)
  UC(uc_flags); UC(uc_link); UC(uc_stack); UC(uc_sigmask); UC(uc_mcontext);
  #define MC(local, native) _Static_assert(offsetof(struct linux_mcontext, local) == offsetof(mcontext_t, native), #local)
  MC(gregs.fault_address, fault_address); MC(gregs.regs, regs);
  MC(gregs.sp, sp); MC(gregs.pc, pc); MC(gregs.pstate, pstate);
  MC(__reserved, __reserved);
  _Static_assert(sizeof(struct linux_mcontext) == sizeof(mcontext_t), "mcontext size");
  _Static_assert(sizeof(struct linux_ucontext) == sizeof(ucontext_t), "ucontext size");
  _Static_assert(_Alignof(struct linux_mcontext) == _Alignof(mcontext_t), "mcontext alignment");
  static volatile sig_atomic_t handled;
  static void handler(int signo, siginfo_t *info, void *context) {
      (void)info;
      assert(signo == SIGUSR1);
      const ucontext_t *native = context;
      const struct linux_ucontext *candidate = context;
      assert(candidate->uc_mcontext.gregs.pc == native->uc_mcontext.pc);
      assert(candidate->uc_mcontext.gregs.sp == native->uc_mcontext.sp);
      assert(candidate->uc_mcontext.gregs.pstate == native->uc_mcontext.pstate);
      assert(candidate->uc_mcontext.gregs.fault_address == native->uc_mcontext.fault_address);
      for (int i = 0; i < 31; ++i)
          assert(candidate->uc_mcontext.gregs.regs[i] == native->uc_mcontext.regs[i]);
      assert(candidate->uc_sigmask & (1ULL << (SIGUSR2 - 1)));
      assert((const void *)candidate->uc_mcontext.__reserved == (const void *)native->uc_mcontext.__reserved);
      handled = 1;
  }
  int main(void) {
      struct sigaction action = {0}, previous;
      action.sa_sigaction = handler; action.sa_flags = SA_SIGINFO;
      sigemptyset(&action.sa_mask);
      assert(sigaction(SIGUSR1, &action, &previous) == 0);
      sigset_t mask, oldmask;
      sigemptyset(&mask); sigaddset(&mask, SIGUSR2);
      assert(sigprocmask(SIG_BLOCK, &mask, &oldmask) == 0);
      assert(raise(SIGUSR1) == 0 && handled);
      assert(sigprocmask(SIG_SETMASK, &oldmask, NULL) == 0);
      assert(sigaction(SIGUSR1, &previous, NULL) == 0);
      printf("PASS: native ABI offsets/sizes and delivered registers/mask; ucontext=%zu\\n", sizeof(ucontext_t));
  }
C
Dir.mktmpdir('arm64-signal-context') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-g',
      '-Wall', '-Wextra', '-fno-strict-aliasing', '-fsanitize=address,undefined', input, '-o', output)
  abort 'signal ABI regression' unless system(output, rlimit_core: 0)
end

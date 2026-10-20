# Exercise the actual syscall handler without sending signals or exiting.
# Usage: ruby darling/tests/terminate-payload.rb [path/to/abort_with_payload.c]
# CASE=delivery skips invalid-PID checks to isolate delivery-error regressions.
require 'tmpdir'

source = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc/abort_with_payload.c', __dir__)
body = File.read(source)[/^long sys_terminate_with_payload\([^\n]+\)\n\{.*?^\}/m]
abort 'could not locate handler' unless body
program = <<~C
  #include <assert.h>
  #include <errno.h>
  #include <limits.h>
  #include <signal.h>
  #include <stdarg.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <string.h>
  static int kill_count, log_count, last_pid, last_signal, last_posix;
  static long kill_result;
  static char diagnostic[256];
  static long sys_kill(int pid, int signal, int posix) {
      ++kill_count;
      last_pid = pid; last_signal = signal; last_posix = posix;
      return kill_result;
  }
  static void __simple_printf(const char *format, ...) {
      ++log_count;
      va_list ap;
      va_start(ap, format);
      int count = vsnprintf(diagnostic, sizeof diagnostic, format, ap);
      va_end(ap);
      assert(count >= 0 && (size_t)count < sizeof diagnostic);
  }
  // Trap the old implementation's exit_group path; never exit the host.
  #define LINUX_SYSCALL1(number, status) assert(!"unexpected exit syscall")
  #{body}
  int main(int argc, char **argv) {
      const int invalid[] = {0, -1, -7, INT_MIN};
      if (argc < 2 || strcmp(argv[1], "delivery") != 0) {
          for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
              assert(sys_terminate_with_payload(invalid[i], 0, 0, NULL, 0,
                  NULL, 0) == -EINVAL);
              assert(kill_count == 0 && log_count == 0);
          }
          assert(sys_terminate_with_payload(0, 0, 0, NULL, 0,
              (const char *)(uintptr_t)1, 0) == -EINVAL);
          assert(kill_count == 0 && log_count == 0);
      }
      const long results[] = {0, -ESRCH, -EPERM};
      for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
          kill_result = results[i];
          assert(sys_terminate_with_payload(4321, 0, ULLONG_MAX, NULL, 0,
              NULL, 0) == results[i]);
          assert(kill_count == (int)i + 1 && log_count == (int)i + 1);
          assert(last_pid == 4321 && last_signal == SIGKILL && last_posix == 1);
          assert(strcmp(diagnostic, "terminate_with_payload: pid=4321 reason: (null); code: 18446744073709551615\\n") == 0);
      }
      puts("PASS: invalid PIDs rejected before logging/signaling; delivery results preserved");
  }
C

Dir.mktmpdir('terminate-payload') do |dir|
  input = File.join(dir, 'probe.c')
  output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-g',
      '-Wall', '-Wextra', '-Wno-unused-parameter', '-fsanitize=address,undefined',
      input, '-o', output)
  abort 'termination contract regression' unless system(output, ENV.fetch('CASE', 'all'), rlimit_core: 0)
end

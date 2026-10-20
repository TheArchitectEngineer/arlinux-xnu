# Native regression probe for the actual reboot stub. No reboot syscall is run:
# only diagnostic output and the existing exit status are observed.
# Usage: ruby darling/tests/reboot-diagnostic.rb [path/to/reboot.c]
require 'tmpdir'

source = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc/reboot.c', __dir__)
body = File.read(source).lines.reject { |line| line.start_with?('#include') }.join
program = <<~C
  #include <assert.h>
  #include <errno.h>
  #include <setjmp.h>
  #include <stdarg.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <string.h>
  static jmp_buf exit_target;
  static int exit_status;
  static char diagnostic[256];
  static void __simple_printf(const char *format, ...)
      __attribute__((format(printf, 1, 2)));
  static void __simple_printf(const char *format, ...) {
      va_list ap;
      va_start(ap, format);
      int count = vsnprintf(diagnostic, sizeof diagnostic, format, ap);
      va_end(ap);
      assert(count >= 0 && (size_t)count < sizeof diagnostic);
  }
  static void sys_exit(int status) __attribute__((noreturn));
  static void sys_exit(int status) {
      exit_status = status;
      longjmp(exit_target, 1);
  }
  #{body}
  static void check(int option, const char *command) {
      exit_status = -1;
      diagnostic[0] = '\\0';
      if (setjmp(exit_target) == 0) {
          sys_reboot(option, command);
          assert(!"reboot stub returned instead of exiting");
      }
      assert(exit_status == 1);
      char expected[256];
      snprintf(expected, sizeof expected,
          "ALERT: The process has asked for system reboot with opt %d - terminating\\n", option);
      assert(strcmp(diagnostic, expected) == 0);
  }
  int main(void) {
      check(0, NULL);
      check(1234, "ignored command");
      check(-7, (const char *)(uintptr_t)1);
      puts("PASS: options logged, command never dereferenced, exit status remains 1");
  }
C

Dir.mktmpdir('reboot-diagnostic') do |dir|
  input = File.join(dir, 'probe.c')
  output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-g',
      '-Wall', '-Wextra', '-Werror=format', '-fsanitize=address,undefined',
      input, '-o', output)
  abort 'reboot diagnostic regression' unless system(output, rlimit_core: 0)
end

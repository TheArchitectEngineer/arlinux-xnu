# Compare actual emulation handlers with XNU's disabled-tracing branch.
# Usage: ruby darling/tests/disabled-kdebug.rb
require 'tmpdir'
root = File.expand_path('../..', __dir__)
emulation = File.join(root, 'darling/src/libsystem_kernel/emulation')
source = File.read(File.join(emulation, 'src/xnu_syscall/bsd/impl/misc/kdebug_trace.c'))
header = File.read(File.join(emulation, 'include/xnu_syscall/bsd/impl/misc/kdebug_trace.h'))
table = File.read(File.join(emulation, 'src/xnu_syscall/bsd/bsd_syscall_table.c'))
{179 => 'sys_kdebug_trace64', 180 => 'sys_kdebug_trace'}.each do |slot, name|
  abort "wrong syscall #{slot}" unless table.match?(/\[#{slot}\]\s*=\s*#{name}\s*,/)
end
cmake = File.read(File.join(emulation, 'CMakeLists.txt'))
abort 'source not in build' unless cmake.include?('src/xnu_syscall/bsd/impl/misc/kdebug_trace.c')
reference = File.read(File.join(root, 'bsd/kern/kdebug.c'))[/^int\nkdebug_trace64\(.*?^\}/m]
abort 'reference handler not found' unless reference
program = <<~C
  #include <assert.h>
  #include <limits.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <stdlib.h>
  #{header}
  #{source.lines.reject { |line| line.start_with?('#include') }.join}
  #define __unused __attribute__((unused))
  #define __probable(value) (value)
  struct proc;
  struct kdebug_trace64_args { uint32_t code; uint64_t arg1, arg2, arg3, arg4; };
  static int kdebug_enable;
  static int kdebug_validate_debugid(uint32_t code) { (void)code; abort(); }
  static void kernel_debug_internal(uint32_t code, uintptr_t a, uintptr_t b,
      uintptr_t c, uintptr_t d, uintptr_t thread, int flags) {
      (void)code; (void)a; (void)b; (void)c; (void)d; (void)thread; (void)flags; abort();
  }
  #define current_thread() 0
  #define thread_tid(thread) 0
  #{reference}
  int main(void) {
      const uint32_t ids[] = {0, 1, 0x07000000, UINT32_MAX};
      for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
          struct kdebug_trace64_args args = {ids[i], UINT64_MAX, 0, 0x123456789abcdef0ULL, 1};
          int32_t result = 0;
          int expected = kdebug_trace64(NULL, &args, &result);
          assert(expected == 0);
          assert(sys_kdebug_trace64(args.code, args.arg1, args.arg2, args.arg3, args.arg4) == expected);
          assert(sys_kdebug_trace(args.code, ULONG_MAX, 0, 17, 1) == expected);
      }
      puts("PASS: both syscall slots/build registration and disabled-tracing results");
  }
C
Dir.mktmpdir('disabled-kdebug') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-g',
      '-Wall', '-Wextra', '-fsanitize=address,undefined', input, '-o', output)
  abort 'disabled tracing regression' unless system(output, rlimit_core: 0)
end

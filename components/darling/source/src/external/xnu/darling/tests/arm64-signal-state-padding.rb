# Tests the actual ARM64 converter and public thread-state declaration.
# Optional argument selects another xnu tree for a baseline comparison.
require 'tmpdir'
root = ARGV[0] || File.expand_path('../..', __dir__)
source = File.read(File.join(root, 'darling/src/libsystem_kernel/emulation/src/linux_premigration/signal/sigexc.c'))
function = source[/^void mcontext_to_thread_state\(const struct linux_gregset\* regs, arm_thread_state64_t\* s\).*?^\}/m]
abort 'ARM64 converter missing' unless function
header = File.read(File.join(root, 'osfmk/mach/arm/_structs.h'))
declaration = header.scan(/_STRUCT_ARM_THREAD_STATE64\n\{\n.*?\n\};/m).find { |text| text.include?('__x[29]') && text.include?('__pad;') }
abort 'nonopaque UNIX03 thread state missing' unless declaration
program = <<~C
  #include <assert.h>
  #include <stdint.h>
  #include <string.h>
  #include <stdio.h>
  #define _STRUCT_ARM_THREAD_STATE64 struct candidate_thread_state
  #{declaration}
  typedef struct candidate_thread_state arm_thread_state64_t;
  // Input fixture uses named fields only; this does not validate Linux layout.
  struct linux_gregset { uint64_t regs[31], sp, pc, pstate; };
  #{function}
  int main(void) {
      struct linux_gregset input;
      for (int i = 0; i < 31; ++i) input.regs[i] = UINT64_C(0xfedcba9800000000) + i;
      input.sp = 0x12345678; input.pc = 0x98765432;
      input.pstate = UINT64_C(0xabcdef0187654321);
      for (int pattern = 0; pattern < 256; ++pattern) {
          arm_thread_state64_t output;
          memset(&output, pattern, sizeof output);
          mcontext_to_thread_state(&input, &output);
          for (int i = 0; i < 29; ++i) assert(output.__x[i] == input.regs[i]);
          assert(output.__fp == input.regs[29] && output.__lr == input.regs[30]);
          assert(output.__sp == input.sp && output.__pc == input.pc);
          assert(output.__cpsr == (uint32_t)input.pstate);
          assert(output.__pad == 0);
      }
      puts("PASS: all register fields and zero padding across 256 destination patterns");
  }
C
Dir.mktmpdir('signal-state-padding') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-Wall', '-Wextra',
    '-fsanitize=address,undefined', input, '-o', output)
  abort 'test failed' unless system(output, rlimit_core: 0)
end

#!/usr/bin/env ruby
# Run on Linux/AArch64 with a native C compiler; no Darling SDK is required.
# Optional first argument selects another bsdthread_register.c for a control.
require 'tmpdir'
abort 'requires Linux/AArch64' unless RUBY_PLATFORM.match?(/aarch64.*linux/)
source_path = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/bsdthread/bsdthread_register.c', __dir__)
source = File.read(source_path)
function = source[/^void wqueue_entry_point_asm_jump\(.*?^\}/m]
abort 'cannot find production jump function' unless function
harness = File.read(File.join(__dir__, 'arm64-workqueue-jump.c'))
Dir.mktmpdir('workqueue-jump') do |dir|
  input = File.join(dir, 'test.c')
  output = File.join(dir, 'test')
  File.write(input, harness + "\n" + function + "\n")
  %w[-O0 -O2 -O3].product(%w[-fomit-frame-pointer -fno-omit-frame-pointer]).each do |optimization, frames|
    abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), optimization, frames, '-Wall', '-Wextra', input, '-o', output)
    puts "#{optimization} #{frames}"
    abort 'handoff test failed' unless system(output, rlimit_core: 0)
  end
end

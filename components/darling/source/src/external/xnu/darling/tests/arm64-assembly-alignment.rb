# Cross-assemble actual sources and inspect section/function alignment.
# Usage: ruby darling/tests/arm64-assembly-alignment.rb [path/to/emulation/src]
require 'tmpdir'
require 'open3'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation/src', __dir__)
files = {
  'linux_premigration/linux-syscall.S' => ['_linux_syscall'],
  'linux_premigration/signal/sig_restorer.S' => ['_sig_restorer'],
  'xnu_syscall/xtrace-hooks.S' => ['__xtrace_thread_exit', '__xtrace_execve_inject', '__xtrace_postfork_child']
}
run = lambda do |*command|
  output, status = Open3.capture2e(*command)
  abort "#{command.join(' ')}\n#{output}" unless status.success?
  output
end
Dir.mktmpdir('arm64-assembly-alignment') do |dir|
  files.each do |file, symbols|
    source = File.read(File.join(root, file))
    ['', ".text\n.byte 0\n"].each do |prefix|
      input = File.join(dir, 'probe.S'); object = File.join(dir, 'probe.o')
      File.write(input, prefix + source)
      run.call(ENV.fetch('CC', 'clang'), '-target', 'arm64-apple-darwin20', '-c', input, '-o', object)
      sections = run.call(ENV.fetch('LLVM_READOBJ', 'llvm-readobj-18'), '--sections', object)
      text = sections[/Name: __text.*?\n\s*\}/m]
      alignment = text && text[/Alignment: (\d+)/, 1]
      abort "#{file}: insufficient text alignment" unless alignment && alignment.to_i >= 2
      names = run.call(ENV.fetch('LLVM_NM', 'llvm-nm-18'), object)
      symbols.each do |symbol|
        address = names[/^([0-9a-fA-F]+)\s+\S\s+#{Regexp.escape(symbol)}$/, 1]
        abort "#{file}: missing or unaligned #{symbol}" unless address && address.to_i(16) % 4 == 0
      end
    end
    puts "PASS: #{file}: section and labels aligned, standalone and after one byte"
  end
end

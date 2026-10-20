# Check the changed dispatcher headers without requiring a complete XNU SDK.
# Included architecture headers are replaced with markers for routing checks;
# the final smoke compile uses real headers and compiler target definitions.
# Usage: ruby darling/tests/machine-header-routing.rb [path/to/xnu]
require 'tmpdir'
require 'open3'
root = File.expand_path(ARGV[0] || '../..', __dir__)
headers = %w[
  bsd/machine/_limits.h
  bsd/machine/_mcontext.h
  bsd/machine/_param.h
  bsd/machine/disklabel.h
  bsd/machine/endian.h
  bsd/machine/fasttrap_isa.h
  bsd/machine/limits.h
  bsd/machine/param.h
  bsd/machine/profile.h
  bsd/machine/psl.h
  bsd/machine/ptrace.h
  bsd/machine/reg.h
  bsd/machine/signal.h
  bsd/machine/smp.h
  bsd/machine/types.h
  bsd/machine/vmparam.h
  osfmk/mach/machine/_structs.h
  osfmk/mach/machine/exception.h
  osfmk/mach/machine/ndr_def.h
  osfmk/mach/machine/processor_info.h
  osfmk/mach/machine/rpc.h
  osfmk/mach/machine/sdt_isa.h
  osfmk/mach/machine/syscall_sw.h
  osfmk/mach/machine/thread_state.h
  osfmk/mach/machine/thread_status.h
  osfmk/mach/machine/vm_param.h
  osfmk/machine/atomic.h
  osfmk/machine/commpage.h
  osfmk/machine/cpu_affinity.h
  osfmk/machine/cpu_capabilities.h
  osfmk/machine/cpu_data.h
  osfmk/machine/cpu_number.h
  osfmk/machine/endian.h
  osfmk/machine/io_map_entries.h
  osfmk/machine/lock.h
  osfmk/machine/locks.h
  osfmk/machine/machine_cpu.h
  osfmk/machine/machine_routines.h
  osfmk/machine/machine_rpc.h
  osfmk/machine/machlimits.h
  osfmk/machine/machparam.h
  osfmk/machine/memory_types.h
  osfmk/machine/pal_routines.h
  osfmk/machine/pmap.h
  osfmk/machine/sched_param.h
  osfmk/machine/setjmp.h
  osfmk/machine/simple_lock.h
  osfmk/machine/smp.h
  osfmk/machine/task.h
  osfmk/machine/thread.h
  osfmk/machine/trap.h
  osfmk/machine/vm_tuning.h
]
cc = ENV.fetch('CC', 'clang')
Dir.mktmpdir('machine-routing') do |dir|
  input = File.join(dir, 'routing.c')
  preprocess = lambda do |defines|
    Open3.capture3(cc, '-E', '-P', '-undef', '-x', 'c',
      '-DKERNEL_PRIVATE=1', '-DPRIVATE=1', *defines.map { |d| "-D#{d}=1" }, input)
  end
  headers.each do |header|
    content = File.read(File.join(root, header))
    content = content.gsub(/^\s*#include\s*[<"]([^>"]+)[>"].*$/) { "selected_header \"#{$1}\"" }
    File.write(input, content)
    results = {}
    %w[__aarch64__ __arm64__ __arm__ __x86_64__].each do |arch|
      output, error, status = preprocess.call([arch])
      abort "#{header} with #{arch}: #{error}" unless status.success?
      results[arch] = output.strip
    end
    abort "ARM routing mismatch: #{header}" unless results['__aarch64__'] == results['__arm64__'] &&
      results['__aarch64__'] == results['__arm__'] &&
      results['__aarch64__'].include?('arm/')
    abort "x86 routing mismatch: #{header}" unless results['__x86_64__'].include?('i386/')
    _, _, status = preprocess.call([])
    abort "unsupported architecture accepted: #{header}" if status.success?
  end
  File.write(input, "#include <machine/_limits.h>\n")
  %w[aarch64-linux-gnu arm64-apple-darwin x86_64-linux-gnu].each do |target|
    output, error, status = Open3.capture3(cc, '-target', target, '-fsyntax-only',
      '-x', 'c', '-I', File.join(root, 'bsd'), input)
    abort "real-header smoke failed (#{target}): #{output}#{error}" unless status.success?
  end
end
puts "PASS: #{headers.length} dispatchers route GNU/Apple ARM64, ARM32 and x86_64; unsupported targets rejected"
puts "PASS: real machine/_limits.h compiles for Linux ARM64/x86_64 and Darwin ARM64"

# Check the real syscall headers and conversion declaration for each supported
# architecture's stat64 type. This is compile-time coverage, not stat I/O.
# Usage: ruby darling/tests/stat64-types.rb [path/to/emulation]
require 'tmpdir'
require 'fileutils'
require 'open3'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation', __dir__)
headers = %w[fstat fstat64_extended fstatat lstat lstat64_extended stat stat64_extended]
Dir.mktmpdir('stat64-types') do |dir|
  FileUtils.mkdir_p(File.join(dir, 'darling'))
  File.symlink(File.join(root, 'include'), File.join(dir, 'darling/emulation'))
  input = File.join(dir, 'probe.c')
  File.write(input, <<~C)
    #include <darling/emulation/conversion/stat/common.h>
    #{headers.map { |h| "#include <darling/emulation/xnu_syscall/bsd/impl/stat/#{h}.h>" }.join("\n")}
    #ifdef stat64
    #error The stat64 token must not be redefined
    #endif
    #if defined(__aarch64__) || defined(__arm64__)
    typedef struct stat expected_stat64_t;
    #else
    typedef struct stat64 expected_stat64_t;
    #endif
    long (*check_fstat)(int, expected_stat64_t *) = sys_fstat64;
    long (*check_fstat_ext)(int, expected_stat64_t *, void *, unsigned long *) = sys_fstat64_extended;
    long (*check_fstatat)(int, const char *, expected_stat64_t *, int) = sys_fstatat64;
    long (*check_lstat)(const char *, expected_stat64_t *) = sys_lstat64;
    long (*check_lstat_ext)(const char *, expected_stat64_t *, void *, unsigned long *) = sys_lstat64_extended;
    long (*check_stat)(const char *, expected_stat64_t *) = sys_stat64;
    long (*check_stat_ext)(const char *, expected_stat64_t *, void *, unsigned long *) = sys_stat64_extended;
    void (*check_conversion)(const struct linux_stat *, expected_stat64_t *) = stat_linux_to_bsd64;
  C
  %w[arm64-apple-darwin20 aarch64-linux-gnu x86_64-apple-darwin20 i386-apple-darwin].each do |target|
    output, status = Open3.capture2e(ENV.fetch('CC', 'clang'), '-target', target,
      '-ffreestanding', '-fsyntax-only', '-Wall', '-Wextra',
      '-Werror=incompatible-function-pointer-types', '-Werror=incompatible-pointer-types',
      '-I', dir, input)
    abort "#{target}: #{output}" unless status.success?
    puts "PASS: #{target}: seven syscall declarations and converter agree; no stat64 macro"
  end
end

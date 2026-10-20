# Configure/build native probes from the real emulation CMake preamble.
# Does not configure or run a complete Darling build.
# Usage: ruby darling/tests/product-version.rb [path/to/emulation/CMakeLists.txt]
require 'tmpdir'
require 'open3'

source = ARGV[0] || File.expand_path('../src/libsystem_kernel/emulation/CMakeLists.txt', __dir__)
text = File.read(source)
marker = "# include src/startup for rtsig.h"
abort 'CMake preamble boundary not found' unless text.include?(marker)
preamble = text.split(marker, 2).first

Dir.mktmpdir('product-version') do |dir|
  File.write(File.join(dir, 'CMakeLists.txt'), <<~CMAKE)
    cmake_minimum_required(VERSION 3.16)
    #{preamble}
    add_executable(normal probe.c)
    add_executable(dyld probe.c)
    target_compile_definitions(dyld PRIVATE VARIANT_DYLD)
  CMAKE
  File.write(File.join(dir, 'probe.c'), <<~C)
    #include <stdio.h>
    #include <string.h>
    int main(int argc, char **argv) {
        if (argc != 2 || strcmp(EMULATED_OSPRODUCTVERSION, argv[1]) != 0)
            return 1;
        if (strcmp(EMULATED_RELEASE, "25.0.0") != 0 ||
            strcmp(EMULATED_VERSION, "Darwin Kernel Version 25.0.0") != 0 ||
            strcmp(EMULATED_OSVERSION, "20G1120") != 0 ||
            strcmp(EMULATED_SYSNAME, "Darwin") != 0)
            return 2;
        puts(EMULATED_OSPRODUCTVERSION);
        return 0;
    }
  C
  build = File.join(dir, 'build')
  run = lambda do |*args|
    output, status = Open3.capture2e(*args)
    abort "Command failed: #{args.join(' ')}\n#{output}" unless status.success?
  end
  [
    ['26.0', []],
    ['26.5', ['-DDARLING_EMULATED_OS_PRODUCT_VERSION=26.5']],
    ['26.5', []], # Reconfiguration must retain the cached override.
    ['12.4', ['-DDARLING_EMULATED_OS_PRODUCT_VERSION=12.4']],
    ['26.0', ['-UDARLING_EMULATED_OS_PRODUCT_VERSION']]
  ].each do |expected, flags|
    run.call('cmake', '-S', dir, '-B', build, *flags)
    run.call('cmake', '--build', build, '--parallel', '2')
    %w[normal dyld].each { |variant| run.call(File.join(build, variant), expected) }
    puts "PASS: #{expected} in normal/dyld; other identity definitions unchanged"
  end
end

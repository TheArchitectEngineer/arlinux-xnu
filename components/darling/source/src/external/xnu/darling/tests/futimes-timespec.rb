# Run the real sys_futimes against the host's utimensat on a temporary file.
# Usage: ruby darling/tests/futimes-timespec.rb [path/to/emulation]
require 'tmpdir'
root = File.expand_path(ARGV[0] || '../src/libsystem_kernel/emulation', __dir__)
source = File.read(File.join(root, 'src/xnu_syscall/bsd/impl/time/futimes.c'))
body = source[/^long sys_futimes\(.*?^\}/m]
abort 'sys_futimes not found' unless body
program = <<~C
  #define _GNU_SOURCE
  #include <assert.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <sys/stat.h>
  #include <sys/syscall.h>
  #include <time.h>
  #include <unistd.h>
  struct bsd_timeval { unsigned long tv_sec; int tv_usec; };
  struct linux_timeval { unsigned long tv_sec; long tv_usec; };
  #define LINUX_EINVAL EINVAL
  static int syscalls;
  #define LINUX_SYSCALL(nr, a, b, c, d) (syscalls++, syscall(nr, a, b, c, d) < 0 ? -errno : 0)
  static long errno_linux_to_bsd(long e) { return e; }
  #{body}
  int main(int argc, char **argv) {
      int fd = open(argv[1], O_RDWR | O_CREAT, 0600);
      assert(fd >= 0);
      struct bsd_timeval tv[2] = { { 1000000000, 250000 }, { 1100000000, 999999 } };
      struct stat st;
      assert(sys_futimes(fd, tv) == 0 && fstat(fd, &st) == 0);
      if (st.st_atim.tv_sec != 1000000000 || st.st_atim.tv_nsec != 250000000 ||
          st.st_mtim.tv_sec != 1100000000 || st.st_mtim.tv_nsec != 999999000) {
          printf("FAIL: atime %ld.%09ld mtime %ld.%09ld\\n", (long)st.st_atim.tv_sec,
              st.st_atim.tv_nsec, (long)st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
          return 1;
      }
      time_t before = time(NULL);
      assert(sys_futimes(fd, NULL) == 0 && fstat(fd, &st) == 0);
      assert(st.st_mtim.tv_sec >= before && st.st_mtim.tv_sec <= time(NULL));
      // The range check must reject before any syscall: the host kernel would also say EINVAL on
      // LP64, so only the syscall count shows the check ran (it guards the i386 overflow).
      int before_bad = syscalls;
      struct bsd_timeval bad[2] = { { 1, 1000000 }, { 1, 0 } };
      assert(sys_futimes(fd, bad) == -EINVAL);
      struct bsd_timeval bad2[2] = { { 1, 0 }, { 1, -1 } };
      assert(sys_futimes(fd, bad2) == -EINVAL);
      assert(syscalls == before_bad);
      puts("PASS: microseconds become nanoseconds, NULL means now, bad usec is EINVAL");
      return 0;
  }
C
Dir.mktmpdir('futimes-timespec') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O1', '-g',
      '-Wall', '-fsanitize=address,undefined', input, '-o', output)
  abort 'futimes regression' unless system(output, File.join(dir, 'file'), rlimit_core: 0)
end

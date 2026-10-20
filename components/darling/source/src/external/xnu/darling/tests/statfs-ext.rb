# Native control-flow/data-copy tests for the actual wrapper, with filesystem
# calls mocked. Cross-compilation against Darwin headers is a separate check.
# Usage: ruby darling/tests/statfs-ext.rb [path/to/statfs_ext.c]
require 'tmpdir'

root = File.expand_path('../..', __dir__)
candidate = File.read(File.join(root, 'libsyscall/wrappers/statfs_ext.c'))
source = ARGV[0] ? File.read(ARGV[0]) : candidate
body = source.lines.reject { |line| line.start_with?('#include') }.join
mount = File.read(File.join(root, 'bsd/sys/mount.h'))
statfs_definition = mount[/^#define __DARWIN_STRUCT_STATFS64 \{.*?^\}/m]
abort 'statfs definition not found' unless statfs_definition
attrs = File.read(File.join(root, 'bsd/sys/attr.h'))
names = %w[ATTR_BIT_MAP_COUNT ATTR_CMN_FSID ATTR_CMN_RETURNED_ATTRS
  ATTR_VOL_INFO ATTR_VOL_FSTYPE ATTR_VOL_MOUNTPOINT ATTR_VOL_MOUNTFLAGS
  ATTR_VOL_MOUNTEDDEVICE ATTR_VOL_FSTYPENAME ATTR_VOL_FSSUBTYPE
  ATTR_VOL_MOUNTEXTFLAGS ATTR_VOL_OWNER FSOPT_NOFOLLOW FSOPT_RETURN_REALDEV]
constants = names.map do |name|
  attrs[/^#define #{name}\s+[^\n]+/] || abort("missing #{name}")
end.join("\n")
# Reuse the wrapper's packed response declaration; this tests interpretation,
# not independent conformance of the wire layout to Darwin.
record = candidate[/\tstruct \{.*?\} __attribute__\(\(aligned\(4\), packed\)\) \*attrbuf;/m]
abort 'attribute response declaration not found' unless record
record = record.sub('struct {', 'struct response {').sub(' *attrbuf;', ';')

program = <<~C
  #include <assert.h>
  #include <errno.h>
  #include <stdint.h>
  #include <stddef.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <sys/types.h>
  #define MAXPATHLEN 1024
  #define MFSTYPENAMELEN 16
  #define STATFS_EXT_NOBLOCK 1
  typedef struct { int32_t val[2]; } test_fsid_t;
  #define fsid_t test_fsid_t
  #{statfs_definition}
  struct statfs __DARWIN_STRUCT_STATFS64;
  typedef struct { uint32_t commonattr, volattr, dirattr, fileattr, forkattr; } attribute_set_t;
  typedef struct { int32_t attr_dataoffset; uint32_t attr_length; } attrreference_t;
  struct attrlist { uint16_t bitmapcount, reserved; uint32_t commonattr, volattr, dirattr, fileattr, forkattr; };
  #{constants}
  #{record}
  static int attr_error, default_error, alloc_failure, attr_calls, default_calls, partial;
  static unsigned long seen_options;
  static const char *seen_path;
  static int seen_fd;
  #define strlcpy probe_strlcpy
  static size_t strlcpy(char *dst, const char *src, size_t size) {
      size_t length = strlen(src);
      if (size) { size_t n = length < size-1 ? length : size-1; memcpy(dst, src, n); dst[n] = 0; }
      return length;
  }
  static void *probe_malloc(size_t size) { return alloc_failure ? NULL : malloc(size); }
  static int ordinary(struct statfs *buf) {
      ++default_calls;
      if (default_error) { errno = default_error; return -1; }
      buf->f_blocks = 9876;
      return 0;
  }
  static int statfs(const char *path, struct statfs *buf) { seen_path = path; return ordinary(buf); }
  static int fstatfs(int fd, struct statfs *buf) { seen_fd = fd; return ordinary(buf); }
  static void reference(attrreference_t *ref, char *storage, const char *value) {
      strcpy(storage, value);
      ref->attr_dataoffset = storage - (char *)ref;
      ref->attr_length = strlen(value) + 1;
  }
  static int attributes(struct attrlist *al, void *data, size_t size, unsigned long options) {
      ++attr_calls; seen_options = options;
      assert(al->bitmapcount == ATTR_BIT_MAP_COUNT);
      assert(al->commonattr == (ATTR_CMN_FSID | ATTR_CMN_RETURNED_ATTRS));
      assert(al->volattr == (ATTR_VOL_INFO | ATTR_VOL_FSTYPE | ATTR_VOL_MOUNTPOINT |
          ATTR_VOL_MOUNTFLAGS | ATTR_VOL_MOUNTEDDEVICE | ATTR_VOL_FSTYPENAME |
          ATTR_VOL_FSSUBTYPE | ATTR_VOL_MOUNTEXTFLAGS | ATTR_VOL_OWNER));
      assert(al->dirattr == 0 && al->fileattr == 0 && al->forkattr == 0);
      if (attr_error) { errno = attr_error; return -1; }
      assert(size == sizeof(struct response));
      struct response *r = data;
      r->size = size;
      r->f_attrs.commonattr = partial ? 0 : ATTR_CMN_FSID;
      r->f_attrs.volattr = partial ? ATTR_VOL_OWNER : al->volattr;
      r->f_fsid.val[0] = 123; r->f_fsid.val[1] = 456;
      r->f_owner = 501; r->f_type = 17; r->f_flags = 23;
      r->f_fssubtype = 42; r->f_flags_ext = 57;
      reference(&r->f_mntonname, r->f_mntonname_buf, "/volume");
      reference(&r->f_mntfromname, r->f_mntfromname_buf, "/dev/test");
      reference(&r->f_fstypename, r->f_fstypename_buf, "testfs");
      return 0;
  }
  static int getattrlist(const char *path, struct attrlist *al, void *data, size_t size, unsigned long options) {
      seen_path = path; return attributes(al, data, size, options);
  }
  static int fgetattrlist(int fd, struct attrlist *al, void *data, size_t size, unsigned long options) {
      seen_fd = fd; return attributes(al, data, size, options);
  }
  #define malloc probe_malloc
  #{body}
  #undef malloc
  static void reset(void) {
      attr_error = default_error = alloc_failure = attr_calls = default_calls = partial = 0;
      seen_path = NULL; seen_fd = -1; seen_options = 0; errno = 0;
  }
  static int invoke(int descriptor, struct statfs *out, int flags) {
      return descriptor ? fstatfs_ext(19, out, flags) : statfs_ext("/volume", out, flags);
  }
  static int zero(const void *data, size_t size) {
      const unsigned char *p = data;
      for (size_t i = 0; i < size; ++i) if (p[i]) return 0;
      return 1;
  }
  int main(void) {
      struct statfs out;
      reset();
      assert(fstatfs_ext(-1, &out, 0) == -1 && errno == EBADF);
      assert(fstatfs_ext(19, NULL, 0) == -1 && errno == EFAULT);
      assert(statfs_ext(NULL, &out, 0) == -1 && errno == EFAULT);
      assert(statfs_ext("/volume", NULL, 0) == -1 && errno == EFAULT);
      assert(attr_calls == 0 && default_calls == 0);
      for (int fd = 0; fd < 2; ++fd) {
          reset(); memset(&out, 0xa5, sizeof out);
          assert(invoke(fd, &out, 2) == -1 && errno == EINVAL);
          assert(zero(&out, sizeof out) && attr_calls == 0 && default_calls == 0);
          reset(); assert(invoke(fd, &out, 0) == 0);
          assert(attr_calls == 0 && default_calls == 1 && out.f_blocks == 9876);
          reset(); memset(&out, 0xa5, sizeof out);
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == 0);
          assert(attr_calls == 1 && default_calls == 0);
          assert(seen_options == (FSOPT_RETURN_REALDEV | (fd ? 0 : FSOPT_NOFOLLOW)));
          assert(fd ? seen_fd == 19 : strcmp(seen_path, "/volume") == 0);
          assert(out.f_fsid.val[0] == 123 && out.f_fsid.val[1] == 456);
          assert(out.f_owner == 501 && out.f_type == 17 && out.f_flags == 23);
          assert(out.f_fssubtype == 42 && out.f_flags_ext == 57);
          assert(strcmp(out.f_mntonname, "/volume") == 0 && strcmp(out.f_mntfromname, "/dev/test") == 0);
          assert(strcmp(out.f_fstypename, "testfs") == 0 && out.f_blocks == 0 && out.f_bsize == 0);
          reset(); partial = 1;
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == 0 && out.f_owner == 501);
          out.f_owner = 0; assert(zero(&out, sizeof out));
          reset(); attr_error = EINVAL;
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == 0);
          assert(attr_calls == 1 && default_calls == 1 && out.f_blocks == 9876);
          reset(); attr_error = EINVAL; default_error = EIO;
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == -1 && errno == EIO);
          reset(); attr_error = EACCES;
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == -1 && errno == EACCES);
          assert(attr_calls == 1 && default_calls == 0);
          reset(); alloc_failure = 1;
          assert(invoke(fd, &out, STATFS_EXT_NOBLOCK) == -1 && errno == ENOMEM);
          assert(attr_calls == 0 && default_calls == 0);
      }
      puts("PASS: path/fd defaults, VFS fields, returned masks, fallback and errors");
  }
C
Dir.mktmpdir('statfs-ext') do |dir|
  input = File.join(dir, 'probe.c'); output = File.join(dir, 'probe')
  File.write(input, program)
  abort 'compile failed' unless system(ENV.fetch('CC', 'clang'), '-O2', '-g',
      '-Wall', '-Wextra', '-fsanitize=address,undefined', input, '-o', output)
  abort 'statfs_ext regression' unless system(output, rlimit_core: 0)
end

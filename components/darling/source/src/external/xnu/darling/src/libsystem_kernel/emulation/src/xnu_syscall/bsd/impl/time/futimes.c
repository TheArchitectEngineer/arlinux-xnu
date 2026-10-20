#include <darling/emulation/xnu_syscall/bsd/impl/time/futimes.h>

#include <stddef.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/duct_errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>


long sys_futimes(int fd, struct bsd_timeval* tv)
{
	int ret;
	// utimensat takes nanoseconds; a NULL tv means "now"
	struct { long tv_sec; long tv_nsec; } times[2];

	if (tv) {
		for (int i = 0; i < 2; ++i) {
			// On LP64 the kernel would reject an out-of-range nsec itself; this check is for i386,
			// where (long)tv_usec * 1000 overflows for |usec| above 2147483.
			if (tv[i].tv_usec < 0 || tv[i].tv_usec >= 1000000)
				return errno_linux_to_bsd(-LINUX_EINVAL);
			times[i].tv_sec = tv[i].tv_sec;
			times[i].tv_nsec = (long)tv[i].tv_usec * 1000;
		}
	}

	ret = LINUX_SYSCALL(__NR_utimensat, fd, NULL, tv ? times : NULL, 0);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
}

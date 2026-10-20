#include <darling/emulation/xnu_syscall/bsd/impl/time/gettimeofday.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

extern uint64_t mach_absolute_time(void);

long sys_gettimeofday(struct bsd_timeval* tv, struct timezone* tz, uint64_t* mach_time)
{
	int ret;
	struct linux_timeval ltv;

	ret = LINUX_SYSCALL(__NR_gettimeofday, &ltv, tz);
	if (ret < 0)
	{
		ret = errno_linux_to_bsd(ret);
	}
	else
	{
		if (tv)
		{
			tv->tv_sec = ltv.tv_sec;
			tv->tv_usec = ltv.tv_usec;
		}
		if (mach_time)
		{
			*mach_time = mach_absolute_time();
		}
	}

	return ret;
}

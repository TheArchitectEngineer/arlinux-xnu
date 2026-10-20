#include <darling/emulation/xnu_syscall/bsd/impl/unistd/exit.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

long sys_exit(int status)
{
	int ret;

	// Darwin's exit syscall is also used by _exit: terminate the process without
	// running the host C library's atexit handlers or destructors.
	ret = LINUX_SYSCALL1(__NR_exit_group, status);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
}

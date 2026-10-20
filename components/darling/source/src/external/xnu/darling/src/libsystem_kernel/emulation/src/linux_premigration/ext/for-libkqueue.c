#include <darling/emulation/linux_premigration/ext/for-libkqueue.h>

#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/network/duct.h>

#include <darlingserver/rpc.h>

/* Raw Linux socket syscalls for dserver kqchan fds (bypass darwinkernel fd table).
 * Syscall numbers are architecture-specific (aarch64: recvfrom=227, sendto=228,
 * recvmsg=272, sendmsg=271). The __NR_* macros from linux-generic.h carry the
 * i386 numbers (207/206/212/211) so we hard-code the aarch64 values here. */
/* aarch64 uses the asm-generic numbers: recvfrom=207, sendto=206,
 * recvmsg=212, sendmsg=211 (verified on-device). */
#define _NR_RECVFROM_RAW 207
#define _NR_SENDTO_RAW   206
#define _NR_RECVMSG_RAW  212
#define _NR_SENDMSG_RAW  211

ssize_t _recv_raw_4libkqueue(int fd, void* buf, size_t len) {
	/* recvfrom(fd, buf, len, flags=0, from=NULL, fromlen=NULL): with from==NULL
	 * the kernel ignores fromlen, so it must be NULL (not a stack address). */
	long ret = LINUX_SYSCALL(_NR_RECVFROM_RAW, fd, buf, (unsigned long)len,
			0, (void*)0, (int*)0);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);
	return (ssize_t) ret;
}

ssize_t _recvmsg_raw_4libkqueue(int fd, struct msghdr* msg, int flags) {
	long ret = LINUX_SYSCALL3(_NR_RECVMSG_RAW, fd, msg, flags);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);
	return (ssize_t) ret;
}

ssize_t _send_raw_4libkqueue(int fd, const void* buf, size_t len) {
	long ret = LINUX_SYSCALL(_NR_SENDTO_RAW, fd, buf, (unsigned long)len,
			0, (void*)0, (unsigned long)0);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);
	return (ssize_t) ret;
}

int _dserver_rpc_kqchan_mach_port_open_4libkqueue(uint32_t port_name, void* receive_buffer, uint64_t receive_buffer_size, uint64_t saved_filter_flags, int* out_socket) {
	return dserver_rpc_kqchan_mach_port_open(port_name, receive_buffer, receive_buffer_size, saved_filter_flags, out_socket);
};

int _dserver_rpc_kqchan_proc_open_4libkqueue(int32_t pid, uint32_t flags, int* out_socket) {
	return dserver_rpc_kqchan_proc_open(pid, flags, out_socket);
};

int _dup_4libkqueue(int fd) {
	int ret;

	ret = LINUX_SYSCALL1(__NR_dup, fd);
	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
};

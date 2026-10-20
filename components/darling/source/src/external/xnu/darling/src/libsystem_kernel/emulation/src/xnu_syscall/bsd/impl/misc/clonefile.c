#include <darling/emulation/xnu_syscall/bsd/impl/misc/clonefile.h>

#include <sys/errno.h>

#include <darling/emulation/common/base.h>
#include <darling/emulation/conversion/common_at.h>
#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/conversion/stat/common.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/linux_premigration/vchroot_expand.h>

extern char* strcpy(char* dst, const char* src);
extern unsigned long strlen(const char* str);
extern void* malloc(unsigned long size);
extern void free(void* ptr);

/* Linux's FS_IOC_FICLONE: _IOW(0x94, 9, int). */
#define LINUX_FICLONE 0x40049409
#define CLONE_NOFOLLOW 0x0001
#define CLONE_NOOWNERCOPY 0x0002
#define LINUX_S_IFMT 0170000
#define LINUX_S_IFREG 0100000

struct linux_clone_timespec {
	long seconds;
	long nanoseconds;
};

static long clone_xattrs(int source, int target) {
	long size = LINUX_SYSCALL(__NR_flistxattr, source, 0, 0);
	if (size <= 0)
		return size;

	char* names = malloc(size);
	if (!names)
		return -ENOMEM;
	long count = LINUX_SYSCALL(__NR_flistxattr, source, names, size);
	if (count < 0) {
		free(names);
		return count;
	}

	long result = 0;
	for (long offset = 0; offset < count;) {
		const char* name = names + offset;
		unsigned long length = strlen(name) + 1;
		if (length > (unsigned long)(count - offset)) {
			result = -EIO;
			break;
		}
		offset += length;
		long value_size = LINUX_SYSCALL(__NR_fgetxattr, source, name, 0, 0);
		if (value_size < 0) {
			result = value_size;
			break;
		}
		void* value = malloc(value_size ? value_size : 1);
		if (!value) {
			result = -ENOMEM;
			break;
		}
		result = LINUX_SYSCALL(__NR_fgetxattr, source, name, value, value_size);
		if (result >= 0)
			result = LINUX_SYSCALL(__NR_fsetxattr, target, name, value, result, 0);
		free(value);
		if (result < 0)
			break;
	}
	free(names);
	return result;
}

static long clone_regular_file(int source, int dest_fd, const char* dest_path, uint32_t flags) {
	struct linux_stat stat;
	struct vchroot_expand_args dest;
	long result, target;

	result = LINUX_SYSCALL(__NR_fstat, source, &stat);
	if (result < 0)
		return errno_linux_to_bsd(result);
	if ((stat.st_mode & LINUX_S_IFMT) != LINUX_S_IFREG)
		return -ENOTSUP;

	dest.flags = 0;
	dest.dfd = atfd(dest_fd);
	if (strlen(dest_path) >= sizeof(dest.path))
		return -ENAMETOOLONG;
	strcpy(dest.path, dest_path);
	result = vchroot_expand(&dest);
	if (result < 0)
		return errno_linux_to_bsd(result);

	/* O_EXCL implements clonefile's requirement that the target not exist. */
	target = LINUX_SYSCALL(__NR_openat, dest.dfd, dest.path,
		LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_CLOEXEC,
		stat.st_mode & 0777);
	if (target < 0)
		return errno_linux_to_bsd(target);

	result = LINUX_SYSCALL(__NR_ioctl, target, LINUX_FICLONE, source);
	if (result >= 0 && !(flags & CLONE_NOOWNERCOPY)) {
		/* Unprivileged callers get the ownership of a newly created file. */
		long uid = LINUX_SYSCALL(__NR_geteuid);
		if (uid == 0)
			result = LINUX_SYSCALL(__NR_fchown, target, stat.st_uid, stat.st_gid);
	}
	if (result >= 0)
		result = clone_xattrs(source, target);
	if (result >= 0)
		result = LINUX_SYSCALL(__NR_fchmod, target, stat.st_mode & 01777);
	if (result >= 0) {
		struct linux_clone_timespec times[2] = {
			{ stat.st_atime, stat.st_atime_nsec },
			{ stat.st_mtime, stat.st_mtime_nsec },
		};
		result = LINUX_SYSCALL(__NR_utimensat, target, "", times, LINUX_AT_EMPTY_PATH);
	}
	LINUX_SYSCALL(__NR_close, target);
	if (result < 0) {
		LINUX_SYSCALL(__NR_unlinkat, dest.dfd, dest.path, 0);
		return errno_linux_to_bsd(result);
	}
	return 0;
}

long sys_clonefileat(int src_fd, const char* src_path, int dest_fd, const char* dest_path, uint32_t flags) {
	struct vchroot_expand_args src;
	long source, result;

	if (!src_path || !dest_path)
		return -EFAULT;
	if (flags & ~(CLONE_NOFOLLOW | CLONE_NOOWNERCOPY))
		return -EINVAL;
	src.flags = (flags & CLONE_NOFOLLOW) ? 0 : VCHROOT_FOLLOW;
	src.dfd = atfd(src_fd);
	if (strlen(src_path) >= sizeof(src.path))
		return -ENAMETOOLONG;
	strcpy(src.path, src_path);
	result = vchroot_expand(&src);
	if (result < 0)
		return errno_linux_to_bsd(result);
	source = LINUX_SYSCALL(__NR_openat, src.dfd, src.path,
		LINUX_O_RDONLY | LINUX_O_CLOEXEC |
		((flags & CLONE_NOFOLLOW) ? LINUX_O_NOFOLLOW : 0), 0);
	if (source < 0)
		return errno_linux_to_bsd(source);
	result = clone_regular_file(source, dest_fd, dest_path, flags);
	LINUX_SYSCALL(__NR_close, source);
	return result;
}

long sys_fclonefileat(int src_fd, int dest_fd, const char* dest_path, uint32_t flags) {
	if (!dest_path)
		return -EFAULT;
	if (flags & ~(CLONE_NOFOLLOW | CLONE_NOOWNERCOPY))
		return -EINVAL;
	return clone_regular_file(src_fd, dest_fd, dest_path, flags);
}

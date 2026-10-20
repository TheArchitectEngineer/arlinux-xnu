#include <darling/emulation/linux_premigration/ext/file_handle.h>

#include <sys/errno.h>
#include <libsimple/lock.h>

#include <darling/emulation/conversion/errno.h>
#include <darling/emulation/conversion/fcntl/open.h>
#include <darling/emulation/common/base.h>
#include <darling/emulation/linux_premigration/vchroot_expand.h>
#include <darling/emulation/common/bsdthread/per_thread_wd.h>
#include <darling/emulation/xnu_syscall/bsd/impl/fcntl/open.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/close.h>
#include <darling/emulation/other/mach/lkm.h>
#include <darling/emulation/conversion/duct_errno.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/access.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>

extern void free(void* ptr);

struct SavedRef
{
	char* path;
	int gen;
};

#define MOUNT_ID_SAVED (-100)
// Every FSRef made in this process takes a slot (see sys_name_to_handle); the oldest is reused
// once the table is full, and references to a reused slot fail the generation check.
static struct SavedRef g_savedRefs[1024];
static int g_nextSavedRef = 0, g_nextGen = 0;
static libsimple_lock_t g_savedRefLock = LIBSIMPLE_LOCK_INITIALIZER;


VISIBLE
int sys_name_to_handle(const char* name, RefData* ref, int follow)
{
	int ret;

	struct vchroot_expand_args vc;
	vc.flags = follow ? VCHROOT_FOLLOW : 0;
	vc.dfd = get_perthread_wd();

	if (strlen(name) >= sizeof(vc.path))
		return -ENAMETOOLONG;

	strcpy(vc.path, name);
	ret = vchroot_expand(&vc);
	if (ret < 0)
		return errno_linux_to_bsd(ret);

	// A kernel file handle (name_to_handle_at) can't be turned back into a path without the Darling
	// kernel module, which no longer exists, and overlayfs often can't produce one anyway. So every
	// reference remembers its path, after checking that it exists (without following a leaf
	// symlink unless asked to; the stat buffer is larger than any Linux struct stat).
	char st_buf[256];
#if defined(__NR_newfstatat)
	ret = LINUX_SYSCALL(__NR_newfstatat, LINUX_AT_FDCWD, vc.path, st_buf, follow ? 0 : LINUX_AT_SYMLINK_NOFOLLOW);
#else
	ret = LINUX_SYSCALL(__NR_fstatat64, LINUX_AT_FDCWD, vc.path, st_buf, follow ? 0 : LINUX_AT_SYMLINK_NOFOLLOW);
#endif

	if (ret == 0)
	{
		char* saved = strdup(name);
		if (saved == NULL)
			return -ENOMEM;

		libsimple_lock_lock(&g_savedRefLock);

		if (g_savedRefs[g_nextSavedRef].path)
			free(g_savedRefs[g_nextSavedRef].path);

		ref->gen = g_nextGen++;
		g_savedRefs[g_nextSavedRef].path = saved;
		g_savedRefs[g_nextSavedRef].gen = ref->gen;
		ref->mount_id = MOUNT_ID_SAVED;
		ref->index = g_nextSavedRef;

		g_nextSavedRef = (g_nextSavedRef + 1) % (sizeof(g_savedRefs) / sizeof(g_savedRefs[0]));

		libsimple_lock_unlock(&g_savedRefLock);

		ret = 0;
	}

	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
}

VISIBLE
int sys_handle_to_name(RefData* ref, char name[4096])
{
	if (ref->mount_id == MOUNT_ID_SAVED)
	{
		int ret = -ENOENT;
		libsimple_lock_lock(&g_savedRefLock);

		// An FSRef is caller memory: check the index before using it.
		if (ref->index >= 0 && ref->index < (int)(sizeof(g_savedRefs) / sizeof(g_savedRefs[0]))
			&& g_savedRefs[ref->index].path != NULL && g_savedRefs[ref->index].gen == ref->gen)
		{
			strlcpy(name, g_savedRefs[ref->index].path, 4096);
			ret = sys_access(name, 0);
		}

		libsimple_lock_unlock(&g_savedRefLock);
		return ret;
	}

	// sys_name_to_handle() only hands out saved references. Turning a kernel file handle back into a
	// path needed the Darling kernel module's NR_handle_to_path call, which no longer exists (calling
	// it trapped with "Something called the old LKM API"), so any other reference can't be resolved.
	return -ENOENT;
}

// Requires CAP_DAC_READ_SEARCH
VISIBLE
int sys_open_by_handle(RefData* ref, int flags)
{
	int linux_flags = oflags_bsd_to_linux(flags);

	if (sizeof(void*) == 4)
		linux_flags |= LINUX_O_LARGEFILE;

	// Now we need to find out the path of ref->mount_id
	struct simple_readline_buf rbuf;
	char line[1024];

	int fd_m = sys_open("/proc/self/mountinfo", BSD_O_RDONLY, 0);
	if (fd_m < 0)
		return fd_m;
	const char* mount_path = NULL;
	
	__simple_readline_init(&rbuf);

	while (__simple_readline(fd_m, &rbuf, line, sizeof(line)))
	{
		char *p, *saveptr;

		p = strtok_r(line, " ", &saveptr);
		if (p == NULL)
			continue;

		if (__simple_atoi(p, NULL) != ref->mount_id)
			continue;

		for (int i = 0; i < 4; i++)
		{
			p = strtok_r(NULL, " ", &saveptr);
			if (p == NULL)
			{
				close_internal(fd_m);
				return -ENOENT;
			}
		}

		mount_path = p;

		break;
	}

	close_internal(fd_m);

	if (mount_path == NULL)
		return -ENOENT;
	
	// We have the path of the mount, lets get a file descriptor
	fd_m = sys_open(mount_path, BSD_O_RDONLY, 0);
	if (fd_m < 0)
		return fd_m;
	
	// And now, finally, open the file by handle relative to the mount
	int ret = LINUX_SYSCALL(__NR_open_by_handle_at, fd_m, &ref->fh, linux_flags);

	close_internal(fd_m);

	if (ret < 0)
		ret = errno_linux_to_bsd(ret);

	return ret;
}

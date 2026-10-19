/* ARM64 Darwin wire ABI used by the native adapter. These are guest values,
 * never host Linux errno/syscall definitions. Sources:
 * XNU bsd/kern/syscalls.master, bsd/sys/{errno,fcntl,mman,stat,ulock}.h;
 * osfmk/kern/syscall_sw.c, mach/{kern_return,message}.h and mach/{mach_host,task}.defs,
 * arm/cpu_capabilities.h; libpthread-330.250.2 kern/kern_internal.h.
 * Repository pins/reference commits are recorded in docs/EXPERIMENT.md.
 * MIG IDs below describe the observed legacy iOS ABI, not a kernel version
 * detection table. Add an interface name and source when extending this file.
 */
#pragma once
#include <stdint.h>

enum darwin_syscall {
    D_SYS_EXIT = 1,
    D_SYS_FORK = 2,
    D_SYS_READ = 3,
    D_SYS_WRITE = 4,
    D_SYS_OPEN = 5,
    D_SYS_ACCESS = 33, D_SYS_READ_NOCANCEL = 396, D_SYS_WRITE_NOCANCEL = 397,
    D_SYS_PROC_INFO = 336, D_SYS_SYSCTLBYNAME = 274,
    D_SYS_CLOSE = 6,
    D_SYS_WAIT4 = 7,
    D_SYS_GETPID = 20,
    D_SYS_GETUID = 24, D_SYS_GETEUID = 25, D_SYS_GETGID = 47, D_SYS_GETEGID = 43,
    D_SYS_GETTIMEOFDAY = 116,
    D_SYS_MUNMAP = 73,
    D_SYS_MPROTECT = 74,
    D_SYS_PREAD = 153,
    D_SYS_MMAP = 197,
    D_SYS_LSEEK = 199,
    D_SYS_SHARED_REGION_CHECK = 294,
    D_SYS_SEMWAIT_SIGNAL = 334,
    D_SYS_PSYNCH_MUTEXWAIT = 301, D_SYS_PSYNCH_MUTEXDROP = 302,
    D_SYS_STAT64 = 338,
    D_SYS_FSTAT64 = 339,
    D_SYS_LSTAT64 = 340,
    D_SYS_BSDTHREAD_CREATE = 360,
    D_SYS_BSDTHREAD_TERMINATE = 361,
    D_SYS_BSDTHREAD_CTL = 364,
    D_SYS_KQUEUE = 362,
    D_SYS_KEVENT_QOS = 374,
    D_SYS_WORKQ_OPEN = 367,
    D_SYS_WORKQ_KERNRETURN = 368,
    D_SYS_BSDTHREAD_REGISTER = 366,
    D_SYS_THREAD_SELFID = 372,
    D_SYS_MAC_SYSCALL = 381,
    D_SYS_OPEN_NOCANCEL = 398,
    D_SYS_CLOSE_NOCANCEL = 399,
    D_SYS_PREAD_NOCANCEL = 414,
    D_SYS_SEMWAIT_SIGNAL_NOCANCEL = 423,
    D_SYS_GETENTROPY = 500,
    D_SYS_ULOCK_WAIT = 515,
    D_SYS_ULOCK_WAKE = 516,
    D_SYS_ABORT_WITH_PAYLOAD = 520,
    D_SYS_TERMINATE_WITH_PAYLOAD = 521,
    D_SYS_PLATFORM = 0x80000000U
};
/* x16 holds the NEGATIVE of these Mach trap numbers. */
enum darwin_mach_trap {
    D_TRAP_ABSOLUTE_TIME = 3,
    D_TRAP_CONTINUOUS_TIME = 4,
    D_TRAP_VM_ALLOCATE = 10,
    D_TRAP_VM_DEALLOCATE = 12,
    D_TRAP_VM_PROTECT = 14,
    D_TRAP_VM_MAP = 15,
    D_TRAP_PORT_CONSTRUCT = 24,
    D_TRAP_PORT_DEALLOCATE = 18,
    D_TRAP_REPLY_PORT = 26,
    D_TRAP_THREAD_SELF = 27,
    D_TRAP_TASK_SELF = 28,
    D_TRAP_HOST_SELF = 29,
    D_TRAP_MSG = 31,
    D_TRAP_MSG_OVERWRITE = 32,
    D_TRAP_SEMAPHORE_SIGNAL = 33,
    D_TRAP_SEMAPHORE_WAIT = 36,
    D_TRAP_TIMEBASE_INFO = 89,
    D_TRAP_HOST_CREATE_VOUCHER = 70,
};
enum darwin_errno {
    D_EPERM = 1,
    D_ENOENT = 2,
    D_ESRCH = 3,
    D_EINTR = 4,
    D_EIO = 5,
    D_ENXIO = 6,
    D_E2BIG = 7,
    D_ENOEXEC = 8,
    D_EBADF = 9,
    D_ECHILD = 10,
    D_ENOMEM = 12,
    D_EACCES = 13,
    D_EFAULT = 14,
    D_EBUSY = 16,
    D_EEXIST = 17,
    D_ENODEV = 19,
    D_ENOTDIR = 20,
    D_EISDIR = 21,
    D_EINVAL = 22,
    D_ENFILE = 23,
    D_EMFILE = 24,
    D_EFBIG = 27,
    D_ENOSPC = 28,
    D_ESPIPE = 29,
    D_EROFS = 30,
    D_EMLINK = 31,
    D_EPIPE = 32,
    D_EDOM = 33,
    D_ERANGE = 34,
    D_EAGAIN = 35,
    D_ENOTSUP = 45,
    D_ETIMEDOUT = 60,
    D_ENAMETOOLONG = 63,
    D_ENOSYS = 78,
    D_EOVERFLOW = 84,
};
enum darwin_kern_return {
    D_KERN_SUCCESS = 0, D_KERN_INVALID_ADDRESS = 1,
    D_KERN_RESOURCE_SHORTAGE = 3, D_KERN_INVALID_ARGUMENT = 4,
    D_KERN_FAILURE = 5, D_KERN_INVALID_NAME = 15, D_KERN_INVALID_TASK = 16,
    D_KERN_NOT_SUPPORTED = 46
};
enum darwin_device_layout {
    D_DEV_MAJOR_SHIFT = 24, D_DEV_MAJOR_MAX = 255, D_DEV_MINOR_MAX = 0xffffff
};
enum darwin_open_flags {
    D_O_NOFOLLOW = 0x100, D_O_DIRECTORY = 0x100000, D_O_CLOEXEC = 0x1000000
};
enum darwin_vm_flags {
    D_VM_FLAGS_ANYWHERE = 1, D_VM_FLAGS_TAG_MASK = 0xff000000U,
    D_MAP_PRIVATE = 2, D_MAP_ANON = 0x1000
};
enum darwin_platform_operation { D_PLATFORM_SET_TSD = 2, D_PLATFORM_GET_TSD = 3 };
enum darwin_ulock_operation {
    D_UL_COMPARE_AND_WAIT = 1, D_UL_UNFAIR_LOCK = 2,
    D_UL_WAKE_ALL = 0x100, D_UL_WAKE_ALLOW_NON_OWNER = 0x400,
    D_UL_WAIT_WORKQ_DATA_CONTENTION = 0x10000, D_UL_WAIT_CANCEL_POINT = 0x20000,
    D_UL_NO_ERRNO = 0x1000000, D_UL_OPERATION_MASK = 0xff
};
enum darwin_pthread_start {
    D_PTHREAD_START_CUSTOM = 0x01000000, D_PTHREAD_START_QOS = 0x08000000,
    D_PTHREAD_START_TSD_SET = 0x10000000, D_PTHREAD_PRIORITY_MASK = 0x00ffffff,
    D_PTHREAD_RELATIVE_PRIORITY_MASK = 255, D_PTHREAD_QOS_SHIFT = 8, D_PTHREAD_QOS_MASK = 0xffff,
    D_PTHREAD_DEFAULT_QOS_BITS = 8, D_PTHREAD_ZERO_RELATIVE_PRIORITY = 255
};
enum darwin_mig_id {
    D_MIG_HOST_INFO = 200, D_MIG_HOST_GET_CLOCK = 206, D_MIG_CLOCK_GET_TIME = 1000,
    D_MIG_TASK_INFO = 3405, D_MIG_TASK_GET_SPECIAL_PORT = 3409, D_MIG_SEMAPHORE_CREATE = 3418,
    D_MIG_REPLY_ID_OFFSET = 100, D_MIG_BAD_ID = -303
};
enum darwin_mach_message {
    D_MACH_PORT_NULL = 0, D_MACH_TYPE_MOVE_SEND = 17, D_MACH_TYPE_MOVE_SEND_ONCE = 18,
    D_MACH_TYPE_COPY_SEND = 19, D_MACH_TYPE_MAKE_SEND_ONCE = 21,
    D_MACH_DESCRIPTOR_DISPOSITION_SHIFT = 16,
    D_MACH_MSG_COMPLEX = 0x80000000U, D_MACH_MSG_LOCAL_BITS_SHIFT = 8,
    D_MACH_SEND_MSG = 1, D_MACH_RCV_MSG = 2, D_MACH_RCV_LARGE = 4,
    D_MACH_SEND_TIMEOUT = 0x10, D_MACH_RCV_TIMEOUT = 0x100,
    D_MACH_SEND_INVALID_DATA = 0x10000002, D_MACH_SEND_INVALID_DEST = 0x10000003,
    D_MACH_SEND_TIMED_OUT = 0x10000004, D_MACH_SEND_INVALID_REPLY = 0x10000009,
    D_MACH_SEND_INVALID_HEADER = 0x10000010, D_MACH_SEND_INVALID_OPTIONS = 0x10000013,
    D_MACH_RCV_INVALID_NAME = 0x10004002, D_MACH_RCV_TIMED_OUT = 0x10004003,
    D_MACH_RCV_TOO_LARGE = 0x10004004, D_MACH_RCV_INTERRUPTED = 0x10004005,
    D_MACH_RCV_INVALID_DATA = 0x10004008
};
enum darwin_service_abi {
    D_HOST_PRIORITY_INFO = 5, D_HOST_PRIORITY_INFO_COUNT = 8,
    D_TASK_AUDIT_TOKEN = 15, D_AUDIT_TOKEN_WORDS = 8,
    D_TASK_BOOTSTRAP_PORT = 4, D_TASK_DEBUG_CONTROL_PORT = 10,
    D_CLOCK_SYSTEM = 0, D_CLOCK_CALENDAR = 1,
    D_AMFI_DYLD_POLICY = 90,
    D_AMFI_ALLOW_AT_PATHS = 1, D_AMFI_ALLOW_PATH_ENV = 2, D_AMFI_ALLOW_CUSTOM_CACHE = 4,
    D_AMFI_ALLOW_FALLBACK_PATHS = 8, D_AMFI_ALLOW_PRINT_ENV = 16, D_AMFI_ALLOW_FAILED_INSERT = 32
};
enum darwin_commpage_offset {
    D_COMM_VERSION = 0x1e, D_COMM_CPU_CAPABILITIES = 0x20,
    D_COMM_USER_PAGE_SHIFT_32 = 0x24, D_COMM_USER_PAGE_SHIFT_64 = 0x25,
    D_COMM_CACHE_LINE_SIZE = 0x26, D_COMM_ACTIVE_CPUS = 0x34,
    D_COMM_PHYSICAL_CPUS = 0x35, D_COMM_LOGICAL_CPUS = 0x36,
    D_COMM_KERNEL_PAGE_SHIFT = 0x37, D_COMM_MEMORY_SIZE = 0x38,
    D_COMM_CPU_COUNT_SHIFT = 16
};
#define D_ARM64_INSN_MOVZ_X8 0xd2800008U
#define D_ARM64_INSN_SVC_0 0xd4000001U
#define D_ARM64_MOVZ_IMMEDIATE_SHIFT 5
#define D_ARM64_SYSCALL_REGISTER 16
#define D_ARM64_CARRY (1UL << 29)

struct darwin_timespec { int64_t seconds,nanoseconds; };
struct darwin_stat64 {
    int32_t device; uint16_t mode,links; uint64_t inode;
    uint32_t uid,gid; int32_t rdevice,padding;
    struct darwin_timespec access,modification,change,birth;
    int64_t size,blocks; int32_t blocksize; uint32_t flags,generation;
    int32_t spare; int64_t qspare[2];
};
struct darwin_pthread_registration {
    uint64_t version,dispatch_queue_offset,main_qos;
    uint32_t tsd_offset,return_to_kernel_offset,mach_thread_self_offset;
    uint64_t stack_addr_hint;
    uint32_t mutex_default_policy;
} __attribute__((packed));
_Static_assert(sizeof(struct darwin_stat64)==144,"Darwin ARM64 stat64 wire layout");
_Static_assert(sizeof(struct darwin_pthread_registration)==48,"Darwin pthread registration wire layout");

/* libpthread-330 kern/kern_internal.h and XNU bsd/pthread/workqueue_syscalls.h.
 * Basic KEVENT callbacks use Linux readiness; WORKLOOP remains omitted. */
enum darwin_workqueue_abi {
    D_PTHREAD_FEATURE_DISPATCH = 0x01, D_PTHREAD_FEATURE_FINEPRIO = 0x02,
    D_PTHREAD_FEATURE_CTL = 0x04, D_PTHREAD_FEATURE_SETSELF = 0x08,
    D_PTHREAD_FEATURE_MAINTENANCE = 0x10, D_PTHREAD_FEATURE_KEVENT = 0x40, D_PTHREAD_FEATURE_DEFAULT = 0x40000000,
    D_WQ_RETURN = 0x004, D_WQ_KEVENT_RETURN = 0x040, D_WQ_NEWSPISUPP = 0x010, D_WQ_REQTHREADS = 0x020,
    D_WQ_EVENT_MANAGER_PRIORITY = 0x080, D_WQ_SHOULD_NARROW = 0x200,
    D_WQ_PRIO_QOS = 0x4000, D_WQ_OVERCOMMIT = 0x10000,
    D_WQ_NEWSPI = 0x40000, D_WQ_KEVENT = 0x80000, D_WQ_EVENT_MANAGER = 0x100000,
    D_WQ_TSD_SET = 0x200000,
    D_PRIORITY_OVERCOMMIT = 0x80000000U,
    D_CTL_SET_QOS = 0x10, D_CTL_GET_QOS = 0x20, D_CTL_SET_SELF = 0x100,
    D_CTL_MAX_PARALLELISM = 0x800,
    D_SET_SELF_QOS = 1, D_SET_SELF_VOUCHER = 2,
    D_SET_SELF_FIXED = 4, D_SET_SELF_TIMESHARE = 8
};

/* XNU bsd/sys/event.h, ARM64 kevent_qos wire layout. */
enum darwin_kevent_abi {
    D_EVFILT_READ=-1, D_EVFILT_WRITE=-2, D_EVFILT_TIMER=-7, D_EVFILT_USER=-10,
    D_EV_ADD=1, D_EV_DELETE=2, D_EV_ENABLE=4, D_EV_DISABLE=8,
    D_EV_ONESHOT=0x10, D_EV_CLEAR=0x20, D_EV_RECEIPT=0x40, D_EV_DISPATCH=0x80,
    D_EV_UDATA_SPECIFIC=0x100, D_EV_ERROR=0x4000, D_EV_EOF=0x8000,
    D_KEVENT_IMMEDIATE=1, D_KEVENT_ERROR_EVENTS=2,
    D_KEVENT_STACK_DATA=8, D_KEVENT_WORKQ=0x20,
    D_NOTE_TRIGGER=0x01000000, D_NOTE_FFAND=0x40000000,
    D_NOTE_FFOR=0x80000000U, D_NOTE_FFCOPY=0xc0000000U,
    D_NOTE_FFCTRLMASK=0xc0000000U, D_NOTE_FFLAGSMASK=0x00ffffff,
    D_NOTE_SECONDS=1, D_NOTE_USECONDS=2, D_NOTE_NSECONDS=4, D_NOTE_ABSOLUTE=8
};
struct darwin_kevent_qos {
    uint64_t ident; int16_t filter; uint16_t flags; int32_t qos;
    uint64_t udata; uint32_t fflags,xflags; int64_t data; uint64_t ext[4];
};
_Static_assert(sizeof(struct darwin_kevent_qos)==72,"ARM64 kevent_qos wire layout");

/* libpthread-330 internal.h mutex options and XNU bank/bank_types.h. */
enum darwin_mutex_voucher_abi {
    D_MUTEX_POLICY_SHIFT=6, D_MUTEX_POLICY_MASK=7, D_MUTEX_FIRSTFIT=2,
    D_MUTEX_SHARED=0x10, D_VOUCHER_KEY_BANK=3, D_VOUCHER_BANK_CREATE=610
};
struct darwin_voucher_recipe { uint32_t key,command,previous_voucher,content_size; };
struct darwin_timeval { int64_t seconds; int32_t microseconds,padding; };

/* XNU bsd/sys/proc_info.h; proc_info syscall's current-process PIDINFO flavor. */
enum darwin_process_abi { D_PROC_CALL_PIDINFO=2, D_PROC_PIDUNIQIDENTIFIERINFO=17 };
struct darwin_proc_unique {
    uint8_t uuid[16]; uint64_t uniqueid,parent_uniqueid;
    int32_t idversion; uint32_t reserved2; uint64_t reserved3,reserved4;
};
_Static_assert(sizeof(struct darwin_proc_unique)==56,"Darwin process unique identity wire layout");

/* Libc-1272 libdarwin/variant.c cached-status wire encoding. */
#define D_OS_VARIANT_INITIAL_BITS UINT64_C(0x70000000f0000000)
enum darwin_variant_status {
    D_OS_VARIANT_STATUS_NO=2, D_OS_VARIANT_STATUS_WIDTH=2,
    D_OS_VARIANT_STATUS_FIELDS=6
};

/* XNU mach/message.h audit_token_t (bsd/security/audit identity ordering). */
struct darwin_audit_token {
    uint32_t audit_uid,euid,egid,ruid,rgid,pid,session,idversion;
};
_Static_assert(sizeof(struct darwin_audit_token)==32,"Darwin audit token wire layout");

/* XNU mach/port.h, LP64 mach_port_construct options. */
enum darwin_port_option_flags {
    D_MPO_CONTEXT_AS_GUARD=1, D_MPO_QLIMIT=2,
    D_MPO_IMPORTANCE_RECEIVER=8, D_MPO_INSERT_SEND_RIGHT=0x10,
    D_MPO_STRICT=0x20, D_PORT_QLIMIT_DEFAULT=5, D_PORT_QLIMIT_MAX=1024
};
struct darwin_port_options { uint32_t flags,queue_limit; uint64_t reserved[2]; };
_Static_assert(sizeof(struct darwin_port_options)==24,"Darwin port options wire layout");

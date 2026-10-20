#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_hw.h>

#include <mach/host_info.h>
#include <mach/machine.h>
#include <mach/mach_init.h>
#include <mach/vm_page_size.h>
#include <sys/errno.h>

#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_kern.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/sysctl_proc.h>
#include <darling/emulation/linux_premigration/ext/sys/utsname.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/common/string.h>
#include <darling/emulation/xnu_syscall/bsd/helper/misc/readline.h>
#include <darling/emulation/xnu_syscall/bsd/impl/fcntl/open.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/close.h>
#include <darling/emulation/conversion/fcntl/open.h>

extern kern_return_t mach_port_deallocate(ipc_space_t task, mach_port_name_t name);
extern kern_return_t host_info(mach_port_name_t host, int itype, void* hinfo, mach_msg_type_number_t* count);

/* Darling specific */
enum {
	_HW_PHYSICAL_CPU = 1000,
	_HW_PHYSICAL_CPU_MAX,
	_HW_LOGICAL_CPU,
	_HW_LOGICAL_CPU_MAX,
	_HW_CPUTYPE,
	_HW_CPUSUBTYPE,
	_HW_CPUTHREADTYPE,
	_HW_64BITCAPABLE,
	_HW_PACKAGES,
	_HW_CPUFREQUENCY = 15,
};

static sysctl_handler(handle_availcpu);
static sysctl_handler(handle_physicalcpu);
static sysctl_handler(handle_physicalcpu_max);
static sysctl_handler(handle_logicalcpu);
static sysctl_handler(handle_logicalcpu_max);
static sysctl_handler(handle_memsize);
static sysctl_handler(handle_pagesize);
static sysctl_handler(handle_cputype);
static sysctl_handler(handle_cpusubtype);
static sysctl_handler(handle_cputhreadtype);
static sysctl_handler(handle_cpu64bitcapable);
static sysctl_handler(handle_machine);
static sysctl_handler(handle_cpufrequency);
static sysctl_handler(handle_cachelinesize);
static sysctl_handler(handle_packages);

const struct known_sysctl sysctls_hw[] = {
	{ .oid = HW_AVAILCPU, .type = CTLTYPE_INT, .exttype = "", .name = "availcpu", .handler = handle_availcpu },
	{ .oid = HW_NCPU, .type = CTLTYPE_INT, .exttype = "", .name = "ncpu", .handler = handle_availcpu },
	{ .oid = _HW_PHYSICAL_CPU, .type = CTLTYPE_INT, .exttype = "", .name = "physicalcpu", .handler = handle_physicalcpu },
	{ .oid = _HW_PHYSICAL_CPU_MAX, .type = CTLTYPE_INT, .exttype = "", .name = "physicalcpu_max", .handler = handle_physicalcpu_max },
	{ .oid = _HW_LOGICAL_CPU, .type = CTLTYPE_INT, .exttype = "", .name = "logicalcpu", .handler = handle_logicalcpu },
	{ .oid = _HW_LOGICAL_CPU_MAX, .type = CTLTYPE_INT, .exttype = "", .name = "logicalcpu_max", .handler = handle_logicalcpu_max },
	{ .oid = HW_MEMSIZE, .type = CTLTYPE_QUAD, .exttype = "U", .name = "memsize", .handler = handle_memsize },
	{ .oid = HW_PAGESIZE, .type = CTLTYPE_INT, .exttype = "I", .name = "pagesize", .handler = handle_pagesize },
	{ .oid = _HW_CPUTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cputype", .handler = handle_cputype },
	{ .oid = _HW_CPUSUBTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cpusubtype", .handler = handle_cpusubtype },
	{ .oid = _HW_CPUTHREADTYPE, .type = CTLTYPE_INT, .exttype = "", .name = "cputhreadtype", .handler = handle_cputhreadtype },
	{ .oid = _HW_64BITCAPABLE, .type = CTLTYPE_INT, .exttype = "", .name = "cpu64bit_capable", .handler = handle_cpu64bitcapable },
	{ .oid = HW_MACHINE, .type = CTLTYPE_STRING, .exttype = "S", .name = "machine", .handler = handle_machine },
	{ .oid = _HW_CPUFREQUENCY, .type = CTLTYPE_INT, .exttype = "", .name = "cpufrequency", .handler = handle_cpufrequency },
	{ .oid = HW_CACHELINE, .type = CTLTYPE_QUAD, .exttype = "U", .name = "cachelinesize", .handler = handle_cachelinesize },
	{ .oid = _HW_PACKAGES, .type = CTLTYPE_INT, .exttype = "I", .name = "packages", .handler = handle_packages },
	{ .oid = -1 }
};

static const struct host_basic_info* gethostinfo(void)
{
	static struct host_basic_info hinfo;

	if (!hinfo.avail_cpus)
	{
		mach_msg_type_number_t hcount = HOST_BASIC_INFO_COUNT;
		mach_port_t hself;

		hself = mach_host_self();
		host_info(hself, HOST_BASIC_INFO, &hinfo, &hcount);
		mach_port_deallocate(mach_task_self(), hself);
	}

	return &hinfo;
}

sysctl_handler(handle_availcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->avail_cpus;
	return 0;
}

sysctl_handler(handle_physicalcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->physical_cpu;
	return 0;
}

sysctl_handler(handle_physicalcpu_max)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->physical_cpu_max;
	return 0;
}

sysctl_handler(handle_logicalcpu)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->logical_cpu;
	return 0;
}

sysctl_handler(handle_logicalcpu_max)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->logical_cpu_max;
	return 0;
}

sysctl_handler(handle_memsize)
{

	sysctl_handle_size(sizeof(unsigned long long));
	*((unsigned long long*) old) = gethostinfo()->max_mem;
	return 0;
}

sysctl_handler(handle_pagesize)
{
	if (old == NULL)
	{
		*oldlen = sizeof(int);
		return 0;
	}
	else if (*oldlen < sizeof(int))
	{
		return -EINVAL;
	}
	else
	{
		// Not 4K everywhere: aarch64 kernels are commonly built with 16K pages. mldr puts the
		// host's page shift in the commpage and libsyscall's mach_init_doit() derives this.
		*((int*) old) = (int) vm_page_size;

		// libc's sysconf passes in a `long` for the argument.
		// Apple's code is actually wrong there because it doesn't check the returned size.
		// to deal with this (since apparently it works on real macOS), what we do is zero out
		// the rest of the buffer so the upper half won't be garbage.
		//
		// note that this only works on little endian systems,
		// but that's okay; that's what all Darling platforms are.
		memset((char*)old + sizeof(int), 0, *oldlen - sizeof(int));

		*oldlen = sizeof(int);
	}
	return 0;
}

sysctl_handler(handle_cputype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_type;
	return 0;
}

sysctl_handler(handle_cpusubtype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_subtype;
	return 0;
}

sysctl_handler(handle_cputhreadtype)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = gethostinfo()->cpu_threadtype;
	return 0;
}

sysctl_handler(handle_cpufrequency)
{
	int freq = 2400000;
	char buf[16];

	if (read_string("/sys/bus/cpu/devices/cpu0/cpufreq/cpuinfo_max_freq", buf, sizeof(buf)))
		freq = __simple_atoi(buf, NULL);
	*((int*) old) = freq;
	
	return 0;
}

sysctl_handler(handle_cpu64bitcapable)
{
	sysctl_handle_size(sizeof(int));
	*((int*) old) = 1; // Darling only supports 64-bit CPUs
	return 0;
}

sysctl_handler(handle_machine)
{
	copyout_string(need_uname()->machine, (char*) old, oldlen);
	return 0;
}

sysctl_handler(handle_cachelinesize)
{
	// A fixed architectural constant, like the page size above. XNU aligns its cache
	// maintenance on 1 << ARM64_CLINE_SHIFT with ARM64_CLINE_SHIFT == 6
	// (osfmk/arm64/proc_reg.h), i.e. 64 bytes on arm64; on arm, MMU_CLINE is 6 for
	// the same reason (osfmk/arm/proc_reg.h).
	sysctl_handle_size(sizeof(unsigned long long));
	*((unsigned long long*) old) = 64;
	return 0;
}

sysctl_handler(handle_packages)
{
    if (!oldlen) return -EINVAL;
    if (_new) return -EPERM;
    sysctl_handle_size(sizeof(uint32_t));
    int fd = sys_open_nocancel("/proc/cpuinfo", BSD_O_RDONLY, 0);
    if (fd < 0) return fd;
    uint32_t packages[256];
    uint32_t count = 0;
    struct rdline_buffer rbuf;
    _readline_init(&rbuf);
    const char* line;
    while ((line = _readline(fd, &rbuf)) != NULL) {
        if (strncmp(line, "physical id", 11) != 0) continue;
        const char* value = line + 11;
        while (*value && *value != ':') ++value;
        if (!*value) continue;
        ++value;
        while (*value == ' ' || *value == '\t') ++value;
        uint32_t package = __simple_atoi(value, NULL);
        uint32_t i;
        for (i = 0; i < count && packages[i] != package; ++i) {}
        if (i == count) {
            if (count == sizeof(packages) / sizeof(packages[0])) {
                close_internal(fd);
                return -EOVERFLOW;
            }
            packages[count++] = package;
        }
    }
    close_internal(fd);
    /* Architectures without a physical-id field describe one package. */
    *(uint32_t*)old = count ? count : 1;
    return 0;
}

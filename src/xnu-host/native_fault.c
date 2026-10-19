/* Linux ARM64 signal-context recovery for explicitly marked kernel instructions.
 * This is not Mach exception delivery. Unmatched faults remain fatal. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <usermode_vm.h>
#include <host/fault.h>

#if !defined(__aarch64__)
#error This signal-context backend requires Linux ARM64
#endif

extern char xnu_host_ticket_fault_start[], xnu_host_ticket_fault_end[];
extern char xnu_host_ticket_fault_return[];
static uintptr_t alternate_start, alternate_end;
static volatile sig_atomic_t recoveries, alternate_recoveries;
static int initialized;

static void
hex_address(char *output, uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned int i = 0; i < 16; i++) {
        output[15 - i] = digits[value & 15];
        value >>= 4;
    }
}

static void
fault_handler(int signal_number, siginfo_t *information, void *context)
{
    int saved_errno = errno;
    ucontext_t *state = context;
    uintptr_t pc = state->uc_mcontext.pc;
    if (information->si_code > 0 && pc >= (uintptr_t)xnu_host_ticket_fault_start &&
        pc < (uintptr_t)xnu_host_ticket_fault_end) {
        char marker;
        uintptr_t stack = (uintptr_t)&marker;
        if (recoveries < SIG_ATOMIC_MAX)
            recoveries++;
        if (stack >= alternate_start && stack < alternate_end &&
            alternate_recoveries < SIG_ATOMIC_MAX)
            alternate_recoveries++;
        state->uc_mcontext.pc = (uintptr_t)xnu_host_ticket_fault_return;
        errno = saved_errno;
        return; /* Linux sigreturn restores registers, SP and the signal mask. */
    }

    /* Async-signal-safe evidence before restoring fatal signal behavior. */
    char message[] = "XNU_HOST_FAULT unhandled pc=0x0000000000000000 addr=0x0000000000000000\n";
    hex_address(message + sizeof("XNU_HOST_FAULT unhandled pc=0x") - 1, pc);
    hex_address(message + sizeof("XNU_HOST_FAULT unhandled pc=0x0000000000000000 addr=0x") - 1,
        (uintptr_t)information->si_addr);
    ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
    (void)written;
    struct sigaction fatal = { .sa_handler = SIG_DFL };
    sigemptyset(&fatal.sa_mask);
    if (sigaction(signal_number, &fatal, NULL) || raise(signal_number))
        _exit(128 + signal_number);
    errno = saved_errno;
}

int
xnu_host_fault_initialize(void)
{
    if (initialized)
        return EALREADY;
    stack_t alternate;
    if (sigaltstack(NULL, &alternate))
        return errno;
    if ((alternate.ss_flags & SS_DISABLE) || alternate.ss_size < (size_t)MINSIGSTKSZ)
        return EINVAL;
    alternate_start = (uintptr_t)alternate.ss_sp;
    alternate_end = alternate_start + alternate.ss_size;
    struct sigaction handler = { .sa_sigaction = fault_handler,
        .sa_flags = SA_SIGINFO | SA_ONSTACK };
    sigemptyset(&handler.sa_mask);
    sigaddset(&handler.sa_mask, SIGSEGV);
    sigaddset(&handler.sa_mask, SIGBUS);
    struct sigaction previous;
    if (sigaction(SIGSEGV, &handler, &previous))
        return errno;
    if (sigaction(SIGBUS, &handler, NULL)) {
        int error = errno;
        sigaction(SIGSEGV, &previous, NULL);
        return error;
    }
    initialized = 1;
    return 0;
}

void
xnu_host_fault_statistics(unsigned long *recovered, unsigned long *alternate)
{
    *recovered = (unsigned long)recoveries;
    *alternate = (unsigned long)alternate_recoveries;
}

int
xnu_host_fault_bus_mapping(unsigned long *address)
{
    int fd = memfd_create("xnu-fault-bus-diagnostic", MFD_CLOEXEC);
    if (fd == -1)
        return errno;
    /* Deliberately keep the file empty. Mapping succeeds, access is beyond EOF. */
    void *mapping = mmap(NULL, XNU_HOST_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int error = mapping == MAP_FAILED ? errno : 0;
    close(fd);
    if (!error)
        *address = (unsigned long)mapping;
    return error;
}

int
xnu_host_fault_bus_unmap(unsigned long address)
{
    return munmap((void *)address, XNU_HOST_PAGE_SIZE) ? errno : 0;
}

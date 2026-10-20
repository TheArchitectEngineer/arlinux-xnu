#include <darling/emulation/linux_premigration/signal/sigexc.h>

#include <stddef.h>
#include <stdint.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <mach/vm_page_size.h>
#include <_libkernel_init.h>

#include <darling/emulation/xnu_syscall/bsd/impl/signal/sigaction.h>
#include <darling/emulation/common/base.h>
#include <darling/emulation/xnu_syscall/bsd/impl/unistd/exit.h>
#include <darling/emulation/linux_premigration/linux-syscalls/linux.h>
#include <darling/emulation/xnu_syscall/bsd/impl/signal/sigaltstack.h>
#include <darling/emulation/other/mach/lkm.h>
#include <darling/emulation/xnu_syscall/bsd/impl/mman/mman.h>
#include <darling/emulation/xnu_syscall/bsd/impl/signal/kill.h>
#include <darling/emulation/common/simple.h>
#include <darling/emulation/common/tsd.h>
#include <darling/emulation/xnu_syscall/machdep/impl/tls.h>

#include <darlingserver/rpc.h>

// Support for Darwin debugging.
// Unlike other Unix-like systems, macOS doesn't use wait() to handle events in the debugged process.
// wait() only receives termination events.

static volatile bool am_i_ptraced = false;
static void handle_rt_signal(int signum);
static void sigexc_fake_suspend(struct linux_ucontext* ctxt);
extern void sig_restorer(void);
extern int getpid(void);

extern _libkernel_functions_t _libkernel_functions;

void darling_sigexc_uninstall(void);
void sigrt_handler(int signum, struct linux_siginfo* info, struct linux_ucontext* ctxt);

#define SIGEXC_STACK_SIZE (48ULL * 1024ULL)

#ifndef SIGALTSTACK_GUARD
	#define SIGALTSTACK_GUARD 1
#endif

#if SIGALTSTACK_GUARD
// The guard is one page, and mprotect needs its address page-aligned. A static array's
// alignment is fixed at compile time, so reserve the largest page we cater for and take
// vm_page_size within it.
#define SIGEXC_GUARD_RESERVE (16ULL * 1024ULL)
static char sigexc_altstack[SIGEXC_STACK_SIZE + SIGEXC_GUARD_RESERVE]
		__attribute__((aligned(SIGEXC_GUARD_RESERVE)));
#else
static char sigexc_altstack[SIGEXC_STACK_SIZE];
#endif
size_t default_sigaltstack_size = SIGEXC_STACK_SIZE;

#if defined(__x86_64__)
static void mcontext_to_thread_state(const struct linux_gregset* regs, x86_thread_state64_t* s);
static void mcontext_to_float_state(const linux_fpregset_t fx, x86_float_state64_t* s);
static void thread_state_to_mcontext(const x86_thread_state64_t* s, struct linux_gregset* regs);
static void float_state_to_mcontext(const x86_float_state64_t* s, linux_fpregset_t fx);
#elif defined(__i386__)
static void mcontext_to_thread_state(const struct linux_gregset* regs, x86_thread_state32_t* s);
static void mcontext_to_float_state(const linux_fpregset_t fx, x86_float_state32_t* s);
static void thread_state_to_mcontext(const x86_thread_state32_t* s, struct linux_gregset* regs);
static void float_state_to_mcontext(const x86_float_state32_t* s, linux_fpregset_t fx);
#elif defined(__aarch64__) || defined(__arm64__)
#include <mach/arm/thread_status.h>
/* RPC exchanges Darwin arm_neon_state64_t, not Linux's FPSIMD record layout. */
typedef struct { __uint128_t vregs[32]; unsigned int fpsr; unsigned int fpcr; } arm_neon_state64_t_linux;
static void mcontext_to_thread_state(const struct linux_gregset* regs, arm_thread_state64_t* s);
static void mcontext_to_float_state(const unsigned char* reserved, arm_neon_state64_t_linux* s);
static void thread_state_to_mcontext(const arm_thread_state64_t* s, struct linux_gregset* regs);
static void float_state_to_mcontext(const arm_neon_state64_t_linux* s, unsigned char* reserved);
#endif

static void state_from_kernel(struct linux_ucontext* ctxt, const void* tstate, const void* fstate);
static void state_to_kernel(struct linux_ucontext* ctxt, void* tstate, void* fstate);

#define DEBUG_SIGEXC
#ifdef DEBUG_SIGEXC
#define kern_printf(...) __simple_kprintf(__VA_ARGS__)
#else
#define kern_printf(...)
#endif

void sigexc_setup1(void)
{
	handle_rt_signal(SIGNAL_SIGEXC_SUSPEND);
	handle_rt_signal(SIGNAL_S2C);
}

void sigexc_setup2(void)
{
	linux_sigset_t set;
	set = (1ull << (SIGNAL_SIGEXC_SUSPEND-1));
	set |= (1ull << (SIGNAL_S2C-1));

	LINUX_SYSCALL(__NR_rt_sigprocmask, 1 /* LINUX_SIG_UNBLOCK */,
		&set, NULL, sizeof(linux_sigset_t));
}

static void dump_gregs(const struct linux_gregset* regs)
{
	unsigned long long* p = (unsigned long long*) regs;
	for (int i = 0; i < 23; i++)
	{
		kern_printf("sigexc:   gregs 0x%llx\n", p[i]);
	}
}

static void handle_rt_signal(int signum)
{
	int rv;
	struct linux_sigaction sa;

	sa.sa_sigaction = (linux_sig_handler*)sigrt_handler;
	sa.sa_mask = (1ull << (SIGNAL_SIGEXC_SUSPEND-1)) | (1ull << (signum-1));
	sa.sa_flags = LINUX_SA_RESTORER | LINUX_SA_SIGINFO | LINUX_SA_RESTART | LINUX_SA_ONSTACK;
	sa.sa_restorer = sig_restorer;

	rv = LINUX_SYSCALL(__NR_rt_sigaction, signum,
			&sa, NULL,
			sizeof(sa.sa_mask));

	//char buf[128];
	//__simple_sprintf(xyzbuf, "Setting handler for RT signal %d: %d", signum, rv);
	//external/lkm_call(0x1028, buf);
}

void sigexc_setup(void)
{
	darling_sigexc_self();
	sigexc_setup1();
	sigexc_setup2();

#ifdef VARIANT_DYLD
	bool started_suspended;
	int code = dserver_rpc_started_suspended(&started_suspended);
	if (code < 0) {
		__simple_printf("Failed to get started_suspended status: %d\n", code);
		__simple_abort();
	}
	if (started_suspended) {
		kern_printf("sigexc: start_suspended -> suspending (ret to %p)\n", __builtin_return_address(0));
		task_suspend(mach_task_self());
		kern_printf("sigexc: start_suspended -> wokenup (ret to %p)\n", __builtin_return_address(0));
	} else {
		int32_t tracer;
		code = dserver_rpc_get_tracer(&tracer);
		if (code < 0) {
			__simple_printf("Failed to get tracer status: %d\n", code);
			__simple_abort();
		}
		if (tracer != 0) {
			kern_printf("sigexc: already traced -> SIGTRAP\n");
			sys_kill(0, SIGTRAP, 0);
		}
	}
#else

#endif
}

// darlingserver RPC failures that mean the server has gone away (e.g. during `darling shutdown`),
// as negative Linux errno values: ENOENT, EPIPE, ECONNRESET, ENOTCONN, ECONNREFUSED.
static bool is_server_gone(int status)
{
	return status == -2 || status == -32 || status == -104 || status == -107 || status == -111;
}

// The process can't continue without darlingserver. Exit without a core dump, but with a failure
// status: exiting with 0 would report success for work that never finished (shellspawn forwards the
// status as the exit code of `darling shell <command>`). 128 + SIGKILL is how shellspawn reports a
// process that was killed by a signal.
static void exit_server_gone(void)
{
	LINUX_SYSCALL(__NR_exit_group, 128 + LINUX_SIGKILL);
}

// Called when an RPC made from a signal handler fails. If darlingserver is gone, the container is
// shutting down and nothing can be done anymore: exit instead of aborting (which would raise
// SIGTRAP/SIGABRT and leave a core dump for every process). Other failures still abort.
static void rpc_failed_in_handler(const char* what, int status)
{
	if (is_server_gone(status))
		exit_server_gone();
	__simple_printf("*** %s failed with code %d ***\n", what, status);
	__simple_abort();
}

void sigrt_handler(int signum, struct linux_siginfo* info, struct linux_ucontext* ctxt)
{

	int status = dserver_rpc_interrupt_enter();

	if (status != 0) {
		rpc_failed_in_handler("dserver_rpc_interrupt_enter", status);
	}

	if (signum == SIGNAL_SIGEXC_SUSPEND) {
#if defined(__x86_64__)
	x86_thread_state64_t tstate;
	x86_float_state64_t fstate;
#elif defined(__i386__)
	x86_thread_state32_t tstate;
	x86_float_state32_t fstate;
#elif defined(__aarch64__) || defined(__arm64__)
	arm_thread_state64_t tstate;
	arm_neon_state64_t_linux fstate;
#endif

	kern_printf("sigexc: sigrt_handler SUSPEND\n");

	thread_t thread = mach_thread_self();
	state_to_kernel(ctxt, &tstate, &fstate);

	int ret = dserver_rpc_thread_suspended(&tstate, &fstate);
	if (ret < 0) {
		rpc_failed_in_handler("dserver_rpc_thread_suspended", ret);
	}

	state_from_kernel(ctxt, &tstate, &fstate);
	} else if (signum == SIGNAL_S2C) {
		__simple_kprintf("sigexc: sigrt_handler S2C");

		int ret = dserver_rpc_s2c_perform();
		if (ret < 0) {
			rpc_failed_in_handler("dserver_rpc_s2c_perform", ret);
		}
	} else {
		__simple_printf("Unknown/unrecognized real-time signal: %d", signum);
	}

	status = dserver_rpc_interrupt_exit();

	if (status != 0) {
		rpc_failed_in_handler("dserver_rpc_interrupt_exit", status);
	}
}


void darling_sigexc_self(void)
{
	am_i_ptraced = true;

	__simple_kprintf("darling_sigexc_self()\n");
	// Make sigexc_handler the handler for all signals in the process
	for (int i = 1; i <= 31; i++)
	{
		if (i == LINUX_SIGSTOP || i == LINUX_SIGKILL || i == LINUX_SIGCHLD)
			continue;

		struct linux_sigaction sa;
		sa.sa_sigaction = (linux_sig_handler*) sigexc_handler;
		sa.sa_mask = 0x7fffffff; // all other standard Unix signals should be blocked while the handler is run
		sa.sa_mask |= (1ull << (SIGNAL_SIGEXC_SUSPEND-1));
		sa.sa_flags = LINUX_SA_RESTORER | LINUX_SA_SIGINFO | LINUX_SA_RESTART | LINUX_SA_ONSTACK;
		sa.sa_restorer = sig_restorer;

		LINUX_SYSCALL(__NR_rt_sigaction, i,
				&sa, NULL,
				sizeof(sa.sa_mask));
	}

#if SIGALTSTACK_GUARD
	// mprotect needs a page-aligned address, and aligned(4096) is not page-aligned on a
	// 16K-page host. vm_page_size must match the kernel's granularity: mprotect rounds
	// the length up, so a smaller value would protect into the stack.
	unsigned long guard = vm_page_size;

	if (guard > SIGEXC_GUARD_RESERVE) {
		// Rounding up from the reserve would PROT_NONE the whole stack instead.
		__simple_kprintf("sigexc: page size %lu exceeds the guard reserve, alt stack unguarded\n", guard);
		guard = 0;
	}
	else if (sys_mprotect(sigexc_altstack, guard, PROT_NONE) < 0) {
		__simple_kprintf("sigexc: could not protect the alt stack guard page\n");
	}
#endif

	struct bsd_stack newstack = {
#if SIGALTSTACK_GUARD
		.ss_sp = sigexc_altstack + guard,
#else
		.ss_sp = sigexc_altstack,
#endif
		.ss_size = SIGEXC_STACK_SIZE,
		.ss_flags = 0
	};
	sys_sigaltstack(&newstack, NULL);
}


static void state_to_kernel(struct linux_ucontext* ctxt, void* tstate, void* fstate)
{
#if defined(__x86_64__)

	dump_gregs(&ctxt->uc_mcontext.gregs);
	mcontext_to_thread_state(&ctxt->uc_mcontext.gregs, (x86_thread_state64_t*) tstate);
	mcontext_to_float_state(ctxt->uc_mcontext.fpregs, (x86_float_state64_t*) fstate);

#elif defined(__i386__)
	mcontext_to_thread_state(&ctxt->uc_mcontext.gregs, (x86_thread_state32_t*) tstate);
	mcontext_to_float_state(ctxt->uc_mcontext.fpregs, (x86_float_state32_t*) fstate);
#elif defined(__aarch64__) || defined(__arm64__)
	mcontext_to_thread_state(&ctxt->uc_mcontext.gregs, (arm_thread_state64_t*) tstate);
	mcontext_to_float_state(ctxt->uc_mcontext.__reserved, (arm_neon_state64_t_linux*) fstate);
#endif

}

static void state_from_kernel(struct linux_ucontext* ctxt, const void* tstate, const void* fstate)
{
#if defined(__x86_64__)

	thread_state_to_mcontext((const x86_thread_state64_t*) tstate, &ctxt->uc_mcontext.gregs);
	float_state_to_mcontext((const x86_float_state64_t*) fstate, ctxt->uc_mcontext.fpregs);

	dump_gregs(&ctxt->uc_mcontext.gregs);

#elif defined(__i386__)
	thread_state_to_mcontext((const x86_thread_state32_t*) tstate, &ctxt->uc_mcontext.gregs);
	float_state_to_mcontext((const x86_float_state32_t*) fstate, ctxt->uc_mcontext.fpregs);
#elif defined(__aarch64__) || defined(__arm64__)
	thread_state_to_mcontext((const arm_thread_state64_t*) tstate, &ctxt->uc_mcontext.gregs);
	float_state_to_mcontext((const arm_neon_state64_t_linux*) fstate, ctxt->uc_mcontext.__reserved);
#endif
}

#if defined(__aarch64__) || defined(__arm64__)
// Private dyld/kernel ABI: UDF #0xda00..0xda1f replaces a cached Darwin
// MRS TPIDRRO_EL0. The low five bits name Xd (31 means XZR). Linux owns
// TPIDR_EL0; returning it here would expose and corrupt native ELF TLS.
static bool emulate_darling_tsd_read(struct linux_ucontext* ctxt)
{
    struct linux_gregset* regs = &ctxt->uc_mcontext.gregs;
    const uint32_t instruction = *(const uint32_t*)regs->pc;
    if ( (instruction & 0xFFFFFFE0U) != 0x0000DA00U )
        return false;
    const uint32_t destination = instruction & 0x1FU;
    if ( destination != 31 )
        regs->regs[destination] = (uintptr_t)sys_thread_get_tsd_base();
    regs->pc += sizeof(instruction);
    return true;
}

#endif

void sigexc_handler(int linux_signum, struct linux_siginfo* info, struct linux_ucontext* ctxt)
{
#if defined(__aarch64__) || defined(__arm64__)
    // Consume the emulated register read before RPC and Mach exception delivery.
    if ( linux_signum == LINUX_SIGILL && info && info->si_code > 0
            && emulate_darling_tsd_read(ctxt) )
        return;
#endif
	// Re-entrancy guard. The first thing this handler does is enter a server RPC,
	// which takes duct-tape mutexes. A thread running inside a signal handler has
	// no registered dtape thread, so dtape_mutex_lock takes its fallback path
	// (duct-tape/src/locks.c) and spins waiting for an owner to drop the lock. If
	// anything inside the handler faults, the kernel reinvokes the handler, which
	// spins again - and with both threads in that state the spin never ends.
	//
	// That was observed turning a single guest SIGSEGV into ~30 nested handler
	// entries and ~3000 "Trying to lock mutex without an active thread!"
	// warnings across two threads, which destroyed the original crash's stack: the
	// cores only ever showed the signal trampoline.
	//
	// One entry per thread is allowed. A fault while already inside the handler
	// cannot be reported through the same path, so kill the thread outright: the
	// process still dies, but the core describes the first fault instead of the
	// livelock.
	//
	// Only for signals that are themselves a fault. A benign re-entry - a SIGWINCH
	// delivered while the handler is already running, which happens during normal
	// startup - must not take the thread down with it.
	// Compiler TLS requires __tlv_bootstrap, which is unavailable in the
	// static dyld variant. Direct TSD is already initialized for signal delivery.
	int in_sigexc_handler_signal = (int)(uintptr_t)
			_pthread_getspecific_direct(__PTK_DARLING_SIGEXC_SIGNAL);

	if (in_sigexc_handler_signal) {
		bool nested_is_fault =
				linux_signum == LINUX_SIGSEGV || linux_signum == LINUX_SIGBUS ||
				linux_signum == LINUX_SIGILL || linux_signum == LINUX_SIGFPE ||
				linux_signum == LINUX_SIGSYS || linux_signum == LINUX_SIGTRAP;

		if (nested_is_fault) {
			kern_printf("sigexc: fault (%d) inside the handler for signal %d, killing this thread\n",
					linux_signum, in_sigexc_handler_signal);

			// Report where it faulted before killing the thread. Without this the
			// register state is lost with the process and the nested fault - the
			// one that actually matters - stays as invisible as it was before the
			// guard existed. The usual dump further down is never reached, because
			// that path would block on the same RPC that faulted.
#if defined(__x86_64__)
			if (ctxt)
				kern_printf("sigexc: nested fault at RIP 0x%llx\n",
					(unsigned long long)ctxt->uc_mcontext.gregs.rip);
#elif defined(__aarch64__) || defined(__arm64__)
			if (ctxt)
				kern_printf("sigexc: nested fault at PC 0x%llx, fault_addr 0x%llx, SP 0x%llx\n",
					(unsigned long long)ctxt->uc_mcontext.gregs.pc,
					(unsigned long long)ctxt->uc_mcontext.gregs.fault_address,
					(unsigned long long)ctxt->uc_mcontext.gregs.sp);
#elif defined(__i386__)
			if (ctxt)
				kern_printf("sigexc: nested fault at EIP 0x%llx\n",
					(unsigned long long)ctxt->uc_mcontext.gregs.eip);
#endif

			LINUX_SYSCALL(__NR_exit_group, 128 + LINUX_SIGKILL);
			// exit_group does not return.
		}

		// Not a fault: fall through and let the nested signal be handled.
	} else {
		_pthread_setspecific_direct(__PTK_DARLING_SIGEXC_SIGNAL,
				(void*)(uintptr_t)linux_signum);
	}

	int status = dserver_rpc_interrupt_enter();

	if (status != 0) {
		rpc_failed_in_handler("dserver_rpc_interrupt_enter", status);
	}

	kern_printf("sigexc_handler(%d, %p, %p)\n", linux_signum, info, ctxt);

	// DARLING-DBG: capture SIGSYS details (bad syscall number, code, call addr)
	if (linux_signum == LINUX_SIGSYS && info)
	{
		const unsigned char* ib = (const unsigned char*)info;
		int d_si_code = info->si_code;
		int d_si_syscall = *(int*)(ib + 24);   // Linux SIGSYS: si_syscall
		int d_si_arch = *(int*)(ib + 28);      // si_arch
		unsigned long long d_call_addr = *(unsigned long long*)(ib + 16); // si_call_addr
		kern_printf("sigexc: SIGSYS si_code=%d si_syscall=%d si_arch=%d si_call_addr=0x%llx\n",
			d_si_code, d_si_syscall, d_si_arch, d_call_addr);
	}

	if (linux_signum == LINUX_SIGCONT)
		goto out;

	int bsd_signum = signum_linux_to_bsd(linux_signum);
	if (bsd_signum <= 0)
	{
		kern_printf("sigexc: Unmapped signal!\n");
		goto out;
	}

#ifdef __x86_64__
	if (__builtin_expect(getenv("DARLING_LOG_SIGNALS") != NULL, 0)) {
		__simple_printf("sigexc: signal %d (bsd %d) in PID %d at RIP 0x%llx\n",
			linux_signum, bsd_signum, getpid(), (unsigned long long)ctxt->uc_mcontext.gregs.rip);
	}
	kern_printf("sigexc: have RIP 0x%llx\n", ctxt->uc_mcontext.gregs.rip);
#elif defined(__aarch64__) || defined(__arm64__)
	if (linux_signum == LINUX_SIGSEGV || linux_signum == LINUX_SIGBUS || linux_signum == LINUX_SIGILL)
	{
		kern_printf("sigexc: fatal signal %d in PID %d at PC 0x%llx, fault_addr 0x%llx, SP 0x%llx\n",
			linux_signum, getpid(),
			(unsigned long long)ctxt->uc_mcontext.gregs.pc,
			(unsigned long long)ctxt->uc_mcontext.gregs.fault_address,
			(unsigned long long)ctxt->uc_mcontext.gregs.sp);
		// Only inspect the saved context. The interrupted FP/SP may point
		// to unmapped memory, so walking frames here could fault again.
		kern_printf("sigexc: ARM64 registers LR=0x%llx FP=0x%llx x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x8=0x%llx x19=0x%llx x20=0x%llx x21=0x%llx x22=0x%llx\n",
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[30],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[29],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[0],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[1],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[2],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[3],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[8],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[19],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[20],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[21],
			(unsigned long long)ctxt->uc_mcontext.gregs.regs[22]);
	}
#endif

	thread_t thread = mach_thread_self();

#if defined(__x86_64__)
	x86_thread_state64_t tstate;
	x86_float_state64_t fstate;
#elif defined(__i386__)
	x86_thread_state32_t tstate;
	x86_float_state32_t fstate;
#elif defined(__aarch64__) || defined(__arm64__)
	arm_thread_state64_t tstate;
	arm_neon_state64_t_linux fstate;
#endif

	state_to_kernel(ctxt, &tstate, &fstate);

	// info is NULL for signals the kernel raises itself rather than delivering
	// from another process, so its fields cannot be read unconditionally.
	int sig_pid = info ? info->si_pid : 0;
	int sig_code = info ? info->si_code : 0;
	unsigned long long sig_addr = info ? (unsigned long long) info->si_addr : 0;

	if (info != NULL && linux_signum == LINUX_SIGTRAP && (sig_code == 1 || sig_code == 128)) {
		/*
		 * CRITICAL: When a TRAP_BRKPT (int3) occurs, Linux leaves the instruction
		 * pointer (RIP/EIP) pointing to the byte AFTER the int3 instruction.
		 * However, the macOS/XNU kernel (and Mach exception handlers) expect
		 * the instruction pointer to point EXACTLY at the int3 instruction.
		 * We must manually decrement the PC here before translating to EXC_BREAKPOINT.
		 * This ensures macOS debuggers (like lldb) and crash reporters map the PC correctly.
		 */
#if defined(__x86_64__)
		tstate.__rip -= 1;
#elif defined(__i386__)
		tstate.__eip -= 1;
#endif
	}

	int ret = dserver_rpc_sigprocess(bsd_signum, linux_signum, sig_pid, sig_code, (void *) sig_addr, &tstate, &fstate, &bsd_signum);
	if (ret < 0 && is_server_gone(ret)) {
		exit_server_gone();
	}
	if (ret < 0) {
#if defined(__x86_64__)
		__simple_printf("sigprocess failed internally while processing Linux signal %d: %d in PID %d (RIP 0x%llx)\n", linux_signum, ret, getpid(), (unsigned long long)ctxt->uc_mcontext.gregs.rip);
#else
		__simple_printf("sigprocess failed internally while processing Linux signal %d: %d in PID %d\n", linux_signum, ret, getpid());
#endif
		__simple_abort();
	}
	state_from_kernel(ctxt, &tstate, &fstate);

	if (!bsd_signum)
	{
		kern_printf("sigexc: drop signal\n");
		goto out;
	}

	linux_signum = signum_bsd_to_linux(bsd_signum);

	if (sig_handlers[linux_signum] != SIG_IGN)
	{
		if (sig_handlers[linux_signum])
		{
			kern_printf("sigexc: will forward signal to app handler (%p)\n", sig_handlers[linux_signum]);

			// Update the signal mask to what the application actually wanted
			linux_sigset_t set = sig_masks[linux_signum];

			// Keep the current signal blocked, which is the default behavior
			if (!(sig_flags[linux_signum] & LINUX_SA_NODEFER))
				set |= (1ull << (linux_signum-1));

			LINUX_SYSCALL(__NR_rt_sigprocmask, 2 /* LINUX_SIG_SETMASK */,
				&set, NULL, sizeof(linux_sigset_t));

			handler_linux_to_bsd(linux_signum, info, ctxt);
		}
		else
		{
			if (bsd_signum == SIGTSTP || bsd_signum == SIGSTOP)
			{
				kern_printf("sigexc: emulating SIGTSTP/SIGSTOP\n");
				LINUX_SYSCALL(__NR_kill, 0, LINUX_SIGSTOP);
			}
			else
			{
				kern_printf("sigexc: emulating default signal effects\n");
				// Set handler to SIG_DFL
				struct linux_sigaction sa;
				sa.sa_sigaction = (linux_sig_handler*) NULL; // SIG_DFL
				sa.sa_mask = 0x7fffffff; // all other standard Unix signals should be blocked while the handler is run
				sa.sa_flags = LINUX_SA_RESTORER | LINUX_SA_SIGINFO | LINUX_SA_RESTART | LINUX_SA_ONSTACK;
				sa.sa_restorer = sig_restorer;

				LINUX_SYSCALL(__NR_rt_sigaction, linux_signum,
						&sa, NULL,
						sizeof(sa.sa_mask));

				// Target the faulting thread. kill(0, sig) is process-directed, and
				// sa_mask above blocks this signal here, so the kernel had to deliver it
				// to some other thread - which then appears in the core as the crasher.
				// Unblocking first is required or a thread-directed signal stays pending.
				linux_sigset_t unblock = (1ull << (linux_signum - 1));
				LINUX_SYSCALL(__NR_rt_sigprocmask, 1 /* LINUX_SIG_UNBLOCK */,
						&unblock, NULL, sizeof(linux_sigset_t));

				int pid = LINUX_SYSCALL(__NR_getpid);
				int tid = LINUX_SYSCALL(__NR_gettid);
				LINUX_SYSCALL(__NR_tgkill, pid, tid, linux_signum);
			}
		}
	}

	kern_printf("sigexc: handler (%d) returning\n", linux_signum);

out:
	status = dserver_rpc_interrupt_exit();

	if (status != 0) {
		rpc_failed_in_handler("dserver_rpc_interrupt_exit", status);
	}

	// Clear the outermost guard, retaining it when a benign nested signal returns.
	_pthread_setspecific_direct(__PTK_DARLING_SIGEXC_SIGNAL,
			(void*)(uintptr_t)in_sigexc_handler_signal);
}

#define DUMPREG(regname) kern_printf("sigexc:   " #regname ": 0x%llx\n", regs->regname);

#if defined(__x86_64__)
void mcontext_to_thread_state(const struct linux_gregset* regs, x86_thread_state64_t* s)
{
	s->__rax = regs->rax;
	s->__rbx = regs->rbx;
	s->__rcx = regs->rcx;
	s->__rdx = regs->rdx;
	s->__rdi = regs->rdi;
	s->__rsi = regs->rsi;
	s->__rbp = regs->rbp;
	s->__rsp = regs->rsp;
	s->__r8 = regs->r8;
	s->__r9 = regs->r9;
	s->__r10 = regs->r10;
	s->__r11 = regs->r11;
	s->__r12 = regs->r12;
	s->__r13 = regs->r13;
	s->__r14 = regs->r14;
	s->__r15 = regs->r15;
	s->__rip = regs->rip;
	s->__rflags = regs->efl;
	s->__cs = regs->cs;
	s->__fs = regs->fs;
	s->__gs = regs->gs;

	kern_printf("sigexc: saving to kernel:\n");
	DUMPREG(rip);
	DUMPREG(efl);
	DUMPREG(rax);
	DUMPREG(rbx);
	DUMPREG(rcx);
	DUMPREG(rdx);
	DUMPREG(rdi);
	DUMPREG(rsi);
	DUMPREG(rbp);
	DUMPREG(rsp);
}

void mcontext_to_float_state(const linux_fpregset_t fx, x86_float_state64_t* s)
{
	// fpregs is NULL on WSL1. See https://github.com/microsoft/WSL/issues/2555.
	if (fx == NULL)
	{
		memset(s, 0, sizeof(x86_float_state64_t));
		return;
	}

	kern_printf("sigexc: fcw out: 0x%x", fx->cwd);
	kern_printf("sigexc: xmm0 out: 0x%x", fx->_xmm[0].element[0]);
	*((uint16_t*)&s->__fpu_fcw) = fx->cwd;
	*((uint16_t*)&s->__fpu_fsw) = fx->swd;
	s->__fpu_ftw = fx->ftw;
	s->__fpu_fop = fx->fop;
	s->__fpu_ip = fx->rip;
	s->__fpu_cs = 0;
	s->__fpu_dp = fx->rdp;
	s->__fpu_ds = 0;
	s->__fpu_mxcsr = fx->mxcsr;
	s->__fpu_mxcsrmask = fx->mxcr_mask;

	memcpy(&s->__fpu_stmm0, fx->_st, 128);
	memcpy(&s->__fpu_xmm0, fx->_xmm, 256);
}

void thread_state_to_mcontext(const x86_thread_state64_t* s, struct linux_gregset* regs)
{
	regs->rax = s->__rax;
	regs->rbx = s->__rbx;
	regs->rcx = s->__rcx;
	regs->rdx = s->__rdx;
	regs->rdi = s->__rdi;
	regs->rsi = s->__rsi;
	regs->rbp = s->__rbp;
	regs->rsp = s->__rsp;
	regs->r8 = s->__r8;
	regs->r9 = s->__r9;
	regs->r10 = s->__r10;
	regs->r11 = s->__r11;
	regs->r12 = s->__r12;
	regs->r13 = s->__r13;
	regs->r14 = s->__r14;
	regs->r15 = s->__r15;
	regs->rip = s->__rip;
	regs->efl = s->__rflags;
	regs->cs = s->__cs;
	regs->fs = s->__fs;
	regs->gs = s->__gs;

	kern_printf("sigexc: restored from kernel:\n");
	DUMPREG(rip);
	DUMPREG(efl);
	DUMPREG(rax);
	DUMPREG(rbx);
	DUMPREG(rcx);
	DUMPREG(rdx);
	DUMPREG(rdi);
	DUMPREG(rsi);
	DUMPREG(rbp);
	DUMPREG(rsp);
}

void float_state_to_mcontext(const x86_float_state64_t* s, linux_fpregset_t fx)
{
	if (fx == NULL)
	{
		return;
	}

	fx->cwd = *((uint16_t*)&s->__fpu_fcw);
	fx->swd = *((uint16_t*)&s->__fpu_fsw);
	fx->ftw = s->__fpu_ftw;
	fx->fop = s->__fpu_fop;
	fx->rip = s->__fpu_ip;
	fx->rdp = s->__fpu_dp;
	fx->mxcsr = s->__fpu_mxcsr;
	fx->mxcr_mask = s->__fpu_mxcsrmask;

	memcpy(fx->_st, &s->__fpu_stmm0, 128);
	memcpy(fx->_xmm, &s->__fpu_xmm0, 256);
	kern_printf("sigexc: fcw in: 0x%x", fx->cwd);
	kern_printf("sigexc: xmm0 in: 0x%x", fx->_xmm[0].element[0]);
}

#elif defined(__i386__)
void mcontext_to_thread_state(const struct linux_gregset* regs, x86_thread_state32_t* s)
{
	s->__eax = regs->eax;
	s->__ebx = regs->ebx;
	s->__ecx = regs->ecx;
	s->__edx = regs->edx;
	s->__edi = regs->edi;
	s->__esi = regs->esi;
	s->__ebp = regs->ebp;
	s->__esp = regs->esp;
	s->__ss = regs->ss;
	s->__eflags = regs->efl;
	s->__eip = regs->eip;
	s->__cs = regs->cs;
	s->__ds = regs->ds;
	s->__es = regs->es;
	s->__fs = regs->fs;
	s->__gs = regs->gs;
}

void mcontext_to_float_state(const linux_fpregset_t fx, x86_float_state32_t* s)
{
	*((uint16_t*)&s->__fpu_fcw) = fx->cw;
	*((uint16_t*)&s->__fpu_fsw) = fx->sw;
	s->__fpu_ftw = fx->tag;
	s->__fpu_fop = 0;
	s->__fpu_ip = fx->ipoff;
	s->__fpu_cs = fx->cssel;
	s->__fpu_dp = fx->dataoff;
	s->__fpu_ds = fx->datasel;
	s->__fpu_mxcsr = fx->mxcsr;
	s->__fpu_mxcsrmask = 0;

	memcpy(&s->__fpu_stmm0, fx->_st, 128);
	memcpy(&s->__fpu_xmm0, fx->_xmm, 128);
	memset(((char*) &s->__fpu_xmm0) + 128, 0, 128);
}

void thread_state_to_mcontext(const x86_thread_state32_t* s, struct linux_gregset* regs)
{
	regs->eax = s->__eax;
	regs->ebx = s->__ebx;
	regs->ecx = s->__ecx;
	regs->edx = s->__edx;
	regs->edi = s->__edi;
	regs->esi = s->__esi;
	regs->ebp = s->__ebp;
	regs->esp = s->__esp;
	regs->ss = s->__ss;
	regs->efl = s->__eflags;
	regs->eip = s->__eip;
	regs->cs = s->__cs;
	regs->ds = s->__ds;
	regs->es = s->__es;
	regs->fs = s->__fs;
	regs->gs = s->__gs;
}

void float_state_to_mcontext(const x86_float_state32_t* s, linux_fpregset_t fx)
{
	fx->cw = *((uint16_t*)&s->__fpu_fcw);
	fx->sw = *((uint16_t*)&s->__fpu_fsw);
	fx->tag = s->__fpu_ftw;
	fx->ipoff = s->__fpu_ip;
	fx->cssel = s->__fpu_cs;
	fx->dataoff = s->__fpu_dp;
	fx->datasel = s->__fpu_ds;
	fx->mxcsr = s->__fpu_mxcsr;

	memcpy(fx->_st, &s->__fpu_stmm0, 128);
	memcpy(fx->_xmm, &s->__fpu_xmm0, 128);
}
#endif

#if defined(__aarch64__) || defined(__arm64__)

#define FPSIMD_MAGIC 0x46508001

void mcontext_to_thread_state(const struct linux_gregset* regs, arm_thread_state64_t* s)
{
	for (int i = 0; i < 29; i++)
		s->__x[i] = regs->regs[i];
	s->__fp = regs->regs[29];
	s->__lr = regs->regs[30];
	s->__sp = regs->sp;
	s->__pc = regs->pc;
	s->__cpsr = (uint32_t)regs->pstate;
	s->__pad = 0;
}

void thread_state_to_mcontext(const arm_thread_state64_t* s, struct linux_gregset* regs)
{
	for (int i = 0; i < 29; i++)
		regs->regs[i] = s->__x[i];
	regs->regs[29] = s->__fp;
	regs->regs[30] = s->__lr;
	regs->sp = s->__sp;
	regs->pc = s->__pc;
	regs->pstate = s->__cpsr;
}

/* Bound for scanning the __reserved area; arbitrarily malformed `size`
 * fields must not cause us to walk off the end. Linux fixed __reserved at
 * 4096 bytes for ARM64. */
#define LINUX_MCONTEXT_RESERVED_SIZE 4096

void mcontext_to_float_state(const unsigned char* reserved, arm_neon_state64_t_linux* s)
{
	const unsigned char* end = reserved + LINUX_MCONTEXT_RESERVED_SIZE;
	const struct linux_aarch64_ctx* ctx = (const struct linux_aarch64_ctx*)reserved;
	while ((const unsigned char*)ctx + sizeof(*ctx) <= end && ctx->magic != 0) {
		if (ctx->magic == FPSIMD_MAGIC) {
			const struct linux_fpsimd_context* fpsimd = (const struct linux_fpsimd_context*)ctx;
			if ((const unsigned char*)fpsimd + sizeof(*fpsimd) > end)
				break;
			s->fpsr = fpsimd->fpsr;
			s->fpcr = fpsimd->fpcr;
			memcpy(s->vregs, fpsimd->vregs, sizeof(s->vregs));
			return;
		}
		if (ctx->size == 0)
			break;
		ctx = (const struct linux_aarch64_ctx*)((const unsigned char*)ctx + ctx->size);
	}
	memset(s, 0, sizeof(*s));
}

void float_state_to_mcontext(const arm_neon_state64_t_linux* s, unsigned char* reserved)
{
	unsigned char* end = reserved + LINUX_MCONTEXT_RESERVED_SIZE;
	struct linux_aarch64_ctx* ctx = (struct linux_aarch64_ctx*)reserved;
	while ((unsigned char*)ctx + sizeof(*ctx) <= end && ctx->magic != 0) {
		if (ctx->magic == FPSIMD_MAGIC) {
			struct linux_fpsimd_context* fpsimd = (struct linux_fpsimd_context*)ctx;
			if ((unsigned char*)fpsimd + sizeof(*fpsimd) > end)
				return;
			fpsimd->fpsr = s->fpsr;
			fpsimd->fpcr = s->fpcr;
			memcpy(fpsimd->vregs, s->vregs, sizeof(s->vregs));
			return;
		}
		if (ctx->size == 0)
			return;
		ctx = (struct linux_aarch64_ctx*)((unsigned char*)ctx + ctx->size);
	}
}

#endif /* __aarch64__ || __arm64__ */

void sigexc_thread_setup(void)
{
	struct bsd_stack newstack = {
		.ss_size = default_sigaltstack_size,
		.ss_flags = 0
	};

#if SIGALTSTACK_GUARD
	// mprotect protects a whole page, so the reserve must be a page: a smaller one
	// leaves the guard overlapping the stack it sits below.
	newstack.ss_sp = (void*) sys_mmap(NULL, newstack.ss_size + vm_page_size, PROT_READ | PROT_WRITE,
			MAP_ANON | MAP_PRIVATE, -1, 0);

	// -1..-4095 is the Linux syscall error range, not an address.
	if (((intptr_t)newstack.ss_sp) < 0 && ((intptr_t)newstack.ss_sp) >= -4095) {
		__simple_kprintf("sigexc: could not allocate a thread alt stack\n");
		return;
	}

	if (sys_mprotect(newstack.ss_sp, vm_page_size, PROT_NONE) < 0) {
		__simple_kprintf("sigexc: could not protect the thread alt stack guard page\n");
	}

	newstack.ss_sp = (char*)newstack.ss_sp + vm_page_size;
#else
	newstack.ss_sp = (void*) sys_mmap(NULL, newstack.ss_size, PROT_READ | PROT_WRITE,
			MAP_ANON | MAP_PRIVATE, -1, 0);
#endif
	sys_sigaltstack(&newstack, NULL);
}

void sigexc_thread_exit(void)
{
	struct bsd_stack oldstack;
	sys_sigaltstack(NULL, &oldstack);

#if SIGALTSTACK_GUARD
	sys_munmap((char*)oldstack.ss_sp - vm_page_size, oldstack.ss_size + vm_page_size);
#else
	sys_munmap(oldstack.ss_sp, oldstack.ss_size);
#endif
}

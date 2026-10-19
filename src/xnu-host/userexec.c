/* Host machine return for real XNU threads. Entered on a reset kernel stack
 * by usermode_cswitch.S, never a recursive C syscall loop or Darwin shim. */
#include <host/userexec.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/thread.h>
#include <kern/task.h>
#include <kern/ast.h>
#include <kern/cpu_data.h>
#include <arm64/machine_machdep.h>
#include <arm64/proc_reg.h>
#include <arm/cpu_number.h>
#include <vm/vm_map.h>
#include <arm/commpage/commpage.h>
#include <machine/machine_routines.h>
#include <string.h>

extern void sleh_synchronous(arm_context_t *, uint32_t, vm_offset_t);
extern void thread_exception_return(void) __attribute__((noreturn));

static arm_context_t *
user_context(thread_t thread)
{
    assert(get_threadtask(thread) != kernel_task && thread_is_64bit_addr(thread));
    arm_context_t *context = thread->machine.contextData;
    assert(context && thread->machine.upcb == &context->ss && thread->machine.uNeon == &context->ns);
    assert(is_saved_state64(&context->ss) && is_neon_saved_state64(&context->ns));
    assert(PSR64_IS_USER(get_saved_state_cpsr(&context->ss)));
    assert(SPSR_INTERRUPTS_ENABLED(get_saved_state_cpsr(&context->ss)));
    return context;
}

static void
load_user_state(thread_t thread, arm_context_t *context, struct xnu_host_guest_state *state)
{
    arm_saved_state64_t *ss = saved_state64(&context->ss);
    arm_neon_saved_state64_t *ns = neon_state64(&context->ns);
    memcpy(state->x, ss->x, sizeof(ss->x));
    state->x[29] = ss->fp;
    state->x[30] = ss->lr;
    state->sp = ss->sp;
    state->pc = ss->pc;
    state->pstate = ss->cpsr;
    memcpy(state->q, ns->v.q, sizeof(state->q));
    state->fpcr = ns->fpcr;
    state->fpsr = ns->fpsr;
    /* Match native ARM return: Darwin TSD is TPIDRRO; TPIDR_EL0 is CPU ID.
     * Neither changes the Linux host's TLS registers. */
    state->tpidrro = thread->machine.cthread_self;
    state->tpidr = cpu_number();
#if __ARM_KERNEL_PROTECT__
    state->x[18] = 0;
#else
    if (!(thread->machine.arm_machine_flags & ARM_MACHINE_THREAD_PRESERVE_X18))
        state->x[18] = 0;
#endif
}

static void
save_user_state(arm_context_t *context, const struct xnu_host_guest_state *state)
{
    arm_saved_state64_t *ss = saved_state64(&context->ss);
    arm_neon_saved_state64_t *ns = neon_state64(&context->ns);
    memcpy(ss->x, state->x, sizeof(ss->x));
    ss->fp = state->x[29];
    ss->lr = state->x[30];
    ss->sp = state->sp;
    set_saved_state_pc(&context->ss, state->pc);
    set_saved_state_cpsr(&context->ss, state->pstate);
    memcpy(ns->v.q, state->q, sizeof(state->q));
    ns->fpcr = state->fpcr;
    ns->fpsr = state->fpsr;
}

__attribute__((noreturn)) void
xnu_usermode_user_return(void)
{
    bool check_pfz = false;
    for (;;) {
        thread_t thread = current_thread();
        assert(get_preemption_level() == 0 && !ml_at_interrupt_context());
        thread->iotier_override = -1; /* Original return_to_user reset. */
        arm_context_t *context = user_context(thread);
        (void)ml_set_interrupts_enabled(FALSE);
        xnu_usermode_timer_poll(&context->ss);
        ast_propagate(thread);
        while (*ast_pending()) {
#if CONFIG_ARM_PFZ
            if (check_pfz && commpage_is_in_pfz64(get_saved_state_pc(&context->ss))) {
                saved_state64(&context->ss)->x[15] = 1;
                break; /* Original commpage code returns to take deferred ASTs. */
            }
#else
            (void)check_pfz;
#endif
            check_pfz = false;
            ast_taken_user(); /* May block/continue through machine return. */
            (void)ml_set_interrupts_enabled(FALSE);
            context = user_context(thread);
        }
        struct xnu_host_guest_state state;
        struct xnu_host_guest_event event;
        load_user_state(thread, context, &state);
        unsigned long long start_pc = state.pc;
        struct xnu_host_guest *guest = xnu_usermode_pmap_engine(thread->map->pmap);
        xnu_usermode_user_interrupts_enable();
        int error = xnu_host_guest_run(guest, &state, 10000, &event);
        (void)ml_set_interrupts_enabled(FALSE);
        if (error)
            panic("host guest CPU transport uc_err=%d start_pc=%#llx", error, start_pc);
        save_user_state(context, &state);
        /* Clock interrupts receive actual stopped user state, not a synthetic
         * kernel safe-point PC. Original FIQ/queues/ASTs remain in charge. */
        xnu_usermode_timer_poll(&context->ss);
        if (event.reason == XNU_HOST_GUEST_SLICE) {
            check_pfz = true;
            continue;
        }
        if (!event.syndrome_valid)
            panic("host guest CPU unsupported exception=%u reason=%u pc=%#llx",
                event.exception, event.reason, state.pc);
        set_saved_state_esr(&context->ss, event.esr);
        set_saved_state_far(&context->ss, event.address);
        /* Original SLEH invokes real BSD/Mach dispatch or vm_fault. Its
         * noreturn paths return through the assembly stack-reset entry. */
        sleh_synchronous(context, event.esr, event.address);
        thread_exception_return();
    }
}

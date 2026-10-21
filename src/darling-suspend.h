/* Native ARM64 counterpart of Darling's sigrt_handler suspension path in
 * libsystem_kernel/emulation/src/linux_premigration/signal/sigexc.c.
 * Reuse the real interrupt/thread_suspended RPC and XNU suspend counts. No
 * guest libc/TLS, fake success, process-wide SIGSTOP or signal suppression. */
#pragma once
#ifdef ARLINUX_DARLING
#include <asm/sigcontext.h>
struct native_arm_thread_state64 {
    uint64_t x[29],fp,lr,sp,pc;
    uint32_t cpsr,padding;
};
struct native_arm_neon_state64 {
    _Alignas(16) unsigned char vectors[32][16];
    uint32_t fpsr,fpcr;
    uint64_t padding;
};
_Static_assert(sizeof(struct native_arm_thread_state64)==272,"ARM_THREAD_STATE64 ABI");
_Static_assert(sizeof(struct native_arm_neon_state64)==528,"ARM_NEON_STATE64 ABI");
static struct fpsimd_context *darling_signal_fpsimd(ucontext_t *context)
{
    unsigned char *cursor=context->uc_mcontext.__reserved;
    unsigned char *end=cursor+sizeof(context->uc_mcontext.__reserved);
    while((size_t)(end-cursor)>=sizeof(struct _aarch64_ctx)) {
        struct _aarch64_ctx *record=(void *)cursor;
        if(!record->magic)return NULL;
        if(record->size<sizeof(*record) || record->size>(size_t)(end-cursor))return NULL;
        if(record->magic==FPSIMD_MAGIC)
            return record->size>=sizeof(struct fpsimd_context) ? (void *)record : NULL;
        cursor+=record->size;
    }
    return NULL;
}
static void darling_suspend(int signal,siginfo_t *info,void *opaque)
{
    (void)info;
    if(signal!=LINUX_SIGRTMIN)darling_abort("unexpected suspension signal");
    ucontext_t *context=opaque;
    struct fpsimd_context *floating=darling_signal_fpsimd(context);
    if(!floating)darling_abort("suspension lacks actual ARM64 FP context");
    struct native_arm_thread_state64 state;
    struct native_arm_neon_state64 neon;
    for(unsigned int i=0;i<29;i++)state.x[i]=context->uc_mcontext.regs[i];
    state.fp=context->uc_mcontext.regs[29];state.lr=context->uc_mcontext.regs[30];
    state.sp=context->uc_mcontext.sp;state.pc=context->uc_mcontext.pc;
    state.cpsr=context->uc_mcontext.pstate;state.padding=0;
    darling_copy(neon.vectors,floating->vregs,sizeof(neon.vectors));
    neon.fpsr=floating->fpsr;neon.fpcr=floating->fpcr;neon.padding=0;
    static unsigned int observations;
    int observe=__atomic_fetch_add(&observations,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT;
    if(observe)trace_record("DARLING_THREAD_SUSPEND_BEGIN pc=0x",state.pc,state.sp,0);
    int status=dserver_rpc_interrupt_enter();
    if(status)darling_abort("suspend interrupt enter");
    status=dserver_rpc_thread_suspended((uintptr_t)&state,(uintptr_t)&neon);
    if(status)darling_abort("wait for actual thread resume");
    for(unsigned int i=0;i<29;i++)context->uc_mcontext.regs[i]=state.x[i];
    context->uc_mcontext.regs[29]=state.fp;context->uc_mcontext.regs[30]=state.lr;
    context->uc_mcontext.sp=state.sp;context->uc_mcontext.pc=state.pc;
    context->uc_mcontext.pstate=state.cpsr;
    darling_copy(floating->vregs,neon.vectors,sizeof(neon.vectors));
    floating->fpsr=neon.fpsr;floating->fpcr=neon.fpcr;
    status=dserver_rpc_interrupt_exit();
    if(status)darling_abort("suspend interrupt exit");
    if(observe)trace_record("DARLING_THREAD_SUSPEND_RESUMED pc=0x",state.pc,state.sp,0);
}
#endif

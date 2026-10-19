/* Native Linux threads entering the registered, real Darwin pthread callback.
 * Initial scope: custom user stacks, no suspended/workqueue/QoS scheduling.
 * Registration feature result 0 declares the legacy basic ABI, not unavailable
 * modern kernel features. No host libc/TLS operation occurs after guest entry. */
#pragma once
#include <linux/futex.h>
#define NATIVE_THREADS 32
struct native_thread {
    uint64_t runtime_top,guest_sp,pthread,port,function,argument,flags;
    int tid,state; /* tid offset 56 is used by the clone trampoline. */
    uint64_t runtime_memory,signal_memory,refs,generation,initial_signal_mask;
};
_Static_assert(offsetof(struct native_thread,tid)==56,"native clone ABI");
static struct native_thread native_threads[NATIVE_THREADS];
static uint64_t thread_start_callback;
static uint32_t registered_pthread_size,registered_tsd,registered_thread_slot;
extern long darwin_clone_thread(struct native_thread *);
extern char darwin_clone_return[];
extern __attribute__((noreturn)) void darwin_thread_enter(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static struct native_thread *native_current_thread(void)
{
    int tid=RAW(SYS_gettid,0,0,0,0,0,0);
    for (unsigned int i=0;i<NATIVE_THREADS;i++)
        if (__atomic_load_n(&native_threads[i].state,__ATOMIC_ACQUIRE)>=2 && native_threads[i].tid==tid)
            return native_threads+i;
    return NULL;
}
static struct native_thread *native_thread_port(uint32_t name)
{
    for (unsigned int i=0;i<NATIVE_THREADS;i++)
        if (__atomic_load_n(&native_threads[i].state,__ATOMIC_ACQUIRE)>=2 && native_threads[i].port==name)
            return native_threads+i;
    return NULL;
}
/* Runtime stacks are reclaimed only after Linux clears child_tid at death. */
static void native_reap_threads(void)
{
    for (unsigned int i=1;i<NATIVE_THREADS;i++) {
        struct native_thread *thread=native_threads+i;
        if (__atomic_load_n(&thread->state,__ATOMIC_ACQUIRE)!=3 ||
            __atomic_load_n(&thread->tid,__ATOMIC_ACQUIRE)!=0 || thread->refs) continue;
        int expected=3;
        if (!__atomic_compare_exchange_n(&thread->state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        RAW(SYS_munmap,thread->runtime_memory,D_THREAD_RUNTIME_STACK_SIZE,0,0,0,0);
        RAW(SYS_munmap,thread->signal_memory,D_SIGNAL_STACK_SIZE,0,0,0,0);
        __atomic_store_n(&thread->state,0,__ATOMIC_RELEASE);
    }
}
__attribute__((noreturn,visibility("hidden")))
void darwin_thread_boot(struct native_thread *thread)
{
    __asm__ volatile("msr tpidr_el0,xzr" ::: "memory");
    stack_t alternate={.ss_sp=(void *)thread->signal_memory,.ss_size=D_SIGNAL_STACK_SIZE};
    if (RAW(SYS_sigaltstack,&alternate,0,0,0,0,0)) {
        RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0); __builtin_unreachable();
    }
    /* clone ran inside SIGSYS: its temporary handler mask must not leak into
     * the new guest thread. Restore the parent's pre-trap Linux signal mask. */
    if (RAW(SYS_rt_sigprocmask,SIG_SETMASK,&thread->initial_signal_mask,0,8,0,0)) {
        RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0); __builtin_unreachable();
    }
    uint64_t tls=thread->pthread+registered_tsd;
    if (registered_tsd) __asm__ volatile("msr tpidr_el0,%0" :: "r"(tls) : "memory");
    darwin_thread_enter(thread->guest_sp,thread_start_callback,thread->pthread,thread->port,
                        thread->function,thread->argument,thread->guest_sp,thread->flags);
}
static long native_thread_register(const unsigned long long *r)
{
    /* Packed _pthread_registration_data, versioned by byte count. Offsets are
     * read from userland, never inferred from a particular pthread structure. */
    if (thread_start_callback) return -EINVAL;
    if (!r[0] || (r[0]&3) || r[0]>=D_GUEST_ADDRESS_LIMIT || r[2]<8 || r[2]>D_PTHREAD_MAX_SIZE ||
        !r[3] || r[4]<sizeof(struct darwin_pthread_registration) || r[4]>128) return -EINVAL;
    unsigned char wire[128];
    if (guest_copy(wire,r[3],r[4],0)) return -EFAULT;
    struct darwin_pthread_registration data;
    __builtin_memcpy(&data,wire,sizeof(data));
    uint32_t tsd=data.tsd_offset,slot=data.mach_thread_self_offset;
    if (data.version!=r[4] || tsd>r[2]-sizeof(uint64_t) ||
        slot>r[2]-tsd-sizeof(uint64_t)) return -EINVAL;
    registered_pthread_size=r[2]; registered_tsd=tsd; registered_thread_slot=slot;
    /* Keep unspecified QoS/default mutex policy. No extra scheduler features. */
    thread_start_callback=r[0];
    return 0;
}
static long native_thread_create(const unsigned long long *r,const sigset_t *original_mask)
{
    if (!thread_start_callback) return -EINVAL;
    uint32_t flags=r[4];
    /* Only ordinary inherited scheduling / default QoS is implemented. */
    if ((flags&D_PTHREAD_START_QOS) &&
        (((flags>>D_PTHREAD_QOS_SHIFT)&D_PTHREAD_QOS_MASK)!=D_PTHREAD_DEFAULT_QOS_BITS || (flags&D_PTHREAD_RELATIVE_PRIORITY_MASK)!=D_PTHREAD_ZERO_RELATIVE_PRIORITY)) return -ENOTSUP;
    if (!(flags&D_PTHREAD_START_CUSTOM) || (flags&~(D_PTHREAD_START_CUSTOM|D_PTHREAD_START_QOS|D_PTHREAD_PRIORITY_MASK)) ||
        (!(flags&D_PTHREAD_START_QOS) && (flags&D_PTHREAD_PRIORITY_MASK)) || !r[2] || (r[2]&15) || !r[3] || r[3]>D_GUEST_COMMPAGE-registered_pthread_size) return -ENOTSUP;
    uint64_t value;
    if (guest_copy(&value,r[2]-16,8,0) || guest_copy(&value,r[3],8,0)) return -EFAULT;
    native_reap_threads();
    for (unsigned int i=1;i<NATIVE_THREADS;i++) {
        struct native_thread *thread=native_threads+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&thread->state,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        long memory=RAW(SYS_mmap,0,D_THREAD_RUNTIME_STACK_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if ((unsigned long)memory>=(unsigned long)-4095) { thread->state=0; return memory; }
        long alternate=RAW(SYS_mmap,0,D_SIGNAL_STACK_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if ((unsigned long)alternate>=(unsigned long)-4095) {
            RAW(SYS_munmap,memory,D_THREAD_RUNTIME_STACK_SIZE,0,0,0,0); thread->state=0; return alternate;
        }
        thread->runtime_memory=memory; thread->runtime_top=memory+D_THREAD_RUNTIME_STACK_SIZE;
        thread->signal_memory=alternate; thread->guest_sp=r[2]; thread->pthread=r[3];
        thread->function=r[0]; thread->argument=r[1]; thread->flags=r[4];
        __builtin_memcpy(&thread->initial_signal_mask,original_mask,8);
        if (registered_tsd) thread->flags|=D_PTHREAD_START_TSD_SET;
        thread->generation=(thread->generation+1)&D_PORT_GENERATION_MASK;
        if (!thread->generation) thread->generation=1;
        thread->port=((D_THREAD_PORT_INDEX_BASE+i)<<D_PORT_NAME_INDEX_SHIFT)|thread->generation; thread->refs=1; thread->tid=-1;
        if (registered_tsd && guest_copy(&thread->port,r[3]+registered_tsd+registered_thread_slot,8,1)) {
            RAW(SYS_munmap,memory,D_THREAD_RUNTIME_STACK_SIZE,0,0,0,0); RAW(SYS_munmap,alternate,D_SIGNAL_STACK_SIZE,0,0,0,0);
            thread->state=0; return -EFAULT;
        }
        __atomic_store_n(&thread->state,2,__ATOMIC_RELEASE);
        long tid=darwin_clone_thread(thread);
        if (tid<0) {
            RAW(SYS_munmap,memory,D_THREAD_RUNTIME_STACK_SIZE,0,0,0,0); RAW(SYS_munmap,alternate,D_SIGNAL_STACK_SIZE,0,0,0,0);
            thread->state=0; return tid;
        }
        return r[3];
    }
    return -EAGAIN;
}
static long native_thread_terminate(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    if (!thread || thread==native_threads || (r[0]&(D_GUEST_PAGE_SIZE-1)) || (r[1]&(D_GUEST_PAGE_SIZE-1)) || r[1]>D_VM_MAX_ALLOCATION) return -EINVAL;
    if (r[3]) return -ENOTSUP; /* custom-stack join semaphore requires separate path */
    if (r[1] && RAW(SYS_munmap,r[0],r[1],0,0,0,0)) return -EINVAL;
    __atomic_store_n(&thread->state,3,__ATOMIC_RELEASE);
    RAW(SYS_exit,0,0,0,0,0,0); /* thread exit; never exit_group */
    __builtin_unreachable();
}
static long native_ulock(const unsigned long long *r)
{
    unsigned int operation=r[0]&D_UL_OPERATION_MASK;
    if ((operation!=D_UL_COMPARE_AND_WAIT && operation!=D_UL_UNFAIR_LOCK) || (r[1]&3)) return -EINVAL;
    if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_ULOCK_WAIT) {
        if (r[0]&~(unsigned long)(D_UL_NO_ERRNO|D_UL_WAIT_WORKQ_DATA_CONTENTION|D_UL_WAIT_CANCEL_POINT|D_UL_OPERATION_MASK)) return -ENOTSUP;
        struct timespec timeout={r[3]/1000000,(r[3]%1000000)*1000};
        long result=RAW(SYS_futex,r[1],FUTEX_WAIT_PRIVATE,(uint32_t)r[2],r[3] ? &timeout : NULL,0,0);
        return result==-EAGAIN ? 0 : result; /* changed value is a successful retry */
    }
    if (r[0]&~(unsigned long)(D_UL_NO_ERRNO|D_UL_WAKE_ALLOW_NON_OWNER|D_UL_WAKE_ALL|D_UL_OPERATION_MASK)) return -ENOTSUP;
    long result=RAW(SYS_futex,r[1],FUTEX_WAKE_PRIVATE,(r[0]&D_UL_WAKE_ALL) ? INT32_MAX : 1,0,0,0);
    return result<0 ? result : 0;
}

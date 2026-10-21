/* Native Linux threads entering the registered, real Darwin pthread callback.
 * Custom stacks and ordinary workqueue callbacks use real Linux threads.
 * QoS is best-effort Linux niceness; event workloops have bound Linux workers.
 * No host libc/TLS operation occurs after guest entry. */
#pragma once
#include <linux/futex.h>
#define NATIVE_THREADS D_NATIVE_GUEST_THREAD_CAPACITY
struct native_thread {
    uint64_t runtime_top,guest_sp,pthread,port,function,argument,flags;
    int tid,state; /* tid offset 56 is used by the clone trampoline. */
    uint64_t runtime_memory,signal_memory,refs,generation,initial_signal_mask;
    uint64_t workqueue,stack_low;
    uint32_t priority;
    int launch_state;
    int event_queue; uint64_t workloop_id; uint32_t special_reply_port;
};
_Static_assert(offsetof(struct native_thread,tid)==56,"native clone ABI");
static struct native_thread native_threads[NATIVE_THREADS];
static uint64_t thread_start_callback,workqueue_start_callback;
static int workqueue_opened,workqueue_registered,workqueue_has_kevents;
static __attribute__((noreturn)) void native_event_worker_start(struct native_thread *);
static long native_event_worker_return(const unsigned long long *);
static void native_event_owner_exit(uint64_t);
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
    /* Parent configures scheduling before any guest callback can run. Failed
     * creation exits only this host thread, preserving the caller's errno. */
    while (!__atomic_load_n(&thread->launch_state,__ATOMIC_ACQUIRE))
        RAW(SYS_futex,&thread->launch_state,FUTEX_WAIT_PRIVATE,0,0,0,0);
    if (thread->launch_state<0) {
        RAW(SYS_exit,0,0,0,0,0,0); __builtin_unreachable();
    }
#ifdef ARLINUX_DARLING
    if (darling_active) {
        thread->port=darling_thread_self();
        if (registered_tsd && guest_copy(&thread->port,thread->pthread+registered_tsd+registered_thread_slot,8,1))
            darling_abort("publish actual thread port");
    }
#endif
    if (thread->workqueue>=2) native_event_worker_start(thread);
    if (thread->workqueue) {
        darwin_thread_enter(thread->guest_sp,workqueue_start_callback,thread->pthread,thread->port,
                            thread->stack_low,0,thread->flags,0);
    }
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
    if (!r[1] || (r[1]&3) || r[1]>=D_GUEST_ADDRESS_LIMIT) return -EINVAL;
    thread_start_callback=r[0]; workqueue_start_callback=r[1];
    return D_PTHREAD_FEATURE_DISPATCH|D_PTHREAD_FEATURE_FINEPRIO|D_PTHREAD_FEATURE_CTL|
           D_PTHREAD_FEATURE_SETSELF|D_PTHREAD_FEATURE_MAINTENANCE|D_PTHREAD_FEATURE_DEFAULT|
           D_PTHREAD_FEATURE_KEVENT|D_PTHREAD_FEATURE_WORKLOOP;
}
static long native_thread_create_impl(const unsigned long long *r,const sigset_t *original_mask,
                                      int workqueue,uint64_t stack_low)
{
    if (!thread_start_callback) return -EINVAL;
    uint32_t flags=r[4];
    unsigned int qos_bits=(flags>>D_PTHREAD_QOS_SHIFT)&D_PTHREAD_QOS_MASK;
    if (!workqueue && (flags&D_PTHREAD_START_QOS) &&
        (!qos_bits || qos_bits>32 || (qos_bits&(qos_bits-1)) || (flags&255)<240)) return -ENOTSUP;
    if (!workqueue && (!(flags&D_PTHREAD_START_CUSTOM) || (flags&~(D_PTHREAD_START_CUSTOM|D_PTHREAD_START_QOS|D_PTHREAD_PRIORITY_MASK)) ||
        (!(flags&D_PTHREAD_START_QOS) && (flags&D_PTHREAD_PRIORITY_MASK)) || !r[2] || (r[2]&15) || !r[3] || r[3]>D_GUEST_COMMPAGE-registered_pthread_size)) return -ENOTSUP;
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
        thread->launch_state=0;
        thread->runtime_memory=memory; thread->runtime_top=memory+D_THREAD_RUNTIME_STACK_SIZE;
        thread->signal_memory=alternate; thread->guest_sp=r[2]; thread->pthread=r[3];
        thread->function=r[0]; thread->argument=r[1]; thread->flags=r[4];
        thread->workqueue=workqueue; thread->stack_low=stack_low;
        thread->event_queue=workqueue>=2 ? (int)r[0] : -1;
        thread->workloop_id=workqueue>=2 ? r[1] : 0;
        thread->special_reply_port=0;
        thread->priority=workqueue ? ((1U<<((flags&0xff)-1+8))|255) : (flags&D_PTHREAD_PRIORITY_MASK);
        __builtin_memcpy(&thread->initial_signal_mask,original_mask,8);
        if (registered_tsd) thread->flags|=workqueue ? D_WQ_TSD_SET : D_PTHREAD_START_TSD_SET;
        thread->generation=thread->generation ?
            (thread->generation+D_PORT_GENERATION_STEP)&D_PORT_GENERATION_MASK : D_PORT_GENERATION_TAG;
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
        if (workqueue || (flags&D_PTHREAD_START_QOS)) {
            unsigned int qos=workqueue ? (flags&255) : (unsigned int)__builtin_ctz(qos_bits)+1;
            int relative=workqueue ? 0 : (int)(flags&255)+1-256;
            int nice=(qos>=4 ? 0 : 20-(int)qos*4)-relative;
            if (nice>19) nice=19;
            long error=RAW(SYS_setpriority,0,tid,nice,0,0,0);
            if (error) {
                thread->refs=0;
                __atomic_store_n(&thread->state,3,__ATOMIC_RELEASE);
                __atomic_store_n(&thread->launch_state,-1,__ATOMIC_RELEASE);
                RAW(SYS_futex,&thread->launch_state,FUTEX_WAKE_PRIVATE,1,0,0,0);
                return error;
            }
        }
        __atomic_store_n(&thread->launch_state,1,__ATOMIC_RELEASE);
        RAW(SYS_futex,&thread->launch_state,FUTEX_WAKE_PRIVATE,1,0,0,0);
        return r[3];
    }
    return -EAGAIN;
}
static long native_thread_create(const unsigned long long *r,const sigset_t *original_mask)
{
    return native_thread_create_impl(r,original_mask,0,0);
}
static long native_thread_terminate(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    if (!thread || thread==native_threads || (r[0]&(D_GUEST_PAGE_SIZE-1)) || (r[1]&(D_GUEST_PAGE_SIZE-1)) || r[1]>D_VM_MAX_ALLOCATION) return -EINVAL;
    if (r[3]) return -ENOTSUP; /* custom-stack join semaphore requires separate path */
    if (r[1] && RAW(SYS_munmap,r[0],r[1],0,0,0,0)) return -EINVAL;
    native_event_owner_exit(thread->port);
    if (thread->workqueue) __atomic_store_n(&thread->refs,0,__ATOMIC_RELEASE);
    __atomic_store_n(&thread->state,3,__ATOMIC_RELEASE);
#ifdef ARLINUX_DARLING
    if (darling_active) { darling_thread_close(); thread->refs=0; }
#endif
    RAW(SYS_exit,0,0,0,0,0,0); /* thread exit; never exit_group */
    __builtin_unreachable();
}
/* XNU ulock_wait returns the remaining waiter count after cleanup. libplatform
 * uses a positive result to preserve its lock word's waiter bit. Returning
 * zero after every Linux futex wake loses later wakeups under contention.
 * At most one wait per native guest thread can be active at a time. */
static struct native_ulock_queue {
    uint64_t address; unsigned int operation; int waiters;
} native_ulock_queues[NATIVE_THREADS];
static int native_ulock_guard;
static void native_ulock_lock(void)
{
    while (__atomic_exchange_n(&native_ulock_guard,1,__ATOMIC_ACQUIRE))
        RAW(SYS_futex,&native_ulock_guard,FUTEX_WAIT_PRIVATE,1,0,0,0);
}
static void native_ulock_unlock(void)
{
    __atomic_store_n(&native_ulock_guard,0,__ATOMIC_RELEASE);
    RAW(SYS_futex,&native_ulock_guard,FUTEX_WAKE_PRIVATE,1,0,0,0);
}
static long native_ulock(const unsigned long long *r)
{
    unsigned int operation=r[0]&D_UL_OPERATION_MASK;
    if ((operation!=D_UL_COMPARE_AND_WAIT && operation!=D_UL_UNFAIR_LOCK) || (r[1]&3)) return -EINVAL;
    if (!r[1]) return -EFAULT;
    int wait=r[D_ARM64_SYSCALL_REGISTER]==D_SYS_ULOCK_WAIT;
    /* XNU's adaptive spin is optional before the same blocking wait. All
     * guest threads share one actual CPU affinity: while this waiter runs,
     * its different owner cannot also be on-core. Use the real futex wait
     * directly, preserving comparison, wakeups, timeout and waiter counts. */
    unsigned long flags=wait ? D_UL_WAIT_WORKQ_DATA_CONTENTION|D_UL_WAIT_CANCEL_POINT|D_UL_WAIT_ADAPTIVE_SPIN :
        D_UL_WAKE_ALLOW_NON_OWNER|D_UL_WAKE_ALL;
    if (r[0]&~(unsigned long)(D_UL_NO_ERRNO|D_UL_OPERATION_MASK|flags)) return -ENOTSUP;
    native_ulock_lock();
    struct native_ulock_queue *queue=NULL,*empty=NULL;
    for (unsigned int i=0;i<NATIVE_THREADS;i++) {
        if (native_ulock_queues[i].address==r[1]) { queue=native_ulock_queues+i; break; }
        if (!native_ulock_queues[i].address && !empty) empty=native_ulock_queues+i;
    }
    if (queue && queue->operation!=operation) { native_ulock_unlock(); return -EDOM; }
    if (!wait) {
        /* A wake holds the registry interlock through the nonblocking futex
         * operation, so the queue cannot be retired/rebound beneath it. */
        long result=queue ? RAW(SYS_futex,r[1],FUTEX_WAKE_PRIVATE,
                               (r[0]&D_UL_WAKE_ALL) ? INT32_MAX : 1,0,0,0) : -ENOENT;
        native_ulock_unlock();
        return result<0 ? result : 0;
    }
    if (!queue) {
        if (!empty) { native_ulock_unlock(); return -ENOMEM; }
        queue=empty; queue->address=r[1]; queue->operation=operation; queue->waiters=0;
    }
    if (queue->waiters==INT32_MAX) { native_ulock_unlock(); return -EOVERFLOW; }
    queue->waiters++;
    native_ulock_unlock();
    struct timespec timeout={r[3]/1000000,(r[3]%1000000)*1000};
    long result=RAW(SYS_futex,r[1],FUTEX_WAIT_PRIVATE,(uint32_t)r[2],r[3] ? &timeout : NULL,0,0);
    native_ulock_lock();
    int remaining=--queue->waiters;
    if (!remaining) { queue->address=0; queue->operation=0; }
    native_ulock_unlock();
    /* Linux checks value/queues atomically: an unlock before parking produces
     * EAGAIN. XNU still performs waiter cleanup and returns the live count. */
    return !result || result==-EAGAIN ? remaining : result;
}

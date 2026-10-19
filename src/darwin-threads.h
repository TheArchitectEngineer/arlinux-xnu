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
    uint64_t runtime_memory,signal_memory,refs,generation;
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
        RAW(SYS_munmap,thread->runtime_memory,65536,0,0,0,0);
        RAW(SYS_munmap,thread->signal_memory,131072,0,0,0,0);
        __atomic_store_n(&thread->state,0,__ATOMIC_RELEASE);
    }
}
__attribute__((noreturn,visibility("hidden")))
void darwin_thread_boot(struct native_thread *thread)
{
    __asm__ volatile("msr tpidr_el0,xzr" ::: "memory");
    stack_t alternate={.ss_sp=(void *)thread->signal_memory,.ss_size=131072};
    if (RAW(SYS_sigaltstack,&alternate,0,0,0,0,0)) {
        RAW(SYS_exit_group,125,0,0,0,0,0); __builtin_unreachable();
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
    if (thread_start_callback) return -22;
    if (!r[0] || (r[0]&3) || r[0]>=0x1000000000ULL || r[2]<8 || r[2]>65536 ||
        !r[3] || r[4]<48 || r[4]>128) return -22;
    unsigned char data[128];
    if (guest_copy(data,r[3],r[4],0)) return -14;
    uint64_t version; uint32_t tsd,slot;
    __builtin_memcpy(&version,data,8); __builtin_memcpy(&tsd,data+24,4);
    __builtin_memcpy(&slot,data+32,4);
    if (version!=r[4] || tsd>r[2]-8 || slot>r[2]-tsd-8) return -22;
    registered_pthread_size=r[2]; registered_tsd=tsd; registered_thread_slot=slot;
    /* Keep unspecified QoS/default mutex policy. No extra scheduler features. */
    thread_start_callback=r[0];
    return 0;
}
static long native_thread_create(const unsigned long long *r)
{
    if (!thread_start_callback) return -22;
    uint32_t flags=r[4];
    /* Only ordinary inherited scheduling / default QoS is implemented. */
    if ((flags&0x08000000U) &&
        (((flags>>8)&0xffff)!=8 || (flags&255)!=255)) return -95;
    if (!(flags&0x01000000U) || (flags&~0x09ffffffU) ||
        (!(flags&0x08000000U) && (flags&0xffffff)) || !r[2] || (r[2]&15) || !r[3] || r[3]>0xfffffc000ULL-registered_pthread_size) return -95;
    uint64_t value;
    if (guest_copy(&value,r[2]-16,8,0) || guest_copy(&value,r[3],8,0)) return -14;
    native_reap_threads();
    for (unsigned int i=1;i<NATIVE_THREADS;i++) {
        struct native_thread *thread=native_threads+i;
        int unused=0;
        if (!__atomic_compare_exchange_n(&thread->state,&unused,1,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        long memory=RAW(SYS_mmap,0,65536,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if ((unsigned long)memory>=(unsigned long)-4095) { thread->state=0; return memory; }
        long alternate=RAW(SYS_mmap,0,131072,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if ((unsigned long)alternate>=(unsigned long)-4095) {
            RAW(SYS_munmap,memory,65536,0,0,0,0); thread->state=0; return alternate;
        }
        thread->runtime_memory=memory; thread->runtime_top=memory+65536;
        thread->signal_memory=alternate; thread->guest_sp=r[2]; thread->pthread=r[3];
        thread->function=r[0]; thread->argument=r[1]; thread->flags=r[4];
        if (registered_tsd) thread->flags|=0x10000000;
        thread->generation=(thread->generation+1)&255;
        if (!thread->generation) thread->generation=1;
        thread->port=((0x200+i)<<8)|thread->generation; thread->refs=1; thread->tid=-1;
        if (registered_tsd && guest_copy(&thread->port,r[3]+registered_tsd+registered_thread_slot,8,1)) {
            RAW(SYS_munmap,memory,65536,0,0,0,0); RAW(SYS_munmap,alternate,131072,0,0,0,0);
            thread->state=0; return -14;
        }
        __atomic_store_n(&thread->state,2,__ATOMIC_RELEASE);
        long tid=darwin_clone_thread(thread);
        if (tid<0) {
            RAW(SYS_munmap,memory,65536,0,0,0,0); RAW(SYS_munmap,alternate,131072,0,0,0,0);
            thread->state=0; return tid;
        }
        return r[3];
    }
    return -11;
}
static long native_thread_terminate(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    if (!thread || thread==native_threads || (r[0]&16383) || (r[1]&16383) || r[1]>(1ULL<<32)) return -22;
    if (r[3]) return -95; /* custom-stack join semaphore requires separate path */
    if (r[1] && RAW(SYS_munmap,r[0],r[1],0,0,0,0)) return -22;
    __atomic_store_n(&thread->state,3,__ATOMIC_RELEASE);
    RAW(SYS_exit,0,0,0,0,0,0); /* thread exit; never exit_group */
    __builtin_unreachable();
}
static long native_ulock(const unsigned long long *r)
{
    unsigned int operation=r[0]&255;
    if ((operation!=1 && operation!=2) || (r[1]&3)) return -22;
    if (r[16]==515) {
        if (r[0]&~0x010300ffUL) return -95;
        struct timespec timeout={r[3]/1000000,(r[3]%1000000)*1000};
        long result=RAW(SYS_futex,r[1],FUTEX_WAIT_PRIVATE,(uint32_t)r[2],r[3] ? &timeout : NULL,0,0);
        return result==-11 ? 0 : result; /* changed value is a successful retry */
    }
    if (r[0]&~0x010005ffUL) return -95;
    long result=RAW(SYS_futex,r[1],FUTEX_WAKE_PRIVATE,(r[0]&0x100) ? 0x7fffffff : 1,0,0,0);
    return result<0 ? result : 0;
}

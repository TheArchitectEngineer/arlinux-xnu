/* Ordinary libdispatch work items, executed by real registered pthread workers.
 * Basic kevent delivery is provided by darwin-kqueue.h; workloops and realtime
 * scheduling remain unsupported. Ordinary workers exit
 * after returning a batch; libpthread runs its actual cleanup/termination path.
 * This favors simple lifetime rules over XNU's idle-thread reuse policy. */
#pragma once

static int native_priority_qos(uint32_t priority)
{
    unsigned int bits=(priority>>D_PTHREAD_QOS_SHIFT)&D_PTHREAD_QOS_MASK;
    if (!bits || bits>32 || (bits&(bits-1))) return 0;
    return __builtin_ctz(bits)+1;
}
static long native_set_priority(struct native_thread *thread,uint32_t priority)
{
    int qos=native_priority_qos(priority);
    if (!thread || !qos || (priority&~D_PTHREAD_PRIORITY_MASK)) return -EINVAL;
    int relative=(int)(priority&D_PTHREAD_RELATIVE_PRIORITY_MASK)+1-256;
    if (relative < -15) return -EINVAL;
    int nice=(qos>=4 ? 0 : 20-qos*4)-relative;
    if (nice>19) nice=19;
    long error=RAW(SYS_setpriority,0,thread->tid,nice,0,0,0);
    if (error) return error; /* Preserve Linux permission failures on increases. */
    thread->priority=priority;
    return 0;
}
static long native_thread_ctl(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    if (!thread) return -ESRCH;
    switch (r[0]) {
    case D_CTL_MAX_PARALLELISM:
        if (r[1]>6 || (r[2]&~1UL) || r[3]) return -EINVAL;
        return 1; /* Actual inherited single-CPU affinity. */
    case D_CTL_GET_QOS:
        thread=r[1] ? native_thread_port(r[1]) : thread;
        return thread ? (long)thread->priority : -ESRCH;
    case D_CTL_SET_QOS:
        if (r[3]) return -EINVAL;
        thread=r[1] ? native_thread_port(r[1]) : thread;
        return native_set_priority(thread,r[2]);
    case D_CTL_SET_SELF: {
        unsigned long flags=r[3];
        if (flags&~(D_SET_SELF_QOS|D_SET_SELF_VOUCHER|D_SET_SELF_TIMESHARE)) return -ENOTSUP;
        if ((flags&D_SET_SELF_VOUCHER) && r[2]) return -ENOTSUP;
        if (flags&D_SET_SELF_TIMESHARE) {
            struct sched_param parameter={.sched_priority=0};
            long error=RAW(SYS_sched_setscheduler,0,SCHED_OTHER,&parameter,0,0,0);
            if (error) return error;
        }
        return flags&D_SET_SELF_QOS ? native_set_priority(thread,r[1]) : 0;
    }
    default: return -ENOTSUP;
    }
}
static long native_workqueue(const unsigned long long *r,const sigset_t *original_mask)
{
    if (!workqueue_start_callback) return -EINVAL;
    if (r[D_ARM64_SYSCALL_REGISTER]==D_SYS_WORKQ_OPEN) {
        if (!workqueue_registered) return -EINVAL;
        workqueue_opened=1;
        return 0;
    }
    switch (r[0]) {
    case D_WQ_NEWSPISUPP:
        /* Direct delivery uses the real process workqueue kqueue below. */
        if (r[1] || r[2]>D_PTHREAD_MAX_SIZE || (r[3]&~1UL)) return -ENOTSUP;
        workqueue_has_kevents=r[3]&1;
        workqueue_registered=1;
        return 0;
    case D_WQ_KEVENT_RETURN: case D_WQ_WORKLOOP_RETURN: return native_event_worker_return(r);
    case D_WQ_RETURN: {
        struct native_thread *thread=native_current_thread();
        if (!thread || !thread->workqueue || r[1] || r[2] || r[3]) return -EINVAL;
        /* Returning zero asks real libpthread to exit and clean up the worker.
         * Work is already consumed by libdispatch; no pending callback is lost. */
        return 0;
    }
    case D_WQ_SHOULD_NARROW:
        if (!workqueue_opened) return -EINVAL;
        unsigned int workers=0;
        for (unsigned int i=1;i<NATIVE_THREADS;i++) if (native_threads[i].workqueue &&
            __atomic_load_n(&native_threads[i].state,__ATOMIC_ACQUIRE)==2) workers++;
        return workers>1; /* Reduce concurrency on the actual single CPU. */
    case D_WQ_REQTHREADS: {
        if (!workqueue_opened || r[1] || !r[2] || r[2]>=NATIVE_THREADS) return -EINVAL;
        uint32_t priority=r[3];
        int qos=native_priority_qos(priority);
        if (!qos || (priority&~(D_PTHREAD_PRIORITY_MASK|D_PRIORITY_OVERCOMMIT))) return -ENOTSUP;
        for (unsigned int i=0;i<r[2];i++) {
            uint64_t size=D_GUEST_PAGE_SIZE+D_WORKQUEUE_STACK_SIZE+
                          ((registered_pthread_size+D_GUEST_PAGE_SIZE-1)&~(D_GUEST_PAGE_SIZE-1));
            long memory=anonymous_aligned(size,PROT_READ|PROT_WRITE,D_GUEST_PAGE_SIZE-1);
            if ((unsigned long)memory>=(unsigned long)-4095) return memory;
            long error=RAW(SYS_mprotect,memory,D_GUEST_PAGE_SIZE,PROT_NONE,0,0,0);
            if (error) { RAW(SYS_munmap,memory,size,0,0,0,0); return error; }
            uint64_t low=memory+D_GUEST_PAGE_SIZE,self=low+D_WORKQUEUE_STACK_SIZE;
            uint32_t flags=D_WQ_PRIO_QOS|D_WQ_NEWSPI|qos;
            if (priority&D_PRIORITY_OVERCOMMIT) flags|=D_WQ_OVERCOMMIT;
            unsigned long long arguments[6]={0,0,self,self,flags,0};
            long result=native_thread_create_impl(arguments,original_mask,1,low);
            if ((unsigned long)result>=(unsigned long)-4095) {
                RAW(SYS_munmap,memory,size,0,0,0,0); return result;
            }
        }
        return 0;
    }
    default: return -ENOTSUP;
    }
}

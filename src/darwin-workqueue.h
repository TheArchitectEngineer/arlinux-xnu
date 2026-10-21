/* Ordinary libdispatch work items, executed by real registered pthread workers.
 * Kevent and basic workloop delivery are provided by darwin-kqueue.h;
 * realtime scheduling remains unsupported. Ordinary workers exit
 * after returning a batch; libpthread runs its actual cleanup/termination path.
 * This favors simple lifetime rules over XNU's idle-thread reuse policy. */
#pragma once
/* Real per-process dispatch metadata, also retained for original-process
 * debugger inspection. Actual callbacks remain owned by registered libpthread. */
static volatile struct darwin_workq_dispatch_config native_dispatch_config;

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
        long result=native_event_dispatch_start(original_mask);
        if(result)return result;
        workqueue_opened=1;
        return 0;
    }
    switch (r[0]) {
    case D_WQ_SETUP_DISPATCH: {
        /* XNU pthread_workqueue.c stores serial/label offsets. Modern
         * libpthread then installs its callbacks and opens the workqueue. */
        if(!r[1] || r[2]<16 || r[2]>128 || r[3])return -EINVAL;
        struct darwin_workq_dispatch_config cfg={0};
        unsigned long bytes=r[2]<sizeof(cfg) ? r[2] : sizeof(cfg);
        if(guest_copy(&cfg,r[1],bytes,0))return -EFAULT;
        if(cfg.flags || cfg.version<1 || cfg.version>2)return -ENOTSUP;
        if(cfg.version==2 && r[2]<sizeof(cfg))return -EINVAL;
        if(cfg.version==1)cfg.label_offset=0;
        native_dispatch_config=cfg;
        workqueue_registered=1;workqueue_has_kevents=1;
        trace_record("DARWIN_DISPATCH_CONFIG version=0x",cfg.version,cfg.serial_offset,cfg.label_offset);
        return 0;
    }
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
        /* The signal-return owner re-enters libpthread with the actual XNU
         * exit upcall. Returning to __workq_kernreturn would trap, even on 0. */
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
        /* XNU extracts the requested QoS and overcommit independently of
         * libdispatch's fallback marker. It still requests a real worker at
         * that QoS; the marker does not create a separate kernel queue. */
        if (!qos || (priority&~(D_PTHREAD_PRIORITY_MASK|D_PRIORITY_OVERCOMMIT|D_PRIORITY_FALLBACK))) return -ENOTSUP;
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

/* Successful workq returns replace the saved guest context, as XNU does.
 * Resume through Linux rt_sigreturn so the alternate stack and signal mask
 * are restored before the next guest callback/trap. No nested guest execution
 * occurs inside the SIGSYS handler. Event counts describe actual collected
 * events; ordinary completed batches enter genuine libpthread cleanup. */
static int native_workqueue_resume(ucontext_t *uc,long result)
{
    unsigned long long *r=uc->uc_mcontext.regs;
    if (result<0 || r[D_ARM64_SYSCALL_REGISTER]!=D_SYS_WORKQ_KERNRETURN) return 0;
    unsigned long operation=r[0];
    if (operation!=D_WQ_RETURN && operation!=D_WQ_KEVENT_RETURN && operation!=D_WQ_WORKLOOP_RETURN) return 0;
    struct native_thread *thread=native_current_thread();
    if (!thread || !thread->workqueue) return 0;
    uint64_t buffer=r[1] ? r[1] : native_event_buffer(thread);
    uint64_t flags=thread->flags|D_WQ_REUSE;
    int count=D_WQ_EXIT_NKEVENT;
    if (operation!=D_WQ_RETURN) {
        flags|=thread->workloop_id ? D_WQ_WORKLOOP : D_WQ_KEVENT|D_WQ_EVENT_MANAGER;
        count=result;
    } else buffer=0;
    trace_record("DARWIN_WORKQUEUE_UPCALL flags=0x",flags,workqueue_start_callback,(unsigned long)count);
    __builtin_memset(r,0,31*sizeof(*r));
    r[0]=thread->pthread;r[1]=thread->port;r[2]=thread->stack_low;
    r[3]=buffer;r[4]=flags;r[5]=(uint64_t)(int64_t)count;
    uc->uc_mcontext.sp=thread->guest_sp;
    uc->uc_mcontext.pc=workqueue_start_callback;
    uc->uc_mcontext.pstate&=~CARRY;
    return 1;
}

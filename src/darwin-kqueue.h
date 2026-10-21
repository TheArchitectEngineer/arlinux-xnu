/* Darwin kevent_qos backed by Linux epoll, eventfd and timerfd. Real readiness
 * drives registered libpthread kevent callbacks; unsupported filters fail.
 * This owns descriptor/workqueue readiness and the supported workloop owner
 * and synchronous-wait protocol, not a complete XNU event implementation. */
#pragma once
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include "darwin-memory-pressure.h"
struct native_knote {
    struct darwin_kevent_qos event;
    int used,fd,enabled; uint32_t generation,memory_state;
    int sync_state,sync_tid,sync_active; /* pending / woken / deleted */
};
#include "darling-kqueue.h"
struct native_kqueue {
    struct native_kqueue *next;
    int used,fd,worker,owner_tid;
    uint64_t dynamic_id; uint32_t priority,client_owner,ownership_epoch;
    struct native_knote watches[D_KQUEUE_WATCHES];
};
/* Published nodes retain stable addresses for workers/readers until process
 * exit. A queue consumes real mapping/epoll resources, not an arbitrary slot. */
static struct native_kqueue *native_kqueues;
static int native_kqueue_mutex,workqueue_kqueue=-1;
static unsigned int native_kevent_errors;
static unsigned int native_workloop_cancels;
static int native_event_dispatch_fd=-1,native_event_dispatch_state;
static uint64_t native_event_dispatch_mask;
/* A workloop is not a permanently allocated thread. Arm its real epoll
 * descriptor; one TLS-free native waiter starts a bound worker only when
 * Linux reports pending events. Existing workers keep their drain ownership. */
static long native_kqueue_arm(struct native_kqueue *queue)
{
    if(native_event_dispatch_fd<0 || !queue->used || __atomic_load_n(&queue->client_owner,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&queue->worker,__ATOMIC_ACQUIRE))return 0;
    struct epoll_event event={.events=EPOLLIN|EPOLLONESHOT,.data.ptr=queue};
    long result=RAW(SYS_epoll_ctl,native_event_dispatch_fd,EPOLL_CTL_MOD,queue->fd,&event,0,0);
    if(result==-ENOENT)result=RAW(SYS_epoll_ctl,native_event_dispatch_fd,EPOLL_CTL_ADD,queue->fd,&event,0,0);
    return result;
}
static void native_kqueue_lock(void)
{
    while (__atomic_exchange_n(&native_kqueue_mutex,1,__ATOMIC_ACQUIRE))
        RAW(SYS_futex,&native_kqueue_mutex,FUTEX_WAIT_PRIVATE,1,0,0,0);
}
static void native_kqueue_unlock(void)
{
    __atomic_store_n(&native_kqueue_mutex,0,__ATOMIC_RELEASE);
    RAW(SYS_futex,&native_kqueue_mutex,FUTEX_WAKE_PRIVATE,1,0,0,0);
}
/* Called with the queue namespace locked. Pending readiness remains queued
 * while a genuine user thread owns the drain session. */
static void native_kqueue_owner(struct native_kqueue *queue,uint32_t port)
{
    if (queue->client_owner==port) return;
    __atomic_store_n(&queue->client_owner,port,__ATOMIC_RELEASE);
    __atomic_fetch_add(&queue->ownership_epoch,1,__ATOMIC_RELEASE);
    RAW(SYS_futex,&queue->ownership_epoch,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    if(!port) {
        long error=native_kqueue_arm(queue);
        if(error)trace_record("DARWIN_EVENT_ARM_ERROR status=0x",-error,queue->dynamic_id,queue->fd);
    }
}
static void native_event_owner_exit(uint64_t port)
{
    struct native_thread *thread=native_thread_port(port);
    native_kqueue_lock();
    for (struct native_kqueue *queue=native_kqueues;queue;queue=queue->next) {
        if (queue->client_owner==port) native_kqueue_owner(queue,0);
        for (unsigned int i=0;thread && i<D_KQUEUE_WATCHES;i++) {
            struct native_knote *note=queue->watches+i;
            if (note->used && note->event.filter==D_EVFILT_WORKLOOP && note->sync_tid==thread->tid) {
                __atomic_store_n(&note->sync_state,2,__ATOMIC_RELEASE);
                RAW(SYS_futex,&note->sync_state,FUTEX_WAKE_PRIVATE,1,0,0,0);
                if (!note->sync_active) note->used=0;
            }
        }
    }
    native_kqueue_unlock();
}
/* A synchronous special-reply wait yields this thread's workloop ownership
 * while it really blocks. It does not fabricate a wake or receive completion. */
static void native_reply_wait(uint32_t name)
{
    struct ipc_endpoint *endpoint=ipc_find(name);
    struct native_thread *thread=native_current_thread();
    if (endpoint && thread && endpoint->special_tid==thread->tid)
        native_event_owner_exit(thread->port);
}
static struct native_kqueue *native_kqueue_find(int fd)
{
    for (struct native_kqueue *queue=__atomic_load_n(&native_kqueues,__ATOMIC_ACQUIRE);queue;queue=queue->next)
        if (queue->fd==fd) return queue;
    return NULL;
}
/* Caller holds the namespace lock while publishing a newly allocated queue. */
static long native_kqueue_create_locked(uint64_t dynamic_id)
{
    unsigned long size=(sizeof(struct native_kqueue)+D_GUEST_PAGE_SIZE-1)&~(D_GUEST_PAGE_SIZE-1UL);
    long memory=anonymous_aligned(size,PROT_READ|PROT_WRITE,0);
    if ((unsigned long)memory>=(unsigned long)-4095) return memory;
    long fd=RAW(SYS_epoll_create1,EPOLL_CLOEXEC,0,0,0,0,0);
    if(fd<0) {
        RAW(SYS_munmap,memory,size,0,0,0,0);
        return fd;
    }
    struct native_kqueue *queue=(void *)memory;
    queue->fd=fd; queue->dynamic_id=dynamic_id; queue->used=1;
    queue->next=native_kqueues;
    __atomic_store_n(&native_kqueues,queue,__ATOMIC_RELEASE);
    return fd;
}
static long native_kqueue_create(void)
{
    native_kqueue_lock();
    long result=native_kqueue_create_locked(0);
    native_kqueue_unlock();
    return result;
}
/* XNU filt_wlupdate: copy/debounce before committing owner changes. A zero
 * owner token preserves the current owner; END_OWNERSHIP may clear it even on
 * ESTALE, but DISCOVER_OWNER must not update a stale request. */
static long native_workloop_owner_update(struct native_kqueue *queue,struct darwin_kevent_qos *event)
{
    uint32_t owner=queue->client_owner;
    int stale=0;
    if (event->ext[D_WL_ADDR]) {
        uint64_t state;
        if ((event->ext[D_WL_ADDR]&7) || guest_copy(&state,event->ext[D_WL_ADDR],8,0)) return -EFAULT;
        uint64_t expected=event->ext[D_WL_VALUE];
        event->ext[D_WL_VALUE]=state;
        stale=(state&event->ext[D_WL_MASK])!=(expected&event->ext[D_WL_MASK]);
        if (!stale && (event->fflags&D_NOTE_WL_DISCOVER_OWNER)) {
            uint32_t name=(uint32_t)state&~(uint32_t)D_PORT_GENERATION_TAG;
            if (name) {
                struct native_thread *thread=native_thread_port(name|D_PORT_GENERATION_TAG);
                if (!thread || thread->state!=2 || thread->tid<=0) return -EOWNERDEAD;
                owner=thread->tid==queue->owner_tid ? 0 : thread->port;
            }
        }
    }
    struct native_thread *caller=native_current_thread();
    if ((event->fflags&D_NOTE_WL_END_OWNERSHIP) && caller && owner==caller->port) owner=0;
    native_kqueue_owner(queue,owner);
    return stale ? -ESTALE : 0;
}
/* XNU event.h/filt_wlattach/filt_wltouch use a distinct disabled knote per
 * synchronous waiter. Linux futex supplies the real sleep/wake; these notes
 * never enter epoll or manufacture an asynchronous workqueue callback. */
static long native_workloop_sync_change(struct native_kqueue *queue,struct darwin_kevent_qos *event,
                                       struct native_knote *note,struct native_knote **waiter)
{
    unsigned int command=event->fflags&D_NOTE_WL_COMMAND_MASK;
    if (!queue->dynamic_id || event->ident==queue->dynamic_id ||
        (event->fflags&~(D_NOTE_WL_SYNC_WAIT|D_NOTE_WL_SYNC_WAKE|D_NOTE_WL_DISCOVER_OWNER|
                         D_NOTE_WL_END_OWNERSHIP|D_NOTE_WL_IGNORE_ESTALE))) return -EINVAL;
    if ((event->flags&D_EV_DELETE) && (event->fflags&D_NOTE_WL_DISCOVER_OWNER)) return -EINVAL;
    if (command==D_NOTE_WL_SYNC_WAIT && (event->fflags&D_NOTE_WL_END_OWNERSHIP)) return -EINVAL;
    if ((event->flags&(D_EV_ENABLE|D_EV_DELETE))==D_EV_ENABLE) return -EINVAL;
    if (!note && (event->flags&(D_EV_ADD|D_EV_DELETE))==(D_EV_ADD|D_EV_DELETE)) return 0;
    if (note && !(note->event.fflags&(D_NOTE_WL_SYNC_WAIT|D_NOTE_WL_SYNC_WAKE))) return -EINVAL;
    if (note && note->sync_state==2) return -ENOENT;
    /* XNU filt_wltouch permits repeated SYNC_WAKE, including owner discovery.
     * A latched wake must still debounce/update ownership below. EALREADY
     * belongs to a second THREAD_REQUEST attach, not this synchronous note. */
    if (!note && (!(event->flags&D_EV_ADD) || (event->flags&D_EV_DELETE))) return -ENOENT;
    if (!note && !(event->flags&D_EV_DISABLE)) return -EINVAL;
    if (command==D_NOTE_WL_SYNC_WAIT && !waiter) return -ENOTSUP;
    struct native_thread *caller=native_current_thread();
    if (command==D_NOTE_WL_SYNC_WAIT && !(event->flags&D_EV_DELETE)) {
        if (!caller) return -ESRCH;
        if (note && note->sync_tid && note->sync_tid!=caller->tid) return -EXDEV;
        if (note && note->sync_active) return -EBUSY;
    }
    if (!note) {
        for (unsigned int i=0;i<D_KQUEUE_WATCHES;i++)
            if (!queue->watches[i].used) { note=queue->watches+i; break; }
        if (!note) return -ENOMEM;
    }
    long result=native_workloop_owner_update(queue,event);
    if (result) return result==-ESTALE && (event->fflags&D_NOTE_WL_IGNORE_ESTALE) ? 0 : result;
    if (!note->used) {
        note->event=*event; note->fd=-1; note->enabled=0;
        note->sync_state=0; note->sync_tid=0; note->sync_active=0;
        note->generation++; if (!note->generation) note->generation=1;
        note->used=1;
    }
    if (event->flags&D_EV_DELETE) {
        __atomic_store_n(&note->sync_state,2,__ATOMIC_RELEASE);
        RAW(SYS_futex,&note->sync_state,FUTEX_WAKE_PRIVATE,1,0,0,0);
        if (!note->sync_active) note->used=0;
        return 0;
    }
    if (command==D_NOTE_WL_SYNC_WAKE) {
        note->event.fflags|=D_NOTE_WL_SYNC_WAKE;
        __atomic_store_n(&note->sync_state,1,__ATOMIC_RELEASE);
        long woke=RAW(SYS_futex,&note->sync_state,FUTEX_WAKE_PRIVATE,1,0,0,0);
        trace_record("DARWIN_WORKLOOP_SYNC_WAKE ident=0x",event->ident,queue->dynamic_id,woke);
        return woke<0 ? woke : 0;
    }
    /* Even an already-latched wake completes the synchronous registration;
     * pin its slot until copyout and do not start an asynchronous worker. */
    note->sync_tid=caller->tid; note->sync_active=1; *waiter=note;
    return 0;
}
static long native_workloop_sync_wait(struct native_kqueue *queue,struct native_knote *note)
{
    trace_record("DARWIN_WORKLOOP_SYNC_WAIT ident=0x",note->event.ident,queue->dynamic_id,note->sync_tid);
    long result=0;
    while (!__atomic_load_n(&note->sync_state,__ATOMIC_ACQUIRE)) {
        result=RAW(SYS_futex,&note->sync_state,FUTEX_WAIT_PRIVATE,0,0,0,0);
        if (result && result!=-EAGAIN) break;
        result=0;
    }
    native_kqueue_lock();
    note->sync_active=0;
    int state=note->sync_state;
    trace_record("DARWIN_WORKLOOP_SYNC_RETURN ident=0x",note->event.ident,state,result);
    if (state==2) note->used=0;
    native_kqueue_unlock();
    return result;
}
static long native_knote_change(struct native_kqueue *queue,struct darwin_kevent_qos *event,
                                struct native_knote **waiter)
{
    if (event->xflags || (event->flags&~(D_EV_ADD|D_EV_DELETE|D_EV_ENABLE|D_EV_DISABLE|
        D_EV_ONESHOT|D_EV_CLEAR|D_EV_RECEIPT|D_EV_DISPATCH|D_EV_UDATA_SPECIFIC|D_EV_VANISHED))) return -ENOTSUP;
    /* UDATA-specific memory sources have separate subscriptions and ordinary
     * DISPATCH rearm/delete semantics. Deferred deletion for arbitrary other
     * source kinds is still unsupported. */
    if ((event->flags&D_EV_UDATA_SPECIFIC) && event->filter!=D_EVFILT_MEMORYSTATUS &&
        event->filter!=D_EVFILT_MACHPORT) return -ENOTSUP;
    if ((event->flags&D_EV_VANISHED) && (event->filter!=D_EVFILT_MACHPORT ||
        (event->flags&(D_EV_UDATA_SPECIFIC|D_EV_DISPATCH))!=(D_EV_UDATA_SPECIFIC|D_EV_DISPATCH))) return -ENOTSUP;
    if (!darling_active && event->filter==D_EVFILT_MACHPORT &&
        (!ipc_receive_options(event->fflags) || event->ext[2] || event->ext[3])) return -ENOTSUP;
    if (event->filter==D_EVFILT_MEMORYSTATUS &&
        (event->ident || (event->fflags&~(D_MEMORY_NORMAL|D_MEMORY_WARN|D_MEMORY_CRITICAL|
            D_MEMORY_PROC_WARN|D_MEMORY_PROC_CRITICAL|D_MEMORY_MSL_STATUS)))) return -ENOTSUP;
    struct native_knote *note=NULL;
    unsigned int index=0;
    for (;index<D_KQUEUE_WATCHES;index++) {
        struct native_knote *candidate=queue->watches+index;
        if (candidate->used && candidate->event.ident==event->ident &&
            candidate->event.filter==event->filter &&
            (!(event->flags&D_EV_UDATA_SPECIFIC) || candidate->event.udata==event->udata)) { note=candidate; break; }
    }
    if (event->filter==D_EVFILT_WORKLOOP) {
        unsigned int command=event->fflags&D_NOTE_WL_COMMAND_MASK;
        if (command==D_NOTE_WL_SYNC_WAIT || command==D_NOTE_WL_SYNC_WAKE)
            return native_workloop_sync_change(queue,event,note,waiter);
        if (!queue->dynamic_id || event->ident!=queue->dynamic_id ||
            !(event->fflags&D_NOTE_WL_THREAD_REQUEST) ||
            (event->fflags&~(D_NOTE_WL_THREAD_REQUEST|D_NOTE_WL_UPDATE_QOS|D_NOTE_WL_IGNORE_ESTALE|
                            D_NOTE_WL_DISCOVER_OWNER|D_NOTE_WL_END_OWNERSHIP))) return -ENOTSUP;
        if (!note && (event->flags&(D_EV_ADD|D_EV_DELETE))==(D_EV_ADD|D_EV_DELETE)) {
            /* XNU kevent_register (darling-xnu 40bc7ec): ADD|DELETE on a
             * workloop is a soft delete. An absent note succeeds before its
             * filter/debounce runs, without creating a note or changing owner. */
            if (__atomic_fetch_add(&native_workloop_cancels,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
                const char label[]="DARWIN_WORKLOOP_SOFT_DELETE queue/owner follows\n";
                RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
                trace_call(0,0,queue->dynamic_id); trace_call(1,0,queue->client_owner);
            }
            return 0;
        }
        long owner_result=native_workloop_owner_update(queue,event);
        if (owner_result) return owner_result==-ESTALE && (event->fflags&D_NOTE_WL_IGNORE_ESTALE) ? 0 : owner_result;
        if ((event->flags&D_EV_ADD) && !(event->flags&D_EV_DELETE)) {
            uint32_t priority=(uint32_t)event->qos&D_PTHREAD_PRIORITY_MASK;
            unsigned int bits=(priority>>D_PTHREAD_QOS_SHIFT)&D_PTHREAD_QOS_MASK;
            if (!bits || bits>32 || (bits&(bits-1))) return -ERANGE;
            if (event->fflags&D_NOTE_WL_UPDATE_QOS) {
                int tid=__atomic_load_n(&queue->owner_tid,__ATOMIC_ACQUIRE);
                if (tid) {
                    unsigned int qos=(unsigned int)__builtin_ctz(bits)+1;
                    int nice=qos>=4 ? 0 : 20-(int)qos*4;
                    long error=RAW(SYS_setpriority,0,tid,nice,0,0,0);
                    if (error) return error;
                }
                __atomic_store_n(&queue->priority,priority,__ATOMIC_RELEASE);
            }
        }
    }
    if (event->flags&D_EV_DELETE) {
        if (!note) return -ENOENT;
        if (note->enabled) RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_DEL,note->fd,0,0,0);
        RAW(SYS_close,note->fd,0,0,0,0,0); note->used=0; return 0;
    }
    int fresh=0;
    if (!note) {
        if (!(event->flags&D_EV_ADD)) return -ENOENT;
        for (index=0;index<D_KQUEUE_WATCHES;index++) if (!queue->watches[index].used) break;
        if (index==D_KQUEUE_WATCHES) return -ENOMEM;
        note=queue->watches+index;
        long fd;
        switch (event->filter) {
        case D_EVFILT_MACHPORT: {
#ifdef ARLINUX_DARLING
            if (darling_active) { fd=darling_kq_open(event); break; }
#endif
            struct ipc_endpoint *endpoint=ipc_find(event->ident);
            if (!endpoint || endpoint->kind) return -ENOENT;
            fd=RAW(SYS_fcntl,endpoint->read_fd,F_DUPFD_CLOEXEC,0,0,0,0); break;
        }
        case D_EVFILT_USER: case D_EVFILT_WORKLOOP:
            fd=RAW(SYS_eventfd2,0,EFD_CLOEXEC|EFD_NONBLOCK,0,0,0,0); break;
        case D_EVFILT_TIMER: case D_EVFILT_MEMORYSTATUS:
            fd=RAW(SYS_timerfd_create,event->filter==D_EVFILT_TIMER &&
                (event->fflags&D_NOTE_MACH_CONTINUOUS_TIME) ? CLOCK_BOOTTIME : CLOCK_MONOTONIC,
                TFD_CLOEXEC|TFD_NONBLOCK,0,0,0,0); break;
        case D_EVFILT_READ: case D_EVFILT_WRITE:
            if (event->fflags) return -ENOTSUP;
            fd=RAW(SYS_fcntl,event->ident,F_DUPFD_CLOEXEC,0,0,0,0); break;
        default: return -ENOTSUP;
        }
        if (fd<0) return fd;
        note->fd=fd; note->enabled=0; note->event=*event;
        note->memory_state=0;
        note->generation++; if (!note->generation) note->generation=1;
        fresh=1;
    }
    long error=0;
    if (event->filter==D_EVFILT_WORKLOOP) {
        note->event.fflags=event->fflags;
        for (unsigned int i=0;i<4;i++) note->event.ext[i]=event->ext[i];
        if (!(event->flags&D_EV_DISABLE)) {
            uint64_t token=1;
            long result=RAW(SYS_write,note->fd,&token,sizeof(token),0,0,0);
            if (result!=(long)sizeof(token)) error=result<0 ? result : -EIO;
        }
    } else if (event->filter==D_EVFILT_MACHPORT && (event->flags&D_EV_ADD)) {
#ifdef ARLINUX_DARLING
        if (darling_active && !fresh && (event->fflags!=note->event.fflags ||
            event->ext[0]!=note->event.ext[0] || event->ext[1]!=note->event.ext[1]))
            error=darling_kq_modify(note->fd,event);
#endif
        note->event.fflags=event->fflags;
        for (unsigned int i=0;i<4;i++) note->event.ext[i]=event->ext[i];
    } else if (event->filter==D_EVFILT_MEMORYSTATUS && (event->flags&D_EV_ADD)) {
        /* Validate real access before accepting a monitor. MSL/Jetsam status
         * is a standalone no-cap policy, not a fabricated limit notification. */
        long state=native_memory_pressure();
        if (state<0) error=state;
        else {
            struct itimerspec timer={.it_value={D_MEMORY_SAMPLE_SECONDS,0},
                                     .it_interval={D_MEMORY_SAMPLE_SECONDS,0}};
            error=RAW(SYS_timerfd_settime,note->fd,0,&timer,0,0,0);
            note->event.fflags=event->fflags;
        }
    } else if (event->filter==D_EVFILT_USER) {
        uint32_t flags=note->event.fflags&D_NOTE_FFLAGSMASK,value=event->fflags&D_NOTE_FFLAGSMASK;
        switch (event->fflags&D_NOTE_FFCTRLMASK) {
        case D_NOTE_FFAND: flags&=value; break;
        case D_NOTE_FFOR: flags|=value; break;
        case D_NOTE_FFCOPY: flags=value; break;
        }
        note->event.fflags=flags;
        if (event->fflags&D_NOTE_TRIGGER) {
            uint64_t token=1;
            long result=RAW(SYS_write,note->fd,&token,sizeof(token),0,0,0);
            if (result!=(long)sizeof(token)) error=result<0 ? result : -EIO;
        }
    } else if (event->filter==D_EVFILT_TIMER && (event->flags&D_EV_ADD)) {
        if (event->fflags&~(D_NOTE_SECONDS|D_NOTE_USECONDS|D_NOTE_NSECONDS|D_NOTE_ABSOLUTE|
                           D_NOTE_LEEWAY|D_NOTE_MACH_CONTINUOUS_TIME|D_NOTE_MACHTIME)) error=-ENOTSUP;
        else if ((note->event.fflags^event->fflags)&D_NOTE_MACH_CONTINUOUS_TIME) error=-ENOTSUP;
        else {
            unsigned int units=event->fflags&(D_NOTE_SECONDS|D_NOTE_USECONDS|D_NOTE_NSECONDS|D_NOTE_MACHTIME);
            uint64_t scale=units==D_NOTE_SECONDS ? 1000000000 : units==D_NOTE_USECONDS ? 1000 :
                (units==D_NOTE_NSECONDS || units==D_NOTE_MACHTIME) ? 1 : 1000000;
            if ((units && (units&(units-1))) ||
                (event->data>0 && (uint64_t)event->data>INT64_MAX/scale) ||
                ((event->fflags&D_NOTE_LEEWAY) && event->ext[1]>INT64_MAX/scale)) error=-EINVAL;
            else {
                uint64_t nanoseconds=event->data>0 ? (uint64_t)event->data*scale : 1;
                /* Non-Mach absolute values name the calendar epoch. XNU
                 * converts it once into the selected uptime/continuous clock;
                 * later wall-clock changes must not retarget the timer. */
                if ((event->fflags&D_NOTE_ABSOLUTE) && !(event->fflags&D_NOTE_MACHTIME)) {
                    struct timespec calendar,clock;
                    error=RAW(SYS_clock_gettime,CLOCK_REALTIME,&calendar,0,0,0,0);
                    if (!error) error=RAW(SYS_clock_gettime,(event->fflags&D_NOTE_MACH_CONTINUOUS_TIME) ?
                        CLOCK_BOOTTIME : CLOCK_MONOTONIC,&clock,0,0,0,0);
                    if (!error) {
                        uint64_t wall=(uint64_t)calendar.tv_sec*1000000000+calendar.tv_nsec;
                        uint64_t now=(uint64_t)clock.tv_sec*1000000000+clock.tv_nsec;
                        uint64_t remaining=nanoseconds>wall ? nanoseconds-wall : 0;
                        if (remaining>INT64_MAX-now) error=-EINVAL;
                        else nanoseconds=now+remaining;
                    }
                }
                struct itimerspec timer={.it_value={nanoseconds/1000000000,nanoseconds%1000000000}};
                if (!(event->flags&D_EV_ONESHOT) && !(event->fflags&D_NOTE_ABSOLUTE) && event->data>0)
                    timer.it_interval=timer.it_value;
                /* Exact-deadline delivery is within the requested leeway.
                 * This standalone scheduling policy adds no coalescing delay. */
                if (!error) error=RAW(SYS_timerfd_settime,note->fd,(event->fflags&D_NOTE_ABSOLUTE) ? TFD_TIMER_ABSTIME : 0,&timer,0,0,0);
                note->event.fflags=event->fflags;
                for (unsigned int i=0;i<4;i++) note->event.ext[i]=event->ext[i];
            }
        }
    }
    if (error) {
        if (fresh) RAW(SYS_close,note->fd,0,0,0,0,0);
        return error;
    }
    if (event->flags&D_EV_ADD) {
        note->event.flags=event->flags;
        if (event->filter==D_EVFILT_TIMER && (event->fflags&D_NOTE_ABSOLUTE)) note->event.flags|=D_EV_ONESHOT;
        if (event->filter==D_EVFILT_WORKLOOP) note->event.flags|=D_EV_CLEAR;
        note->event.udata=event->udata; note->event.qos=event->qos; note->event.data=event->data;
    }
    int enable=!(event->flags&D_EV_DISABLE) && (fresh || note->enabled || (event->flags&(D_EV_ENABLE|D_EV_ADD)));
    if (enable) {
        struct epoll_event ep={.events=event->filter==D_EVFILT_WRITE ? EPOLLOUT : EPOLLIN};
        if (note->event.flags&D_EV_CLEAR) ep.events|=EPOLLET;
        if (note->event.flags&(D_EV_ONESHOT|D_EV_DISPATCH)) ep.events|=EPOLLONESHOT;
        ep.data.u64=((uint64_t)note->generation<<32)|index;
        error=RAW(SYS_epoll_ctl,queue->fd,note->enabled ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,note->fd,&ep,0,0);
        if (error==-EEXIST) error=RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_MOD,note->fd,&ep,0,0);
    } else if (note->enabled) error=RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_DEL,note->fd,0,0,0);
    if (error) { if (fresh) RAW(SYS_close,note->fd,0,0,0,0,0); return error; }
    note->enabled=enable; note->used=1;
    return 0;
}
static long native_kqueue_changes(struct native_kqueue *queue,uint64_t changes,uint64_t count,
                                  uint64_t output,unsigned int capacity,int *did_wait)
{
    if (count>D_KQUEUE_WATCHES) return -EINVAL;
    unsigned int emitted=0;
    for (unsigned int i=0;i<count;i++) {
        struct darwin_kevent_qos event;
        if (guest_copy(&event,changes+(uint64_t)i*sizeof(event),sizeof(event),0)) return -EFAULT;
        struct native_knote *waiter=NULL;
        native_kqueue_lock();
        long result=native_knote_change(queue,&event,did_wait ? &waiter : NULL);
        native_kqueue_unlock();
        if (!result && waiter) {
            *did_wait=1;
            result=native_workloop_sync_wait(queue,waiter);
        }
        if (result && __atomic_fetch_add(&native_kevent_errors,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char label[]="DARWIN_KEVENT_ERROR ident/filter/flags/fflags/xflags/result follows\n";
            RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
            trace_call(0,0,event.ident); trace_call(1,0,(long)event.filter);
            trace_call(2,0,event.flags); trace_call(3,0,event.fflags);
            trace_call(4,0,event.xflags); trace_call(5,0,result);
            if (event.filter==D_EVFILT_WORKLOOP) {
                const char detail[]="DARWIN_WORKLOOP_ERROR_STATE queue/owner/servicer/qos/udata/ext0..3 follows\n";
                RAW(SYS_write,native_trace_fd,detail,sizeof(detail)-1,0,0,0);
                trace_call(0,0,queue->dynamic_id); trace_call(1,0,queue->client_owner);
                trace_call(2,0,queue->owner_tid); trace_call(3,0,event.qos);
                trace_call(4,0,event.udata);
                for (unsigned int x=0;x<4;x++) trace_call(5+x,0,event.ext[x]);
            }
        }
        if (result || (event.flags&D_EV_RECEIPT)) {
            if (emitted==capacity) return result ? result : -ENOBUFS;
            event.flags|=D_EV_ERROR; event.data=result ? darwin_error(result) : 0;
            if (guest_copy(&event,output+(uint64_t)emitted*sizeof(event),sizeof(event),1)) return -EFAULT;
            emitted++;
        }
        if (waiter) return emitted;
    }
    return emitted;
}
struct native_kevent_data {
    uint64_t address,remaining,initial,available; int stack;
};
/* XNU kevent_get/put_data_size: both pointers enable the LP64 input capacity;
 * write back the residual only when a filter actually used some space. */
static long native_kevent_data_init(struct native_kevent_data *data,
                                    const unsigned long long *r,unsigned long flags)
{
    *data=(struct native_kevent_data){.stack=!!(flags&D_KEVENT_STACK_DATA)};
    if (!r[5] || !r[6]) return 0;
    if (guest_copy(&data->remaining,r[6],sizeof(data->remaining),0)) return -EFAULT;
    if (data->remaining>UINT64_MAX-r[5]) return -EINVAL;
    data->initial=data->remaining; data->address=r[5]; data->available=r[6];
    return 0;
}
static long native_kevent_data_finish(struct native_kevent_data *data,long result)
{
    if (data->available && data->remaining!=data->initial &&
        guest_copy(&data->remaining,data->available,sizeof(data->remaining),1)) return -EFAULT;
    return result;
}
static long native_kqueue_collect(struct native_kqueue *queue,uint64_t output,unsigned int capacity,int timeout,
                                  struct native_kevent_data *data)
{
    if (!capacity) return 0;
    if (capacity>D_KQUEUE_BATCH) capacity=D_KQUEUE_BATCH;
    struct epoll_event events[D_KQUEUE_BATCH];
    for (;;) {
        long count=RAW(SYS_epoll_pwait,queue->fd,events,capacity,timeout,0,8);
        if (count<=0) return count;
        unsigned int emitted=0;
        native_kqueue_lock();
        if (queue->client_owner) {
            struct native_thread *owner=native_thread_port(queue->client_owner);
            struct native_thread *caller=native_current_thread();
            if (!owner || owner->state!=2 || owner->tid<=0) native_kqueue_owner(queue,0);
            else if (!caller || caller->port!=queue->client_owner) {
                uint32_t epoch=__atomic_load_n(&queue->ownership_epoch,__ATOMIC_ACQUIRE);
                native_kqueue_unlock();
                long result=RAW(SYS_futex,&queue->ownership_epoch,FUTEX_WAIT_PRIVATE,epoch,0,0,0);
                if (result && result!=-EAGAIN) return result;
                continue;
            }
        }
        for (int i=0;i<count;i++) {
            unsigned int index=(uint32_t)events[i].data.u64;
            if (index>=D_KQUEUE_WATCHES) continue;
            struct native_knote *note=queue->watches+index;
            if (!note->used || !note->enabled || note->generation!=(events[i].data.u64>>32)) continue;
            struct darwin_kevent_qos event=note->event;
            event.flags&=D_EV_ONESHOT|D_EV_CLEAR|D_EV_DISPATCH|D_EV_UDATA_SPECIFIC;
            if (events[i].events&(EPOLLHUP|EPOLLRDHUP)) event.flags|=D_EV_EOF;
            if (event.filter==D_EVFILT_MACHPORT) {
#ifdef ARLINUX_DARLING
                if (darling_active) {
                    uint64_t before=data ? data->remaining : 0,residual=before;
                    long ready=darling_kq_read(note->fd,&event,data ? data->address : 0,
                                              before,data ? data->stack : 0,&residual);
                    if (ready<0) { native_kqueue_unlock(); return ready; }
                    if (data && ready) {
                        if (!data->stack) data->address+=before-residual;
                        data->remaining=residual;
                    }
                    if (!ready) {
                        struct epoll_event ep={.events=EPOLLIN};
                        if (note->event.flags&D_EV_CLEAR) ep.events|=EPOLLET;
                        if (note->event.flags&(D_EV_ONESHOT|D_EV_DISPATCH)) ep.events|=EPOLLONESHOT;
                        ep.data.u64=((uint64_t)note->generation<<32)|index;
                        long rearm=RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_MOD,note->fd,&ep,0,0);
                        if (rearm) { native_kqueue_unlock(); return rearm; }
                        continue;
                    }
                } else
#endif
                {
                struct ipc_endpoint *endpoint=ipc_find(event.ident);
                if (!endpoint || endpoint->kind) {
                    event.flags|=D_EV_VANISHED|D_EV_ONESHOT;
                    event.fflags=0; event.data=0;
                } else {
                    ipc_queue_lock(endpoint);
                    struct ipc_packet packet;
                    long size=__atomic_load_n(&endpoint->kind,__ATOMIC_ACQUIRE) ? -ECANCELED : ipc_peek(endpoint,&packet);
                    unsigned long message_size=0,total_size=0;
                    unsigned long status=D_KERN_SUCCESS;
                    if (size>=0 && (event.fflags&D_MACH_RCV_MSG)) {
                        unsigned long options=event.fflags;
                        uint64_t address=event.ext[0],bytes=event.ext[1];
                        int carved=!bytes;
                        if (carved) {
                            options|=D_MACH_RCV_LARGE|D_MACH_RCV_LARGE_IDENTITY;
                            /* Probe under the same queue lock to obtain the
                             * real message+trailer size without consuming it. */
                            status=ipc_receive_locked(endpoint,0,0,options,&message_size,&total_size);
                            address=data ? data->address+(data->stack ? data->remaining : 0) : 0;
                            if (status==D_MACH_RCV_TOO_LARGE && data && data->address && total_size<=data->remaining) {
                                address=data->address+(data->stack ? data->remaining-total_size : 0);
                                bytes=data->remaining>UINT32_MAX ? UINT32_MAX : data->remaining;
                            }
                        }
                        if (!carved || bytes) status=ipc_receive_locked(endpoint,address,bytes,options,
                            &message_size,&total_size);
                        if (carved) {
                            event.ext[0]=address;
                            if (!status) {
                                data->remaining-=total_size;
                                if (!data->stack) data->address+=total_size;
                                const char label[]="DARWIN_KEVENT_BUFFER_DELIVERY port/address/bytes/remaining follows\n";
                                RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
                                trace_call(0,0,endpoint->name); trace_call(1,0,address);
                                trace_call(2,0,total_size); trace_call(3,0,data->remaining);
                            }
                        }
                        event.ext[1]=status==D_MACH_RCV_TOO_LARGE ? message_size :
                            status ? 0 : total_size;
                        event.data=(status==D_MACH_RCV_TOO_LARGE &&
                            (options&D_MACH_RCV_LARGE_IDENTITY)) ? endpoint->name : 0;
                        event.fflags=status;
                    } else if (size>=0) { event.fflags=0; event.data=endpoint->name; }
                    ipc_queue_unlock(endpoint);
                    if (size==-EAGAIN || status==D_MACH_RCV_TIMED_OUT) {
                        struct epoll_event ep={.events=EPOLLIN};
                        if (note->event.flags&D_EV_CLEAR) ep.events|=EPOLLET;
                        if (note->event.flags&(D_EV_ONESHOT|D_EV_DISPATCH)) ep.events|=EPOLLONESHOT;
                        ep.data.u64=((uint64_t)note->generation<<32)|index;
                        long rearm=RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_MOD,note->fd,&ep,0,0);
                        if (rearm) { native_kqueue_unlock(); return rearm; }
                        continue;
                    }
                    if (size==-ECANCELED) {
                        event.flags|=D_EV_VANISHED|D_EV_ONESHOT;
                        event.fflags=0; event.data=0;
                    } else if (size<0) { native_kqueue_unlock(); return size; }
                }
                }
            } else if (event.filter==D_EVFILT_TIMER || event.filter==D_EVFILT_MEMORYSTATUS ||
                event.filter==D_EVFILT_WORKLOOP || (event.filter==D_EVFILT_USER &&
                (note->event.flags&(D_EV_CLEAR|D_EV_ONESHOT)))) {
                uint64_t value;
                long read=RAW(SYS_read,note->fd,&value,sizeof(value),0,0,0);
                if (read==-EAGAIN) continue;
                if (read!=(long)sizeof(value)) { native_kqueue_unlock(); return read<0 ? read : -EIO; }
                if (event.filter==D_EVFILT_TIMER) event.data=value;
                if (event.filter==D_EVFILT_MEMORYSTATUS) {
                    long state=native_memory_pressure();
                    if (state<0) { native_kqueue_unlock(); return state; }
                    uint32_t changed=note->memory_state!=(uint32_t)state;
                    note->memory_state=state;
                    event.fflags=changed ? ((uint32_t)state&note->event.fflags) : 0;
                    event.data=0;
                    if (!event.fflags) {
                        /* A sampling tick is not a pressure event. Keep the
                         * oneshot epoll registration armed until real change. */
                        struct epoll_event ep={.events=EPOLLIN};
                        if (note->event.flags&D_EV_CLEAR) ep.events|=EPOLLET;
                        if (note->event.flags&(D_EV_ONESHOT|D_EV_DISPATCH)) ep.events|=EPOLLONESHOT;
                        ep.data.u64=((uint64_t)note->generation<<32)|index;
                        long rearm=RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_MOD,note->fd,&ep,0,0);
                        if (rearm) { native_kqueue_unlock(); return rearm; }
                        continue;
                    }
                }
            } else if (event.filter!=D_EVFILT_USER) {
                int available=0;
                long result=RAW(SYS_ioctl,note->fd,FIONREAD,&available,0,0,0);
                event.data=result ? 1 : available;
            }
            if (guest_copy(&event,output+(uint64_t)emitted*sizeof(event),sizeof(event),1)) { native_kqueue_unlock(); return -EFAULT; }
            emitted++;
            if ((note->event.flags&D_EV_ONESHOT) || (event.flags&D_EV_VANISHED)) {
                RAW(SYS_epoll_ctl,queue->fd,EPOLL_CTL_DEL,note->fd,0,0,0);
                RAW(SYS_close,note->fd,0,0,0,0,0); note->used=0;
            } else if (note->event.flags&D_EV_DISPATCH) note->enabled=0;
        }
        native_kqueue_unlock();
        if (emitted || !timeout) return emitted;
    }
}
static uint64_t native_event_buffer(struct native_thread *thread)
{
    return thread->pthread-D_GUEST_PAGE_SIZE+D_KQUEUE_BUFFER_OFFSET;
}
static long native_event_collect(struct native_kqueue *queue,struct native_thread *thread,uint64_t buffer)
{
    /* The allocated page below pthread contains the event array followed by
     * actual Mach message storage. It stays alive until this worker returns. */
    uint64_t address=native_event_buffer(thread)+D_KQUEUE_BATCH*sizeof(struct darwin_kevent_qos);
    if (address>thread->pthread) return -EINVAL;
    struct native_kevent_data data={.address=address,.remaining=thread->pthread-address,.stack=1};
    return native_kqueue_collect(queue,buffer,D_KQUEUE_BATCH,-1,&data);
}
static __attribute__((noreturn)) void native_event_worker_start(struct native_thread *thread)
{
    struct native_kqueue *queue=native_kqueue_find(thread->event_queue);
    uint64_t buffer=native_event_buffer(thread);
    if (queue && queue->dynamic_id) {
        if (guest_copy(&queue->dynamic_id,buffer-sizeof(uint64_t),sizeof(uint64_t),1)) {
            RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0); __builtin_unreachable();
        }
        __atomic_store_n(&queue->owner_tid,thread->tid,__ATOMIC_RELEASE);
    }
    long count;
    do { count=queue ? native_event_collect(queue,thread,buffer) : -EBADF; } while (count==-EINTR);
    if (count<=0) { RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0); __builtin_unreachable(); }
    const char label[]="DARWIN_EVENT_DELIVERY queue-id/count/tid follows\n";
    RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
    trace_call(0,0,queue->dynamic_id); trace_call(1,0,count); trace_call(2,0,thread->tid);
    darwin_thread_enter(thread->guest_sp,workqueue_start_callback,thread->pthread,thread->port,
                        thread->stack_low,buffer,thread->flags|
                        (thread->workloop_id ? D_WQ_WORKLOOP : D_WQ_KEVENT|D_WQ_EVENT_MANAGER),count);
}
static long native_event_worker_return(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    struct native_kqueue *queue=thread ? native_kqueue_find(thread->event_queue) : NULL;
    if (!thread || thread->workqueue<2 || !queue || r[3]) return -EINVAL;
    if ((r[0]==D_WQ_WORKLOOP_RETURN)!=(thread->workloop_id!=0)) return -EINVAL;
    if (thread->workloop_id) {
        uint64_t id;
        if (guest_copy(&id,native_event_buffer(thread)-sizeof(id),sizeof(id),0)) return -EFAULT;
        if (id!=thread->workloop_id || queue->owner_tid!=thread->tid) return -ENOTSUP;
    }
    uint64_t buffer=r[1] ? r[1] : native_event_buffer(thread);
    long result=native_kqueue_changes(queue,r[1],r[2],buffer,D_KQUEUE_BATCH,NULL);
    if (result) return result;
    do { result=native_event_collect(queue,thread,buffer); } while (result==-EINTR);
    return result;
}
static long native_kqueue_start_worker(struct native_kqueue *queue,const sigset_t *mask)
{
    int expected=0;
    while (!__atomic_compare_exchange_n(&queue->worker,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        if (expected==2) return 0;
        RAW(SYS_futex,&queue->worker,FUTEX_WAIT_PRIVATE,1,0,0,0);
        expected=0;
    }
    uint64_t size=D_GUEST_PAGE_SIZE+D_WORKQUEUE_STACK_SIZE+
                  ((registered_pthread_size+D_GUEST_PAGE_SIZE-1)&~(D_GUEST_PAGE_SIZE-1));
    long memory=anonymous_aligned(size,PROT_READ|PROT_WRITE,D_GUEST_PAGE_SIZE-1);
    if ((unsigned long)memory>=(unsigned long)-4095) {
        __atomic_store_n(&queue->worker,0,__ATOMIC_RELEASE);
        RAW(SYS_futex,&queue->worker,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
        return memory;
    }
    long result=RAW(SYS_mprotect,memory,D_GUEST_PAGE_SIZE,PROT_NONE,0,0,0);
    uint64_t low=memory+D_GUEST_PAGE_SIZE,self=low+D_WORKQUEUE_STACK_SIZE;
    if (!result) {
        unsigned int bits=(__atomic_load_n(&queue->priority,__ATOMIC_ACQUIRE)>>D_PTHREAD_QOS_SHIFT)&D_PTHREAD_QOS_MASK;
        unsigned int qos=(bits && bits<=32 && !(bits&(bits-1))) ? (unsigned int)__builtin_ctz(bits)+1 : 4;
        unsigned long long arguments[6]={(unsigned int)queue->fd,queue->dynamic_id,
            self-D_GUEST_PAGE_SIZE,self,D_WQ_PRIO_QOS|D_WQ_NEWSPI|qos,0};
        result=native_thread_create_impl(arguments,mask,queue->dynamic_id ? 3 : 2,low);
        if ((unsigned long)result<(unsigned long)-4095) {
            __atomic_store_n(&queue->worker,2,__ATOMIC_RELEASE);
            RAW(SYS_futex,&queue->worker,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
            return 0;
        }
    }
    RAW(SYS_munmap,memory,size,0,0,0,0);
    __atomic_store_n(&queue->worker,0,__ATOMIC_RELEASE);
    RAW(SYS_futex,&queue->worker,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    return result;
}
static void native_event_dispatcher(void *argument)
{
    uint64_t mask=*(uint64_t *)argument;
    if(RAW(SYS_rt_sigprocmask,SIG_SETMASK,&mask,0,8,0,0))darling_abort("readiness signal mask");
    trace_record("DARWIN_EVENT_DISPATCHER tid=0x",RAW(SYS_gettid,0,0,0,0,0,0),native_event_dispatch_fd,0);
    for(;;) {
        struct epoll_event events[D_KQUEUE_BATCH];
        long count=RAW(SYS_epoll_pwait,native_event_dispatch_fd,events,D_KQUEUE_BATCH,-1,0,8);
        if(count==-EINTR)continue;
        if(count<0)darling_abort("wait for real workloop readiness");
        for(long i=0;i<count;i++) {
            struct native_kqueue *queue=events[i].data.ptr;
            if(!queue->used || __atomic_load_n(&queue->client_owner,__ATOMIC_ACQUIRE) ||
               __atomic_load_n(&queue->worker,__ATOMIC_ACQUIRE))continue;
            long result;
            unsigned int deferred=0;
            do {
                /* The clone helper and worker inherit the caller's actual
                 * syscall filter. No glibc, guest TLS or framework call is
                 * made by this native dispatcher. Kernel masks are 8 bytes. */
                result=native_kqueue_start_worker(queue,(const sigset_t *)&mask);
                if(result==-EAGAIN) {
                    if(!deferred++)trace_record("DARWIN_EVENT_WORKER_DEFERRED id=0x",queue->dynamic_id,0,result);
                    struct timespec delay={.tv_nsec=10000000};
                    RAW(SYS_nanosleep,&delay,0,0,0,0,0);
                }
            } while(result==-EAGAIN);
            if(result) {
                trace_record("DARWIN_EVENT_WORKER_ERROR id=0x",queue->dynamic_id,queue->fd,result);
                darling_abort("start ready workloop worker");
            }
        }
    }
}
static long native_event_dispatch_start(const sigset_t *mask)
{
    int expected=0;
    while(!__atomic_compare_exchange_n(&native_event_dispatch_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        if(expected==2)return 0;
        RAW(SYS_futex,&native_event_dispatch_state,FUTEX_WAIT_PRIVATE,1,0,0,0);expected=0;
    }
    long fd=RAW(SYS_epoll_create1,EPOLL_CLOEXEC,0,0,0,0,0);
    long memory=-ENOMEM,result=fd;
    if(fd>=0) {
        memory=RAW(SYS_mmap,0,D_THREAD_RUNTIME_STACK_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        result=memory;
        if((unsigned long)memory<(unsigned long)-4095) {
            native_event_dispatch_fd=fd;
            __builtin_memcpy(&native_event_dispatch_mask,mask,8);
            result=darwin_clone_callback(memory+D_THREAD_RUNTIME_STACK_SIZE,native_event_dispatcher,&native_event_dispatch_mask);
        }
    }
    if(result<0) {
        if((unsigned long)memory<(unsigned long)-4095)RAW(SYS_munmap,memory,D_THREAD_RUNTIME_STACK_SIZE,0,0,0,0);
        if(fd>=0)RAW(SYS_close,fd,0,0,0,0,0);
        native_event_dispatch_fd=-1;
    }
    __atomic_store_n(&native_event_dispatch_state,result<0 ? 0 : 2,__ATOMIC_RELEASE);
    RAW(SYS_futex,&native_event_dispatch_state,FUTEX_WAKE_PRIVATE,INT32_MAX,0,0,0);
    return result<0 ? result : 0;
}
static long native_kevent_qos(const unsigned long long *r,const sigset_t *mask)
{
    (void)mask;
    if (r[7]&~(D_KEVENT_IMMEDIATE|D_KEVENT_ERROR_EVENTS|D_KEVENT_STACK_DATA|D_KEVENT_WORKQ)) return -ENOTSUP;
    if (r[4]>D_KQUEUE_WATCHES) return -EINVAL;
    struct native_kevent_data data;
    long error=native_kevent_data_init(&data,r,r[7]);
    if (error) return error;
    int fd=r[0];
    if (r[7]&D_KEVENT_WORKQ) {
        if (!workqueue_opened || !workqueue_has_kevents) return -EINVAL;
        native_kqueue_lock();
        if (workqueue_kqueue<0) {
            long created=native_kqueue_create_locked(0);
            if (created<0) { native_kqueue_unlock(); return created; }
            workqueue_kqueue=created;
        }
        native_kqueue_unlock();
        fd=workqueue_kqueue;
    }
    struct native_kqueue *queue=native_kqueue_find(fd);
    if (!queue) return -EBADF;
    long result=native_kqueue_changes(queue,r[1],r[2],r[3],r[4],NULL);
    if (result) return result;
    if (r[7]&D_KEVENT_WORKQ) {
        result=native_kqueue_arm(queue);
        if (result) return result;
    }
    if (r[7]&D_KEVENT_ERROR_EVENTS) return 0;
    result=native_kqueue_collect(queue,r[3],r[4],(r[7]&D_KEVENT_IMMEDIATE) ? 0 : -1,&data);
    return native_kevent_data_finish(&data,result);
}
/* Dynamic queues are distinct objects. One bound Linux worker serializes
 * delivery for each queue; this does not implement EVFILT_WORKLOOP owner
 * scheduling beyond the Linux-backed owner gate and sync waits, or the
 * workloop control syscall. */
static long native_kevent_id(const unsigned long long *r,const sigset_t *mask)
{
    (void)mask;
    unsigned long flags=r[7];
    if (!(flags&D_KEVENT_WORKLOOP) ||
        (flags&~(unsigned long)(D_KEVENT_WORKLOOP|D_KEVENT_IMMEDIATE|D_KEVENT_ERROR_EVENTS|D_KEVENT_STACK_DATA|
            D_KEVENT_MUST_EXIST|D_KEVENT_MUST_NOT_EXIST)) || !r[0] || r[0]==UINT64_MAX) return -ENOTSUP;
    if (r[2]>D_KQUEUE_WATCHES || r[4]>D_KQUEUE_WATCHES ||
        ((flags&(D_KEVENT_MUST_EXIST|D_KEVENT_MUST_NOT_EXIST))==
         (D_KEVENT_MUST_EXIST|D_KEVENT_MUST_NOT_EXIST))) return -EINVAL;
    struct native_kevent_data data;
    long error=native_kevent_data_init(&data,r,flags);
    if (error) return error;
    if (!workqueue_opened || !workqueue_has_kevents) return -EINVAL;
    struct native_kqueue *queue=NULL;
    native_kqueue_lock();
    for (struct native_kqueue *candidate=native_kqueues;candidate;candidate=candidate->next)
        if (candidate->dynamic_id==r[0]) { queue=candidate; break; }
    if (queue && (flags&D_KEVENT_MUST_NOT_EXIST)) { native_kqueue_unlock(); return -EEXIST; }
    if (!queue && (flags&D_KEVENT_MUST_EXIST)) { native_kqueue_unlock(); return -ENOENT; }
    if (!queue) {
        long fd=native_kqueue_create_locked(r[0]);
        if (fd<0) { native_kqueue_unlock(); return fd; }
        queue=native_kqueue_find(fd);
    }
    native_kqueue_unlock();
    /* Polling a bound workloop belongs to its actual servicer. Registration
     * with ERROR_EVENTS is allowed from the client that owns its sources. */
    if (!(flags&D_KEVENT_ERROR_EVENTS) && queue->owner_tid!=RAW(SYS_gettid,0,0,0,0,0,0)) {
        struct native_thread *caller=native_current_thread();
        if (!caller || __atomic_load_n(&queue->client_owner,__ATOMIC_ACQUIRE)!=caller->port) return -EXDEV;
    }
    int waited=0;
    long result=native_kqueue_changes(queue,r[1],r[2],r[3],r[4],&waited);
    if (result || waited) return result;
    if (r[2]) {
        struct darwin_kevent_qos event;
        if (guest_copy(&event,r[1],sizeof(event),0)) return -EFAULT;
        if (event.qos) __atomic_store_n(&queue->priority,(uint32_t)event.qos&D_PTHREAD_PRIORITY_MASK,__ATOMIC_RELEASE);
    }
    result=native_kqueue_arm(queue);
    if (result) return result;
    if (flags&D_KEVENT_ERROR_EVENTS) return 0;
    result=native_kqueue_collect(queue,r[3],r[4],(flags&D_KEVENT_IMMEDIATE) ? 0 : -1,&data);
    return native_kevent_data_finish(&data,result);
}

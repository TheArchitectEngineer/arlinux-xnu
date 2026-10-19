/* Darwin kevent_qos backed by Linux epoll, eventfd and timerfd. Real readiness
 * drives registered libpthread kevent callbacks; unsupported filters fail.
 * This is the process workqueue/basic descriptor event path, not workloops,
 * arbitrary Mach message transfer or a complete XNU event implementation. */
#pragma once
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include "darwin-memory-pressure.h"
struct native_knote {
    struct darwin_kevent_qos event;
    int used,fd,enabled; uint32_t generation,memory_state;
};
struct native_kqueue {
    int used,fd,worker;
    struct native_knote watches[D_KQUEUE_WATCHES];
};
static struct native_kqueue native_kqueues[D_KQUEUE_COUNT];
static int native_kqueue_mutex,workqueue_kqueue=-1;
static unsigned int native_kevent_errors;
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
static struct native_kqueue *native_kqueue_find(int fd)
{
    for (unsigned int i=0;i<D_KQUEUE_COUNT;i++)
        if (__atomic_load_n(&native_kqueues[i].used,__ATOMIC_ACQUIRE) && native_kqueues[i].fd==fd)
            return native_kqueues+i;
    return NULL;
}
/* Caller holds the namespace lock while publishing a newly allocated queue. */
static long native_kqueue_create_locked(void)
{
    for (unsigned int i=0;i<D_KQUEUE_COUNT;i++) if (!native_kqueues[i].used) {
        long fd=RAW(SYS_epoll_create1,EPOLL_CLOEXEC,0,0,0,0,0);
        if (fd>=0) {
            native_kqueues[i].fd=fd;
            __atomic_store_n(&native_kqueues[i].used,1,__ATOMIC_RELEASE);
        }
        return fd;
    }
    return -EMFILE;
}
static long native_kqueue_create(void)
{
    native_kqueue_lock();
    long result=native_kqueue_create_locked();
    native_kqueue_unlock();
    return result;
}
static long native_knote_change(struct native_kqueue *queue,const struct darwin_kevent_qos *event)
{
    if (event->xflags || (event->flags&~(D_EV_ADD|D_EV_DELETE|D_EV_ENABLE|D_EV_DISABLE|
        D_EV_ONESHOT|D_EV_CLEAR|D_EV_RECEIPT|D_EV_DISPATCH|D_EV_UDATA_SPECIFIC))) return -ENOTSUP;
    /* UDATA-specific memory sources have separate subscriptions and ordinary
     * DISPATCH rearm/delete semantics. Deferred deletion for arbitrary other
     * source kinds is still unsupported. */
    if ((event->flags&D_EV_UDATA_SPECIFIC) && event->filter!=D_EVFILT_MEMORYSTATUS) return -ENOTSUP;
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
        case D_EVFILT_USER: fd=RAW(SYS_eventfd2,0,EFD_CLOEXEC|EFD_NONBLOCK,0,0,0,0); break;
        case D_EVFILT_TIMER: case D_EVFILT_MEMORYSTATUS:
            fd=RAW(SYS_timerfd_create,CLOCK_MONOTONIC,TFD_CLOEXEC|TFD_NONBLOCK,0,0,0,0); break;
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
    if (event->filter==D_EVFILT_MEMORYSTATUS && (event->flags&D_EV_ADD)) {
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
        if (event->data<=0 || (event->fflags&~(D_NOTE_SECONDS|D_NOTE_USECONDS|D_NOTE_NSECONDS|D_NOTE_ABSOLUTE))) error=-ENOTSUP;
        else {
            unsigned int units=event->fflags&(D_NOTE_SECONDS|D_NOTE_USECONDS|D_NOTE_NSECONDS);
            uint64_t scale=units==D_NOTE_SECONDS ? 1000000000 : units==D_NOTE_USECONDS ? 1000 : units==D_NOTE_NSECONDS ? 1 : 1000000;
            if ((units && (units&(units-1))) || (uint64_t)event->data>INT64_MAX/scale) error=-EINVAL;
            else {
                uint64_t nanoseconds=(uint64_t)event->data*scale;
                struct itimerspec timer={.it_value={nanoseconds/1000000000,nanoseconds%1000000000}};
                if (!(event->flags&(D_EV_ONESHOT)) && !(event->fflags&D_NOTE_ABSOLUTE)) timer.it_interval=timer.it_value;
                error=RAW(SYS_timerfd_settime,note->fd,(event->fflags&D_NOTE_ABSOLUTE) ? TFD_TIMER_ABSTIME : 0,&timer,0,0,0);
                note->event.fflags=event->fflags;
            }
        }
    }
    if (error) {
        if (fresh) RAW(SYS_close,note->fd,0,0,0,0,0);
        return error;
    }
    if (event->flags&D_EV_ADD) {
        note->event.flags=event->flags;
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
                                  uint64_t output,unsigned int capacity)
{
    if (count>D_KQUEUE_WATCHES) return -EINVAL;
    unsigned int emitted=0;
    for (unsigned int i=0;i<count;i++) {
        struct darwin_kevent_qos event;
        if (guest_copy(&event,changes+(uint64_t)i*sizeof(event),sizeof(event),0)) return -EFAULT;
        native_kqueue_lock(); long result=native_knote_change(queue,&event); native_kqueue_unlock();
        if (result && __atomic_fetch_add(&native_kevent_errors,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
            const char label[]="DARWIN_KEVENT_ERROR ident/filter/flags/fflags/xflags/result follows\n";
            RAW(SYS_write,2,label,sizeof(label)-1,0,0,0);
            trace_call(0,0,event.ident); trace_call(1,0,(long)event.filter);
            trace_call(2,0,event.flags); trace_call(3,0,event.fflags);
            trace_call(4,0,event.xflags); trace_call(5,0,result);
        }
        if (result || (event.flags&D_EV_RECEIPT)) {
            if (emitted==capacity) return result ? result : -ENOBUFS;
            event.flags=D_EV_ERROR; event.data=result ? darwin_error(result) : 0;
            if (guest_copy(&event,output+(uint64_t)emitted*sizeof(event),sizeof(event),1)) return -EFAULT;
            emitted++;
        }
    }
    return emitted;
}
static long native_kqueue_collect(struct native_kqueue *queue,uint64_t output,unsigned int capacity,int timeout)
{
    if (!capacity) return 0;
    if (capacity>D_KQUEUE_BATCH) capacity=D_KQUEUE_BATCH;
    struct epoll_event events[D_KQUEUE_BATCH];
    for (;;) {
        long count=RAW(SYS_epoll_pwait,queue->fd,events,capacity,timeout,0,8);
        if (count<=0) return count;
        unsigned int emitted=0;
        native_kqueue_lock();
        for (int i=0;i<count;i++) {
            unsigned int index=(uint32_t)events[i].data.u64;
            if (index>=D_KQUEUE_WATCHES) continue;
            struct native_knote *note=queue->watches+index;
            if (!note->used || !note->enabled || note->generation!=(events[i].data.u64>>32)) continue;
            struct darwin_kevent_qos event=note->event;
            event.flags&=D_EV_ONESHOT|D_EV_CLEAR|D_EV_DISPATCH|D_EV_UDATA_SPECIFIC;
            if (events[i].events&(EPOLLHUP|EPOLLRDHUP)) event.flags|=D_EV_EOF;
            if (event.filter==D_EVFILT_TIMER || event.filter==D_EVFILT_MEMORYSTATUS || (event.filter==D_EVFILT_USER &&
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
            if (note->event.flags&D_EV_ONESHOT) {
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
static __attribute__((noreturn)) void native_event_worker_start(struct native_thread *thread)
{
    struct native_kqueue *queue=native_kqueue_find(workqueue_kqueue);
    uint64_t buffer=native_event_buffer(thread);
    long count;
    do { count=queue ? native_kqueue_collect(queue,buffer,D_KQUEUE_BATCH,-1) : -EBADF; } while (count==-EINTR);
    if (count<=0) { RAW(SYS_exit_group,D_RUNTIME_FATAL_EXIT,0,0,0,0,0); __builtin_unreachable(); }
    darwin_thread_enter(thread->guest_sp,workqueue_start_callback,thread->pthread,thread->port,
                        thread->stack_low,buffer,thread->flags|D_WQ_KEVENT|D_WQ_EVENT_MANAGER,count);
}
static long native_event_worker_return(const unsigned long long *r)
{
    struct native_thread *thread=native_current_thread();
    struct native_kqueue *queue=native_kqueue_find(workqueue_kqueue);
    if (!thread || thread->workqueue!=2 || !queue || r[3]) return -EINVAL;
    uint64_t buffer=r[1] ? r[1] : native_event_buffer(thread);
    long result=native_kqueue_changes(queue,r[1],r[2],buffer,D_KQUEUE_BATCH);
    if (result) return result;
    do { result=native_kqueue_collect(queue,buffer,D_KQUEUE_BATCH,-1); } while (result==-EINTR);
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
        unsigned long long arguments[6]={0,0,self-D_GUEST_PAGE_SIZE,self,D_WQ_PRIO_QOS|D_WQ_NEWSPI|4,0};
        result=native_thread_create_impl(arguments,mask,2,low);
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
static long native_kevent_qos(const unsigned long long *r,const sigset_t *mask)
{
    if (r[7]&~(D_KEVENT_IMMEDIATE|D_KEVENT_ERROR_EVENTS|D_KEVENT_STACK_DATA|D_KEVENT_WORKQ)) return -ENOTSUP;
    if (r[4]>D_KQUEUE_WATCHES) return -EINVAL;
    int fd=r[0];
    if (r[7]&D_KEVENT_WORKQ) {
        if (!workqueue_opened || !workqueue_has_kevents) return -EINVAL;
        native_kqueue_lock();
        if (workqueue_kqueue<0) {
            long created=native_kqueue_create_locked();
            if (created<0) { native_kqueue_unlock(); return created; }
            workqueue_kqueue=created;
        }
        native_kqueue_unlock();
        fd=workqueue_kqueue;
    }
    struct native_kqueue *queue=native_kqueue_find(fd);
    if (!queue) return -EBADF;
    long result=native_kqueue_changes(queue,r[1],r[2],r[3],r[4]);
    if (result) return result;
    if (r[7]&D_KEVENT_WORKQ) {
        result=native_kqueue_start_worker(queue,mask);
        if (result) return result;
    }
    if (r[7]&D_KEVENT_ERROR_EVENTS) return 0;
    return native_kqueue_collect(queue,r[3],r[4],(r[7]&D_KEVENT_IMMEDIATE) ? 0 : -1);
}

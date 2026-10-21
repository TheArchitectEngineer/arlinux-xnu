/* Real Linux COW fork, with a distinct Darling task/client in the child.
 * Framework sessions and multithreaded runtime quiescence are not implemented
 * here. Reject those cases before creating a child rather than copying live
 * service ownership, locked queues or compositor descriptors into a new task. */
#pragma once
static long native_fork(int *is_child)
{
    *is_child=0;
#ifdef ARLINUX_DARLING
    struct native_thread *current=native_current_thread();
    if(!darling_active || current!=native_threads || native_bootstrap_port ||
       ui.control || workqueue_opened)return -ENOTSUP;
    for(unsigned int i=1;i<NATIVE_THREADS;i++)
        if(__atomic_load_n(&native_threads[i].state,__ATOMIC_ACQUIRE)>=2 &&
           __atomic_load_n(&native_threads[i].tid,__ATOMIC_ACQUIRE))return -ENOTSUP;
    int ready[2];
    long error=RAW(SYS_pipe2,ready,O_CLOEXEC,0,0,0,0);
    if(error)return error;
    long result=RAW(SYS_clone,SIGCHLD,0,0,0,0,0);
    if(!result) {
        RAW(SYS_close,ready[0],0,0,0,0,0);
        native_threads[0].tid=RAW(SYS_gettid,0,0,0,0,0,0);
        native_threads[0].special_reply_port=0;
        /* Original libpthread registers its child callbacks again. Its parent
         * registration does not describe a new kernel task's registration. */
        thread_start_callback=workqueue_start_callback=0;
        registered_pthread_size=registered_tsd=registered_thread_slot=0;
        darling_after_fork();
        native_threads[0].port=darling_thread_self();
        trace_record("DARWIN_FORK_CHILD pid=0x",native_threads[0].tid,CURRENT_TASK_PORT,native_threads[0].port);
        unsigned char byte=1;long written;
        do {written=RAW(SYS_write,ready[1],&byte,1,0,0,0);}while(written==-EINTR);
        RAW(SYS_close,ready[1],0,0,0,0,0);
        if(written!=1)darling_abort("publish child registration");
        *is_child=1;
        return native_threads[0].tid;
    }
    RAW(SYS_close,ready[1],0,0,0,0,0);
    if(result>0) {
        /* Keep the real parent alive until the core has inherited its task
         * special ports. No guest child code runs before that registration. */
        struct pollfd poller={.fd=ready[0],.events=POLLIN};
        struct timespec timeout={.tv_sec=10};long status;
        do {status=RAW(SYS_ppoll,&poller,1,&timeout,0,0,0);}while(status==-EINTR);
        unsigned char byte=0;
        long bytes=status>0 ? RAW(SYS_read,ready[0],&byte,1,0,0,0) : status;
        if(bytes!=1 || byte!=1) {
            RAW(SYS_kill,result,SIGKILL,0,0,0,0);
            long waited;do {waited=RAW(SYS_wait4,result,0,0,0,0,0);}while(waited==-EINTR);
            trace_record("DARWIN_FORK_REGISTRATION_FAILED child=0x",result,status,bytes);
            result=-EIO;
        } else trace_record("DARWIN_FORK_PARENT child=0x",result,CURRENT_TASK_PORT,0);
    }
    RAW(SYS_close,ready[0],0,0,0,0,0);
    return result;
#else
    return -ENOTSUP;
#endif
}

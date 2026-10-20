/* Darling kqchannel transport. Follows the community libkqueue Mach-port
 * filter, using raw Linux FDs and the caller's actual kevent data area. */
#pragma once
#ifdef ARLINUX_DARLING
static unsigned int darling_kq_traces;
static long darling_kq_packet(int fd,void *packet,size_t size,int flags)
{
    long count;
    do { count=RAW(SYS_recvfrom,fd,packet,size,flags,0,0); } while (count==-EINTR);
    return count;
}
static long darling_kq_send(int fd,const void *packet,size_t size)
{
    long count;
    do { count=RAW(SYS_sendto,fd,packet,size,MSG_NOSIGNAL,0,0); } while (count==-EINTR);
    return count==(long)size ? 0 : count<0 ? count : -EIO;
}
static long darling_kq_open(const struct darwin_kevent_qos *event)
{
    int fd=-1;
    int status=dserver_rpc_kqchan_mach_port_open(event->ident,(void *)event->ext[0],
                                               event->ext[1],event->fflags,&fd);
    if (status || fd<0) return status<0 ? status : -EIO;
    long error=RAW(SYS_fcntl,fd,F_SETFD,FD_CLOEXEC,0,0,0);
    if (error) { RAW(SYS_close,fd,0,0,0,0,0); return error; }
    if (__atomic_fetch_add(&darling_kq_traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARLING_KQCHAN_OPEN port/fd/flags follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,event->ident); trace_call(1,0,fd); trace_call(2,0,event->fflags);
    }
    return fd;
}
static long darling_kq_modify(int fd,const struct darwin_kevent_qos *event)
{
    (void)darling_socket(); /* The server resolves this real caller thread. */
    dserver_kqchan_call_mach_port_modify_t call={
        .header={.number=dserver_kqchan_msgnum_mach_port_modify,
                 .pid=RAW(SYS_getpid,0,0,0,0,0,0),.tid=RAW(SYS_gettid,0,0,0,0,0,0)},
        .receive_buffer=event->ext[0],.receive_buffer_size=event->ext[1],
        .saved_filter_flags=event->fflags};
    long error=darling_kq_send(fd,&call,sizeof(call));
    if (error) return error;
    /* The fork rechecks the actual Mach queue after the modification ack.
     * Earlier notifications can therefore be drained without losing readiness. */
    for (;;) {
        union { dserver_kqchan_call_notification_t notification;
                dserver_kqchan_reply_mach_port_modify_t reply; } packet;
        long count=darling_kq_packet(fd,&packet,sizeof(packet),0);
        if (count<0) return count;
        if (count==(long)sizeof(packet.notification) &&
            packet.notification.header.number==dserver_kqchan_msgnum_notification) continue;
        if (count!=(long)sizeof(packet.reply) ||
            packet.reply.header.number!=dserver_kqchan_msgnum_mach_port_modify) return -EPROTO;
        return packet.reply.header.code;
    }
}
static long darling_kq_read(int fd,struct darwin_kevent_qos *event,uint64_t address,
                            uint64_t size,int stack,uint64_t *residual)
{
    dserver_kqchan_call_notification_t notification;
    long count=darling_kq_packet(fd,&notification,sizeof(notification),MSG_DONTWAIT);
    if (count==-EAGAIN) return 0;
    if (count<0) return count;
    if (count!=(long)sizeof(notification) ||
        notification.header.number!=dserver_kqchan_msgnum_notification) return -EPROTO;
    (void)darling_socket();
    dserver_kqchan_call_mach_port_read_t call={
        .header={.number=dserver_kqchan_msgnum_mach_port_read,
                 .pid=RAW(SYS_getpid,0,0,0,0,0,0),.tid=RAW(SYS_gettid,0,0,0,0,0,0)},
        .default_buffer=address,.default_buffer_size=size,
        .data_flags=stack ? D_KEVENT_STACK_DATA : 0};
    long error=darling_kq_send(fd,&call,sizeof(call));
    if (error) return error;
    dserver_kqchan_reply_mach_port_read_t reply;
    count=darling_kq_packet(fd,&reply,sizeof(reply),0);
    if (count<0) return count;
    if (count!=(long)sizeof(reply) || reply.header.number!=dserver_kqchan_msgnum_mach_port_read) return -EPROTO;
    if (reply.header.code==0xdead) return 0; /* upstream's explicit no-event reply */
    if (reply.header.code) return reply.header.code<0 ? reply.header.code : -EIO;
    if (reply.data_residual>size) return -EPROTO;
    *residual=reply.data_residual;
    if (reply.kev.flags) event->flags=reply.kev.flags;
    event->data=reply.kev.data; event->fflags=reply.kev.fflags;
    for (unsigned int i=0;i<4;i++) event->ext[i]=reply.kev.ext[i];
    if (__atomic_fetch_add(&darling_kq_traces,1,__ATOMIC_RELAXED)<D_DIAGNOSTIC_REQUEST_LIMIT) {
        const char label[]="DARLING_KQCHAN_EVENT port/status/address/bytes/residual follows\n";
        RAW(SYS_write,native_trace_fd,label,sizeof(label)-1,0,0,0);
        trace_call(0,0,event->ident); trace_call(1,0,event->fflags);
        trace_call(2,0,event->ext[0]); trace_call(3,0,event->ext[1]); trace_call(4,0,*residual);
    }
    return 1;
}
#endif

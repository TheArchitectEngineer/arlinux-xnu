#ifndef _DARLING_EMULATION_FOR_LIBKQUEUE_H_
#define _DARLING_EMULATION_FOR_LIBKQUEUE_H_

#include <stdint.h>
#include <sys/socket.h>
#include <stddef.h>

#include <darling/emulation/common/base.h>

VISIBLE
int _dserver_rpc_kqchan_mach_port_open_4libkqueue(uint32_t port_name, void* receive_buffer, uint64_t receive_buffer_size, uint64_t saved_filter_flags, int* out_socket);

VISIBLE
int _dserver_rpc_kqchan_proc_open_4libkqueue(int32_t pid, uint32_t flags, int* out_socket);

VISIBLE
int _dup_4libkqueue(int fd);

VISIBLE
ssize_t _recv_raw_4libkqueue(int fd, void* buf, size_t len);

VISIBLE
ssize_t _recvmsg_raw_4libkqueue(int fd, struct msghdr* msg, int flags);

VISIBLE
ssize_t _send_raw_4libkqueue(int fd, const void* buf, size_t len);

#endif // _DARLING_EMULATION_FOR_LIBKQUEUE_H_

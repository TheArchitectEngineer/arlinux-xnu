#include <darlingserver/duct-tape.h>

#include <mach/mach_traps.h>
#include <kern/task.h>
#include <kern/ipc_tt.h>
#include <ipc/ipc_object.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_space.h>
#include <mach/task_server.h>

int dtape_task_set_special_port(int32_t which, uint32_t name) {
	ipc_object_t object = IO_NULL;
	if (name != MACH_PORT_NULL) {
		mach_msg_guard_flags_t guard_flags = 0;
		kern_return_t kr = ipc_object_copyin(current_space(), name, MACH_MSG_TYPE_COPY_SEND,
			&object, 0, &guard_flags, 0);
		if (kr != KERN_SUCCESS) return kr;
	}
	// XNU consumes the copied send reference only on success.
	kern_return_t kr = task_set_special_port(current_task(), which, (ipc_port_t)object);
	if (kr != KERN_SUCCESS && IP_VALID((ipc_port_t)object)) ipc_port_release_send((ipc_port_t)object);
	return kr;
}

uint32_t dtape_task_self_trap(void) {
	return task_self_trap(NULL);
};

uint32_t dtape_host_self_trap(void) {
	return host_self_trap(NULL);
};

uint32_t dtape_thread_self_trap(void) {
	return thread_self_trap(NULL);
};

uint32_t dtape_mach_reply_port(void) {
	return mach_reply_port(NULL);
};

uint32_t dtape_thread_get_special_reply_port(void) {
	return thread_get_special_reply_port(NULL);
};

uint32_t dtape_mk_timer_create(void) {
	return mk_timer_create_trap(NULL);
};

DSERVER_DTAPE_DEFS;

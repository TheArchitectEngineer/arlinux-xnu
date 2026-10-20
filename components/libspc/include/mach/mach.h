/* Darwin LP64 message ABI for the unmodified community codec on Linux.
 * Layout source: XNU osfmk/mach/message.h. This is not a second Mach backend. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "darwin-abi.h"
typedef uint32_t mach_port_t, mach_port_right_t, mach_msg_type_name_t;
typedef uint8_t mach_msg_descriptor_type_t;
typedef struct {
    uint32_t msgh_bits, msgh_size, msgh_remote_port, msgh_local_port, msgh_voucher_port;
    int32_t msgh_id;
} mach_msg_header_t;
typedef struct { uint32_t msgh_descriptor_count; } mach_msg_body_t;
typedef struct {
    uint32_t name, pad1; uint16_t pad2; uint8_t disposition, type;
} mach_msg_port_descriptor_t;
typedef struct {
    uint32_t pad1, pad2; uint16_t pad3; uint8_t pad4, type;
} mach_msg_type_descriptor_t;
typedef struct {
    uint64_t address; uint8_t deallocate, copy, pad1, type; uint32_t size;
} __attribute__((packed,aligned(4))) mach_msg_ool_descriptor_t;
typedef mach_msg_ool_descriptor_t mach_msg_ool_ports_descriptor_t;
#define MACH_PORT_NULL D_MACH_PORT_NULL
#define MACH_MSGH_BITS_COMPLEX D_MACH_MSG_COMPLEX
#define MACH_MSGH_BITS(r,l) ((r)|((l)<<8))
#define MACH_MSGH_BITS_REMOTE(b) ((b)&255)
#define MACH_MSGH_BITS_LOCAL(b) (((b)>>8)&255)
#define MACH_MSG_PORT_DESCRIPTOR 0
#define MACH_MSG_OOL_DESCRIPTOR 1
#define MACH_MSG_OOL_PORTS_DESCRIPTOR 2
#define MACH_MSG_TYPE_COPY_SEND D_MACH_TYPE_COPY_SEND
#define MACH_MSG_TYPE_MOVE_RECEIVE 16
#define MACH_PORT_RIGHT_SEND D_PORT_RIGHT_SEND
#define MACH_PORT_RIGHT_RECEIVE D_PORT_RIGHT_RECEIVE
mach_port_t mach_task_self(void);
int mach_port_deallocate(mach_port_t task, mach_port_t port);

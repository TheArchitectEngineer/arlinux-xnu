/* Linux HTTP transport shared with the TLS-free guest API. No Apple objects or
 * callbacks cross into the host worker. Ownership: submit -> done -> release;
 * cancelled transfers remain owned by the worker until libcurl has stopped. */
#pragma once
#include "ios-platform-abi.h"
#define ARLINUX_HTTP_SLOTS 8U
#define ARLINUX_HTTP_BODY_LIMIT (16U*1024U*1024U)
#define ARLINUX_HTTP_HEADER_LIMIT 65536U
enum native_http_state { HTTP_FREE,HTTP_CLAIMED,HTTP_PENDING,HTTP_WORKING,HTTP_DONE };
struct native_http_slot {
    uint32_t state,cancelled,released,generation;
    char url[8192],method[32],headers[ARLINUX_HTTP_HEADER_LIMIT];
    void *request_body;
    uint64_t request_size,deadline_ns;
    struct arlinux_http_response response;
};
extern struct native_http_slot native_http_slots[ARLINUX_HTTP_SLOTS];
extern int native_http_event;
int native_http_start(void);

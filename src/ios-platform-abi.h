/* Private ARLinux guest-platform ABI, not an Apple implementation or syscall.
 * Query v5 returns a sized normal-call API. The syscall handler itself never
 * calls UIKit/CF: the real guest dylib constructor invokes it after return. */
#pragma once
#include <stdint.h>
#define ARLINUX_IOS_PLATFORM_ABI_VERSION 5UL
#define ARLINUX_IOS_PLATFORM_QUERY 0x4152494fUL
#define ARLINUX_IOS_PLATFORM_LIBRARY "libarlinux-ios.dylib"
/* Actual per-dimension limit enforced by the CPU CALayer backing allocator. */
#define ARLINUX_IOS_SOFTWARE_SURFACE_MAX 8192U
enum arlinux_ios_window_flags {
    ARLINUX_IOS_WINDOW_CONFIGURED=1U,
    ARLINUX_IOS_WINDOW_ACTIVE=2U,
    ARLINUX_IOS_WINDOW_CLOSE_REQUESTED=4U,
    ARLINUX_IOS_WINDOW_BACKEND_ALIVE=8U,
    ARLINUX_IOS_WINDOW_KEYBOARD_AVAILABLE=16U
};
/* Agreed surface-buffer geometry, not an unaccepted resize proposal. Identity
 * is the actual Darling audit PID/incarnation of this guest process. */
struct arlinux_ios_window_state {
    uint32_t generation,width,height,refresh_millihz,flags,pid,incarnation;
};
enum arlinux_ios_pointer_phase {
    ARLINUX_IOS_POINTER_DOWN=1, ARLINUX_IOS_POINTER_MOVE,
    ARLINUX_IOS_POINTER_UP, ARLINUX_IOS_POINTER_CANCEL
};
/* Surface-local Wayland 24.8 coordinates, Linux monotonic nanoseconds (the
 * native runtime's mach_absolute_time scale). One primary-pointer contact. */
enum arlinux_ios_input_kind {
    ARLINUX_IOS_INPUT_POINTER=1,ARLINUX_IOS_INPUT_TEXT,ARLINUX_IOS_INPUT_DELETE
};
struct arlinux_ios_input_event {
    uint64_t nanoseconds;
    uint32_t kind,sequence;
    union {
        struct {int32_t x_fixed,y_fixed;uint32_t phase;} pointer;
        char utf8[64];
    } data;
};
#define ARLINUX_IOS_INPUT_CAPACITY 64U
enum arlinux_http_error { AR_HTTP_OK,AR_HTTP_IO,AR_HTTP_TIMEOUT,AR_HTTP_DNS,
    AR_HTTP_CONNECT,AR_HTTP_TLS,AR_HTTP_CANCELLED,AR_HTTP_TOO_LARGE,AR_HTTP_INVALID };
struct arlinux_http_request {
    const char *url,*method,*headers;
    const void *body;
    uint64_t body_size;
    uint32_t timeout_ms,reserved;
};
struct arlinux_http_response {
    const void *body;
    const char *headers;
    uint64_t body_size,header_size;
    uint32_t status,error,transport_code,reserved;
};
struct arlinux_ios_platform_api {
    uint32_t version,size;
    long (*attach)(void);
    long (*read_window)(struct arlinux_ios_window_state *state,unsigned long size);
    /* 1 event, 0 empty, negative errno; overflow discards the broken sequence
     * and requires cancelling any active guest contact. Main-loop consumer. */
    long (*read_input)(struct arlinux_ios_input_event *event,unsigned long size);
    long (*http_submit)(const struct arlinux_http_request *request);
    /* 0 pending, 1 immutable result, negative errno. Result pointers are valid
     * until release; caller serializes result copying against cancellation. */
    long (*http_result)(uint64_t token,struct arlinux_http_response *response);
    long (*http_release)(uint64_t token);
};

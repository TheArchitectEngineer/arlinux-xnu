/* Private ARLinux guest-platform ABI, not an Apple implementation or syscall.
 * Query v3 returns a sized normal-call API. The syscall handler itself never
 * calls UIKit/CF: the real guest dylib constructor invokes it after return. */
#pragma once
#include <stdint.h>
#define ARLINUX_IOS_PLATFORM_ABI_VERSION 3UL
#define ARLINUX_IOS_PLATFORM_QUERY 0x4152494fUL
#define ARLINUX_IOS_PLATFORM_LIBRARY "libarlinux-ios.dylib"
enum arlinux_ios_window_flags {
    ARLINUX_IOS_WINDOW_CONFIGURED=1U,
    ARLINUX_IOS_WINDOW_ACTIVE=2U,
    ARLINUX_IOS_WINDOW_CLOSE_REQUESTED=4U,
    ARLINUX_IOS_WINDOW_BACKEND_ALIVE=8U
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
struct arlinux_ios_pointer_event {
    uint64_t nanoseconds;
    int32_t x_fixed,y_fixed;
    uint32_t phase,sequence;
};
#define ARLINUX_IOS_INPUT_CAPACITY 64U
struct arlinux_ios_platform_api {
    uint32_t version,size;
    long (*attach)(void);
    long (*read_window)(struct arlinux_ios_window_state *state,unsigned long size);
    /* 1 event, 0 empty, negative errno; overflow discards the broken sequence
     * and requires cancelling any active guest contact. Main-loop consumer. */
    long (*read_pointer)(struct arlinux_ios_pointer_event *event,unsigned long size);
};

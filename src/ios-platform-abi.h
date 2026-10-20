/* Private ARLinux guest-platform ABI, not an Apple implementation or syscall.
 * Query v2 returns a sized normal-call API. The syscall handler itself never
 * calls UIKit/CF: the real guest dylib constructor invokes it after return. */
#pragma once
#include <stdint.h>
#define ARLINUX_IOS_PLATFORM_ABI_VERSION 2UL
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
struct arlinux_ios_platform_api {
    uint32_t version,size;
    long (*attach)(void);
    long (*read_window)(struct arlinux_ios_window_state *state,unsigned long size);
};

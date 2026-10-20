/* Private ARLinux guest-platform ABI, not an Apple implementation or syscall.
 * Query v1 returns a normal-call entry point. The syscall handler itself never
 * calls UIKit/CF: the real guest dylib constructor invokes it after return. */
#pragma once
#define ARLINUX_IOS_PLATFORM_ABI_VERSION 1UL
#define ARLINUX_IOS_PLATFORM_QUERY 0x4152494fUL
#define ARLINUX_IOS_PLATFORM_LIBRARY "libarlinux-ios.dylib"
typedef long (*arlinux_ios_platform_attach_fn)(void);

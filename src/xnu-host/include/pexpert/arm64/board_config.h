#ifndef ARLINUX_XNU_USERMODE_BOARD_CONFIG_H
#define ARLINUX_XNU_USERMODE_BOARD_CONFIG_H
/* New host platform: no Apple SoC, EL1, MMIO, PAC or hardware interrupt driver.
 * This describes the intended platform; it does not provide a pmap backend. */
#define MAX_CPUS 1
#define MAX_CPU_CLUSTERS 1
#define MAX_CPU_CLUSTER_PHY_ID 0
#define MAX_L2_CLINE 6
/* The userspace host has no direct hardware performance counters. */
#define CORE_NCTRS 0
#define __ARM_16K_PG__ 1
#define __ARM64_ARCH_8__ 1
#define __ARM_ARCH_8_0__ 1
#define NO_MONITOR 1
#define PRECISE_USER_KERNEL_TIME 0
#endif

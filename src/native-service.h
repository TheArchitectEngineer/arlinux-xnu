/* Host-side execution only. Successful spawn is not XPC check-in/readiness. */
#pragma once
#include <stdint.h>
#include <sys/types.h>
struct native_service_child { pid_t pid; int pidfd; uint64_t start_ticks; };
int native_service_spawn(uint32_t owner,uint32_t incarnation,const char *service,
                         const unsigned char instance[16],struct native_service_child *child);

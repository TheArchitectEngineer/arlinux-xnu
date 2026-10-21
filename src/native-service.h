/* Host-side execution only. Successful spawn is not XPC check-in/readiness. */
#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <plist/plist.h>
struct native_service_child { pid_t pid; int pidfd; uint64_t start_ticks; };
int native_service_spawn(uint32_t owner,uint32_t incarnation,const char *service,
                         const unsigned char instance[16],struct native_service_child *child);
/* Owned copy of the selected real bundle's XPCService dictionary. */
int native_service_metadata(uint32_t owner,uint32_t incarnation,const char *service,plist_t *configuration);

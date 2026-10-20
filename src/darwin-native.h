#pragma once
#include <stdint.h>

long darwin_native_memory(unsigned long size,int protection);
int darwin_native_display_register(uint32_t width,uint32_t height);
uint32_t darwin_native_service_lookup(uint32_t bootstrap,uint64_t name,uint64_t output,
                                    int32_t target,uint64_t instance,uint64_t flags);

/* Single native guest process. No host libc use after guest TLS takes over. */
void darwin_native_prepare(const char *root,const char *program,uint64_t cache_base,const unsigned char uuid[16]);
__attribute__((noreturn)) void darwin_native_enter(uint64_t stack, uint64_t entry);

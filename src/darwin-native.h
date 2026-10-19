#pragma once
#include <stdint.h>

long darwin_native_memory(unsigned long size,int protection);

/* Single native guest process. No host libc use after guest TLS takes over. */
void darwin_native_prepare(const char *root,const char *program,uint64_t cache_base);
__attribute__((noreturn)) void darwin_native_enter(uint64_t stack, uint64_t entry);

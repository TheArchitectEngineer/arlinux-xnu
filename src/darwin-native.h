#pragma once
#include <stdint.h>

/* Single native guest process. No host libc use after guest TLS takes over. */
void darwin_native_prepare(const char *root,const char *program);
__attribute__((noreturn)) void darwin_native_enter(uint64_t stack, uint64_t entry);

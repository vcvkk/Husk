/* SPDX-License-Identifier: GPL-2.0-or-later */
/* husk-port: the W^X toggle husk-ios-jit.c takes from QEMU's tcg/ (not part of this port): pthread_jit_write_protect_np, looked
 * up at run time (the iOS SDK's header marks it unavailable; where the system has none, there is no toggle). */
#ifndef HUSK_PORT_TCG_APPLE_JIT_H
#define HUSK_PORT_TCG_APPLE_JIT_H
#include <dlfcn.h>
#include <stdbool.h>
static inline bool jit_write_protect_supported(void)
{
    int (*f)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_supported_np");
    return f && f() && dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np");
}
static inline void jit_write_protect(int on)
{
    void (*f)(int) = (void (*)(int))dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np");
    if (f) f(on);
}
#endif

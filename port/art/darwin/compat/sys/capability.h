/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux capabilities, absent on Darwin: capget/capset fail with ENOSYS. */
#ifndef HUSK_COMPAT_CAPABILITY_H
#define HUSK_COMPAT_CAPABILITY_H
#include <errno.h>
#include <stdint.h>
#define _LINUX_CAPABILITY_VERSION_3 0x20080522
#define _LINUX_CAPABILITY_U32S_3 2
typedef struct __user_cap_header_struct { uint32_t version; int pid; } *cap_user_header_t;
typedef struct __user_cap_data_struct { uint32_t effective, permitted, inheritable; } *cap_user_data_t;
static inline int capget(cap_user_header_t h, cap_user_data_t d) { (void)h; (void)d; errno = ENOSYS; return -1; }
static inline int capset(cap_user_header_t h, const struct __user_cap_data_struct* d) { (void)h; (void)d; errno = ENOSYS; return -1; }
#endif

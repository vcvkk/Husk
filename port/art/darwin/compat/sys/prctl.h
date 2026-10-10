/* SPDX-License-Identifier: GPL-2.0-or-later */
/* prctl has no Darwin counterpart: every option fails with EINVAL. */
#ifndef HUSK_COMPAT_PRCTL_H
#define HUSK_COMPAT_PRCTL_H
#include <errno.h>
#define PR_SET_PDEATHSIG 1
#define PR_GET_DUMPABLE 3
#define PR_SET_DUMPABLE 4
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_CAP_AMBIENT 47
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
static inline int prctl(int option, ...) { (void)option; errno = EINVAL; return -1; }
#endif

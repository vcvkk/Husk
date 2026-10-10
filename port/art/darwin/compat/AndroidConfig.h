/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AOSP's per-platform configuration, for Darwin. The darwin-x86 one in
 * art_standalone's tree is years stale; this follows the linux-any one that is
 * maintained, less what Darwin lacks (malloc.h, off64_t).
 */
#ifndef _ANDROID_CONFIG_H
#define _ANDROID_CONFIG_H

#define HAVE_PTHREADS 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_UIO_H 1
#define OS_PATH_SEPARATOR '/'
#define __BIONIC_HAVE_UCONTEXT_T

#endif

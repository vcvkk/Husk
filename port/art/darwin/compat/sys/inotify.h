/* SPDX-License-Identifier: GPL-2.0-or-later */
/* inotify does not exist on Darwin (FSEvents and kqueue do instead): calls fail with ENOSYS. */
#ifndef HUSK_COMPAT_INOTIFY_H
#define HUSK_COMPAT_INOTIFY_H
#include <errno.h>
#include <stdint.h>
struct inotify_event { int wd; uint32_t mask, cookie, len; char name[]; };
#define IN_ACCESS 0x1
#define IN_MODIFY 0x2
#define IN_ATTRIB 0x4
#define IN_CLOSE_WRITE 0x8
#define IN_MOVED_FROM 0x40
#define IN_MOVED_TO 0x80
#define IN_CREATE 0x100
#define IN_DELETE 0x200
#define IN_DELETE_SELF 0x400
#define IN_MOVE_SELF 0x800
#define IN_UNMOUNT 0x2000
#define IN_Q_OVERFLOW 0x4000
#define IN_IGNORED 0x8000
#define IN_ONLYDIR 0x1000000
#define IN_NONBLOCK 04000
#define IN_CLOEXEC 02000000
static inline int inotify_init(void) { errno = ENOSYS; return -1; }
static inline int inotify_init1(int flags) { (void)flags; errno = ENOSYS; return -1; }
static inline int inotify_add_watch(int fd, const char* path, uint32_t mask) { (void)fd; (void)path; (void)mask; errno = ENOSYS; return -1; }
static inline int inotify_rm_watch(int fd, int wd) { (void)fd; (void)wd; errno = ENOSYS; return -1; }
#endif

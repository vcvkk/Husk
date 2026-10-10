/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux's sendfile(out, in, &offset, count), over Darwin's socket-only sendfile, or a read/write loop. */
#ifndef HUSK_COMPAT_SENDFILE_H
#define HUSK_COMPAT_SENDFILE_H
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#ifdef __cplusplus
static inline ssize_t sendfile(int out_fd, int in_fd, off_t* offset, size_t count) {
    off_t pos = offset ? *offset : lseek(in_fd, 0, SEEK_CUR);
    char buf[65536];
    size_t done = 0;
    while (done < count) {
        size_t want = count - done < sizeof(buf) ? count - done : sizeof(buf);
        ssize_t n = pread(in_fd, buf, want, pos + (off_t)done);
        if (n < 0) { if (errno == EINTR) continue; return done ? (ssize_t)done : -1; }
        if (n == 0) break;
        ssize_t w = write(out_fd, buf, (size_t)n);
        if (w < 0) { if (errno == EINTR) continue; return done ? (ssize_t)done : -1; }
        done += (size_t)w;
        if (w < n) break;
    }
    if (offset) *offset = pos + (off_t)done; else lseek(in_fd, pos + (off_t)done, SEEK_SET);
    return (ssize_t)done;
}
#endif
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Force-included into every source of ART's Darwin build: the Linux spellings
 * ART, libcore and OpenJDK's native code use, for what Darwin has under other
 * names. Darwin's off_t and file offsets are 64-bit already.
 */
#ifndef HUSK_DARWIN_COMPAT_H
#define HUSK_DARWIN_COMPAT_H
#if defined(__APPLE__)

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define off64_t off_t
#define loff_t off_t
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat
#define fstatat64 fstatat
#define lseek64 lseek
#define pread64 pread
#define pwrite64 pwrite
#define ftruncate64 ftruncate
#define flock64 flock
#define mmap64 mmap
#define open64 open
#define statfs64 statfs
#define fstatfs64 fstatfs
#define readdir64 readdir
#define dirent64 dirent
#define getrlimit64 getrlimit
#define setrlimit64 setrlimit
#define rlimit64 rlimit


/* The thread id Linux code gets from syscall(__NR_gettid). */
static inline pid_t husk_gettid(void) { uint64_t t = 0; pthread_threadid_np(NULL, &t); return (pid_t)t; }

static inline int pipe2(int fds[2], int flags) {
    if (pipe(fds) != 0) return -1;
    for (int i = 0; i < 2; i++) {
        if ((flags & O_CLOEXEC) && fcntl(fds[i], F_SETFD, FD_CLOEXEC) != 0) return -1;
        if ((flags & O_NONBLOCK) && fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK) != 0) return -1;
    }
    return 0;
}

static inline int fdatasync(int fd) { return fsync(fd); }

static inline int posix_fallocate(int fd, off_t offset, off_t len) {
    fstore_t store = { F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, offset + len, 0 };
    if (fcntl(fd, F_PREALLOCATE, &store) == -1) {
        store.fst_flags = F_ALLOCATEALL;
        if (fcntl(fd, F_PREALLOCATE, &store) == -1) return errno;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < offset + len && ftruncate(fd, offset + len) != 0) return errno;
    return 0;
}

/* Peer credentials: Darwin has LOCAL_PEERCRED/xucred instead; SO_PEERCRED fails. */
struct ucred { pid_t pid; uid_t uid; gid_t gid; };
#ifndef SO_PEERCRED
#define SO_PEERCRED 0x1022
#endif

/* No real-time signals: what Linux code takes as SIGRTMAX - 2 is SIGUSR2. */
#ifndef SIGRTMAX
#define SIGRTMAX (SIGUSR2 + 2)
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif
#ifndef MREMAP_MAYMOVE
#define MREMAP_MAYMOVE 1
#endif
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x2000
#endif

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ __typeof__(exp) _rc; do { _rc = (exp); } while (_rc == -1 && errno == EINTR); _rc; })
#endif

#ifdef __cplusplus
/* Darwin's mincore takes char*. */
static inline int mincore(void* addr, size_t length, unsigned char* vec) {
    return ::mincore(addr, length, reinterpret_cast<char*>(vec));
}
#endif

#endif
#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux's mount table API, over Darwin's getmntinfo. setmntent ignores its file and lists live mounts. */
#ifndef HUSK_COMPAT_MNTENT_H
#define HUSK_COMPAT_MNTENT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mount.h>
struct mntent { char* mnt_fsname; char* mnt_dir; char* mnt_type; char* mnt_opts; int mnt_freq; int mnt_passno; };
typedef struct { struct statfs* list; int count, at; struct mntent ent; } husk_mnt_iter;
static inline FILE* setmntent(const char* file, const char* mode) {
    (void)file; (void)mode;
    husk_mnt_iter* it = (husk_mnt_iter*)calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->count = getmntinfo(&it->list, MNT_NOWAIT);
    return (FILE*)it;
}
static inline struct mntent* getmntent_r(FILE* f, struct mntent* ent, char* buf, int len) {
    husk_mnt_iter* it = (husk_mnt_iter*)f;
    (void)buf; (void)len;
    if (!it || it->at >= it->count) return NULL;
    struct statfs* s = &it->list[it->at++];
    ent->mnt_fsname = s->f_mntfromname; ent->mnt_dir = s->f_mntonname; ent->mnt_type = s->f_fstypename;
    ent->mnt_opts = (char*)((s->f_flags & MNT_RDONLY) ? "ro" : "rw"); ent->mnt_freq = 0; ent->mnt_passno = 0;
    return ent;
}
static inline struct mntent* getmntent(FILE* f) {
    husk_mnt_iter* it = (husk_mnt_iter*)f;
    return it ? getmntent_r(f, &it->ent, NULL, 0) : NULL;
}
static inline int endmntent(FILE* f) { free(f); return 1; }
static inline char* hasmntopt(const struct mntent* m, const char* opt) { return m && m->mnt_opts ? strstr(m->mnt_opts, opt) : NULL; }
#endif

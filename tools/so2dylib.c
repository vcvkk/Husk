/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * so2dylib: an app's arm64 Android libraries as iOS dylibs made ahead of time, so they run on a phone with no JIT: the guest
 * linker's mapping and code rewrites done here (tl_aot_make_dylib, port/native), each written as aot-v<N>-<hash>-<library>.dylib,
 * the name the linker looks for in the folder tl_ld_set_aot_dir names (the app's Frameworks). Unsigned: the IPA's signing signs
 * them with the app. Runs on a Mac (the linker maps the library the way it would at run time).
 *
 *   so2dylib OUT_DIR lib1.so [lib2.so ...]
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "husk-tl-aot.h"

void tl_log_line(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

/* jnigraphics' functions the bionic tables name: the framework's at run time (gfx.c), never called while converting */
int tl_AndroidBitmap_getInfo(void *env, void *bitmap, void *info) { (void)env; (void)bitmap; (void)info; return -1; }
int tl_AndroidBitmap_lockPixels(void *env, void *bitmap, void **addr) { (void)env; (void)bitmap; (void)addr; return -1; }
int tl_AndroidBitmap_unlockPixels(void *env, void *bitmap) { (void)env; (void)bitmap; return -1; }
int32_t tl_AndroidBitmap_getDataSpace(void *env, void *bitmap) { (void)env; (void)bitmap; return 0; }

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = l > 0 ? malloc((size_t)l) : NULL;
    if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
    fclose(f);
    *n = b ? (size_t)l : 0;
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: so2dylib OUT_DIR lib.so...\n"); return 2; }
    mkdir(argv[1], 0755);
    int failed = 0;
    for (int i = 2; i < argc; i++) {
        size_t n; uint8_t *data = slurp(argv[i], &n);
        if (!data) { fprintf(stderr, "so2dylib: %s: %s\n", argv[i], strerror(errno)); failed++; continue; }
        const char *base = strrchr(argv[i], '/'); base = base ? base + 1 : argv[i];
        char name[200], out[1200], err[300] = "";
        tl_aot_file_name(name, sizeof(name), tl_aot_hash(data, n), base);
        snprintf(out, sizeof(out), "%s/%s", argv[1], name);
        tl_ld_aot_image info;
        if (tl_aot_make_dylib(base, data, n, out, true, &info, err, sizeof(err)))
            printf("so2dylib: %s -> %s (%zu KiB of image, %zu pages copied)\n", base, name, info.size >> 10, info.n_mixed);
        else { fprintf(stderr, "so2dylib: %s: %s\n", base, err); failed++; }
        free(data);
    }
    return failed ? 1 : 0;
}

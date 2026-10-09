/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvm-run: run a DEX program on husk-port's Dalvik VM, on a computer.
 *
 *   dvm-run [--stats] -cp boot.dex:app.dex[:app.apk] com.example.Main [args...]
 *
 * --stats prints the heap after the run to stderr, for the collector's tests.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dvm/dvm.h"

int main(int argc, char **argv)
{
    const char *cp = NULL;
    bool stats = false;
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-cp") && i + 1 < argc) cp = argv[++i];
        else if (!strcmp(argv[i], "--stats")) stats = true;
        else break;
    }
    if (!cp || i >= argc) {
        fprintf(stderr, "usage: dvm-run [--stats] -cp a.dex:b.dex Main [args...]\n");
        return 2;
    }
    DvmVM *vm = dvm_create();
    char *paths = strdup(cp), *save = NULL;
    for (char *p = strtok_r(paths, ":", &save); p; p = strtok_r(NULL, ":", &save)) {
        char err[512];
        if (!dvm_add_dex_file(vm, p, err, sizeof(err))) {
            fprintf(stderr, "dvm-run: %s\n", err);
            return 2;
        }
    }
    free(paths);
    int status = dvm_run_main(vm, argv[i], argc - i - 1, (const char **)argv + i + 1);
    if (stats) {
        dvm_gc(vm);
        fprintf(stderr, "dvm-stats: objects=%zu bytes=%zu\n", dvm_heap_objects(vm), dvm_heap_bytes(vm));
    }
    dvm_destroy(vm);
    return status;
}

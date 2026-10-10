/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * bionic_translation's loader API, as ART's JNI calls it
 * (art/runtime/jni/java_vm_ext.cc), for Darwin.
 *
 * On Linux, ATL's libdl_bio loads bionic-linked libraries from APKs. On Darwin a
 * library the runtime loads is either one of its own (Mach-O, signed with the
 * app: dlopen does) or an APK's arm64 .so, which Husk's loader
 * (src/translation-layer-next/husk-tl-ld.c) will take; until it is wired in,
 * those fail to load with a message that says so.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static char err[512];

void *bionic_dlopen(const char *filename, int flag)
{
    void *h = dlopen(filename, flag);
    if (!h) snprintf(err, sizeof(err), "%s", dlerror());
    return h;
}

void *bionic_dlsym(void *handle, const char *symbol) { return dlsym(handle, symbol); }

const char *bionic_dlerror(void) { return err[0] ? err : NULL; }

int bionic_dlclose(void *handle) { return dlclose(handle); }

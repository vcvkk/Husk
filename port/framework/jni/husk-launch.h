/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ART with a boot image, and an app on it (launch.c). */
#ifndef HUSK_LAUNCH_H
#define HUSK_LAUNCH_H
#include <jni.h>
#include <stdbool.h>

bool husk_vm_start(const char *art_dir, const char *data_dir, const char *const *extra, int nextra, JavaVM **vm, JNIEnv **env);
void husk_app_run(JNIEnv *env);

#endif

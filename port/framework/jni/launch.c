/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ART with a boot image, and an app on it: what the iOS app and the macOS test host both do.
 *
 *   husk_vm_start(art_dir, ...)   art_dir: boot.art (and arm64/), jars/, boot.classpath
 *                                 (port/art/boot-image.sh's layout); no JIT: everything runs from
 *                                 code compiled ahead of time and signed with the app
 *   husk_app_run(env)             the framework's natives registered, the manifest read, and the app
 *                                 run on this thread (husk.AppRunner): returns when it ends
 */
#include "husk-launch.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "husk-framework.h"
#include "shim.h"

void husk_app_set_package(const char *pkg);

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *s = NULL; size_t n = 0;
    ssize_t r = getline(&s, &n, f);
    fclose(f);
    if (r <= 0) { free(s); return NULL; }
    while (r > 0 && (s[r - 1] == '\n' || s[r - 1] == ' ')) s[--r] = 0;
    return s;
}

/* "/boot/a.jar:/boot/b.jar" -> "<art>/jars/a.jar:<art>/jars/b.jar" */
static char *jar_paths(const char *art, const char *locations)
{
    size_t cap = strlen(locations) * 2 + strlen(art) * 64 + 64;
    char *out = malloc(cap), *o = out;
    *o = 0;
    for (const char *p = locations; *p;) {
        const char *end = strchr(p, ':'); if (!end) end = p + strlen(p);
        const char *slash = p; for (const char *q = p; q < end; q++) if (*q == '/') slash = q + 1;
        o += snprintf(o, cap - (size_t)(o - out), "%s%s/jars/%.*s", o == out ? "" : ":", art, (int)(end - slash), slash);
        p = *end ? end + 1 : end;
    }
    return out;
}

bool husk_vm_start(const char *art, const char *data_dir, const char *const *extra, int nextra, JavaVM **vm, JNIEnv **env)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/boot.classpath", art);
    char *locations = slurp(path);
    if (!locations) { tl_log_line("husk-port: no %s", path); return false; }
    char *jars = jar_paths(art, locations);

    /* ART wants ANDROID_DATA/dalvik-cache/<isa>, and ANDROID_ROOT set */
    snprintf(path, sizeof path, "%s/dalvik-cache", data_dir); mkdir(data_dir, 0755); mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/dalvik-cache/arm64", data_dir); mkdir(path, 0755);
    setenv("ANDROID_DATA", data_dir, 1);
    if (!getenv("ANDROID_ROOT")) setenv("ANDROID_ROOT", art, 1);   /* and its etc/security/cacerts, wolfssljni's */
    /* System.loadLibrary's path (java.library.path): the runtime's directory, where libwolfssljni is too */
    Dl_info dl;
    if (dladdr((void *)JNI_CreateJavaVM, &dl) && dl.dli_fname) {
        char lib[4096];
        const char *old = getenv("LD_LIBRARY_PATH");
        snprintf(lib, sizeof lib, "%s", dl.dli_fname);
        char *slash = strrchr(lib, '/');
        if (slash) {
            *slash = 0;
            if (old && *old) snprintf(slash, sizeof lib - (size_t)(slash - lib), ":%s", old);
            setenv("LD_LIBRARY_PATH", lib, 1);
        }
    }

    char image[2048], bcp[65536], bcl[65536];
    snprintf(image, sizeof image, "-Ximage:%s/boot.art", art);
    snprintf(bcp, sizeof bcp, "-Xbootclasspath:%s", jars);
    snprintf(bcl, sizeof bcl, "-Xbootclasspath-locations:%s", locations);
    const char *base[] = { "-Xusejit:false", image, bcp, bcl, "-Xss8m" };
    int n = (int)(sizeof base / sizeof *base);
    /* art/vm.options, if there: more options, one a line (-Dwolfjsse.debug=true and the like, for a diagnostic build) */
    char *more[32]; int nmore = 0;
    snprintf(path, sizeof path, "%s/vm.options", art);
    FILE *f = fopen(path, "r");
    if (f) {
        char line[1024];
        while (nmore < 32 && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] && line[0] != '#') more[nmore++] = strdup(line);
        }
        fclose(f);
    }
    JavaVMOption *o = calloc((size_t)(n + nextra + nmore), sizeof *o);
    for (int i = 0; i < n; i++) o[i].optionString = (char *)base[i];
    for (int i = 0; i < nextra; i++) o[n + i].optionString = (char *)extra[i];
    for (int i = 0; i < nmore; i++) o[n + nextra + i].optionString = more[i];
    JavaVMInitArgs args = { .version = JNI_VERSION_1_6, .nOptions = n + nextra + nmore, .options = o };
    bool ok = JNI_CreateJavaVM(vm, env, &args) == JNI_OK;
    if (!ok) tl_log_line("husk-port: JNI_CreateJavaVM failed");
    free(o); free(jars); free(locations);
    return ok;
}

static bool check(JNIEnv *env, const char *what)
{
    if (!(*env)->ExceptionCheck(env)) return true;
    tl_log_line("husk-port: %s threw:", what);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
    return false;
}

void husk_app_run(JNIEnv *env)
{
    if (!husk_framework_register(env)) tl_log_line("husk-port: not all of the framework's natives registered");
    jclass manifest = (*env)->FindClass(env, "husk/Manifest");
    if (!check(env, "husk.Manifest") || !manifest) return;
    (*env)->CallStaticVoidMethod(env, manifest, (*env)->GetStaticMethodID(env, manifest, "read", "()V"));
    if (!check(env, "Manifest.read")) return;
    jstring pkg = (*env)->GetStaticObjectField(env, manifest, (*env)->GetStaticFieldID(env, manifest, "packageName", "Ljava/lang/String;"));
    if (pkg) {
        const char *p = (*env)->GetStringUTFChars(env, pkg, NULL);
        husk_app_set_package(p);
        (*env)->ReleaseStringUTFChars(env, pkg, p);
    }
    jobject launcher = (*env)->CallStaticObjectMethod(env, manifest,
        (*env)->GetStaticMethodID(env, manifest, "launcher", "()Lhusk/Manifest$Component;"));
    if (!check(env, "Manifest.launcher") || !launcher) { tl_log_line("husk-port: the app has no launcher activity"); return; }
    jclass component = (*env)->GetObjectClass(env, launcher);
    jstring activity = (*env)->GetObjectField(env, launcher, (*env)->GetFieldID(env, component, "name", "Ljava/lang/String;"));
    const char *a = (*env)->GetStringUTFChars(env, activity, NULL);
    tl_log_line("husk-port: starting %s", a);
    (*env)->ReleaseStringUTFChars(env, activity, a);

    jclass runner = (*env)->FindClass(env, "husk/AppRunner");
    if (!check(env, "husk.AppRunner") || !runner) return;
    (*env)->CallStaticVoidMethod(env, runner,
        (*env)->GetStaticMethodID(env, runner, "run", "(Ljava/lang/String;Ljava/lang/String;)V"), activity, NULL);
    check(env, "the app");
    tl_log_line("husk-port: the app's main thread ended");
}

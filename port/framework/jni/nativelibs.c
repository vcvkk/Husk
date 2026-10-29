/* SPDX-License-Identifier: GPL-2.0-or-later */
/* husk.NativeLibs: an app's own native libraries through the guest linker; the JNI entries for them (guestjni.inc). */
#include "guestjni.inc"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_ready;
static const char *g_apk, *g_aot_dir;

/* the host's paths: the APK (its lib/arm64-v8a/), where the dylibs made from them ahead of time are (or NULL) */
void husk_native_libs_configure(const char *apk, const char *aot_dir);
void husk_native_libs_configure(const char *apk, const char *aot_dir)
{
    g_apk = apk ? strdup(apk) : NULL;
    g_aot_dir = aot_dir ? strdup(aot_dir) : NULL;
}

/* ART asks for a native method's code after its own libraries: the guest libraries' Java_... functions */
static void *lookup(const char *short_name, const char *long_name)
{
    void *p = tl_ld_sym(NULL, short_name);
    if (!p && long_name) p = tl_ld_sym(NULL, long_name);
    return p;
}

static bool ready(void)
{
    if (g_ready) return true;
    if (!g_apk || !tl_ld_add_apk(g_apk)) { tl_log_line("husk-port: native libraries: no APK to take them from"); return false; }
    if (g_aot_dir) tl_ld_set_aot_dir(g_aot_dir);
    install_table();
    art_husk_set_native_lookup(lookup);
    g_ready = true;
    return true;
}

typedef jint (*onload_fn)(JavaVM *vm, void *reserved);
/* (String file: libfoo.so) -> whether it is loaded (its constructors run, then its JNI_OnLoad) */
static jboolean NL_load(JNIEnv *env, jclass cls, jstring jname)
{
    (void)cls;
    const char *name = jname ? (*env)->GetStringUTFChars(env, jname, NULL) : NULL;
    if (!name) return JNI_FALSE;
    pthread_mutex_lock(&g_lock);
    bool ok = false, fresh = false;
    tl_lib *L = NULL;
    if (ready()) {
        fresh = tl_ld_find_lib(name) == NULL;
        L = tl_ld_load(name);
        ok = L && tl_ld_init(L);
    }
    pthread_mutex_unlock(&g_lock);
    if (ok && fresh) {
        onload_fn onload = (onload_fn)tl_ld_sym(L, "JNI_OnLoad");
        tl_log_line("husk-port: %s loaded%s", name, onload ? ", JNI_OnLoad" : "");
        if (onload) {
            JavaVM *vm = NULL;
            (*env)->GetJavaVM(env, &vm);
            jint v = onload(vm, NULL);
            if ((*env)->ExceptionCheck(env)) tl_log_line("husk-port: %s: JNI_OnLoad left an exception pending", name);
            tl_log_line("husk-port: %s: JNI_OnLoad -> %#x", name, (unsigned)v);
        }
    } else if (!ok) tl_log_line("husk-port: %s could not be loaded", name);
    (*env)->ReleaseStringUTFChars(env, jname, name);
    return ok ? JNI_TRUE : JNI_FALSE;
}

static const JNINativeMethod k_nativelibs[] = {
    { "nativeLoad", "(Ljava/lang/String;)Z", (void *)NL_load },
};
bool husk_register_nativelibs(JNIEnv *env);
bool husk_register_nativelibs(JNIEnv *env)
{
    jclass c = (*env)->FindClass(env, "husk/NativeLibs");
    if (!c) { (*env)->ExceptionClear(env); return false; }
    bool ok = (*env)->RegisterNatives(env, c, k_nativelibs, 1) == 0;
    if (!ok) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    return ok;
}

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

/* Java method hooks: ART's own (patch 0015), for apps that hook their methods the way LSPlant does; exteraGram's plugins
 * declare them on dev.exterahook.runtime.bridge.JniBridgeBindings, whose native side (shadowhook over libart's code) cannot
 * work here */
const JNINativeMethod *art_husk_hook_natives(int *count);
static void *hook_native(const char *short_name)
{
    static const char pre[] = "Java_dev_exterahook_runtime_bridge_JniBridgeBindings_";
    if (strncmp(short_name, pre, sizeof pre - 1)) return NULL;
    int n = 0;
    const JNINativeMethod *k = art_husk_hook_natives(&n);
    for (int i = 0; i < n; i++) if (!strcmp(k[i].name, short_name + sizeof pre - 1)) return k[i].fnPtr;
    return NULL;
}
/* and over what a library's JNI_OnLoad registered for that class, once it is there */
static void hook_bindings(JNIEnv *env, jobject loader)
{
    if (!loader) return;
    jclass lc = (*env)->FindClass(env, "java/lang/ClassLoader");
    jmethodID load = lc ? (*env)->GetMethodID(env, lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : NULL;
    jstring name = (*env)->NewStringUTF(env, "dev.exterahook.runtime.bridge.JniBridgeBindings");
    jclass c = load && name ? (jclass)(*env)->CallObjectMethod(env, loader, load, name) : NULL;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); c = NULL; }
    if (c) {
        int n = 0, ok = 0;
        const JNINativeMethod *k = art_husk_hook_natives(&n);
        for (int i = 0; i < n; i++) {
            if ((*env)->RegisterNatives(env, c, &k[i], 1) == 0) ok++;
            else (*env)->ExceptionClear(env);
        }
        tl_log_line("husk-port: Java method hooks: %d of %d of JniBridgeBindings' natives are ART's", ok, n);
        (*env)->DeleteLocalRef(env, c);
    }
    if (name) (*env)->DeleteLocalRef(env, name);
    if (lc) (*env)->DeleteLocalRef(env, lc);
}

/* ART asks for a native method's code after its own libraries: the guest libraries' Java_... functions */
static void *lookup(const char *short_name, const char *long_name)
{
    void *h = hook_native(short_name);
    if (h) return h;
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

void art_husk_set_class_loader_override(jobject loader);
typedef jint (*onload_fn)(JavaVM *vm, void *reserved);
/* (String file: libfoo.so) -> whether it is loaded (its constructors run, then its JNI_OnLoad) */
static jboolean NL_load(JNIEnv *env, jclass cls, jstring jname, jobject loader)
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
            /* its FindClass looks in the loading class's ClassLoader, as during Runtime.nativeLoad */
            jobject gl = loader ? (*env)->NewGlobalRef(env, loader) : NULL;
            art_husk_set_class_loader_override(gl);
            jint v = onload(vm, NULL);
            art_husk_set_class_loader_override(NULL);
            if (gl) (*env)->DeleteGlobalRef(env, gl);
            if ((*env)->ExceptionCheck(env)) {
                tl_log_line("husk-port: %s: JNI_OnLoad left an exception pending:", name);
                (*env)->ExceptionDescribe(env);
                (*env)->ExceptionClear(env);
            }
            tl_log_line("husk-port: %s: JNI_OnLoad -> %#x", name, (unsigned)v);
            hook_bindings(env, loader);
        }
    } else if (!ok) tl_log_line("husk-port: %s could not be loaded", name);
    (*env)->ReleaseStringUTFChars(env, jname, name);
    return ok ? JNI_TRUE : JNI_FALSE;
}

static const JNINativeMethod k_nativelibs[] = {
    { "nativeLoad", "(Ljava/lang/String;Ljava/lang/ClassLoader;)Z", (void *)NL_load },
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

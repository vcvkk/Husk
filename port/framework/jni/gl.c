/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenGL ES for apps, through ANGLE (OpenGL ES over Metal; port/gl/build-angle.sh, its libANGLE-shared.dylib in the app's
 * Frameworks): EGL contexts and pbuffer surfaces for the app's own threads (husk.EGLNative, which HuskEGL's EGL10 and EGL14 are
 * built on), GL functions for the Java bindings (android.opengl.GLES10 to GLES32: gles/*.c, the main line's, through gen-jni.py)
 * and for the app's native libraries (tl_egl_resolve: the guest linker binds their gl* imports here).
 *
 * A window surface is a pbuffer the view's size: eglSwapBuffers reads the frame back (HuskEGL.readBack) into the view's frames,
 * drawn with the window's views. Husk's main line composites on a GL render thread instead; here the window is drawn on the CPU.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

typedef void *EGLDisplay, *EGLSurface, *EGLContext, *EGLConfig;
typedef int32_t EGLint;
#define EGL_NONE 0x3038

const char *husk_app_native_dir(void);      /* native.c: the app's Frameworks, or NULL */

static struct {
    pthread_mutex_t lk;
    bool tried, ready, es3;
    void *h;
    void *(*proc)(const char *);
    EGLDisplay dpy; EGLConfig cfg;
} G = { .lk = PTHREAD_MUTEX_INITIALIZER };

static void *sym(const char *name)
{
    void *p = G.proc ? G.proc(name) : NULL;
    if (!p && G.h) p = dlsym(G.h, name);
    return p;
}

/* ANGLE opened and its display made, on Metal: once */
static bool angle(void)
{
    pthread_mutex_lock(&G.lk);
    if (G.tried) { pthread_mutex_unlock(&G.lk); return G.ready; }
    G.tried = true;
    char path[1200];
    const char *dirs[3] = { getenv("HUSK_ANGLE_DIR"), husk_app_native_dir(), NULL };
    for (int i = 0; i < 2 && !G.h; i++) {
        if (!dirs[i]) continue;
        snprintf(path, sizeof path, "%s/libANGLE-shared.dylib", dirs[i]);
        G.h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!G.h) tl_log_line("gl: %s: %s", path, dlerror());
    }
    if (!G.h) G.h = dlopen("@rpath/libANGLE-shared.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!G.h) { tl_log_line("gl: no ANGLE: apps get no OpenGL ES"); pthread_mutex_unlock(&G.lk); return false; }
    G.proc = (void *(*)(const char *))dlsym(G.h, "eglGetProcAddress");
    EGLDisplay (*platform)(unsigned, void *, const EGLint *) = sym("eglGetPlatformDisplayEXT");
    EGLDisplay (*getDisplay)(void *) = sym("eglGetDisplay");
    unsigned (*init)(EGLDisplay, EGLint *, EGLint *) = sym("eglInitialize");
    unsigned (*bind)(unsigned) = sym("eglBindAPI");
    unsigned (*choose)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = sym("eglChooseConfig");
    const EGLint metal[] = { 0x3203 /* EGL_PLATFORM_ANGLE_TYPE_ANGLE */, 0x3489 /* METAL */, EGL_NONE };
    G.dpy = platform ? platform(0x3202 /* EGL_PLATFORM_ANGLE_ANGLE */, NULL, metal) : NULL;
    if (!G.dpy && getDisplay) G.dpy = getDisplay(NULL);
    EGLint maj = 0, min = 0;
    if (!G.dpy || !init || !init(G.dpy, &maj, &min)) { tl_log_line("gl: ANGLE's display did not start"); pthread_mutex_unlock(&G.lk); return false; }
    if (bind) bind(0x30A0 /* EGL_OPENGL_ES_API */);
    for (int pass = 0; pass < 2 && !G.cfg; pass++) {
        const EGLint want[] = { 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3025, 24, 0x3026, 8,   /* RGBA8, depth 24, stencil 8 */
                                0x3033, 1 /* EGL_SURFACE_TYPE: PBUFFER */, 0x3040, pass ? 4 : 4 | 0x40 /* ES2 (| ES3) */, EGL_NONE };
        EGLint n = 0;
        if (choose && choose(G.dpy, want, &G.cfg, 1, &n) && n > 0) G.es3 = !pass; else G.cfg = NULL;
    }
    G.ready = G.cfg != NULL;
    tl_log_line("gl: ANGLE on Metal, EGL %d.%d, OpenGL ES %s", maj, min, G.ready ? (G.es3 ? "3" : "2") : "none (no config)");
    pthread_mutex_unlock(&G.lk);
    return G.ready;
}

/* GL and EGL by name, for the app's native libraries (port/native's bionic binds their gl* and egl* imports here) and the bindings */
void *tl_egl_resolve(const char *name) { return angle() ? sym(name) : NULL; }
void *gl_fn(const char *name) { return tl_egl_resolve(name); }

/* ---- the bindings' arguments */
const char *gl_str(jobj *s) { return s ? tl_jni_string(s) : NULL; }
static size_t esize(char e) { return e == 'J' || e == 'D' ? 8 : e == 'S' || e == 'C' ? 2 : e == 'B' || e == 'Z' ? 1 : 4; }
void *gl_array(jobj *a, int32_t off)
{
    if (!a || a->kind != TL_K_PRIM_ARRAY || !a->arr.data) return NULL;
    return (char *)a->arr.data + (size_t)off * esize(a->arr.elem);
}
/* a java.nio buffer's elements from its position: a direct one's memory, a heap one's array (pinned for the call by the shim) */
void *gl_buffer(jobj *b)
{
    if (!b || !b->ref) return NULL;
    JNIEnv *env = shim_env();
    static jfieldID f_pos, f_shift;
    static jmethodID m_array, m_offset;
    if (!f_pos) {
        jclass c = (*env)->FindClass(env, "java/nio/Buffer");
        f_pos = (*env)->GetFieldID(env, c, "position", "I");
        f_shift = (*env)->GetFieldID(env, c, "_elementSizeShift", "I");
        m_array = (*env)->GetMethodID(env, c, "array", "()Ljava/lang/Object;");
        m_offset = (*env)->GetMethodID(env, c, "arrayOffset", "()I");
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, c);
    }
    jint pos = f_pos ? (*env)->GetIntField(env, b->ref, f_pos) : 0, shift = f_shift ? (*env)->GetIntField(env, b->ref, f_shift) : 0;
    char *p = (*env)->GetDirectBufferAddress(env, b->ref);
    if (p) return p + ((size_t)pos << shift);
    if (!m_array) return NULL;
    jobject arr = (*env)->CallObjectMethod(env, b->ref, m_array);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    jint off = (*env)->CallIntMethod(env, b->ref, m_offset);
    jobj *o = shim_in(arr, '[');
    return o && o->arr.data ? (char *)o->arr.data + ((size_t)(off + pos) << shift) : NULL;
}

/* ---- husk.EGLNative: contexts and pbuffer surfaces on the app's threads */
typedef struct { EGLContext c; int version; } app_ctx;
typedef struct { EGLSurface pb; int w, h; } app_surf;

static jlong EN_createContext(JNIEnv *env, jclass cls, jint version, jlong share)
{
    (void)env; (void)cls;
    if (!angle()) return 0;
    EGLContext (*create)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = sym("eglCreateContext");
    int v = version <= 1 ? 1 : version >= 3 && G.es3 ? 3 : 2;
    app_ctx *sh = (app_ctx *)(uintptr_t)share, *c = calloc(1, sizeof *c);
    for (int tries = 0; tries < 2 && !c->c; tries++) {
        const EGLint attr[] = { 0x3098, v, EGL_NONE };
        c->c = create(G.dpy, G.cfg, sh ? sh->c : NULL, attr);
        if (!c->c && v == 1) v = 2;
    }
    if (!c->c) { tl_log_line("gl: no ES %d context for the app", v); free(c); return 0; }
    c->version = v;
    tl_log_line("gl: an ES %d context for the app's thread", v);
    return (jlong)(uintptr_t)c;
}
static void EN_destroyContext(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    app_ctx *c = (app_ctx *)(uintptr_t)h;
    if (!c) return;
    unsigned (*destroy)(EGLDisplay, EGLContext) = sym("eglDestroyContext");
    if (destroy) destroy(G.dpy, c->c);
    free(c);
}
static jlong EN_createSurface(JNIEnv *env, jclass cls, jint w, jint h)
{
    (void)env; (void)cls;
    if (!angle()) return 0;
    EGLSurface (*pbuffer)(EGLDisplay, EGLConfig, const EGLint *) = sym("eglCreatePbufferSurface");
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    const EGLint attr[] = { 0x3057, w, 0x3056, h, EGL_NONE };
    EGLSurface pb = pbuffer ? pbuffer(G.dpy, G.cfg, attr) : NULL;
    if (!pb) { tl_log_line("gl: no %dx%d surface for the app", w, h); return 0; }
    app_surf *s = calloc(1, sizeof *s);
    s->pb = pb; s->w = w; s->h = h;
    return (jlong)(uintptr_t)s;
}
static void EN_destroySurface(JNIEnv *env, jclass cls, jlong h)
{
    (void)env; (void)cls;
    app_surf *s = (app_surf *)(uintptr_t)h;
    if (!s) return;
    unsigned (*destroy)(EGLDisplay, EGLSurface) = sym("eglDestroySurface");
    if (destroy) destroy(G.dpy, s->pb);
    free(s);
}
/* (surface, context): no context releases the thread's; no surface, a 1x1 pbuffer of the thread's if surfaceless does not go */
static jboolean EN_makeCurrent(JNIEnv *env, jclass cls, jlong sh, jlong ch)
{
    (void)env; (void)cls;
    app_surf *s = (app_surf *)(uintptr_t)sh;
    app_ctx *c = (app_ctx *)(uintptr_t)ch;
    if (!G.ready) return !c;
    unsigned (*make)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = sym("eglMakeCurrent");
    if (!c) return make(G.dpy, NULL, NULL, NULL) != 0;
    EGLSurface surf = s ? s->pb : NULL;
    if (!surf) {
        static __thread EGLSurface tiny;
        if (make(G.dpy, NULL, NULL, c->c)) return JNI_TRUE;
        if (!tiny) { EGLSurface (*pbuffer)(EGLDisplay, EGLConfig, const EGLint *) = sym("eglCreatePbufferSurface"); const EGLint at[] = { 0x3057, 1, 0x3056, 1, EGL_NONE }; tiny = pbuffer(G.dpy, G.cfg, at); }
        surf = tiny;
    }
    bool ok = make(G.dpy, surf, surf, c->c) != 0;
    if (!ok) tl_log_line("gl: makeCurrent failed for the app's thread");
    return ok;
}
/* the frame is read back in Java (HuskEGL.readBack); nothing to composite here */
static jboolean EN_swap(JNIEnv *env, jclass cls, jlong s, jlong c, jint x, jint y, jint w, jint h)
{
    (void)env; (void)cls; (void)s; (void)c; (void)x; (void)y; (void)w; (void)h;
    return JNI_TRUE;
}
static jboolean EN_isRenderThread(JNIEnv *env, jclass cls) { (void)env; (void)cls; return JNI_FALSE; }
static void EN_hide(JNIEnv *env, jclass cls) { (void)env; (void)cls; }
static const JNINativeMethod k_egl[] = {
    { "createContext", "(IJ)J", (void *)EN_createContext }, { "destroyContext", "(J)V", (void *)EN_destroyContext },
    { "createSurface", "(II)J", (void *)EN_createSurface }, { "destroySurface", "(J)V", (void *)EN_destroySurface },
    { "makeCurrent", "(JJ)Z", (void *)EN_makeCurrent }, { "swap", "(JJIIII)Z", (void *)EN_swap },
    { "isRenderThread", "()Z", (void *)EN_isRenderThread }, { "hide", "()V", (void *)EN_hide },
};

bool husk_register_gles20(JNIEnv *env), husk_register_gles30(JNIEnv *env), husk_register_gles31(JNIEnv *env), husk_register_gles32(JNIEnv *env),
     husk_register_gles10(JNIEnv *env), husk_register_gles10ext(JNIEnv *env), husk_register_gles11(JNIEnv *env), husk_register_gles11ext(JNIEnv *env);
bool husk_register_gl(JNIEnv *env);
bool husk_register_gl(JNIEnv *env)
{
    bool ok = shim_register(env, "husk/EGLNative", k_egl, (int)(sizeof k_egl / sizeof *k_egl));
    ok &= husk_register_gles20(env); ok &= husk_register_gles30(env); ok &= husk_register_gles31(env); ok &= husk_register_gles32(env);
    ok &= husk_register_gles10(env); ok &= husk_register_gles10ext(env); ok &= husk_register_gles11(env); ok &= husk_register_gles11ext(env);
    return ok;
}

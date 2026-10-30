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
/* glGetString and the like give Java a string: the interpreter's name for it, the shim's way */
jobj *dvm_new_string_utf8(const char *s);
jobj *dvm_new_string_utf8(const char *s) { return tl_jni_new_string(s); }
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
/* the current surface's w x h pixels into a direct buffer, rows from the top (GL's run from the bottom) */
static jboolean EN_readPixels(JNIEnv *env, jclass cls, jobject buf, jint w, jint h)
{
    (void)cls;
    void (*rd)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void *) =
        (void (*)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, void *))gl_fn("glReadPixels");
    void (*st)(int32_t, int32_t) = (void (*)(int32_t, int32_t))gl_fn("glPixelStorei");
    uint8_t *px = buf ? (*env)->GetDirectBufferAddress(env, buf) : NULL;
    if (!rd || !px || w <= 0 || h <= 0 || (*env)->GetDirectBufferCapacity(env, buf) < (jlong)w * h * 4) return JNI_FALSE;
    /* the window's own pixels, whatever framebuffer the app left bound (its offscreen ones are often drawn upside down) */
    void (*gi)(int32_t, int32_t *) = (void (*)(int32_t, int32_t *))gl_fn("glGetIntegerv");
    void (*bind)(int32_t, uint32_t) = (void (*)(int32_t, uint32_t))gl_fn("glBindFramebuffer");
    int32_t fb = 0, pack = 4;
    if (gi) { gi(0x8CA6, &fb); gi(0x0D05, &pack); }   /* FRAMEBUFFER_BINDING, PACK_ALIGNMENT */
    if (fb && bind) bind(0x8D40, 0);                   /* FRAMEBUFFER */
    if (st) st(0x0D05, 4);   /* rows of w * 4 bytes, packed as they are */
    rd(0, 0, w, h, 0x1908, 0x1401, px);   /* RGBA, UNSIGNED_BYTE */
    if (st && pack != 4) st(0x0D05, pack);
    if (fb && bind) bind(0x8D40, (uint32_t)fb);
    size_t row = (size_t)w * 4;
    uint8_t *tmp = malloc(row);
    if (!tmp) return JNI_FALSE;
    for (int y = 0; y < h / 2; y++) {
        uint8_t *a = px + (size_t)y * row, *b = px + (size_t)(h - 1 - y) * row;
        memcpy(tmp, a, row); memcpy(a, b, row); memcpy(b, tmp, row);
    }
    free(tmp);
    return JNI_TRUE;
}
static jboolean EN_isRenderThread(JNIEnv *env, jclass cls) { (void)env; (void)cls; return JNI_FALSE; }
static void EN_hide(JNIEnv *env, jclass cls) { (void)env; (void)cls; }
static const JNINativeMethod k_egl[] = {
    { "createContext", "(IJ)J", (void *)EN_createContext }, { "destroyContext", "(J)V", (void *)EN_destroyContext },
    { "createSurface", "(II)J", (void *)EN_createSurface }, { "destroySurface", "(J)V", (void *)EN_destroySurface },
    { "makeCurrent", "(JJ)Z", (void *)EN_makeCurrent }, { "swap", "(JJIIII)Z", (void *)EN_swap },
    { "readPixels", "(Ljava/nio/ByteBuffer;II)Z", (void *)EN_readPixels },
    { "isRenderThread", "()Z", (void *)EN_isRenderThread }, { "hide", "()V", (void *)EN_hide },
};

/* ---- the GLES natives the generated tables leave out: Java strings and string arrays in, strings and mapped buffers out */
#define GLF(var, ret, name, ...) static ret (*var)(__VA_ARGS__); if (!var) var = (ret (*)(__VA_ARGS__))gl_fn(name)
/* an IntBuffer's elements from its position: a direct one's memory, else a heap one's array, pinned until put_ints */
typedef struct { jintArray arr; jint *els; } ints_pin;
static jint *get_ints(JNIEnv *env, jobject b, ints_pin *pin)
{
    pin->arr = NULL; pin->els = NULL;
    if (!b) return NULL;
    static jfieldID f_pos;
    static jmethodID m_array, m_offset;
    if (!f_pos) {
        jclass c = (*env)->FindClass(env, "java/nio/Buffer");
        f_pos = (*env)->GetFieldID(env, c, "position", "I");
        m_array = (*env)->GetMethodID(env, c, "array", "()Ljava/lang/Object;");
        m_offset = (*env)->GetMethodID(env, c, "arrayOffset", "()I");
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, c);
    }
    jint pos = (*env)->GetIntField(env, b, f_pos);
    jint *p = (*env)->GetDirectBufferAddress(env, b);
    if (p) return p + pos;
    pin->arr = (*env)->CallObjectMethod(env, b, m_array);
    if ((*env)->ExceptionCheck(env) || !pin->arr) { (*env)->ExceptionClear(env); pin->arr = NULL; return NULL; }
    jint off = (*env)->CallIntMethod(env, b, m_offset);
    pin->els = (*env)->GetIntArrayElements(env, pin->arr, NULL);
    return pin->els ? pin->els + off + pos : NULL;
}
static void put_ints(JNIEnv *env, ints_pin *pin) { if (pin->els) (*env)->ReleaseIntArrayElements(env, pin->arr, pin->els, 0); }
/* a String[] as C strings, for one call */
typedef struct { int n; const char **s; jstring *js; } strs;
static strs get_strs(JNIEnv *env, jobjectArray a)
{
    strs r = { a ? (*env)->GetArrayLength(env, a) : 0, NULL, NULL };
    r.s = calloc((size_t)r.n + 1, sizeof *r.s); r.js = calloc((size_t)r.n + 1, sizeof *r.js);
    for (int i = 0; i < r.n; i++) {
        r.js[i] = (*env)->GetObjectArrayElement(env, a, i);
        r.s[i] = r.js[i] ? (*env)->GetStringUTFChars(env, r.js[i], NULL) : "";
    }
    return r;
}
static void put_strs(JNIEnv *env, strs *r)
{
    for (int i = 0; i < r->n; i++) if (r->js[i]) { (*env)->ReleaseStringUTFChars(env, r->js[i], r->s[i]); (*env)->DeleteLocalRef(env, r->js[i]); }
    free(r->s); free(r->js);
}

static void GX_glShaderSource(JNIEnv *env, jclass cls, jint shader, jstring src)
{
    (void)cls;
    GLF(f, void, "glShaderSource", uint32_t, int32_t, const char *const *, const int32_t *);
    if (!f || !src) return;
    const char *s = (*env)->GetStringUTFChars(env, src, NULL);
    f((uint32_t)shader, 1, &s, NULL);
    (*env)->ReleaseStringUTFChars(env, src, s);
}
/* an info log or a shader's source: its length by glGet*iv, then the text */
static jstring gl_text(JNIEnv *env, const char *getiv, int32_t lenparam, const char *get, jint obj)
{
    void (*iv)(uint32_t, int32_t, int32_t *) = (void (*)(uint32_t, int32_t, int32_t *))gl_fn(getiv);
    void (*text)(uint32_t, int32_t, int32_t *, char *) = (void (*)(uint32_t, int32_t, int32_t *, char *))gl_fn(get);
    if (!iv || !text) return (*env)->NewStringUTF(env, "");
    int32_t n = 0, got = 0;
    iv((uint32_t)obj, lenparam, &n);
    char *buf = calloc((size_t)(n > 0 ? n : 0) + 1, 1);
    if (n > 0) text((uint32_t)obj, n, &got, buf);
    jstring r = (*env)->NewStringUTF(env, buf);
    free(buf);
    return r;
}
static jstring GX_glGetProgramInfoLog(JNIEnv *env, jclass c, jint p) { (void)c; return gl_text(env, "glGetProgramiv", 0x8B84, "glGetProgramInfoLog", p); }
static jstring GX_glGetShaderInfoLog(JNIEnv *env, jclass c, jint s) { (void)c; return gl_text(env, "glGetShaderiv", 0x8B84, "glGetShaderInfoLog", s); }
static jstring GX_glGetShaderSource(JNIEnv *env, jclass c, jint s) { (void)c; return gl_text(env, "glGetShaderiv", 0x8B88, "glGetShaderSource", s); }
/* glGetActiveAttrib/Uniform giving the name as a String */
static jstring active(JNIEnv *env, bool uniform, jint prog, jint index, jint *size, jint *type)
{
    void (*iv)(uint32_t, int32_t, int32_t *) = (void (*)(uint32_t, int32_t, int32_t *))gl_fn("glGetProgramiv");
    void (*get)(uint32_t, uint32_t, int32_t, int32_t *, int32_t *, uint32_t *, char *) =
        (void (*)(uint32_t, uint32_t, int32_t, int32_t *, int32_t *, uint32_t *, char *))gl_fn(uniform ? "glGetActiveUniform" : "glGetActiveAttrib");
    if (!iv || !get) return NULL;
    int32_t n = 0, len = 0, sz = 0;
    uint32_t ty = 0;
    iv((uint32_t)prog, uniform ? 0x8B87 : 0x8B8A, &n);   /* ACTIVE_UNIFORM_MAX_LENGTH, ACTIVE_ATTRIBUTE_MAX_LENGTH */
    char *buf = calloc((size_t)(n > 0 ? n : 0) + 1, 1);
    get((uint32_t)prog, (uint32_t)index, n > 0 ? n : 1, &len, &sz, &ty, buf);
    if (size) *size = sz;
    if (type) *type = (jint)ty;
    jstring r = (*env)->NewStringUTF(env, buf);
    free(buf);
    return r;
}
static jstring active_arr(JNIEnv *env, bool uniform, jint prog, jint index, jintArray size, jint so, jintArray type, jint to)
{
    jint sz = 0, ty = 0;
    jstring r = active(env, uniform, prog, index, &sz, &ty);
    if (size) (*env)->SetIntArrayRegion(env, size, so, 1, &sz);
    if (type) (*env)->SetIntArrayRegion(env, type, to, 1, &ty);
    return r;
}
static jstring active_buf(JNIEnv *env, bool uniform, jint prog, jint index, jobject size, jobject type)
{
    ints_pin ps, pt;
    jint *s = get_ints(env, size, &ps), *t = get_ints(env, type, &pt);
    jstring r = active(env, uniform, prog, index, s, t);
    put_ints(env, &ps); put_ints(env, &pt);
    return r;
}
static jstring GX_glGetActiveAttrib_0(JNIEnv *env, jclass c, jint p, jint i, jintArray s, jint so, jintArray t, jint to) { (void)c; return active_arr(env, false, p, i, s, so, t, to); }
static jstring GX_glGetActiveAttrib_1(JNIEnv *env, jclass c, jint p, jint i, jobject s, jobject t) { (void)c; return active_buf(env, false, p, i, s, t); }
static jstring GX_glGetActiveUniform_0(JNIEnv *env, jclass c, jint p, jint i, jintArray s, jint so, jintArray t, jint to) { (void)c; return active_arr(env, true, p, i, s, so, t, to); }
static jstring GX_glGetActiveUniform_1(JNIEnv *env, jclass c, jint p, jint i, jobject s, jobject t) { (void)c; return active_buf(env, true, p, i, s, t); }
static jstring GX_glGetString(JNIEnv *env, jclass c, jint name)
{
    (void)c;
    GLF(f, const char *, "glGetString", int32_t);
    const char *s = f ? f(name) : NULL;
    return s ? (*env)->NewStringUTF(env, s) : NULL;
}
static const JNINativeMethod k_gx20[] = {
    { "glShaderSource", "(ILjava/lang/String;)V", (void *)GX_glShaderSource },
    { "glGetString", "(I)Ljava/lang/String;", (void *)GX_glGetString },
    { "glGetProgramInfoLog", "(I)Ljava/lang/String;", (void *)GX_glGetProgramInfoLog },
    { "glGetShaderInfoLog", "(I)Ljava/lang/String;", (void *)GX_glGetShaderInfoLog },
    { "glGetShaderSource", "(I)Ljava/lang/String;", (void *)GX_glGetShaderSource },
    { "glGetActiveAttrib", "(II[II[II)Ljava/lang/String;", (void *)GX_glGetActiveAttrib_0 },
    { "glGetActiveAttrib", "(IILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)Ljava/lang/String;", (void *)GX_glGetActiveAttrib_1 },
    { "glGetActiveUniform", "(II[II[II)Ljava/lang/String;", (void *)GX_glGetActiveUniform_0 },
    { "glGetActiveUniform", "(IILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)Ljava/lang/String;", (void *)GX_glGetActiveUniform_1 },
};

/* a mapped buffer's memory as a direct ByteBuffer */
static jobject GX_glMapBufferRange(JNIEnv *env, jclass c, jint target, jint offset, jint length, jint access)
{
    (void)c;
    GLF(f, void *, "glMapBufferRange", int32_t, intptr_t, intptr_t, int32_t);
    void *p = f ? f(target, offset, length, access) : NULL;
    return p ? (*env)->NewDirectByteBuffer(env, p, length) : NULL;
}
static jobject GX_glGetBufferPointerv(JNIEnv *env, jclass c, jint target, jint pname)
{
    (void)c;
    GLF(f, void, "glGetBufferPointerv", int32_t, int32_t, void **);
    GLF(len, void, "glGetBufferParameteri64v", int32_t, int32_t, int64_t *);
    void *p = NULL;
    int64_t n = 0;
    if (f) f(target, pname, &p);
    if (len) len(target, 0x9120, &n);   /* BUFFER_MAP_LENGTH */
    return p ? (*env)->NewDirectByteBuffer(env, p, n) : NULL;
}
static void GX_glTransformFeedbackVaryings(JNIEnv *env, jclass c, jint prog, jobjectArray v, jint mode)
{
    (void)c;
    GLF(f, void, "glTransformFeedbackVaryings", uint32_t, int32_t, const char *const *, int32_t);
    strs s = get_strs(env, v);
    if (f) f((uint32_t)prog, s.n, s.s, mode);
    put_strs(env, &s);
}
static void uniform_indices(JNIEnv *env, jint prog, jobjectArray names, jint *out)
{
    GLF(f, void, "glGetUniformIndices", uint32_t, int32_t, const char *const *, uint32_t *);
    strs s = get_strs(env, names);
    if (f && out) f((uint32_t)prog, s.n, s.s, (uint32_t *)out);
    put_strs(env, &s);
}
static void GX_glGetUniformIndices_0(JNIEnv *env, jclass c, jint prog, jobjectArray names, jobject out)
{
    (void)c;
    ints_pin p;
    uniform_indices(env, prog, names, get_ints(env, out, &p));
    put_ints(env, &p);
}
static void GX_glGetUniformIndices_1(JNIEnv *env, jclass c, jint prog, jobjectArray names, jintArray out, jint off)
{
    (void)c;
    jint *e = out ? (*env)->GetIntArrayElements(env, out, NULL) : NULL;
    uniform_indices(env, prog, names, e ? e + off : NULL);
    if (e) (*env)->ReleaseIntArrayElements(env, out, e, 0);
}
static const JNINativeMethod k_gx30[] = {
    { "glMapBufferRange", "(IIII)Ljava/nio/Buffer;", (void *)GX_glMapBufferRange },
    { "glGetBufferPointerv", "(II)Ljava/nio/Buffer;", (void *)GX_glGetBufferPointerv },
    { "glTransformFeedbackVaryings", "(I[Ljava/lang/String;I)V", (void *)GX_glTransformFeedbackVaryings },
    { "glGetUniformIndices", "(I[Ljava/lang/String;Ljava/nio/IntBuffer;)V", (void *)GX_glGetUniformIndices_0 },
    { "glGetUniformIndices", "(I[Ljava/lang/String;[II)V", (void *)GX_glGetUniformIndices_1 },
};
static jint GX_glCreateShaderProgramv(JNIEnv *env, jclass c, jint type, jobjectArray src)
{
    (void)c;
    GLF(f, uint32_t, "glCreateShaderProgramv", int32_t, int32_t, const char *const *);
    strs s = get_strs(env, src);
    jint r = f ? (jint)f(type, s.n, s.s) : 0;
    put_strs(env, &s);
    return r;
}
static const JNINativeMethod k_gx31[] = {
    { "glCreateShaderProgramv", "(I[Ljava/lang/String;)I", (void *)GX_glCreateShaderProgramv },
};

bool husk_register_gles20(JNIEnv *env), husk_register_gles30(JNIEnv *env), husk_register_gles31(JNIEnv *env), husk_register_gles32(JNIEnv *env),
     husk_register_gles10(JNIEnv *env), husk_register_gles10ext(JNIEnv *env), husk_register_gles11(JNIEnv *env), husk_register_gles11ext(JNIEnv *env);
bool husk_register_gl(JNIEnv *env);
bool husk_register_gl(JNIEnv *env)
{
    bool ok = shim_register(env, "husk/EGLNative", k_egl, (int)(sizeof k_egl / sizeof *k_egl));
    ok &= husk_register_gles20(env); ok &= husk_register_gles30(env); ok &= husk_register_gles31(env); ok &= husk_register_gles32(env);
    ok &= husk_register_gles10(env); ok &= husk_register_gles10ext(env); ok &= husk_register_gles11(env); ok &= husk_register_gles11ext(env);
#define N(k) (int)(sizeof k / sizeof *k)
    ok &= shim_register(env, "android/opengl/GLES20", k_gx20, N(k_gx20));
    ok &= shim_register(env, "android/opengl/GLES30", k_gx30, N(k_gx30));
    ok &= shim_register(env, "android/opengl/GLES31", k_gx31, N(k_gx31));
#undef N
    return ok;
}

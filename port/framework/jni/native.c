/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * husk.Native on JNI: what the framework (port/framework) asks of the system it runs on: the APK's
 * files and assets, the app's directories, the screen, the input queue, the keyboard and the rest,
 * as the host app configures and feeds them (husk-framework.h). Modelled on Husk's main line's
 * (husk-tl-dvm-android.c), on JNI.
 */
#define _DARWIN_C_SOURCE
#include "husk-framework.h"
#include <CoreFoundation/CoreFoundation.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "shim.h"

/* ================================================================== the APKs, as zip archives */

typedef struct zentry { const char *name; uint32_t method, csize, usize; uint64_t local; } zentry;
typedef struct zip { int fd; const uint8_t *map; size_t size; zentry *e; size_t n; char *names; } zip;

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool zip_open(zip *z, const char *path)
{
    memset(z, 0, sizeof *z);
    z->fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (z->fd < 0 || fstat(z->fd, &st) != 0 || st.st_size < 22) return false;
    z->size = (size_t)st.st_size;
    z->map = mmap(NULL, z->size, PROT_READ, MAP_PRIVATE, z->fd, 0);
    if (z->map == MAP_FAILED) { z->map = NULL; return false; }
    const uint8_t *eocd = NULL;
    for (size_t i = z->size - 22; i + 1 > 0 && z->size - i < 65557; i--) if (u32(z->map + i) == 0x06054b50) { eocd = z->map + i; break; }
    if (!eocd) return false;
    size_t n = u16(eocd + 10), cd = u32(eocd + 16);
    z->e = calloc(n ? n : 1, sizeof *z->e);
    z->names = malloc(u32(eocd + 12) + n + 1);
    char *np = z->names;
    const uint8_t *p = z->map + cd;
    for (size_t i = 0; i < n && p + 46 <= z->map + z->size && u32(p) == 0x02014b50; i++) {
        uint16_t nl = u16(p + 28), xl = u16(p + 30), cl = u16(p + 32);
        memcpy(np, p + 46, nl); np[nl] = 0;
        z->e[z->n++] = (zentry){ np, u16(p + 10), u32(p + 20), u32(p + 24), u32(p + 42) };
        np += nl + 1;
        p += 46 + nl + xl + cl;
    }
    return true;
}

static const zentry *zip_find(const zip *z, const char *name)
{
    if (!z->map || !name) return NULL;
    for (size_t i = 0; i < z->n; i++) if (!strcmp(z->e[i].name, name)) return &z->e[i];
    return NULL;
}

static uint64_t zip_data_offset(const zip *z, const zentry *e)
{
    const uint8_t *lh = z->map + e->local;
    return e->local + 30 + u16(lh + 26) + u16(lh + 28);
}

/* an entry's bytes, inflated if need be: malloc'd */
static uint8_t *zip_read(const zip *z, const zentry *e)
{
    const uint8_t *src = z->map + zip_data_offset(z, e);
    uint8_t *out = malloc(e->usize ? e->usize : 1);
    if (e->method == 0) { memcpy(out, src, e->usize); return out; }
    z_stream s = { 0 };
    s.next_in = (Bytef *)src; s.avail_in = e->csize;
    s.next_out = out; s.avail_out = e->usize;
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { free(out); return NULL; }
    int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    return out;
}

/* ================================================================== the app, as the host configures it */

static struct {
    husk_app_config cfg;
    zip apk, fwres;
    bool fwres_tried;
    pthread_mutex_t q_lock;
    int32_t q[4096]; int qn;
    pthread_mutex_t t_lock;
    char **text; int ntext, ctext;
    int insets[5];
    /* the app's main thread waits here (Native.waitEvents) for what wakes it: the host's input, a message from another
     * thread, the display's refresh it asked for */
    pthread_mutex_t w_lock;
    pthread_cond_t w_cond;
    bool w_flag;
    atomic_bool want_vsync;
} A = { .q_lock = PTHREAD_MUTEX_INITIALIZER, .t_lock = PTHREAD_MUTEX_INITIALIZER,
        .w_lock = PTHREAD_MUTEX_INITIALIZER, .w_cond = PTHREAD_COND_INITIALIZER };

static void wake(void)
{
    pthread_mutex_lock(&A.w_lock);
    A.w_flag = true;
    pthread_cond_signal(&A.w_cond);
    pthread_mutex_unlock(&A.w_lock);
}

static char *dup_or_empty(const char *s) { return strdup(s ? s : ""); }

void husk_app_configure(const husk_app_config *cfg)
{
    A.cfg = *cfg;
    { void tl_set_data_dir(const char *dir); tl_set_data_dir(cfg->data); }
    A.cfg.apk = dup_or_empty(cfg->apk);
    A.cfg.framework_res = cfg->framework_res ? strdup(cfg->framework_res) : NULL;
    A.cfg.data = dup_or_empty(cfg->data);
    A.cfg.external = dup_or_empty(cfg->external);
    A.cfg.package = dup_or_empty(cfg->package);
    A.cfg.native_dir = cfg->native_dir ? strdup(cfg->native_dir) : NULL;   /* the host's string goes with its autorelease pool */
    if (!zip_open(&A.apk, A.cfg.apk)) tl_log_line("husk-port: cannot read the APK %s", A.cfg.apk);
}

/* the package, once the manifest says it, when the host did not */
void husk_app_set_package(const char *pkg);
void husk_app_set_package(const char *pkg) { if (pkg && (!A.cfg.package || !*A.cfg.package)) A.cfg.package = strdup(pkg); }

static const zip *apk_n(int n, const char **path)
{
    if (path) *path = NULL;
    if (n == 0) { if (path) *path = A.cfg.apk; return &A.apk; }
    if (n == 1) {
        if (!A.fwres_tried) {
            A.fwres_tried = true;
            if (A.cfg.framework_res && !zip_open(&A.fwres, A.cfg.framework_res))
                tl_log_line("husk-port: no platform resources at %s", A.cfg.framework_res);
        }
        if (path) *path = A.cfg.framework_res;
        return A.fwres.map ? &A.fwres : NULL;
    }
    return NULL;                     /* splits: later */
}

/* ================================================================== husk.Native */

/* standard UTF-8 (the host's: emoji and all) as a Java string; JNI's own UTF-8 is a modified one */
static jstring jstr(JNIEnv *env, const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s), k = 0;
    jchar *u = malloc((n + 1) * sizeof *u);
    const unsigned char *p = (const unsigned char *)s, *e = p + n;
    while (p < e) {
        uint32_t c = *p++;
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (more) c &= 0x3F >> more;
        for (; more && p < e && (*p & 0xC0) == 0x80; more--) c = c << 6 | (*p++ & 0x3F);
        if (more) c = 0xFFFD;
        if (c >= 0x10000) { c -= 0x10000; u[k++] = (jchar)(0xD800 | c >> 10); u[k++] = (jchar)(0xDC00 | (c & 0x3FF)); }
        else u[k++] = (jchar)c;
    }
    jstring r = (*env)->NewString(env, u, (jsize)k);
    free(u);
    return r;
}
static jbyteArray jbytes(JNIEnv *env, const uint8_t *d, size_t n)
{
    jbyteArray a = (*env)->NewByteArray(env, (jsize)n);
    if (a) (*env)->SetByteArrayRegion(env, a, 0, (jsize)n, (const jbyte *)d);
    return a;
}
static jobjectArray jstrings(JNIEnv *env, char **v, int n)
{
    jclass sc = (*env)->FindClass(env, "java/lang/String");
    jobjectArray a = (*env)->NewObjectArray(env, n, sc, NULL);
    for (int i = 0; i < n; i++) { jstring s = jstr(env, v[i]); (*env)->SetObjectArrayElement(env, a, i, s); (*env)->DeleteLocalRef(env, s); }
    return a;
}
/* a Java string's UTF-8, for the length of a call */
#define UTF(env, js, var, body) do { const char *var = (js) ? (*env)->GetStringUTFChars(env, js, NULL) : NULL; body; \
                                     if (var) (*env)->ReleaseStringUTFChars(env, js, var); } while (0)

static void N_log(JNIEnv *env, jclass c, jint prio, jstring tag, jstring msg)
{
    (void)c;
    static const char lv[] = "??VDIWEF";
    UTF(env, tag, t, UTF(env, msg, m, tl_log_line("%c/%s: %s", prio >= 0 && prio < 8 ? lv[prio] : '?', t ? t : "", m ? m : "")));
}

static jbyteArray read_entry(JNIEnv *env, const zip *z, const char *name)
{
    const zentry *e = z ? zip_find(z, name) : NULL;
    if (!e) return NULL;
    uint8_t *d = zip_read(z, e);
    if (!d) return NULL;
    jbyteArray a = jbytes(env, d, e->usize);
    free(d);
    return a;
}


/* an asset's entry; "/lib/x.so" is a file from the APK's root instead (openNonAssetFd), which asset names never start with */
static void asset_name(char *n, size_t cap, const char *p)
{
    if (p && p[0] == '/') snprintf(n, cap, "%s", p + 1); else snprintf(n, cap, "assets/%s", p ? p : "");
}

static jbyteArray N_readAsset(JNIEnv *env, jclass c, jstring path)
{
    (void)c;
    jbyteArray r = NULL;
    UTF(env, path, p, { char n[1024]; asset_name(n, sizeof n, p); r = read_entry(env, &A.apk, n); });
    return r;
}

static jobjectArray N_listAssets(JNIEnv *env, jclass c, jstring dir)
{
    (void)c;
    char prefix[1024];
    UTF(env, dir, d, snprintf(prefix, sizeof prefix, "assets/%s%s", d ? d : "", d && *d ? "/" : ""));
    size_t pl = strlen(prefix);
    char **names = NULL; int n = 0, cap = 0;
    for (size_t i = 0; i < A.apk.n; i++) {
        const char *en = A.apk.e[i].name;
        if (strncmp(en, prefix, pl) || !en[pl]) continue;
        const char *rest = en + pl;
        size_t l = strcspn(rest, "/");
        bool dup = false;
        for (int k = 0; k < n && !dup; k++) dup = strlen(names[k]) == l && !strncmp(names[k], rest, l);
        if (dup) continue;
        if (n == cap) { cap = cap ? cap * 2 : 32; names = realloc(names, (size_t)cap * sizeof *names); }
        names[n++] = strndup(rest, l);
    }
    jobjectArray a = jstrings(env, names, n);
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    return a;
}

/* a stored entry can be read in place: a file descriptor << 40 | its data's offset, as Android hands them out */
static jlong entry_fd(const zip *z, const char *path, const char *name)
{
    const zentry *e = z ? zip_find(z, name) : NULL;
    if (!e || e->method != 0) return -1;
    int fd = path ? open(path, O_RDONLY | O_CLOEXEC) : dup(z->fd);
    return fd < 0 ? -1 : ((jlong)fd << 40) | (jlong)zip_data_offset(z, e);
}

static jlong N_assetFd(JNIEnv *env, jclass c, jstring path)
{
    (void)c;
    jlong r = -1;
    UTF(env, path, p, { char n[1024]; asset_name(n, sizeof n, p); r = entry_fd(&A.apk, A.cfg.apk, n); });
    return r;
}
static jlong N_assetLength(JNIEnv *env, jclass c, jstring path)
{
    (void)c;
    jlong r = -1;
    UTF(env, path, p, { char n[1024]; asset_name(n, sizeof n, p); const zentry *e = zip_find(&A.apk, n); r = e ? (jlong)e->usize : -1; });
    return r;
}
static jbyteArray N_readApkFile(JNIEnv *env, jclass c, jint apk, jstring name)
{
    (void)c;
    jbyteArray r = NULL;
    UTF(env, name, n, r = n ? read_entry(env, apk_n(apk, NULL), n) : NULL);
    return r;
}
static jlong N_apkFileFd(JNIEnv *env, jclass c, jint apk, jstring name)
{
    (void)c;
    jlong r = -1;
    const char *path;
    const zip *z = apk_n(apk, &path);
    UTF(env, name, n, r = n ? entry_fd(z, path, n) : -1);
    return r;
}
static jlong N_apkFileLength(JNIEnv *env, jclass c, jint apk, jstring name)
{
    (void)c;
    jlong r = -1;
    const zip *z = apk_n(apk, NULL);
    UTF(env, name, n, { const zentry *e = z && n ? zip_find(z, n) : NULL; r = e ? (jlong)e->usize : -1; });
    return r;
}
static jstring N_apkPath(JNIEnv *env, jclass c) { (void)c; return jstr(env, A.cfg.apk); }
static jobjectArray N_splitPaths(JNIEnv *env, jclass c) { (void)c; return jstrings(env, NULL, 0); }
static void N_looperPrepare(JNIEnv *env, jclass c) { (void)env; (void)c; }
static jboolean N_looperPoll(JNIEnv *env, jclass c) { (void)env; (void)c; return JNI_FALSE; }
/* the main thread's wait: until woken, or ms passes (ms < 0: no limit) */
static void N_waitEvents(JNIEnv *env, jclass c, jlong ms)
{
    (void)env; (void)c;
    pthread_mutex_lock(&A.w_lock);
    if (!A.w_flag && ms != 0) {
        if (ms < 0) pthread_cond_wait(&A.w_cond, &A.w_lock);
        else {
#ifdef __APPLE__
            struct timespec rel = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
            pthread_cond_timedwait_relative_np(&A.w_cond, &A.w_lock, &rel);
#else
            struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
            t.tv_sec += (time_t)(ms / 1000); t.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&A.w_cond, &A.w_lock, &t);
#endif
        }
    }
    A.w_flag = false;
    pthread_mutex_unlock(&A.w_lock);
}
static void N_wake(JNIEnv *env, jclass c) { (void)env; (void)c; wake(); }
/* the Choreographer wants the display's next refresh (husk_app_vsync); false where the host has none to give */
static jboolean N_requestVsync(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
    if (!A.cfg.vsync) return JNI_FALSE;
    atomic_store(&A.want_vsync, true);
    return JNI_TRUE;
}
static jfloat N_refreshRate(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.refresh_rate > 0 ? A.cfg.refresh_rate : 60; }
static void N_setAppClassLoader(JNIEnv *env, jclass c, jobject loader) { (void)env; (void)c; (void)loader; }
static jstring N_dataDir(JNIEnv *env, jclass c) { (void)c; return jstr(env, A.cfg.data); }
static jstring N_externalDir(JNIEnv *env, jclass c) { (void)c; return jstr(env, A.cfg.external); }
static jstring N_packageName(JNIEnv *env, jclass c) { (void)c; return jstr(env, A.cfg.package); }
static jint N_screenWidth(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.width; }
static jint N_screenHeight(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.height; }
static jfloat N_density(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.density > 0 ? A.cfg.density : 3.0f; }
static void N_startGL(JNIEnv *env, jclass c, jobject view, jobject renderer, jint es)
{
    (void)env; (void)c; (void)view; (void)renderer;
    tl_log_line("husk-port: GLSurfaceView (GLES %d) is not there yet", es);
}
static void N_requestRender(JNIEnv *env, jclass c) { (void)env; (void)c; }
static void N_setContentView(JNIEnv *env, jclass c, jobject v) { (void)env; (void)c; (void)v; }

static jintArray N_pollInput(JNIEnv *env, jclass c)
{
    (void)c;
    pthread_mutex_lock(&A.q_lock);
    int n = A.qn / 4;
    jintArray a = NULL;
    if (n) {
        a = (*env)->NewIntArray(env, 1 + A.qn);
        jint first = n;
        (*env)->SetIntArrayRegion(env, a, 0, 1, &first);
        (*env)->SetIntArrayRegion(env, a, 1, A.qn, A.q);
        A.qn = 0;
    }
    pthread_mutex_unlock(&A.q_lock);
    return a;
}

static jobjectArray N_pollText(JNIEnv *env, jclass c)
{
    (void)c;
    pthread_mutex_lock(&A.t_lock);
    jobjectArray a = NULL;
    if (A.ntext) {
        a = jstrings(env, A.text, A.ntext);
        for (int i = 0; i < A.ntext; i++) free(A.text[i]);
        A.ntext = 0;
    }
    pthread_mutex_unlock(&A.t_lock);
    return a;
}

static void N_showKeyboard(JNIEnv *env, jclass c, jboolean show, jint type, jint ime)
{
    (void)env; (void)c;
    if (A.cfg.show_keyboard) A.cfg.show_keyboard(show, type, ime);
}
static void N_setClipboard(JNIEnv *env, jclass c, jstring text)
{
    (void)c;
    UTF(env, text, t, if (A.cfg.set_clipboard) A.cfg.set_clipboard(t ? t : ""));
}
static jstring N_getClipboard(JNIEnv *env, jclass c)
{
    (void)c;
    char *t = A.cfg.get_clipboard ? A.cfg.get_clipboard() : NULL;
    jstring s = jstr(env, t);
    free(t);
    return s;
}
static void N_share(JNIEnv *env, jclass c, jstring text) { (void)c; UTF(env, text, t, if (A.cfg.share && t) A.cfg.share(t)); }
static void N_pickDocuments(JNIEnv *env, jclass c, jstring types, jboolean multiple)
{
    (void)c;
    if (!A.cfg.pick_documents) { husk_app_documents_picked(NULL, 0); return; }   /* no picker here (host tests): the user backed out */
    UTF(env, types, t, A.cfg.pick_documents(t ? t : "*/*", multiple != 0));
}
static void N_setOrientation(JNIEnv *env, jclass c, jint o) { (void)env; (void)c; if (A.cfg.set_orientation) A.cfg.set_orientation(o); }
static jintArray N_insets(JNIEnv *env, jclass c)
{
    (void)c;
    jintArray a = (*env)->NewIntArray(env, 5);
    (*env)->SetIntArrayRegion(env, a, 0, 5, A.insets);
    return a;
}
static jboolean N_nightMode(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.night_mode; }
/* the phone's clock: 12-hour when its locale's hour pattern ("j") has an am/pm marker */
static jboolean N_is24Hour(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
    CFLocaleRef loc = CFLocaleCopyCurrent();
    CFStringRef f = CFDateFormatterCreateDateFormatFromTemplate(NULL, CFSTR("j"), 0, loc);
    bool h24 = !f || CFStringFind(f, CFSTR("a"), 0).location == kCFNotFound;
    if (f) CFRelease(f);
    CFRelease(loc);
    return h24;
}
static jint S_battery(JNIEnv *env, jclass c) { (void)env; (void)c; return A.cfg.battery ? A.cfg.battery() : -1; }
static void N_vibrate(JNIEnv *env, jclass c, jlong ms) { (void)env; (void)c; if (A.cfg.vibrate) A.cfg.vibrate((int)ms); }
static void N_openUrl(JNIEnv *env, jclass c, jstring url)
{
    (void)c;
    UTF(env, url, u, { tl_log_line("husk-port: open %s", u ? u : "?"); if (A.cfg.open_url && u) A.cfg.open_url(u); });
}
static void N_exit(JNIEnv *env, jclass c) { (void)env; (void)c; tl_log_line("husk-port: the app finished"); if (A.cfg.exit) A.cfg.exit(); }
static jlong N_uptimeNanos(JNIEnv *env, jclass c)
{
    (void)env; (void)c;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (jlong)t.tv_sec * 1000000000ll + t.tv_nsec;
}

static const JNINativeMethod k_native[] = {
    { "log", "(ILjava/lang/String;Ljava/lang/String;)V", (void *)N_log },
    { "readAsset", "(Ljava/lang/String;)[B", (void *)N_readAsset },
    { "listAssets", "(Ljava/lang/String;)[Ljava/lang/String;", (void *)N_listAssets },
    { "assetFd", "(Ljava/lang/String;)J", (void *)N_assetFd },
    { "assetLength", "(Ljava/lang/String;)J", (void *)N_assetLength },
    { "readApkFile", "(ILjava/lang/String;)[B", (void *)N_readApkFile },
    { "apkFileFd", "(ILjava/lang/String;)J", (void *)N_apkFileFd },
    { "apkFileLength", "(ILjava/lang/String;)J", (void *)N_apkFileLength },
    { "apkPath", "()Ljava/lang/String;", (void *)N_apkPath },
    { "splitPaths", "()[Ljava/lang/String;", (void *)N_splitPaths },
    { "nativeLooperPrepare", "()V", (void *)N_looperPrepare },
    { "nativeLooperPoll", "()Z", (void *)N_looperPoll },
    { "waitEvents", "(J)V", (void *)N_waitEvents },
    { "wake", "()V", (void *)N_wake },
    { "requestVsync", "()Z", (void *)N_requestVsync },
    { "refreshRate", "()F", (void *)N_refreshRate },
    { "setAppClassLoader", "(Ljava/lang/ClassLoader;)V", (void *)N_setAppClassLoader },
    { "dataDir", "()Ljava/lang/String;", (void *)N_dataDir },
    { "externalDir", "()Ljava/lang/String;", (void *)N_externalDir },
    { "packageName", "()Ljava/lang/String;", (void *)N_packageName },
    { "screenWidth", "()I", (void *)N_screenWidth },
    { "screenHeight", "()I", (void *)N_screenHeight },
    { "density", "()F", (void *)N_density },
    { "startGL", "(Ljava/lang/Object;Ljava/lang/Object;I)V", (void *)N_startGL },
    { "requestRender", "()V", (void *)N_requestRender },
    { "setContentView", "(Ljava/lang/Object;)V", (void *)N_setContentView },
    { "pollInput", "()[I", (void *)N_pollInput },
    { "pollText", "()[Ljava/lang/String;", (void *)N_pollText },
    { "showKeyboard", "(ZII)V", (void *)N_showKeyboard },
    { "setClipboard", "(Ljava/lang/String;)V", (void *)N_setClipboard },
    { "getClipboard", "()Ljava/lang/String;", (void *)N_getClipboard },
    { "share", "(Ljava/lang/String;)V", (void *)N_share },
    { "pickDocuments", "(Ljava/lang/String;Z)V", (void *)N_pickDocuments },
    { "setOrientation", "(I)V", (void *)N_setOrientation },
    { "insets", "()[I", (void *)N_insets },
    { "nightMode", "()Z", (void *)N_nightMode },
    { "is24Hour", "()Z", (void *)N_is24Hour },
    { "vibrate", "(J)V", (void *)N_vibrate },
    { "openUrl", "(Ljava/lang/String;)V", (void *)N_openUrl },
    { "exit", "()V", (void *)N_exit },
    { "uptimeNanos", "()J", (void *)N_uptimeNanos },
};

/* husk.Sensors: none yet (CoreMotion comes later) */
static jboolean S_available(JNIEnv *env, jclass c, jint type) { (void)env; (void)c; (void)type; return JNI_FALSE; }
static void S_start(JNIEnv *env, jclass c, jint type, jint us) { (void)env; (void)c; (void)type; (void)us; }
static void S_stop(JNIEnv *env, jclass c, jint type) { (void)env; (void)c; (void)type; }
static jlong S_read(JNIEnv *env, jclass c, jint type, jfloatArray v) { (void)env; (void)c; (void)type; (void)v; return 0; }
static const JNINativeMethod k_sensors[] = {
    { "available", "(I)Z", (void *)S_available }, { "start", "(II)V", (void *)S_start },
    { "stop", "(I)V", (void *)S_stop }, { "read", "(I[F)J", (void *)S_read }, { "battery", "()I", (void *)S_battery },
};

/* husk.EGLNative: OpenGL ES is not part of this port yet (the main line's is ANGLE over Metal). An app's own EGL gets no context
 * (EGL_NO_CONTEXT, as a device without the API would answer) and goes on without GL: Telegram's intro shows without its animation. */
static jlong E_createContext(JNIEnv *env, jclass c, jint version, jlong share) { (void)env; (void)c; (void)version; (void)share; return 0; }
static void E_destroy(JNIEnv *env, jclass c, jlong h) { (void)env; (void)c; (void)h; }
static jlong E_createSurface(JNIEnv *env, jclass c, jint w, jint h) { (void)env; (void)c; (void)w; (void)h; return 0; }
static jboolean E_makeCurrent(JNIEnv *env, jclass c, jlong s, jlong x) { (void)env; (void)c; (void)s; (void)x; return JNI_FALSE; }
static jboolean E_swap(JNIEnv *env, jclass c, jlong s, jlong x, jint a, jint b, jint w, jint h) { (void)env; (void)c; (void)s; (void)x; (void)a; (void)b; (void)w; (void)h; return JNI_FALSE; }
static jboolean E_isRenderThread(JNIEnv *env, jclass c) { (void)env; (void)c; return JNI_FALSE; }
static void E_hide(JNIEnv *env, jclass c) { (void)env; (void)c; }
static const JNINativeMethod k_egl[] = {
    { "createContext", "(IJ)J", (void *)E_createContext }, { "destroyContext", "(J)V", (void *)E_destroy },
    { "createSurface", "(II)J", (void *)E_createSurface }, { "destroySurface", "(J)V", (void *)E_destroy },
    { "makeCurrent", "(JJ)Z", (void *)E_makeCurrent }, { "swap", "(JJIIII)Z", (void *)E_swap },
    { "isRenderThread", "()Z", (void *)E_isRenderThread }, { "hide", "()V", (void *)E_hide },
};

bool husk_register_gfx(JNIEnv *env);   /* gfx_jni.c, from gen-jni.py */
bool husk_register_sqlite(JNIEnv *env);   /* sqlite.c */
bool husk_register_web(JNIEnv *env);      /* web_jni.m, from gen-jni.py */
bool husk_register_audio(JNIEnv *env);    /* audio_jni.c, from gen-jni.py */
bool husk_register_nativelibs(JNIEnv *env);   /* nativelibs.c */
void husk_native_libs_configure(const char *apk, const char *aot_dir);

bool husk_framework_register(JNIEnv *env)
{
    bool ok = shim_register(env, "husk/Native", k_native, (int)(sizeof k_native / sizeof *k_native));
    ok &= shim_register(env, "husk/Sensors", k_sensors, (int)(sizeof k_sensors / sizeof *k_sensors));
    ok &= husk_register_sqlite(env);
    ok &= husk_register_web(env);
    ok &= husk_register_audio(env);
    ok &= husk_register_nativelibs(env);
    ok &= shim_register(env, "husk/EGLNative", k_egl, (int)(sizeof k_egl / sizeof *k_egl));
    husk_native_libs_configure(A.cfg.apk, A.cfg.native_dir);
    return husk_register_gfx(env) && ok;
}

/* ================================================================== from the host */

static void push(int phase, int id, float x, float y)
{
    union { float f; int32_t i; } fx = { x }, fy = { y };
    pthread_mutex_lock(&A.q_lock);
    if (A.qn + 4 <= 4096) { A.q[A.qn++] = phase; A.q[A.qn++] = id; A.q[A.qn++] = fx.i; A.q[A.qn++] = fy.i; }
    pthread_mutex_unlock(&A.q_lock);
    wake();
}
/* the display refreshed: a frame for the Choreographer, when it asked for one */
void husk_app_vsync(void) { if (atomic_exchange(&A.want_vsync, false)) push(10, 0, 0, 0); }
void husk_app_touch(int phase, int id, float x, float y) { push(phase, id, x, y); }
void husk_app_back(void) { push(5, 0, 0, 0); }
void husk_app_web_event(void) { push(9, 0, 0, 0); }   /* web.m: husk.Web has events to poll */
/* tl_data_dir: bionic's (husk-tl-bionic-io.c), set to the app's data directory when the host configures the app */

static void text_push(const char *kind, const char *utf8)
{
    pthread_mutex_lock(&A.t_lock);
    if (A.ntext == A.ctext) { A.ctext = A.ctext ? A.ctext * 2 : 16; A.text = realloc(A.text, (size_t)A.ctext * sizeof *A.text); }
    size_t l = strlen(kind) + (utf8 ? strlen(utf8) : 0) + 1;
    char *e = malloc(l);
    snprintf(e, l, "%s%s", kind, utf8 ? utf8 : "");
    A.text[A.ntext++] = e;
    pthread_mutex_unlock(&A.t_lock);
    push(7, 0, 0, 0);
}
void husk_app_text(const char *utf8) { if (utf8 && *utf8) text_push("t", utf8); }
void husk_app_text_delete(void) { text_push("d", NULL); }
void husk_app_text_action(void) { text_push("e", NULL); }
void husk_app_text_hidden(void) { text_push("h", NULL); }
/* husk.Documents: "f<path>" each file picked, then "F" */
void husk_app_documents_picked(const char *const *paths, int n)
{
    for (int i = 0; i < n; i++) if (paths[i] && *paths[i]) text_push("f", paths[i]);
    text_push("F", NULL);
}
void husk_app_insets(int l, int t, int r, int b, int ime)
{
    A.insets[0] = l; A.insets[1] = t; A.insets[2] = r; A.insets[3] = b; A.insets[4] = ime;
    push(8, 0, 0, 0);
}

/* gfx.c */
bool tl_ui_frame_take(const uint8_t **px, int *w, int *h, bool *any);
void tl_ui_frame_done(void);
bool husk_frame_take(const uint8_t **px, int *w, int *h, bool *any) { return tl_ui_frame_take(px, w, h, any); }
void husk_frame_done(void) { tl_ui_frame_done(); }
bool tl_ui_frame_hold(const uint8_t **px, int *w, int *h, bool *any, int *token);
void tl_ui_frame_release(int token);
bool husk_frame_hold(const uint8_t **px, int *w, int *h, bool *any, int *token) { return tl_ui_frame_hold(px, w, h, any, token); }
void husk_frame_release(int token) { tl_ui_frame_release(token); }

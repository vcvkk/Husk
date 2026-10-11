/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * An app on ART and the framework, on macOS, with no window: what it draws is written out as PNG
 * files, a frame a second. For CI, where it shows what the iOS app would show.
 *
 *   mac-host --art DIR --apk APK [--fwres framework-res.apk] [--data DIR] [--size 1080x2400] [--density 3] [--seconds 20]
 *            [--frames DIR] [--tap X,Y@SECONDS]... [--type TEXT@SECONDS]...
 *
 * --art is a boot image directory (port/art/boot-image.sh's layout) with the framework in it;
 * the APK's compiled code is beside it, in oat/arm64/<apk name>.odex{,.dylib}.
 */
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../jni/husk-framework.h"
#include "../jni/husk-launch.h"

static const char *g_art, *g_data;

static void *app_main(void *unused)
{
    (void)unused;
    pthread_setname_np("main");
    JavaVM *vm; JNIEnv *env;
    const char *extra[] = { "-verbose:oat" };
    if (!husk_vm_start(g_art, g_data, extra, 0, &vm, &env)) exit(3);
    husk_app_run(env);
    fprintf(stderr, "mac-host: the app ended\n");
    (*vm)->DetachCurrentThread(vm);
    return NULL;
}

static void write_png(const char *path, const uint8_t *px, int w, int h)
{
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef c = CGBitmapContextCreate((void *)px, (size_t)w, (size_t)h, 8, (size_t)w * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(c);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    CGImageDestinationRef d = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(d, img, NULL);
    CGImageDestinationFinalize(d);
    CFRelease(d); CFRelease(url); CGImageRelease(img); CGContextRelease(c); CGColorSpaceRelease(cs);
}

/* the display's refreshes, as a display link would give them (a headless Mac has none) */
static void *vsync_main(void *arg)
{
    (void)arg;
    uint64_t period = 16666667, next = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + period;
    for (;;) {
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (next > now) { struct timespec ts = { 0, (long)(next - now) }; nanosleep(&ts, NULL); }
        husk_app_vsync();
        next += period;
        now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (next < now) next = now + period;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    husk_app_config cfg = { .width = 1080, .height = 2400, .density = 3.0f };
    int seconds = 20;
    const char *frames = "frames";
    struct { float x, y; int at; } taps[16]; int ntaps = 0;
    struct { char text[256]; int at; } types[8]; int ntypes = 0;   /* text typed, as the keyboard commits it */
    for (int i = 1; i + 1 < argc; i += 2) {
        const char *k = argv[i], *v = argv[i + 1];
        if (!strcmp(k, "--art")) g_art = v;
        else if (!strcmp(k, "--apk")) cfg.apk = v;
        else if (!strcmp(k, "--fwres")) cfg.framework_res = v;
        else if (!strcmp(k, "--data")) g_data = v;
        else if (!strcmp(k, "--size")) sscanf(v, "%dx%d", &cfg.width, &cfg.height);
        else if (!strcmp(k, "--density")) cfg.density = strtof(v, NULL);
        else if (!strcmp(k, "--seconds")) seconds = atoi(v);
        else if (!strcmp(k, "--frames")) frames = v;
        else if (!strcmp(k, "--tap") && ntaps < 16) {
            if (sscanf(v, "%f,%f@%d", &taps[ntaps].x, &taps[ntaps].y, &taps[ntaps].at) == 3) ntaps++;
        } else if (!strcmp(k, "--type") && ntypes < 8) {
            const char *at = strrchr(v, '@');
            if (at) { snprintf(types[ntypes].text, sizeof types[ntypes].text, "%.*s", (int)(at - v), v); types[ntypes++].at = atoi(at + 1); }
        } else { fprintf(stderr, "mac-host: unknown option %s\n", k); return 2; }
    }
    if (!g_art || !cfg.apk) { fprintf(stderr, "mac-host: --art and --apk are needed\n"); return 2; }
    char data[1024], ext[1100];
    if (!g_data) { snprintf(data, sizeof data, "%s/husk-data", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"); g_data = data; }
    snprintf(ext, sizeof ext, "%s/external", g_data);
    mkdir(g_data, 0755); mkdir(ext, 0755); mkdir(frames, 0755);
    cfg.data = g_data; cfg.external = ext;
    cfg.vsync = true; cfg.refresh_rate = 60;     /* a 60 Hz display: vsync_main */
    husk_app_configure(&cfg);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 << 20);
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);   /* as the iOS shell's */
    pthread_t t;
    pthread_create(&t, &attr, app_main, NULL);
    pthread_t vt;
    pthread_create(&vt, &attr, vsync_main, NULL);

    int written = 0;
    for (int tenth = 0; tenth < seconds * 10; tenth++) {
        usleep(100000);
        for (int i = 0; i < ntaps; i++) if (taps[i].at * 10 == tenth) {
            fprintf(stderr, "mac-host: tap %.0f,%.0f\n", taps[i].x, taps[i].y);
            husk_app_touch(0, 0, taps[i].x, taps[i].y);
            husk_app_touch(2, 0, taps[i].x, taps[i].y);
        }
        for (int i = 0; i < ntypes; i++) if (types[i].at * 10 == tenth) {
            fprintf(stderr, "mac-host: type %s\n", types[i].text);
            husk_app_text(types[i].text);
        }
        if (tenth % 10 != 9) continue;
        const uint8_t *px; int w, h; bool any;
        if (!husk_frame_take(&px, &w, &h, &any)) continue;
        if (any) {
            char path[1200];
            snprintf(path, sizeof path, "%s/frame-%02ds.png", frames, (tenth + 1) / 10);
            write_png(path, px, w, h);
            fprintf(stderr, "mac-host: %s (%dx%d)\n", path, w, h);
            written++;
        }
        husk_frame_done();
    }
    fprintf(stderr, "mac-host: %d frames written\n", written);
    return written ? 0 : 1;
}

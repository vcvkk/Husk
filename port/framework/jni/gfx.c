/* SPDX-License-Identifier: GPL-2.0-or-later */
/* From Husk's main line (Leviidev/Husk src/translation-layer-next/husk-tl-dvm-graphics.c at 2425372), as
 * it is but for its includes, txBreak's letter spacing (make_attributed) and pathOp: its natives run on JNI here (shim.h,
 * gen-jni.py writes their JNI methods from k_gfx). */
/*
 * android.graphics for Java apps on Husk's Dalvik runtime: Bitmap, Canvas, Paint's text, Typeface and the shaders, drawn with
 * CoreGraphics and CoreText (the natives of husk.Gfx; the classes themselves are Java, in src/java-framework).
 *
 * A Bitmap is premultiplied RGBA in memory, rows top first, which is what a CGBitmapContext draws into and what GL uploads as is.
 * A Canvas is a CGContext over a bitmap, its CTM flipped once so that y grows down as Android's does. A Paint is an int[] the Java
 * side keeps current (the P_* slots below), so a draw call reads it in one go instead of calling back into Java.
 *
 * Also here: the frame the view system hands over (husk.Native.present) for the GL thread to put on screen.
 */
#define _DARWIN_C_SOURCE
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shim.h"



/* ================================================================== the Paint packet (android.graphics.Paint.mP) */

enum {
    P_COLOR, P_STYLE, P_STROKE, P_FLAGS, P_CAP, P_JOIN, P_TEXTSIZE, P_ALIGN, P_MITER, P_SHRAD, P_SHDX, P_SHDY, P_SHCOL,
    P_SCALEX, P_SKEWX, P_LSPACE, P_XFER, P_CFCOLOR, P_CFMODE, P_ALPHA, P_COUNT
};
enum { PF_AA = 1, PF_FILTER = 2, PF_FAKEBOLD = 4, PF_UNDERLINE = 8, PF_STRIKE = 16 };

static float pf(const int32_t *p, int i) { union { int32_t i; float f; } u = { p[i] }; return u.f; }
static const int32_t *paint_of(jobj *a)
{
    static const int32_t k_default[P_COUNT] = { (int32_t)0xFF000000, 0, 0, PF_AA, 0, 0, 0x41400000 /* 12.0f */, 0, 0x40800000, 0, 0, 0, 0,
                                                0x3F800000, 0, 0, 3, 0, -1, 255 };
    if (!a || a->kind != TL_K_PRIM_ARRAY || a->arr.len < P_COUNT) return k_default;
    return a->arr.data;
}

/* ================================================================== bitmaps */

typedef struct gbitmap {
    int w, h;
    size_t stride;
    uint8_t *px;
    CGContextRef ctx;               /* drawn into by a Canvas; also how its pixels become a CGImage */
    struct gscache *cache;          /* blit: the bitmap scaled for its last size drawn */
} gbitmap;

static CGColorSpaceRef rgb(void)
{
    static CGColorSpaceRef cs;
    if (!cs) cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    return cs;
}

static gbitmap *bm_new(int w, int h)
{
    if (w <= 0 || h <= 0 || (int64_t)w * h > (1ll << 28)) return NULL;
    gbitmap *b = calloc(1, sizeof(*b));
    b->w = w; b->h = h; b->stride = (size_t)w * 4;
    b->px = calloc((size_t)h, b->stride);
    if (!b->px) { free(b); return NULL; }
    b->ctx = CGBitmapContextCreate(b->px, (size_t)w, (size_t)h, 8, b->stride, rgb(), kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (!b->ctx) { free(b->px); free(b); return NULL; }
    return b;
}
/* libjnigraphics: native code reads and writes a Bitmap's pixels in place (Lottie's and Rive's renderers, image decoders). Husk's
 * bitmaps are premultiplied RGBA rows, which is ANDROID_BITMAP_FORMAT_RGBA_8888 as Android lays it out. */
typedef struct { uint32_t width, height, stride; int32_t format; uint32_t flags; } tl_abitmap_info;
/* env: the runtime's JNIEnv, as an app's library (port/native) has it, and the Bitmap a JNI reference */
static gbitmap *bm_of_java(void *env, void *bitmap)
{
    JNIEnv *e = env;
    if (!e || !bitmap) return NULL;
    static jfieldID fid;
    if (!fid) {
        jclass c = (*e)->FindClass(e, "android/graphics/Bitmap");
        if (c) { fid = (*e)->GetFieldID(e, c, "mNative", "J"); (*e)->DeleteLocalRef(e, c); }
        if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); fid = NULL; }
        if (!fid) return NULL;
    }
    return (gbitmap *)(uintptr_t)(*e)->GetLongField(e, (jobject)bitmap, fid);
}
int tl_AndroidBitmap_getInfo(void *env, void *bitmap, tl_abitmap_info *info)
{
    gbitmap *b = bm_of_java(env, bitmap);
    if (!b || !info) return -1;                                 /* ANDROID_BITMAP_RESULT_BAD_PARAMETER */
    info->width = (uint32_t)b->w; info->height = (uint32_t)b->h; info->stride = (uint32_t)b->stride;
    info->format = 1;                                           /* RGBA_8888 */
    info->flags = 0;                                            /* premultiplied */
    return 0;
}
int tl_AndroidBitmap_lockPixels(void *env, void *bitmap, void **addr)
{
    gbitmap *b = bm_of_java(env, bitmap);
    if (!b) return -1;
    if (addr) *addr = b->px;
    return 0;
}
int tl_AndroidBitmap_unlockPixels(void *env, void *bitmap) { return bm_of_java(env, bitmap) ? 0 : -1; }
int32_t tl_AndroidBitmap_getDataSpace(void *env, void *bitmap) { (void)env; (void)bitmap; return 142671872; /* ADATASPACE_SRGB */ }

static void bm_cache_free(gbitmap *b);
static void bm_free(gbitmap *b)
{
    if (!b) return;
    CGContextRelease(b->ctx);
    bm_cache_free(b);
    free(b->px);
    free(b);
}
static CGImageRef bm_image(gbitmap *b) { return CGBitmapContextCreateImage(b->ctx); }

/* ARGB (unpremultiplied, as Android hands colours) to and from the premultiplied RGBA bytes */
static inline uint32_t to_argb(const uint8_t *p)
{
    uint32_t a = p[3];
    if (!a) return 0;
    uint32_t r = p[0], g = p[1], b = p[2];
    if (a != 255) { r = (r * 255 + a / 2) / a; g = (g * 255 + a / 2) / a; b = (b * 255 + a / 2) / a; if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
    return a << 24 | r << 16 | g << 8 | b;
}
static inline void from_argb(uint8_t *p, uint32_t c)
{
    uint32_t a = c >> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (a != 255) { r = (r * a + 127) / 255; g = (g * a + 127) / 255; b = (b * a + 127) / 255; }
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static gbitmap *bm_from_image(CGImageRef img, int sample)
{
    if (sample < 1) sample = 1;
    int w = (int)CGImageGetWidth(img) / sample, h = (int)CGImageGetHeight(img) / sample;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    gbitmap *b = bm_new(w, h);
    if (!b) return NULL;
    CGContextSetInterpolationQuality(b->ctx, kCGInterpolationHigh);
    CGContextSetBlendMode(b->ctx, kCGBlendModeCopy);
    CGContextDrawImage(b->ctx, CGRectMake(0, 0, w, h), img);
    CGContextSetBlendMode(b->ctx, kCGBlendModeNormal);
    return b;
}

/* ================================================================== typefaces */

typedef struct gface {
    CTFontRef base;                 /* at 12 pt; sized copies are cached below */
    int style;                      /* Typeface.BOLD 1, ITALIC 2 */
    int weight;
} gface;

static CTFontRef roboto(void)
{
    static CTFontRef f; static bool tried;
    if (tried) return f;
    tried = true;
    char buf[1100];
    const char *p = tl_path_resolve("/system/fonts/Roboto-Regular.ttf", buf, sizeof(buf));
    struct stat st;
    if (getenv("TL_SYSFONTS")) { snprintf(buf, sizeof(buf), "%s/fonts/Roboto-Regular.ttf", getenv("TL_SYSFONTS")); p = buf; }
    if (!p || stat(p, &st) != 0 || p[0] != '/' || !strncmp(p, "/system/", 8)) return NULL;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)p, (CFIndex)strlen(p), false);
    CFArrayRef ds = url ? CTFontManagerCreateFontDescriptorsFromURL(url) : NULL;
    if (ds && CFArrayGetCount(ds) > 0) f = CTFontCreateWithFontDescriptor(CFArrayGetValueAtIndex(ds, 0), 12, NULL);
    if (ds) CFRelease(ds);
    if (url) CFRelease(url);
    return f;
}

/* A variable font (Roboto is one) at a weight: its 'wght' axis; others get the bold trait from 700 up. */
static CTFontRef with_weight(CTFontRef f, int weight, bool italic)
{
    CTFontRef out = NULL;
    CFArrayRef axes = CTFontCopyVariationAxes(f);
    if (axes && CFArrayGetCount(axes) > 0 && weight != 400) {
        int tag = 0x77676874; /* 'wght' */
        CFNumberRef k = CFNumberCreate(NULL, kCFNumberIntType, &tag);
        double w = weight;
        CFNumberRef v = CFNumberCreate(NULL, kCFNumberDoubleType, &w);
        CFDictionaryRef var = CFDictionaryCreate(NULL, (const void **)&k, (const void **)&v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        const void *ak[] = { kCTFontVariationAttribute }; const void *av[] = { var };
        CFDictionaryRef attrs = CFDictionaryCreate(NULL, ak, av, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CTFontDescriptorRef d = CTFontDescriptorCreateWithAttributes(attrs);
        out = CTFontCreateCopyWithAttributes(f, 12, NULL, d);
        CFRelease(d); CFRelease(attrs); CFRelease(var); CFRelease(v); CFRelease(k);
    }
    if (axes) CFRelease(axes);
    if (!out) {
        CTFontSymbolicTraits t = (weight >= 600 ? kCTFontBoldTrait : 0) | (italic ? kCTFontItalicTrait : 0);
        if (t) out = CTFontCreateCopyWithSymbolicTraits(f, 12, NULL, t, t);
        if (!out) { CFRetain(f); out = f; }
    } else if (italic) {
        CTFontRef it = CTFontCreateCopyWithSymbolicTraits(out, 12, NULL, kCTFontItalicTrait, kCTFontItalicTrait);
        if (it) { CFRelease(out); out = it; }
    }
    return out;
}

static gface *face_make(CTFontRef base, int style, int weight)
{
    gface *g = calloc(1, sizeof(*g));
    if (!weight) weight = (style & 1) ? 700 : 400;
    g->base = with_weight(base, weight, (style & 2) != 0);
    g->style = style; g->weight = weight;
    return g;
}

static gface *face_family(const char *family, int style)
{
    CTFontRef base = NULL; bool own = false;
    int weight = (style & 1) ? 700 : 400;
    char fam[96]; snprintf(fam, sizeof(fam), "%s", family ? family : "sans-serif");
    for (char *c = fam; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    if (strstr(fam, "medium")) weight = (style & 1) ? 800 : 500;
    else if (strstr(fam, "light")) weight = 300;
    else if (strstr(fam, "thin")) weight = 200;
    else if (strstr(fam, "black")) weight = 900;
    if (strstr(fam, "mono")) { base = CTFontCreateWithName(CFSTR("Menlo"), 12, NULL); own = true; }
    else if (strstr(fam, "serif") && !strstr(fam, "sans")) { base = CTFontCreateWithName(CFSTR("Georgia"), 12, NULL); own = true; }
    else if ((base = roboto())) own = false;
    else { base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 12, NULL); own = true; }
    gface *g = face_make(base, style, weight);
    if (own) CFRelease(base);
    return g;
}

static gface *default_face(void)
{
    static gface *f;
    if (!f) f = face_family("sans-serif", 0);
    return f;
}

/* sized fonts, cached by face and size; the caller releases what it is given */
static CTFontRef sized(gface *g, float size)
{
    static struct { gface *g; int q; CTFontRef f; } cache[512];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    if (!g) g = default_face();
    if (!(size > 0)) size = 12;
    int q = (int)lrintf(size * 8);
    unsigned h = ((unsigned)(uintptr_t)g * 2654435761u ^ (unsigned)q * 40503u) % 512;
    pthread_mutex_lock(&lock);
    if (cache[h].g == g && cache[h].q == q) { CTFontRef f = cache[h].f; CFRetain(f); pthread_mutex_unlock(&lock); return f; }
    CTFontRef f = CTFontCreateCopyWithAttributes(g->base, q / 8.0, NULL, NULL);
    if (cache[h].f) CFRelease(cache[h].f);
    cache[h].g = g; cache[h].q = q; cache[h].f = f;
    CFRetain(f);
    pthread_mutex_unlock(&lock);
    return f;
}

/* ================================================================== shaders */

enum { SH_LINEAR, SH_RADIAL, SH_SWEEP, SH_BITMAP };
typedef struct gshader {
    int kind;
    float x0, y0, x1, y1, r;
    CGGradientRef grad;
    int ncolors;
    uint32_t *colors; float *pos;
    int tile;                       /* 0 clamp, 1 repeat, 2 mirror */
    CGImageRef image;               /* SH_BITMAP */
    int iw, ih;
    CGAffineTransform local;
    uint32_t *lut; int lut_alpha;   /* grad_fill's 256 premultiplied colours, for the paint alpha lut_alpha */
} gshader;

static CGColorRef color_of(uint32_t c, float alpha)
{
    CGFloat comp[4] = { ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, (c >> 24) / 255.0 * alpha };
    return CGColorCreate(rgb(), comp);
}

static gshader *shader_new(int kind, const uint32_t *colors, const float *pos, int n, int tile)
{
    gshader *s = calloc(1, sizeof(*s));
    s->kind = kind; s->tile = tile; s->local = CGAffineTransformIdentity;
    if (n < 1) n = 1;
    s->ncolors = n;
    s->colors = malloc(sizeof(uint32_t) * (size_t)n);
    s->pos = malloc(sizeof(float) * (size_t)n);
    CGFloat *comp = malloc(sizeof(CGFloat) * 4 * (size_t)n), *loc = malloc(sizeof(CGFloat) * (size_t)n);
    for (int i = 0; i < n; i++) {
        uint32_t c = colors ? colors[i] : 0xFF000000u;
        s->colors[i] = c;
        s->pos[i] = pos ? pos[i] : (n == 1 ? 0 : (float)i / (n - 1));
        comp[4 * i] = ((c >> 16) & 255) / 255.0; comp[4 * i + 1] = ((c >> 8) & 255) / 255.0; comp[4 * i + 2] = (c & 255) / 255.0; comp[4 * i + 3] = (c >> 24) / 255.0;
        loc[i] = s->pos[i];
    }
    s->grad = CGGradientCreateWithColorComponents(rgb(), comp, loc, (size_t)n);
    free(comp); free(loc);
    return s;
}

/* The colour a sweep gradient has at t in [0, 1] */
static uint32_t sweep_at(const gshader *s, float t)
{
    if (s->ncolors == 1 || t <= s->pos[0]) return s->colors[0];
    for (int i = 1; i < s->ncolors; i++) {
        if (t <= s->pos[i]) {
            float span = s->pos[i] - s->pos[i - 1], k = span > 0 ? (t - s->pos[i - 1]) / span : 0;
            uint32_t a = s->colors[i - 1], b = s->colors[i], out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                float ca = (a >> sh) & 255, cb = (b >> sh) & 255;
                out |= (uint32_t)lrintf(ca + (cb - ca) * k) << sh;
            }
            return out;
        }
    }
    return s->colors[s->ncolors - 1];
}

/* Fill the current clip with the shader, the paint's alpha applied. The path to fill is already the clip. */
static void shader_fill(CGContextRef c, const gshader *s, float alpha)
{
    CGContextSaveGState(c);
    CGContextSetAlpha(c, alpha);
    CGContextConcatCTM(c, s->local);
    CGGradientDrawingOptions ext = s->tile == 0 ? (kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation) : 0;
    if (s->kind == SH_LINEAR) {
        if (s->tile == 0) CGContextDrawLinearGradient(c, s->grad, CGPointMake(s->x0, s->y0), CGPointMake(s->x1, s->y1), ext);
        else {
            /* repeat or mirror: the gradient laid out again along its axis, over what the clip covers */
            CGRect box = CGContextGetClipBoundingBox(c);
            float dx = s->x1 - s->x0, dy = s->y1 - s->y0, len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-3f) { CGContextRestoreGState(c); return; }
            float reach = (float)(fabs(box.size.width) + fabs(box.size.height) + fabs(box.origin.x - s->x0) + fabs(box.origin.y - s->y0));
            int nrep = (int)(reach / len) + 2;
            if (nrep > 400) nrep = 400;
            for (int k = -nrep; k <= nrep; k++) {
                bool flip = s->tile == 2 && (k & 1);
                CGPoint a = CGPointMake(s->x0 + dx * k, s->y0 + dy * k), b = CGPointMake(s->x1 + dx * k, s->y1 + dy * k);
                CGContextSaveGState(c);
                /* the band this repetition covers */
                CGMutablePathRef band = CGPathCreateMutable();
                float nx = -dy / len * reach * 2, ny = dx / len * reach * 2;
                CGPathMoveToPoint(band, NULL, a.x + nx, a.y + ny); CGPathAddLineToPoint(band, NULL, b.x + nx, b.y + ny);
                CGPathAddLineToPoint(band, NULL, b.x - nx, b.y - ny); CGPathAddLineToPoint(band, NULL, a.x - nx, a.y - ny);
                CGPathCloseSubpath(band);
                CGContextAddPath(c, band); CGContextClip(c); CGPathRelease(band);
                if (flip) CGContextDrawLinearGradient(c, s->grad, b, a, kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
                else CGContextDrawLinearGradient(c, s->grad, a, b, kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
                CGContextRestoreGState(c);
            }
        }
    } else if (s->kind == SH_RADIAL) {
        CGContextDrawRadialGradient(c, s->grad, CGPointMake(s->x0, s->y0), 0, CGPointMake(s->x0, s->y0), s->r, ext);
    } else if (s->kind == SH_SWEEP) {
        /* wedges around the centre; Android's sweep starts at 3 o'clock and turns clockwise (y down) */
        CGRect box = CGContextGetClipBoundingBox(c);
        float R = (float)(fabs(box.size.width) + fabs(box.size.height) + fabs(box.origin.x - s->x0) + fabs(box.origin.y - s->y0)) + 1;
        const int N = 180;
        for (int i = 0; i < N; i++) {
            float a0 = (float)(2 * M_PI * i / N), a1 = (float)(2 * M_PI * (i + 1) / N) + 0.01f;
            CGColorRef col = color_of(sweep_at(s, (i + 0.5f) / N), 1);
            CGContextSetFillColorWithColor(c, col);
            CGColorRelease(col);
            CGContextMoveToPoint(c, s->x0, s->y0);
            CGContextAddLineToPoint(c, s->x0 + R * cosf(a0), s->y0 + R * sinf(a0));
            CGContextAddLineToPoint(c, s->x0 + R * cosf(a1), s->y0 + R * sinf(a1));
            CGContextClosePath(c);
            CGContextFillPath(c);
        }
    } else if (s->kind == SH_BITMAP && s->image) {
        CGRect box = CGContextGetClipBoundingBox(c);
        if (s->tile == 0) {
            CGContextTranslateCTM(c, 0, s->ih); CGContextScaleCTM(c, 1, -1);
            CGContextDrawImage(c, CGRectMake(0, 0, s->iw, s->ih), s->image);
        } else {
            int x0 = (int)floor(box.origin.x / s->iw) - 1, y0 = (int)floor(box.origin.y / s->ih) - 1;
            int x1 = (int)ceil((box.origin.x + box.size.width) / s->iw) + 1, y1 = (int)ceil((box.origin.y + box.size.height) / s->ih) + 1;
            if ((int64_t)(x1 - x0) * (y1 - y0) > 20000) { CGContextRestoreGState(c); return; }
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
                CGContextSaveGState(c);
                CGContextTranslateCTM(c, x * s->iw, (y + 1) * s->ih); CGContextScaleCTM(c, 1, -1);
                CGContextDrawImage(c, CGRectMake(0, 0, s->iw, s->ih), s->image);
                CGContextRestoreGState(c);
            }
        }
    }
    CGContextRestoreGState(c);
}

/* ================================================================== canvases */

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
/* how much of the pixel centred at (px, py) the rounded rectangle covers */
static inline float rrect_cover(float px, float py, float x0, float y0, float x1, float y1, float rx, float ry)
{
    float cx = clamp01(fminf(px - x0, x1 - px) + 0.5f), cy = clamp01(fminf(py - y0, y1 - py) + 0.5f);
    float ex = px < x0 + rx ? x0 + rx : px > x1 - rx ? x1 - rx : px;
    float ey = py < y0 + ry ? y0 + ry : py > y1 - ry ? y1 - ry : py;
    if (ex == px || ey == py || rx <= 0 || ry <= 0) return cx * cy;
    float nx = (px - ex) / rx, ny = (py - ey) / ry;
    float f = nx * nx + ny * ny - 1, gx = nx / rx, gy = ny / ry, g = 2 * sqrtf(gx * gx + gy * gy);
    return g > 0 ? clamp01(0.5f - f / g) : 1;
}
/* the same with each corner its own radii: top left, top right, bottom right, bottom left */
static inline float rrect4_cover(float px, float py, float x0, float y0, float x1, float y1, const float *rx, const float *ry)
{
    int q = (px < (x0 + x1) / 2 ? 0 : 1);
    q = py < (y0 + y1) / 2 ? q : (q == 1 ? 2 : 3);
    float cx = q == 0 || q == 3 ? x0 + rx[q] : x1 - rx[q], cy = q < 2 ? y0 + ry[q] : y1 - ry[q];
    bool in_corner = (q == 0 || q == 3 ? px < cx : px > cx) && (q < 2 ? py < cy : py > cy);
    float ex = clamp01(fminf(px - x0, x1 - px) + 0.5f), ey = clamp01(fminf(py - y0, y1 - py) + 0.5f);
    if (!in_corner || rx[q] <= 0 || ry[q] <= 0) return ex * ey;
    float nx = (px - cx) / rx[q], ny = (py - cy) / ry[q];
    float f = nx * nx + ny * ny - 1, gx = nx / rx[q], gy = ny / ry[q], g = 2 * sqrtf(gx * gx + gy * gy);
    return g > 0 ? clamp01(0.5f - f / g) : 1;
}

/* a clip other than rectangles (a path, a difference, a turned rectangle) as Husk's blending sees it, over the clip's bounds
 * (x0, y0 from the bitmap's top left), shared by the save levels it was set at and above */
typedef struct cmask {
    int x0, y0, w, h, refs;
    /* the path under the transform t, times the mask before it (or left out of it, a difference): clip_region */
    CGPathRef path; CGAffineTransform t; bool even_odd, difference; struct cmask *prev;
    /* a rounded rectangle (Compose's RoundedCornerShape, CircleShape) on whole axes: worked out, not filled by CoreGraphics */
    bool rr; float rl, rt, rr_, rb, rx[4], ry[4];
} cmask;
static void cmask_release(cmask *k)
{
    if (!k || --k->refs > 0) return;
    if (k->path) CGPathRelease(k->path);
    cmask_release(k->prev);
    free(k);
}

typedef struct gcanvas {
    gbitmap *bm;
    CGContextRef c;
    CGAffineTransform base;         /* the flip: Android's identity */
    int depth;                      /* Android's save count - 1 */
    uint64_t layer_bits[4];         /* which save levels are layers (end a transparency layer on restore) */
    /* for blend_fill: whether the clip may be other than rectangles (a path, a difference, a rotated rect), as it was at each
     * save level, and how many transparency layers are open (drawing then goes to CoreGraphics' layer, not the bitmap) */
    bool complex;
    uint64_t complex_bits[4];
    int layers;
    cmask *mask;                    /* the clip's coverage, where it is other than rectangles */
    cmask *mask_saved[256];         /* as it was at each save level */
    /* The clip is Husk's: its bounds (CoreGraphics' device space, y up) and the clips that made it, given to CoreGraphics
     * only when it draws (cv_flush). Each clip it takes makes it intersect masks a pixel at a time, and Compose clips
     * every list item and shape: tens a frame, most never drawn through by CoreGraphics. */
    CGRect cb;
    CGRect cb_saved[256];
    struct clipop *ops; int nops, cops;
} gcanvas;
/* HUSK_FRAME_STATS: the clip's costs (reported with the blit's) */
static int64_t g_cs_masks, g_cs_mask_px, g_cs_mask_ns, g_cs_flushes, g_cs_flushed_ops, g_cs_flush_ns, g_cs_shaped_rects;
typedef struct clipop {
    CGAffineTransform t;            /* the transform when it was set */
    CGPathRef path;                 /* or NULL: the rectangle */
    CGRect r;
    bool even_odd, difference;
    int level;                      /* the save level it was set at */
    int applied;                    /* the save level CoreGraphics took it at, or -1 */
} clipop;
/* CoreGraphics takes the clips it has not: before it draws */
static int64_t now_ns(void);
static void cv_flush(gcanvas *v)
{
    CGContextRef c = v->c;
    int64_t t0 = now_ns(); int before = 0;
    for (int i = 0; i < v->nops; i++) before += v->ops[i].applied < 0;
    if (before) { g_cs_flushes++; g_cs_flushed_ops += before; }
    for (int i = 0; i < v->nops; i++) {
        clipop *o = &v->ops[i];
        if (o->applied >= 0) continue;
        CGAffineTransform cur = CGContextGetCTM(c);
        CGContextConcatCTM(c, CGAffineTransformInvert(cur));
        CGContextConcatCTM(c, o->t);
        if (o->difference) {
            CGRect all = CGRectApplyAffineTransform(CGRectMake(-2, -2, v->bm->w + 4, v->bm->h + 4), CGAffineTransformInvert(o->t));
            CGContextAddRect(c, all);
            if (o->path) CGContextAddPath(c, o->path); else CGContextAddRect(c, o->r);
            CGContextEOClip(c);
        } else if (o->path) {
            CGContextAddPath(c, o->path);
            if (o->even_odd) CGContextEOClip(c); else CGContextClip(c);
        } else CGContextClipToRect(c, o->r);
        CGContextConcatCTM(c, CGAffineTransformInvert(o->t));
        CGContextConcatCTM(c, cur);
        o->applied = v->depth;
    }
    if (before) g_cs_flush_ns += now_ns() - t0;
}
static CGContextRef cg(gcanvas *v) { cv_flush(v); return v->c; }
/* the clip's bounds in the canvas's coordinates now */
static CGRect clip_user(gcanvas *v)
{
    if (CGRectIsNull(v->cb) || v->cb.size.width <= 0 || v->cb.size.height <= 0) return CGRectNull;
    return CGRectApplyAffineTransform(v->cb, CGAffineTransformInvert(CGContextGetCTM(v->c)));
}
static void clip_record(gcanvas *v, CGPathRef path, CGRect r, bool even_odd, bool difference)
{
    if (v->nops == v->cops) { v->cops = v->cops ? v->cops * 2 : 16; v->ops = realloc(v->ops, (size_t)v->cops * sizeof *v->ops); }
    clipop *o = &v->ops[v->nops++];
    o->t = CGContextGetCTM(v->c); o->path = path ? CGPathRetain(path) : NULL; o->r = r;
    o->even_odd = even_odd; o->difference = difference; o->level = v->depth; o->applied = -1;
}

static gcanvas *cv_new(gbitmap *b)
{
    gcanvas *v = calloc(1, sizeof(*v));
    v->bm = b; v->c = b->ctx;
    CGContextSaveGState(v->c);              /* the canvas's own level, undone when it is freed */
    CGContextTranslateCTM(v->c, 0, b->h);
    CGContextScaleCTM(v->c, 1, -1);
    v->base = CGContextGetCTM(v->c);
    CGContextSetTextMatrix(v->c, CGAffineTransformMakeScale(1, -1));
    v->cb = CGRectMake(0, 0, b->w, b->h);
    return v;
}
static void cv_free(gcanvas *v)
{
    if (!v) return;
    while (v->depth > 0) {
        if (v->layer_bits[(v->depth - 1) >> 6] & (1ull << ((v->depth - 1) & 63))) CGContextEndTransparencyLayer(v->c);
        CGContextRestoreGState(v->c);
        v->depth--;
        if (v->depth < 256) { cmask_release(v->mask); v->mask = v->mask_saved[v->depth]; }
    }
    cmask_release(v->mask);
    for (int i = 0; i < v->nops; i++) if (v->ops[i].path) CGPathRelease(v->ops[i].path);
    free(v->ops);
    CGContextRestoreGState(v->c);
    free(v);
}
static CGBlendMode blend_of(int mode);
static int cv_save_mode(gcanvas *v, bool layer, float alpha, CGRect bounds, int mode);
static int cv_save(gcanvas *v, bool layer, float alpha, CGRect bounds) { return cv_save_mode(v, layer, alpha, bounds, 3); }
/* a layer is composited on restore with the alpha and blend mode set when it began */
static void cv_flush(gcanvas *v);
static int cv_save_mode(gcanvas *v, bool layer, float alpha, CGRect bounds, int mode)
{
    if (layer) cv_flush(v);
    CGContextSaveGState(v->c);
    if (v->depth < 256) v->cb_saved[v->depth] = v->cb;
    if (v->depth < 256) {
        uint64_t bit = 1ull << (v->depth & 63);
        if (v->complex) v->complex_bits[v->depth >> 6] |= bit; else v->complex_bits[v->depth >> 6] &= ~bit;
        v->mask_saved[v->depth] = v->mask;
        if (v->mask) v->mask->refs++;
        if (layer) {
            v->layers++;
            v->layer_bits[v->depth >> 6] |= bit;
            CGContextSetAlpha(v->c, alpha);
            CGContextSetBlendMode(v->c, blend_of(mode));
            if (bounds.size.width > 0 && bounds.size.height > 0) CGContextBeginTransparencyLayerWithRect(v->c, bounds, NULL);
            else CGContextBeginTransparencyLayer(v->c, NULL);
            CGContextSetAlpha(v->c, 1);
            CGContextSetBlendMode(v->c, kCGBlendModeNormal);
        } else v->layer_bits[v->depth >> 6] &= ~bit;
    }
    return ++v->depth;
}
static void cv_restore(gcanvas *v)
{
    if (v->depth <= 0) return;
    v->depth--;
    if (v->depth < 256) {
        uint64_t bit = 1ull << (v->depth & 63);
        if (v->layer_bits[v->depth >> 6] & bit) { CGContextEndTransparencyLayer(v->c); v->layers--; }
        v->complex = (v->complex_bits[v->depth >> 6] & bit) != 0;
        cmask_release(v->mask);
        v->mask = v->mask_saved[v->depth];
        v->cb = v->cb_saved[v->depth];
    }
    int k = 0;
    for (int i = 0; i < v->nops; i++) {
        clipop o = v->ops[i];
        if (o.level > v->depth) { if (o.path) CGPathRelease(o.path); continue; }
        if (o.applied > v->depth) o.applied = -1;
        v->ops[k++] = o;
    }
    v->nops = k;
    CGContextRestoreGState(v->c);
}

/* After the CoreGraphics clip took a path (user space), the mask takes it too: its coverage times the mask's so far (or the
 * coverage left out of it, for a difference). Where that cannot be, blending leaves the canvas to CoreGraphics (complex). */
static void clip_mask_add_(gcanvas *v, CGPathRef path, bool even_odd, bool difference);
static void clip_mask_add(gcanvas *v, CGPathRef path, bool even_odd, bool difference)
{
    int64_t t0 = now_ns();
    clip_mask_add_(v, path, even_odd, difference);
    g_cs_masks++; g_cs_mask_ns += now_ns() - t0;
    if (v->mask) g_cs_mask_px += (int64_t)v->mask->w * v->mask->h;
}
/* the clip just added is the rounded rectangle l t r b with radii rad[8] (corners' x, y: top left, top right, bottom right,
 * bottom left), user space: worked out where the transform keeps axes */
static void clip_mask_rrect(gcanvas *v, float l, float t, float r, float b, const float *rad)
{
    cmask *k = v->mask;
    if (!k || k->difference) return;
    CGAffineTransform m = k->t;
    if (fabs(m.b) > 1e-6 || fabs(m.c) > 1e-6) return;
    int h = v->bm->h;
    float X0 = (float)(m.a * l + m.tx), X1 = (float)(m.a * r + m.tx);
    float Y0 = (float)(h - (m.d * t + m.ty)), Y1 = (float)(h - (m.d * b + m.ty));
    bool fx = X1 < X0, fy = Y1 < Y0;
    k->rl = fminf(X0, X1); k->rr_ = fmaxf(X0, X1); k->rt = fminf(Y0, Y1); k->rb = fmaxf(Y0, Y1);
    float sx = (float)fabs(m.a), sy = (float)fabs(m.d), w = k->rr_ - k->rl, hh = k->rb - k->rt;
    for (int q = 0; q < 4; q++) {
        /* the user's corner q lands at corner Q in memory */
        int col = (q == 0 || q == 3) ? 0 : 1, row = q < 2 ? 0 : 1;
        if (fx) col ^= 1;
        if (fy) row ^= 1;
        int Q = row == 0 ? (col == 0 ? 0 : 1) : (col == 0 ? 3 : 2);
        k->rx[Q] = fminf(fmaxf(rad[2 * q], 0) * sx, w / 2); k->ry[Q] = fminf(fmaxf(rad[2 * q + 1], 0) * sy, hh / 2);
    }
    k->rr = true;
}
static void clip_mask_add_(gcanvas *v, CGPathRef path, bool even_odd, bool difference)
{
    if (v->depth >= 256) { v->complex = true; return; }
    gbitmap *b = v->bm;
    cmask *k = calloc(1, sizeof *k);
    if (!k) { v->complex = true; return; }
    k->refs = 1;
    if (!CGRectIsNull(v->cb)) {
        CGRect d = CGRectIntersection(v->cb, CGRectMake(0, 0, b->w, b->h));
        if (!CGRectIsNull(d) && d.size.width > 0 && d.size.height > 0) {
            int dx0 = (int)floor(d.origin.x), dy0 = (int)floor(d.origin.y);
            int dx1 = (int)ceil(d.origin.x + d.size.width), dy1 = (int)ceil(d.origin.y + d.size.height);
            k->x0 = dx0; k->y0 = b->h - dy1; k->w = dx1 - dx0; k->h = dy1 - dy0;
        }
    }
    k->path = CGPathRetain(path); k->t = CGContextGetCTM(v->c); k->even_odd = even_odd; k->difference = difference;
    k->prev = v->mask;                      /* the canvas's reference, now the new mask's */
    v->mask = k;
}
/* The clip's coverage over a box of the bitmap (x0, y0 from its top left, w x h), into g_clipcov: each mask's path filled
 * there by CoreGraphics, times the masks before it. Only where a draw blends, so a clip costs what is drawn through it,
 * not its whole area. */
static uint8_t *g_clipcov, *g_cliptmp; static size_t g_clipcov_cap;
static int g_cc_x0, g_cc_y0, g_cc_w;
static uint8_t *g_cc_full; static size_t g_cc_full_cap;   /* per row of the region: wholly covered (no mask needed) */
/* a corner's ellipse (centre cx, cy; radii rx, ry) on row j: how far in from the corner's side the row is wholly covered (at the
 * row's edge farthest from the centre) and covered at all (nearest); top: the corner is above its centre */
static inline void rr_corner(float cy, float rx, float ry, bool top, int j, float t, float b, float *full, float *any)
{
    float yfar = top ? fmaxf((float)j, t) : fminf((float)(j + 1), b), ynear = top ? fminf((float)(j + 1), cy) : fmaxf((float)j, cy);
    float df = (yfar - cy) / ry, dn = (ynear - cy) / ry;
    *full = rx - (df * df < 1 ? rx * sqrtf(1 - df * df) : 0);
    *any = rx - (dn * dn < 1 ? rx * sqrtf(1 - dn * dn) : 0);
}
/* a rounded rectangle's coverage of one row, x0 .. x0 + w, pixel row j: none outside it, the row's own where it covers all of a
 * pixel, each pixel's own only along the edges (a circle's rows were a square root a pixel, all the way across) */
static void rr_row(const cmask *k, uint8_t *o, int x0, int w, int j)
{
    float py = j + 0.5f;
    float cy = clamp01(fminf(py - k->rt, k->rb - py) + 0.5f);
    if (cy <= 0) { memset(o, 0, (size_t)w); return; }
    /* each side's inset where the row is wholly covered and where it is covered at all */
    float lfull = 0, lany = 0, rfull = 0, rany = 0;
    if (k->rx[0] > 0 && k->ry[0] > 0 && py < k->rt + k->ry[0]) rr_corner(k->rt + k->ry[0], k->rx[0], k->ry[0], true, j, k->rt, k->rb, &lfull, &lany);
    else if (k->rx[3] > 0 && k->ry[3] > 0 && py > k->rb - k->ry[3]) rr_corner(k->rb - k->ry[3], k->rx[3], k->ry[3], false, j, k->rt, k->rb, &lfull, &lany);
    if (k->rx[1] > 0 && k->ry[1] > 0 && py < k->rt + k->ry[1]) rr_corner(k->rt + k->ry[1], k->rx[1], k->ry[1], true, j, k->rt, k->rb, &rfull, &rany);
    else if (k->rx[2] > 0 && k->ry[2] > 0 && py > k->rb - k->ry[2]) rr_corner(k->rb - k->ry[2], k->rx[2], k->ry[2], false, j, k->rt, k->rb, &rfull, &rany);
    int f0 = (int)ceilf(k->rl + lfull) - x0, f1 = (int)floorf(k->rr_ - rfull) - x0;
    int a0 = (int)floorf(k->rl + lany) - 1 - x0, a1 = (int)ceilf(k->rr_ - rany) + 1 - x0;
    if (a0 < 0) a0 = 0;
    if (a1 > w) a1 = w;
    if (a0 >= a1) { memset(o, 0, (size_t)w); return; }
    if (f0 < a0) f0 = a0;
    if (f1 > a1) f1 = a1;
    if (f1 <= f0) f0 = f1 = a1;
    memset(o, 0, (size_t)a0);
    memset(o + a1, 0, (size_t)(w - a1));
    if (f1 > f0) memset(o + f0, (int)lrintf(cy * 255), (size_t)(f1 - f0));
    for (int xx = a0; xx < f0; xx++) o[xx] = (uint8_t)lrintf(rrect4_cover(x0 + xx + 0.5f, py, k->rl, k->rt, k->rr_, k->rb, k->rx, k->ry) * 255);
    for (int xx = f1; xx < a1; xx++) o[xx] = (uint8_t)lrintf(rrect4_cover(x0 + xx + 0.5f, py, k->rl, k->rt, k->rr_, k->rb, k->rx, k->ry) * 255);
}
/* whether the rounded rectangle covers all of row j from x0 to x0 + w */
static inline bool rr_row_full(const cmask *k, int x0, int w, int j)
{
    float py = j + 0.5f;
    return j >= k->rt && j + 1 <= k->rb && py >= k->rt + fmaxf(k->ry[0], k->ry[1]) && py <= k->rb - fmaxf(k->ry[2], k->ry[3])
        && x0 >= k->rl && x0 + w <= k->rr_;
}
static inline bool all_255(const uint8_t *p, int n)
{
    int i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t q; memcpy(&q, p + i, 8); if (q != ~0ull) return false; }
    for (; i < n; i++) if (p[i] != 255) return false;
    return true;
}
static bool clip_region(const gcanvas *v, int x0, int y0, int w, int h)
{
    size_t n = (size_t)w * h;
    if (n > g_clipcov_cap) {
        free(g_clipcov); free(g_cliptmp);
        g_clipcov_cap = n + n / 4; g_clipcov = malloc(g_clipcov_cap); g_cliptmp = malloc(g_clipcov_cap);
        if (!g_clipcov || !g_cliptmp) { free(g_clipcov); free(g_cliptmp); g_clipcov = g_cliptmp = NULL; g_clipcov_cap = 0; return false; }
    }
    g_cc_x0 = x0; g_cc_y0 = y0; g_cc_w = w;
    if ((size_t)h > g_cc_full_cap) { free(g_cc_full); g_cc_full_cap = (size_t)h + 64; g_cc_full = malloc(g_cc_full_cap); if (!g_cc_full) { g_cc_full_cap = 0; return false; } }
    int bh = v->bm->h;
    bool all_rr = true;
    for (const cmask *k = v->mask; k; k = k->prev) if (!k->rr) { all_rr = false; break; }
    if (all_rr) {
        /* rounded rectangles only: a row they all cover wholly is marked, the rest worked out */
        for (int yy = 0; yy < h; yy++) {
            int j = y0 + yy;
            bool full = true;
            for (const cmask *k = v->mask; k && full; k = k->prev) full = rr_row_full(k, x0, w, j);
            g_cc_full[yy] = full;
            if (full) continue;
            uint8_t *o = g_clipcov + (size_t)yy * w;
            bool first = true;
            for (const cmask *k = v->mask; k; k = k->prev, first = false) {
                if (first) { rr_row(k, o, x0, w, j); continue; }
                rr_row(k, g_cliptmp, x0, w, j);
                for (int xx = 0; xx < w; xx++) o[xx] = (uint8_t)((o[xx] * (uint32_t)g_cliptmp[xx] + 127) / 255);
            }
        }
        return true;
    }
    bool first = true;
    for (const cmask *k = v->mask; k; k = k->prev, first = false) {
        uint8_t *out = first ? g_clipcov : g_cliptmp;
        if (k->rr) {
            for (int yy = 0; yy < h; yy++) rr_row(k, out + (size_t)yy * w, x0, w, y0 + yy);
            if (!first) for (size_t i = 0; i < n; i++) g_clipcov[i] = (uint8_t)((g_clipcov[i] * (uint32_t)g_cliptmp[i] + 127) / 255);
            continue;
        }
        memset(out, 0, n);
        /* the box's part inside this mask's bounds: outside them, nothing (a difference: what was) */
        int ix0 = x0 > k->x0 ? x0 : k->x0, iy0 = y0 > k->y0 ? y0 : k->y0;
        int ix1 = x0 + w < k->x0 + k->w ? x0 + w : k->x0 + k->w, iy1 = y0 + h < k->y0 + k->h ? y0 + h : k->y0 + k->h;
        if (ix0 < ix1 && iy0 < iy1) {
            CGContextRef mc = CGBitmapContextCreate(out, (size_t)w, (size_t)h, 8, (size_t)w, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
            if (!mc) return false;
            CGContextClipToRect(mc, CGRectMake(ix0 - x0, (y0 + h) - iy1, ix1 - ix0, iy1 - iy0));
            CGContextConcatCTM(mc, CGAffineTransformConcat(k->t, CGAffineTransformMakeTranslation(-x0, -(bh - y0 - h))));
            CGContextSetGrayFillColor(mc, 0, 1);
            CGContextAddPath(mc, k->path);
            if (k->even_odd) CGContextEOFillPath(mc); else CGContextFillPath(mc);
            CGContextRelease(mc);
        }
        if (first && !k->difference) continue;      /* the path's coverage is the clip's */
        for (int yy = 0; yy < h; yy++) {
            uint8_t *cv = g_clipcov + (size_t)yy * w; const uint8_t *tp = out + (size_t)yy * w;
            bool rin = y0 + yy >= iy0 && y0 + yy < iy1;
            for (int xx = 0; xx < w; xx++) {
                uint32_t c = tp[xx];
                if (k->difference) c = (rin && x0 + xx >= ix0 && x0 + xx < ix1) ? 255u - c : 255u;
                cv[xx] = first ? (uint8_t)c : (uint8_t)((cv[xx] * c + 127) / 255);
            }
        }
    }
    for (int yy = 0; yy < h; yy++) g_cc_full[yy] = all_255(g_clipcov + (size_t)yy * w, w);
    return true;
}
/* the rows and columns the clip's mask leaves to draw (all, without one); false when none */
/* whether every pixel of the box is wholly inside the clip's rounded rectangles: then it needs no mask */
static bool clip_whole(const gcanvas *v, int x0, int x1, int y0, int y1)
{
    for (const cmask *k = v->mask; k; k = k->prev) {
        if (!k->rr) return false;
        if (x0 < k->rl || x1 > k->rr_ || y0 < k->rt || y1 > k->rb) return false;
        if (x0 < k->rl + k->rx[0] && y0 < k->rt + k->ry[0]) return false;
        if (x1 > k->rr_ - k->rx[1] && y0 < k->rt + k->ry[1]) return false;
        if (x1 > k->rr_ - k->rx[2] && y1 > k->rb - k->ry[2]) return false;
        if (x0 < k->rl + k->rx[3] && y1 > k->rb - k->ry[3]) return false;
    }
    return true;
}
/* whether a draw over the box must go through the clip's mask (a box in no rounded rectangle's corners needs none) */
static bool clip_need(const gcanvas *v, int x0, int x1, int y0, int y1)
{
    const cmask *k = v->mask;
    if (!k) return false;
    if (x0 < k->x0) x0 = k->x0;
    if (y0 < k->y0) y0 = k->y0;
    if (x1 > k->x0 + k->w) x1 = k->x0 + k->w;
    if (y1 > k->y0 + k->h) y1 = k->y0 + k->h;
    if (x0 >= x1 || y0 >= y1) return true;
    return !clip_whole(v, x0, x1, y0, y1);
}
static bool g_cc_on;                /* clip_at answers the mask: clip_box found the box needs one */
static inline bool clip_box(const gcanvas *v, int *x0, int *x1, int *y0, int *y1)
{
    const cmask *k = v->mask;
    g_cc_on = false;
    if (k) {
        if (*x0 < k->x0) *x0 = k->x0;
        if (*y0 < k->y0) *y0 = k->y0;
        if (*x1 > k->x0 + k->w) *x1 = k->x0 + k->w;
        if (*y1 > k->y0 + k->h) *y1 = k->y0 + k->h;
        if (*x0 >= *x1 || *y0 >= *y1) return false;
        if (clip_whole(v, *x0, *x1, *y0, *y1)) return true;
        g_cc_on = true;
        return clip_region(v, *x0, *y0, *x1 - *x0, *y1 - *y0);
    }
    return *x0 < *x1 && *y0 < *y1;
}
/* the mask at (x, y), inside clip_box's; NULL without a mask */
static inline const uint8_t *clip_at(const gcanvas *v, int x, int y)
{
    (void)v;
    if (!g_cc_on || g_cc_full[y - g_cc_y0]) return NULL;    /* no mask, or this row wholly covered */
    return g_clipcov + (size_t)(y - g_cc_y0) * g_cc_w + (x - g_cc_x0);
}

/* ---- par_rows: a big draw's rows shared out among a few threads (the iPhone has six cores; a frame drew on one). The rows go
 * in chunks taken in turn, so the efficiency cores' slower chunks do not hold up the rest; the caller takes its share and waits
 * for the others. Small draws (under ~200 thousand pixels) stay on the calling thread: the hand-off would cost more. */
#include <sys/sysctl.h>
typedef void (^rows_blk)(int j0, int j1);
static struct {
    pthread_mutex_t mu, busy;
    pthread_cond_t go, done;
    int workers, active, y0, y1, chunk, nchunks;
    _Atomic int next;
    rows_blk blk;
    uint64_t gen;
    bool tried;
} P = { .mu = PTHREAD_MUTEX_INITIALIZER, .busy = PTHREAD_MUTEX_INITIALIZER, .go = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER };
static void par_take(rows_blk blk, int y0, int y1, int chunk, int nchunks)
{
    for (;;) {
        int k = atomic_fetch_add(&P.next, 1);
        if (k >= nchunks) return;
        int a = y0 + k * chunk, z = a + chunk < y1 ? a + chunk : y1;
        if (a < z) blk(a, z);
    }
}
static void *par_worker(void *arg)
{
    (void)arg;
    pthread_setname_np("husk-raster");
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint64_t seen = 0;
    pthread_mutex_lock(&P.mu);
    for (;;) {
        while (P.gen == seen) pthread_cond_wait(&P.go, &P.mu);
        seen = P.gen;
        rows_blk blk = P.blk; int y0 = P.y0, y1 = P.y1, chunk = P.chunk, nch = P.nchunks;
        pthread_mutex_unlock(&P.mu);
        par_take(blk, y0, y1, chunk, nch);
        pthread_mutex_lock(&P.mu);
        if (--P.active == 0) pthread_cond_signal(&P.done);
    }
    return NULL;
}
static bool par_init(void)
{
    if (P.tried) return P.workers > 0;
    P.tried = true;
    int n = 0; size_t sz = sizeof n;
    if (sysctlbyname("hw.activecpu", &n, &sz, NULL, 0) != 0) n = 1;
    const char *e = getenv("HUSK_RASTER_THREADS");
    if (e) n = atoi(e) + 1;
    n = n - 1; if (n > 3) n = 3; if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at);
        if (pthread_create(&t, &at, par_worker, NULL) == 0) P.workers++;
        pthread_attr_destroy(&at);
    }
    if (P.workers) tl_log_line("gfx: %d raster threads besides the drawing one", P.workers);
    return P.workers > 0;
}
/* blk(j0, j1) for rows y0 .. y1 (px: about how many pixels in all), the rows split among the threads; back when all are done */
static void par_rows(int64_t px, int y0, int y1, rows_blk blk)
{
    if (y1 <= y0) return;
    if (px < 200000 || y1 - y0 < 32 || !par_init() || pthread_mutex_trylock(&P.busy) != 0) { blk(y0, y1); return; }
    int nch = (P.workers + 1) * 4, rows = y1 - y0;
    int chunk = (rows + nch - 1) / nch;
    if (chunk < 8) chunk = 8;
    nch = (rows + chunk - 1) / chunk;
    pthread_mutex_lock(&P.mu);
    P.blk = blk; P.y0 = y0; P.y1 = y1; P.chunk = chunk; P.nchunks = nch;
    atomic_store(&P.next, 0);
    P.active = P.workers; P.gen++;
    pthread_cond_broadcast(&P.go);
    pthread_mutex_unlock(&P.mu);
    par_take(blk, y0, y1, chunk, nch);
    pthread_mutex_lock(&P.mu);
    while (P.active) pthread_cond_wait(&P.done, &P.mu);
    pthread_mutex_unlock(&P.mu);
    pthread_mutex_unlock(&P.busy);
}

static inline uint32_t over(uint32_t d, uint32_t s)
{
    uint32_t inv = 255 - (s >> 24);       /* no branches: inv 0 gives s, 255 gives d, and the loop vectorises */
    uint32_t rb = (d & 0x00FF00FFu) * inv + 0x00800080u, ag = ((d >> 8) & 0x00FF00FFu) * inv + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
    return (rb | ag) + s;
}
static inline uint32_t scale_px(uint32_t s, uint32_t a)    /* s times a / 255, every channel (a premultiplied colour's alpha) */
{
    uint32_t rb = (s & 0x00FF00FFu) * a + 0x00800080u, ag = ((s >> 8) & 0x00FF00FFu) * a + 0x00800080u;
    rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
    return rb | ag;
}

/* ---- blend_fill: a colour over a rectangle of the bitmap itself. CoreGraphics blends a translucent fill a pixel at a time
 * (a screen's worth takes it some 10 ms, against 0.2 ms opaque); Compose draws its translucent backgrounds, scrims and
 * state layers so, and a frame took tens of ms. Only where the result is the same as CoreGraphics': no transparency layer
 * open, the clip rectangles, the transform a scale and a move. */
static bool blend_ok(gcanvas *v, CGAffineTransform *m)
{
    if (v->layers || v->complex || v->depth >= 256) return false;
    *m = CGContextGetCTM(v->c);
    return fabs(m->b) < 1e-6 && fabs(m->c) < 1e-6;
}
/* source over: d = s + d * (255 - sa) / 255, s premultiplied in the bitmap's byte order (R G B A) */
static inline void blend_run(uint32_t *p, int n, uint32_t s, uint32_t inv)
{
    if (inv == 0) { for (int k = 0; k < n; k++) p[k] = s; return; }     /* opaque: the colour itself */
    for (int k = 0; k < n; k++) {
        uint32_t d = p[k];
        uint32_t rb = (d & 0x00FF00FFu) * inv + 0x00800080u, ag = ((d >> 8) & 0x00FF00FFu) * inv + 0x00800080u;
        rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
        ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
        p[k] = (rb | ag) + s;
    }
}
static inline uint32_t premul(uint32_t argb, float cover, uint32_t *inv)
{
    uint32_t a = (uint32_t)lrintf((float)(argb >> 24) * cover);
    uint32_t r = ((argb >> 16) & 255) * a, g = ((argb >> 8) & 255) * a, b = (argb & 255) * a;
    r = (r + 127) / 255; g = (g + 127) / 255; b = (b + 127) / 255;
    *inv = 255 - a;
    return r | g << 8 | b << 16 | a << 24;
}
/* the user-space rectangle r, cut to the clip, filled with argb (unpremultiplied); false where blend_ok says no */
static bool blend_fill(gcanvas *v, CGRect r, uint32_t argb, bool aa)
{
    CGAffineTransform m;
    if (!blend_ok(v, &m)) return false;
    gbitmap *b = v->bm;
    CGRect d = CGRectStandardize(CGRectApplyAffineTransform(CGRectStandardize(r), m));
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip)) return true;
    d = CGRectIntersection(d, CGRectStandardize(CGRectApplyAffineTransform(clip, m)));
    d = CGRectIntersection(d, CGRectMake(0, 0, b->w, b->h));
    if (CGRectIsNull(d) || d.size.width <= 0 || d.size.height <= 0 || !(argb >> 24)) return true;
    /* rows from the top, as the bitmap's memory has them (CoreGraphics' device space has y up) */
    double x0 = d.origin.x, x1 = x0 + d.size.width, y0 = b->h - (d.origin.y + d.size.height), y1 = b->h - d.origin.y;
    if (!aa) { x0 = ceil(x0 - 0.5); x1 = ceil(x1 - 0.5); y0 = ceil(y0 - 0.5); y1 = ceil(y1 - 0.5); }
    int ix0 = (int)floor(x0), ix1 = (int)ceil(x1), iy0 = (int)floor(y0), iy1 = (int)ceil(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > b->w) ix1 = b->w;
    if (iy1 > b->h) iy1 = b->h;
    if (ix0 >= ix1 || iy0 >= iy1) return true;
    /* the edges' coverage: a partly covered first or last column or row blends with its share of the alpha */
    float cl = (float)(fmin(ix0 + 1, x1) - fmax(ix0, x0)), cr = (float)(fmin(ix1, x1) - fmax(ix1 - 1, x0));
    int fx0 = ix0, fx1 = ix1;               /* the fully covered columns */
    bool le = false, re = false;            /* a partly covered first, last column */
    if (ix1 - ix0 == 1) { if (cl < 0.999f) { le = true; fx0 = fx1 = ix0; } }
    else {
        if (cl < 0.999f) { le = true; fx0++; }
        if (cr < 0.999f) { re = true; fx1--; }
    }
    uint32_t inv_full, s_full = premul(argb, 1, &inv_full);
    if (clip_need(v, ix0, ix1, iy0, iy1)) {
        /* through the clip's mask: each pixel's coverage times the mask's */
        int mx0 = ix0, mx1 = ix1, my0 = iy0, my1 = iy1;
        if (!clip_box(v, &mx0, &mx1, &my0, &my1)) return true;
        uint32_t cl8 = (uint32_t)lrintf(cl * 255), cr8 = (uint32_t)lrintf(cr * 255);
        par_rows((int64_t)(mx1 - mx0) * (my1 - my0), my0, my1, ^(int j0_, int j1_) {
        for (int j = j0_; j < j1_; j++) {
            float cy = (float)(fmin(j + 1, y1) - fmax(j, y0));
            if (cy <= 0) continue;
            uint32_t *row = (uint32_t *)(b->px + (size_t)j * b->stride);
            const uint8_t *mk = clip_at(v, mx0, j);
            uint32_t inv, sv;
            if (cy >= 0.999f) { sv = s_full; inv = inv_full; } else sv = premul(argb, cy, &inv);
            int a0 = fx0 > mx0 ? fx0 : mx0, a1 = fx1 < mx1 ? fx1 : mx1;
            if (!mk) {
                /* wholly covered: as unclipped */
                if (a1 > a0) blend_run(row + a0, a1 - a0, sv, inv);
                if (le && ix0 >= mx0) { uint32_t i2, s2 = premul(argb, cy * cl, &i2); blend_run(row + ix0, 1, s2, i2); }
                if (re && ix1 - 1 < mx1) { uint32_t i2, s2 = premul(argb, cy * cr, &i2); blend_run(row + ix1 - 1, 1, s2, i2); }
                continue;
            }
            const uint8_t *m0 = mk - mx0;
            for (int i = a0; i < a1; i++) row[i] = over(row[i], scale_px(sv, m0[i]));
            uint32_t sf = s_full, cy8 = (uint32_t)lrintf(cy * 255);
            if (le && ix0 >= mx0) row[ix0] = over(row[ix0], scale_px(sf, (((cy8 * cl8 + 127) / 255) * m0[ix0] + 127) / 255));
            if (re && ix1 - 1 < mx1) row[ix1 - 1] = over(row[ix1 - 1], scale_px(sf, (((cy8 * cr8 + 127) / 255) * m0[ix1 - 1] + 127) / 255));
        }
        });
        return true;
    }
    par_rows((int64_t)(ix1 - ix0) * (iy1 - iy0), iy0, iy1, ^(int j0_, int j1_) {
    for (int j = j0_; j < j1_; j++) {
        float cy = (float)(fmin(j + 1, y1) - fmax(j, y0));
        if (cy <= 0) continue;
        uint32_t *row = (uint32_t *)(b->px + (size_t)j * b->stride);
        uint32_t inv, sv;
        if (cy >= 0.999f) { sv = s_full; inv = inv_full; } else sv = premul(argb, cy, &inv);
        if (fx1 > fx0) blend_run(row + fx0, fx1 - fx0, sv, inv);
        if (le) { uint32_t i2, s2 = premul(argb, cy * cl, &i2); blend_run(row + ix0, 1, s2, i2); }
        if (re) { uint32_t i2, s2 = premul(argb, cy * cr, &i2); blend_run(row + ix1 - 1, 1, s2, i2); }
    }
    });
    return true;
}

/* ---- mask_draw: any path filled or stroked in a plain colour. CoreGraphics only works out its coverage, into an 8-bit mask
 * (quick: an opaque fill of one channel), and Husk blends the colour through it (CoreGraphics' translucent blending is a
 * pixel at a time: Compose's shapes, Material's expressive polygons, ovals and strokes). Where blend_ok allows. */
static uint8_t *g_mask; static size_t g_mask_cap;
static void mask_blend(gcanvas *v, const uint8_t *mask, int dx0, int dy1, int W, int H, uint32_t argb);
/* g_mask zeroed for W x H; false without memory */
static bool mask_room(int W, int H)
{
    size_t need = (size_t)W * H;
    if (need > g_mask_cap) { free(g_mask); g_mask_cap = need + need / 4; g_mask = malloc(g_mask_cap); if (!g_mask) { g_mask_cap = 0; return false; } }
    memset(g_mask, 0, need);
    return true;
}
static bool mask_draw(gcanvas *v, CGPathRef path, const int32_t *p, bool even_odd)
{
    int style = p[P_STYLE];
    if (p[P_CFMODE] >= 0 || pf(p, P_SHRAD) > 0 || p[P_XFER] != 3) return false;
    CGAffineTransform m;
    if (!blend_ok(v, &m)) return false;
    uint32_t argb = (uint32_t)p[P_COLOR];
    if (!(argb >> 24)) return true;
    gbitmap *b = v->bm;
    float sw = pf(p, P_STROKE);
    CGRect box = CGPathGetBoundingBox(path);
    if (CGRectIsNull(box)) return true;
    if (style != 0) { float e = (sw > 0 ? sw : 1) * (p[P_JOIN] == 0 ? fmaxf(pf(p, P_MITER), 4) : 1) + 2; box = CGRectInset(box, -e, -e); }
    box = CGRectApplyAffineTransform(box, m);
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip)) return true;
    box = CGRectIntersection(box, CGRectApplyAffineTransform(clip, m));
    box = CGRectIntersection(box, CGRectMake(0, 0, b->w, b->h));
    if (CGRectIsNull(box) || box.size.width <= 0 || box.size.height <= 0) return true;
    /* the box in whole pixels, CoreGraphics' device space (y up) */
    int dx0 = (int)floor(box.origin.x), dy0 = (int)floor(box.origin.y);
    int dx1 = (int)ceil(box.origin.x + box.size.width), dy1 = (int)ceil(box.origin.y + box.size.height);
    int W = dx1 - dx0, H = dy1 - dy0;
    if (W <= 0 || H <= 0) return true;
    if (!mask_room(W, H)) return false;
    CGContextRef mc = CGBitmapContextCreate(g_mask, (size_t)W, (size_t)H, 8, (size_t)W, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
    if (!mc) return false;
    CGContextConcatCTM(mc, CGAffineTransformConcat(m, CGAffineTransformMakeTranslation(-dx0, -dy0)));
    CGContextSetShouldAntialias(mc, (p[P_FLAGS] & PF_AA) != 0);
    CGContextSetGrayFillColor(mc, 0, 1); CGContextSetGrayStrokeColor(mc, 0, 1);
    CGContextSetLineWidth(mc, sw > 0 ? sw : 1.0f);
    CGContextSetLineCap(mc, p[P_CAP] == 1 ? kCGLineCapRound : p[P_CAP] == 2 ? kCGLineCapSquare : kCGLineCapButt);
    CGContextSetLineJoin(mc, p[P_JOIN] == 1 ? kCGLineJoinRound : p[P_JOIN] == 2 ? kCGLineJoinBevel : kCGLineJoinMiter);
    CGContextSetMiterLimit(mc, pf(p, P_MITER) > 0 ? pf(p, P_MITER) : 4);
    CGContextAddPath(mc, path);
    CGContextDrawPath(mc, style == 1 ? kCGPathStroke : style == 2 ? (even_odd ? kCGPathEOFillStroke : kCGPathFillStroke) : (even_odd ? kCGPathEOFill : kCGPathFill));
    CGContextRelease(mc);
    mask_blend(v, g_mask, dx0, dy1, W, H, argb);
    return true;
}
/* argb blended through g_mask, W x H, over the bitmap from column dx0 and (device space) row dy1 down; and the clip's mask */
static void mask_blend(gcanvas *v, const uint8_t *mask, int dx0, int dy1, int W, int H, uint32_t argb)
{
    gbitmap *b = v->bm;
    /* the part inside the clip's bounds and the bitmap: a cached mask (text_cached) comes whole */
    {
        int top0 = b->h - dy1, x0 = dx0, y0 = top0, x1 = dx0 + W, y1 = top0 + H;
        CGRect cl = clip_user(v);
        if (CGRectIsNull(cl)) return;
        CGRect cd = CGRectStandardize(CGRectApplyAffineTransform(cl, CGContextGetCTM(v->c)));
        int cx0 = (int)floor(cd.origin.x), cx1 = (int)ceil(cd.origin.x + cd.size.width);
        int cy0 = (int)floor(b->h - (cd.origin.y + cd.size.height)), cy1 = (int)ceil(b->h - cd.origin.y);
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > b->w) x1 = b->w;
        if (y1 > b->h) y1 = b->h;
        if (x0 >= x1 || y0 >= y1) return;
        if (x0 != dx0 || y0 != top0 || x1 != dx0 + W || y1 != top0 + H) {
            /* a sub-mask: the same rows, from (x0, y0), its stride still W */
            mask += (size_t)(y0 - top0) * W + (x0 - dx0);
            int nw = x1 - x0, nh = y1 - y0;
            for (int r = 0; r < nh; r++) {
                const uint8_t *mk = mask + (size_t)r * W;
                int j = y0 + r;
                uint32_t inv, s_full = premul(argb, 1, &inv);
                uint32_t *d = (uint32_t *)(b->px + (size_t)j * b->stride) + x0;
                int bx0 = x0, bx1 = x1, by0 = j, by1 = j + 1;
                const uint8_t *ck = NULL;
                if (clip_need(v, x0, x1, j, j + 1)) {
                    if (!clip_box(v, &bx0, &bx1, &by0, &by1)) continue;
                    ck = clip_at(v, bx0, j);
                }
                for (int i = bx0; i < bx1; i++) {
                    uint32_t c = mk[i - x0];
                    if (ck) c = (c * (uint32_t)ck[i - bx0] + 127) / 255;
                    d[i - x0] = over(d[i - x0], scale_px(s_full, c));
                }
            }
            (void)nw;
            return;
        }
    }
    /* the mask's rows from the top, as the bitmap's: row r is the bitmap's row (h - dy1) + r */
    uint32_t inv, s_full = premul(argb, 1, &inv);
    int top = b->h - dy1;
    if (clip_need(v, dx0, dx0 + W, b->h - dy1, b->h - dy1 + H)) {
        int mx0 = dx0, mx1 = dx0 + W, my0 = top, my1 = top + H;
        if (!clip_box(v, &mx0, &mx1, &my0, &my1)) return;
        for (int j = my0; j < my1; j++) {
            const uint8_t *mk = mask + (size_t)(j - top) * W + (mx0 - dx0), *ck = clip_at(v, mx0, j);
            uint32_t *d = (uint32_t *)(b->px + (size_t)j * b->stride) + mx0;
            if (!ck) { for (int i = 0; i < mx1 - mx0; i++) d[i] = over(d[i], scale_px(s_full, mk[i])); continue; }
            for (int i = 0; i < mx1 - mx0; i++) d[i] = over(d[i], scale_px(s_full, (mk[i] * (uint32_t)ck[i] + 127) / 255));
        }
        return;
    }
    for (int r = 0; r < H; r++) {
        const uint8_t *mk = mask + (size_t)r * W;
        uint32_t *d = (uint32_t *)(b->px + (size_t)(top + r) * b->stride) + dx0;
        for (int i = 0; i < W; i++) {
            uint32_t c = mk[i];
            d[i] = over(d[i], scale_px(s_full, c));
        }
    }
    return;
}


static CGBlendMode blend_of(int mode)
{
    switch (mode) {
    case 0: return kCGBlendModeClear;
    case 1: return kCGBlendModeCopy;
    case 4: return kCGBlendModeDestinationOver;
    case 5: return kCGBlendModeSourceIn;
    case 6: return kCGBlendModeDestinationIn;
    case 7: return kCGBlendModeSourceOut;
    case 8: return kCGBlendModeDestinationOut;
    case 9: return kCGBlendModeSourceAtop;
    case 10: return kCGBlendModeDestinationAtop;
    case 11: return kCGBlendModeXOR;
    case 12: return kCGBlendModePlusLighter;
    case 13: return kCGBlendModeMultiply;
    case 14: return kCGBlendModeScreen;
    case 15: return kCGBlendModeOverlay;
    case 16: return kCGBlendModeDarken;
    case 17: return kCGBlendModeLighten;
    default: return kCGBlendModeNormal;
    }
}

/* Set up the context for one draw with this paint: colour, stroke, blend, shadow. Returns false when nothing would show. */
static bool paint_begin(CGContextRef c, const int32_t *p, bool *layered)
{
    *layered = false;
    if (p[P_XFER] == 2) return false;                               /* DST: leaves the destination as it is */
    CGContextSaveGState(c);
    uint32_t col = (uint32_t)p[P_COLOR];
    CGColorRef cc = color_of(col, 1);
    CGContextSetFillColorWithColor(c, cc);
    CGContextSetStrokeColorWithColor(c, cc);
    CGColorRelease(cc);
    CGContextSetShouldAntialias(c, (p[P_FLAGS] & PF_AA) != 0);
    float sw = pf(p, P_STROKE);
    CGContextSetLineWidth(c, sw > 0 ? sw : 1.0f);    /* hairline: one pixel */
    CGContextSetLineCap(c, p[P_CAP] == 1 ? kCGLineCapRound : p[P_CAP] == 2 ? kCGLineCapSquare : kCGLineCapButt);
    CGContextSetLineJoin(c, p[P_JOIN] == 1 ? kCGLineJoinRound : p[P_JOIN] == 2 ? kCGLineJoinBevel : kCGLineJoinMiter);
    CGContextSetMiterLimit(c, pf(p, P_MITER) > 0 ? pf(p, P_MITER) : 4);
    CGContextSetInterpolationQuality(c, (p[P_FLAGS] & PF_FILTER) ? kCGInterpolationDefault : kCGInterpolationNone);
    if (pf(p, P_SHRAD) > 0) {
        CGColorRef sc = color_of((uint32_t)p[P_SHCOL], 1);
        CGContextSetShadowWithColor(c, CGSizeMake(pf(p, P_SHDX), -pf(p, P_SHDY)), pf(p, P_SHRAD), sc);
        CGColorRelease(sc);
    }
    if (p[P_CFMODE] >= 0) {
        /* a colour filter: the draw goes into a layer, then the filter colour is laid over it with its mode */
        CGContextBeginTransparencyLayer(c, NULL);
        *layered = true;
    } else CGContextSetBlendMode(c, blend_of(p[P_XFER]));
    return true;
}
/* What was drawn, as a clip: MULTIPLY's filter colour is laid only over it (Android keeps the drawing's alpha, Sa*Da; CoreGraphics's
   multiply would fill the whole area -- Telegram's tinted wordmark came out a solid block). */
typedef void (*clip_fn)(CGContextRef c, const void *ctx);
static void paint_end_clip(CGContextRef c, const int32_t *p, bool layered, CGRect area, clip_fn clip, const void *ctx);
static void paint_end(CGContextRef c, const int32_t *p, bool layered, CGRect area) { paint_end_clip(c, p, layered, area, NULL, NULL); }
static void paint_end_clip(CGContextRef c, const int32_t *p, bool layered, CGRect area, clip_fn clip, const void *ctx)
{
    if (layered) {
        CGColorRef fc = color_of((uint32_t)p[P_CFCOLOR], 1);
        CGContextSetFillColorWithColor(c, fc);
        CGColorRelease(fc);
        CGContextSetShadowWithColor(c, CGSizeZero, 0, NULL);
        int m = p[P_CFMODE];
        CGContextSetBlendMode(c, m == 3 ? kCGBlendModeNormal : blend_of(m));
        if (m == 13 && clip) { CGContextSaveGState(c); clip(c, ctx); CGContextFillRect(c, CGRectInset(area, -2, -2)); CGContextRestoreGState(c); }
        else CGContextFillRect(c, CGRectInset(area, -2, -2));
        CGContextEndTransparencyLayer(c);
    }
    CGContextRestoreGState(c);
}

struct path_clip { CGPathRef path; bool even_odd; int style; };
static void clip_path(CGContextRef c, const void *ctx)
{
    const struct path_clip *pc = ctx;
    CGContextAddPath(c, pc->path);
    if (pc->style == 1) { CGContextReplacePathWithStrokedPath(c); CGContextClip(c); }
    else if (pc->even_odd) CGContextEOClip(c); else CGContextClip(c);
}
struct text_clip { CTLineRef line; CGFloat x, y; };
static void clip_text(CGContextRef c, const void *ctx)
{
    const struct text_clip *tc = ctx;
    CGContextSetTextDrawingMode(c, kCGTextClip);
    CGContextSetTextPosition(c, tc->x, tc->y);
    CTLineDraw(tc->line, c);
}
struct image_clip { CGImageRef img; CGAffineTransform t; CGRect r; };
static void clip_image(CGContextRef c, const void *ctx)
{
    const struct image_clip *ic = ctx;
    CGContextConcatCTM(c, ic->t);
    CGContextClipToMask(c, ic->r, ic->img);
    CGContextConcatCTM(c, CGAffineTransformInvert(ic->t));
}

/* Draw the path now in the context with this paint (fill, stroke or both; through the shader when there is one). */
static void draw_path(CGContextRef c, CGPathRef path, const int32_t *p, gshader *sh, bool even_odd)
{
    bool layered;
    if (!paint_begin(c, p, &layered)) return;
    int style = p[P_STYLE];
    float alpha = (float)((uint32_t)p[P_COLOR] >> 24) / 255.0f;
    struct path_clip pc = { path, even_odd, style };
    CGRect area = CGPathGetBoundingBox(path);
    area = CGRectInset(area, -pf(p, P_STROKE), -pf(p, P_STROKE));
    if (sh) {
        CGContextSaveGState(c);
        CGContextAddPath(c, path);
        if (style == 1) { CGContextReplacePathWithStrokedPath(c); CGContextClip(c); }
        else if (even_odd) CGContextEOClip(c); else CGContextClip(c);
        shader_fill(c, sh, alpha);
        CGContextRestoreGState(c);
        if (style == 2) {
            CGContextSaveGState(c);
            CGContextAddPath(c, path); CGContextReplacePathWithStrokedPath(c); CGContextClip(c);
            shader_fill(c, sh, alpha);
            CGContextRestoreGState(c);
        }
    } else {
        CGContextAddPath(c, path);
        CGContextDrawPath(c, style == 1 ? kCGPathStroke : style == 2 ? (even_odd ? kCGPathEOFillStroke : kCGPathFillStroke) : (even_odd ? kCGPathEOFill : kCGPathFill));
    }
    paint_end_clip(c, p, layered, area, clip_path, &pc);
}

/* ---- Path, as husk.Gfx streams it: op codes and their floats */
enum { OP_MOVE, OP_LINE, OP_QUAD, OP_CUBIC, OP_CLOSE, OP_ARC, OP_RECT, OP_OVAL, OP_RRECT, OP_TRANSFORM, OP_RMOVE, OP_RLINE, OP_RQUAD, OP_RCUBIC, OP_RRECT8 };

static void add_rrect8(CGMutablePathRef p, const CGAffineTransform *t, float l, float tp, float r, float b, const float *rad, bool ccw)
{
    /* corners: top-left, top-right, bottom-right, bottom-left, each (rx, ry), as Android's addRoundRect(float[]) */
    float w = r - l, h = b - tp;
    float rx[4], ry[4];
    for (int i = 0; i < 4; i++) { rx[i] = fminf(fmaxf(rad[2 * i], 0), w / 2); ry[i] = fminf(fmaxf(rad[2 * i + 1], 0), h / 2); }
    const float k = 0.5522847f;
    (void)ccw;
    CGPathMoveToPoint(p, t, l + rx[0], tp);
    CGPathAddLineToPoint(p, t, r - rx[1], tp);
    CGPathAddCurveToPoint(p, t, r - rx[1] + rx[1] * k, tp, r, tp + ry[1] - ry[1] * k, r, tp + ry[1]);
    CGPathAddLineToPoint(p, t, r, b - ry[2]);
    CGPathAddCurveToPoint(p, t, r, b - ry[2] + ry[2] * k, r - rx[2] + rx[2] * k, b, r - rx[2], b);
    CGPathAddLineToPoint(p, t, l + rx[3], b);
    CGPathAddCurveToPoint(p, t, l + rx[3] - rx[3] * k, b, l, b - ry[3] + ry[3] * k, l, b - ry[3]);
    CGPathAddLineToPoint(p, t, l, tp + ry[0]);
    CGPathAddCurveToPoint(p, t, l, tp + ry[0] - ry[0] * k, l + rx[0] - rx[0] * k, tp, l + rx[0], tp);
    CGPathCloseSubpath(p);
}

static void add_arc(CGMutablePathRef p, float l, float t, float r, float b, float start, float sweep, bool move)
{
    float cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
    if (rx <= 0 || ry <= 0) return;
    CGAffineTransform m = CGAffineTransformMakeTranslation(cx, cy);
    m = CGAffineTransformScale(m, rx, ry);
    double a0 = start * M_PI / 180, a1 = (start + sweep) * M_PI / 180;
    CGPoint s = CGPointApplyAffineTransform(CGPointMake(cos(a0), sin(a0)), m);
    if (move || CGPathIsEmpty(p)) CGPathMoveToPoint(p, NULL, s.x, s.y);
    else CGPathAddLineToPoint(p, NULL, s.x, s.y);
    if (fabsf(sweep) >= 360) {
        CGPathAddEllipseInRect(p, NULL, CGRectMake(l, t, r - l, b - t));
        CGPathMoveToPoint(p, NULL, s.x, s.y);
        return;
    }
    CGPathAddRelativeArc(p, &m, 0, 0, 1, a0, a1 - a0);
}

static CGPathRef build_path(const float *f, int n)
{
    CGMutablePathRef p = CGPathCreateMutable();
    float lx = 0, ly = 0;                       /* the last point, for the relative ops */
    for (int i = 0; i < n; ) {
        int op = (int)f[i++];
        switch (op) {
        case OP_MOVE: CGPathMoveToPoint(p, NULL, f[i], f[i + 1]); lx = f[i]; ly = f[i + 1]; i += 2; break;
        case OP_RMOVE: lx += f[i]; ly += f[i + 1]; CGPathMoveToPoint(p, NULL, lx, ly); i += 2; break;
        case OP_LINE: case OP_RLINE: {
            float x = f[i], y = f[i + 1]; if (op == OP_RLINE) { x += lx; y += ly; }
            if (CGPathIsEmpty(p)) CGPathMoveToPoint(p, NULL, 0, 0);
            CGPathAddLineToPoint(p, NULL, x, y); lx = x; ly = y; i += 2; break; }
        case OP_QUAD: case OP_RQUAD: {
            float ox = op == OP_RQUAD ? lx : 0, oy = op == OP_RQUAD ? ly : 0;
            if (CGPathIsEmpty(p)) CGPathMoveToPoint(p, NULL, 0, 0);
            CGPathAddQuadCurveToPoint(p, NULL, f[i] + ox, f[i + 1] + oy, f[i + 2] + ox, f[i + 3] + oy);
            lx = f[i + 2] + ox; ly = f[i + 3] + oy; i += 4; break; }
        case OP_CUBIC: case OP_RCUBIC: {
            float ox = op == OP_RCUBIC ? lx : 0, oy = op == OP_RCUBIC ? ly : 0;
            if (CGPathIsEmpty(p)) CGPathMoveToPoint(p, NULL, 0, 0);
            CGPathAddCurveToPoint(p, NULL, f[i] + ox, f[i + 1] + oy, f[i + 2] + ox, f[i + 3] + oy, f[i + 4] + ox, f[i + 5] + oy);
            lx = f[i + 4] + ox; ly = f[i + 5] + oy; i += 6; break; }
        case OP_CLOSE: if (!CGPathIsEmpty(p)) { CGPathCloseSubpath(p); CGPoint c = CGPathGetCurrentPoint(p); lx = (float)c.x; ly = (float)c.y; } break;
        case OP_ARC: add_arc(p, f[i], f[i + 1], f[i + 2], f[i + 3], f[i + 4], f[i + 5], f[i + 6] != 0); { CGPoint c = CGPathGetCurrentPoint(p); lx = (float)c.x; ly = (float)c.y; } i += 7; break;
        case OP_RECT: {
            float l = f[i], t = f[i + 1], r = f[i + 2], b = f[i + 3];
            if (f[i + 4] != 0) { CGPathMoveToPoint(p, NULL, l, t); CGPathAddLineToPoint(p, NULL, l, b); CGPathAddLineToPoint(p, NULL, r, b); CGPathAddLineToPoint(p, NULL, r, t); CGPathCloseSubpath(p); }
            else CGPathAddRect(p, NULL, CGRectMake(fminf(l, r), fminf(t, b), fabsf(r - l), fabsf(b - t)));
            lx = l; ly = t; i += 5; break; }
        case OP_OVAL: CGPathAddEllipseInRect(p, NULL, CGRectMake(f[i], f[i + 1], f[i + 2] - f[i], f[i + 3] - f[i + 1])); i += 5; break;
        case OP_RRECT: { float rad[8] = { f[i + 4], f[i + 5], f[i + 4], f[i + 5], f[i + 4], f[i + 5], f[i + 4], f[i + 5] };
            add_rrect8(p, NULL, f[i], f[i + 1], f[i + 2], f[i + 3], rad, f[i + 6] != 0); i += 7; break; }
        case OP_RRECT8: add_rrect8(p, NULL, f[i], f[i + 1], f[i + 2], f[i + 3], f + i + 4, f[i + 12] != 0); i += 13; break;
        case OP_TRANSFORM: {
            CGAffineTransform m = CGAffineTransformMake(f[i], f[i + 3], f[i + 1], f[i + 4], f[i + 2], f[i + 5]);
            CGMutablePathRef q = CGPathCreateMutableCopyByTransformingPath(p, &m);
            CGPathRelease(p); p = q;
            CGPoint c = CGPointApplyAffineTransform(CGPointMake(lx, ly), m); lx = (float)c.x; ly = (float)c.y;
            i += 6; break; }
        default: i = n; break;
        }
    }
    return p;
}

/* ================================================================== text */

static CFStringRef cfstr(jobj *s, int start, int end)
{
    int32_t n = 0;
    const uint16_t *u = s ? tl_dvm_string_chars(s, &n) : NULL;
    if (!u) return CFStringCreateWithCharacters(NULL, NULL, 0);
    if (start < 0) start = 0;
    if (end > n || end < 0) end = n;
    if (end < start) end = start;
    return CFStringCreateWithCharacters(NULL, u + start, end - start);
}

/* husk-port: the text with the paint's font and letter spacing, which measuring, breaking and drawing all lay out alike
 * (txBreak had no letter spacing: text with a negative one measured narrower than it broke, and broke mid-word) */
static CFAttributedStringRef make_attributed(CFStringRef str, const int32_t *p, gface *g)
{
    CTFontRef f = sized(g, pf(p, P_TEXTSIZE));
    float ls = pf(p, P_LSPACE) * pf(p, P_TEXTSIZE);
    CFNumberRef kern = CFNumberCreate(NULL, kCFNumberFloatType, &ls);
    const void *k[] = { kCTFontAttributeName, kCTForegroundColorFromContextAttributeName, kCTKernAttributeName };
    const void *v[] = { f, kCFBooleanTrue, kern };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, k, v, ls != 0 ? 3 : 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, attrs);
    CFRelease(attrs); CFRelease(kern); CFRelease(f);
    return as;
}

static CTLineRef make_line(CFStringRef str, const int32_t *p, gface *g)
{
    CFAttributedStringRef as = make_attributed(str, p, g);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CFRelease(as);
    return line;
}

static float line_width(CTLineRef line, const int32_t *p)
{
    return (float)CTLineGetTypographicBounds(line, NULL, NULL, NULL) * (pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1);
}

/* Text in a plain colour, as mask_draw draws a path: CoreText's glyphs as coverage, Husk blending the colour through it (and
 * through the clip's mask, without handing CoreGraphics the clip). Not colour glyphs (emoji): those are their own colours. */
static bool text_mask(gcanvas *v, CTLineRef line, float x, float y, float w, const int32_t *p)
{
    if (p[P_CFMODE] >= 0 || pf(p, P_SHRAD) > 0 || p[P_XFER] != 3 || p[P_STYLE] != 0
        || (p[P_FLAGS] & (PF_FAKEBOLD | PF_UNDERLINE | PF_STRIKE))) return false;
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    for (CFIndex i = 0; i < CFArrayGetCount(runs); i++) {
        CTFontRef f = CFDictionaryGetValue(CTRunGetAttributes(CFArrayGetValueAtIndex(runs, i)), kCTFontAttributeName);
        if (f && (CTFontGetSymbolicTraits(f) & kCTFontTraitColorGlyphs)) return false;
    }
    CGAffineTransform m;
    if (!blend_ok(v, &m)) return false;
    uint32_t argb = (uint32_t)p[P_COLOR];
    if (!(argb >> 24)) return true;
    gbitmap *b = v->bm;
    CGFloat asc = 0, desc = 0;
    CTLineGetTypographicBounds(line, &asc, &desc, NULL);
    float sx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1, skew = pf(p, P_SKEWX);
    /* room for what glyphs reach past their advance and the line's bounds (italics, accents, swashes) */
    float pad = (float)(asc + desc) * 0.5f + 2;
    CGRect area = CGRectMake(x - pad, y - (float)asc - pad, w * sx + 2 * pad + fabsf(skew) * (float)(asc + desc), (float)(asc + desc) + 2 * pad);
    CGRect box = CGRectApplyAffineTransform(area, m);
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip)) return true;
    box = CGRectIntersection(box, CGRectApplyAffineTransform(clip, m));
    box = CGRectIntersection(box, CGRectMake(0, 0, b->w, b->h));
    if (CGRectIsNull(box) || box.size.width <= 0 || box.size.height <= 0) return true;
    int dx0 = (int)floor(box.origin.x), dy0 = (int)floor(box.origin.y);
    int dx1 = (int)ceil(box.origin.x + box.size.width), dy1 = (int)ceil(box.origin.y + box.size.height);
    int W = dx1 - dx0, H = dy1 - dy0;
    if (W <= 0 || H <= 0) return true;
    if (!mask_room(W, H)) return false;
    CGContextRef mc = CGBitmapContextCreate(g_mask, (size_t)W, (size_t)H, 8, (size_t)W, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
    if (!mc) return false;
    CGContextConcatCTM(mc, CGAffineTransformConcat(m, CGAffineTransformMakeTranslation(-dx0, -dy0)));
    CGContextSetShouldAntialias(mc, (p[P_FLAGS] & PF_AA) != 0);
    CGContextSetShouldSmoothFonts(mc, false);
    CGContextSetGrayFillColor(mc, 0, 1);
    CGContextSetTextMatrix(mc, CGAffineTransformMake(sx, 0, -skew, -1, 0, 0));
    CGContextSetTextDrawingMode(mc, kCGTextFill);
    CGContextSetTextPosition(mc, x, y);
    CTLineDraw(line, mc);
    CGContextRelease(mc);
    mask_blend(v, g_mask, dx0, dy1, W, H, argb);
    return true;
}
/* ---- text_cached: a line of text's coverage, kept: an app draws the same labels every frame, and CoreText laid each out and
 * filled its glyphs every time (30 to 50 lines a frame). Keyed by the characters, the font and paint, the transform's scale
 * and where in its pixel the line starts (quarters); an entry a slot, the newest wins. Lines with colour glyphs are kept
 * as such, for CoreGraphics. */
typedef struct tcache {
    uint64_t key;
    uint16_t *chars; int n;
    gface *face; int32_t size, lspace, sx, skew, flags; float ma, md; int phx, phy;
    float w;                        /* the line's advance, for the paint's alignment */
    bool color;                     /* colour glyphs: CoreGraphics draws it */
    int ox, oy, W, H;               /* the mask's top left from the start's pixel, its size */
    uint8_t *mask;
} tcache;
#define TCACHE_SLOTS 1024
static tcache g_tc[TCACHE_SLOTS];
static size_t g_tc_bytes;
static int64_t g_tc_hit, g_tc_miss, g_tc_color, g_tc_skip, g_tc_px;     /* HUSK_FRAME_STATS */
static void tc_free(tcache *e) { free(e->chars); if (e->mask) g_tc_bytes -= (size_t)e->W * e->H; free(e->mask); memset(e, 0, sizeof *e); }

static bool text_cached(gcanvas *v, jobj *s, int start, int end, float x, float y, const int32_t *p, gface *g)
{
    if (p[P_CFMODE] >= 0 || pf(p, P_SHRAD) > 0 || p[P_XFER] != 3 || p[P_STYLE] != 0
        || (p[P_FLAGS] & (PF_FAKEBOLD | PF_UNDERLINE | PF_STRIKE))) { g_tc_skip++; return false; }
    CGAffineTransform m;
    if (!blend_ok(v, &m)) { g_tc_skip++; return false; }
    int32_t n = 0;
    const uint16_t *u = s ? tl_dvm_string_chars(s, &n) : NULL;
    if (!u) return false;
    if (start < 0) start = 0;
    if (end > n || end < 0) end = n;
    if (end <= start) return true;
    u += start; n = end - start;
    uint32_t argb = (uint32_t)p[P_COLOR];
    gbitmap *b = v->bm;
    /* the key: the characters and everything the glyphs' shapes depend on, but not where they go */
    int32_t sx = p[P_SCALEX], skew = p[P_SKEWX], size = p[P_TEXTSIZE], ls = p[P_LSPACE], flags = p[P_FLAGS] & PF_AA;
    uint64_t k = 0xcbf29ce484222325ull;
    for (int i = 0; i < n; i++) k = (k ^ u[i]) * 0x100000001b3ull;
    uint64_t kp = (uint64_t)(uintptr_t)g ^ ((uint64_t)(uint32_t)size << 1) ^ ((uint64_t)(uint32_t)ls << 17) ^ ((uint64_t)(uint32_t)sx << 29)
                ^ ((uint64_t)(uint32_t)skew << 7) ^ (uint64_t)flags;
    k = (k ^ kp) * 0x100000001b3ull;
    float ma = (float)m.a, md = (float)m.d;
    k = (k ^ (uint64_t)(uint32_t)lrintf(ma * 1000) ^ ((uint64_t)(uint32_t)lrintf(md * 1000) << 32)) * 0x100000001b3ull;
    tcache *e = NULL;
    /* the alignment needs the advance, which the entry has: look for it at each quarter of a pixel the start may be in */
    float w = -1;
    for (int pass = 0; pass < 2; pass++) {
        float ax = x;
        if (w >= 0) { if (p[P_ALIGN] == 1) ax -= w / 2; else if (p[P_ALIGN] == 2) ax -= w; }
        double X = m.a * ax + m.tx, Y = b->h - (m.d * y + m.ty);
        int ix = (int)floor(X), iy = (int)floor(Y), phx = (int)floor((X - ix) * 4), phy = (int)floor((Y - iy) * 4);
        uint64_t key = (k ^ (uint64_t)(phx | phy << 2)) * 0x100000001b3ull;
        tcache *t = &g_tc[key % TCACHE_SLOTS];
        bool hit = t->key == key && t->n == n && t->face == g && t->size == size && t->lspace == ls && t->sx == sx && t->skew == skew
                && t->flags == flags && t->ma == ma && t->md == md && t->phx == phx && t->phy == phy && !memcmp(t->chars, u, (size_t)n * 2);
        if (hit && (p[P_ALIGN] == 0 || pass == 1)) {
            if (t->color) { g_tc_color++; return false; }
            g_tc_hit++; g_tc_px += (int64_t)t->W * t->H;
            if (t->mask && (argb >> 24)) mask_blend(v, t->mask, ix + t->ox, b->h - (iy + t->oy), t->W, t->H, argb);
            return true;
        }
        if (hit) { w = t->w; continue; }
        if (p[P_ALIGN] != 0 && pass == 0) {
            /* no entry at the unaligned start: lay the line out for its advance, then look again */
            CFStringRef str = CFStringCreateWithCharacters(NULL, u, n);
            CTLineRef line = make_line(str, p, g);
            CFRelease(str);
            w = line_width(line, p);
            CFRelease(line);
            continue;
        }
        /* not kept: lay it out and fill its coverage, unclipped, for this and the frames after */
        g_tc_miss++;
        e = t;
        tc_free(e);
        CFStringRef str = CFStringCreateWithCharacters(NULL, u, n);
        CTLineRef line = make_line(str, p, g);
        CFRelease(str);
        e->key = key; e->n = n; e->face = g; e->size = size; e->lspace = ls; e->sx = sx; e->skew = skew; e->flags = flags;
        e->ma = ma; e->md = md; e->phx = phx; e->phy = phy;
        e->chars = malloc((size_t)n * 2);
        if (!e->chars) { CFRelease(line); memset(e, 0, sizeof *e); return false; }
        memcpy(e->chars, u, (size_t)n * 2);
        e->w = line_width(line, p);
        CFArrayRef runs = CTLineGetGlyphRuns(line);
        for (CFIndex i = 0; i < CFArrayGetCount(runs); i++) {
            CTFontRef f = CFDictionaryGetValue(CTRunGetAttributes(CFArrayGetValueAtIndex(runs, i)), kCTFontAttributeName);
            if (f && (CTFontGetSymbolicTraits(f) & kCTFontTraitColorGlyphs)) e->color = true;
        }
        if (e->color) { CFRelease(line); return false; }
        CGFloat asc = 0, desc = 0;
        CTLineGetTypographicBounds(line, &asc, &desc, NULL);
        float fsx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1, fskew = pf(p, P_SKEWX);
        float pad = (float)(asc + desc) * 0.5f + 2;
        CGRect area = CGRectMake(ax - pad, y - (float)asc - pad, e->w + 2 * pad + fabsf(fskew) * (float)(asc + desc), (float)(asc + desc) + 2 * pad);
        CGRect box = CGRectStandardize(CGRectApplyAffineTransform(area, m));
        int dx0 = (int)floor(box.origin.x), dy0 = (int)floor(box.origin.y);
        int W = (int)ceil(box.origin.x + box.size.width) - dx0, H = (int)ceil(box.origin.y + box.size.height) - dy0;
        if (W <= 0 || H <= 0 || (int64_t)W * H > 4 << 20) { CFRelease(line); tc_free(e); return false; }
        e->mask = calloc((size_t)W * H, 1);
        CGContextRef mc = e->mask ? CGBitmapContextCreate(e->mask, (size_t)W, (size_t)H, 8, (size_t)W, NULL, (CGBitmapInfo)kCGImageAlphaOnly) : NULL;
        if (!mc) { CFRelease(line); tc_free(e); return false; }
        g_tc_bytes += (size_t)W * H;
        CGContextConcatCTM(mc, CGAffineTransformConcat(m, CGAffineTransformMakeTranslation(-dx0, -dy0)));
        CGContextSetShouldAntialias(mc, (p[P_FLAGS] & PF_AA) != 0);
        CGContextSetShouldSmoothFonts(mc, false);
        CGContextSetGrayFillColor(mc, 0, 1);
        CGContextSetTextMatrix(mc, CGAffineTransformMake(fsx, 0, -fskew, -1, 0, 0));
        CGContextSetTextDrawingMode(mc, kCGTextFill);
        CGContextSetTextPosition(mc, ax, y);
        CTLineDraw(line, mc);
        CGContextRelease(mc);
        CFRelease(line);
        /* kept to the glyphs' own bounds: the box had room around them for what glyphs may reach, mostly empty */
        int tx0 = W, ty0 = H, tx1 = -1, ty1 = -1;
        for (int r = 0; r < H; r++) {
            const uint8_t *row = e->mask + (size_t)r * W;
            int a0 = 0, a1 = W - 1;
            while (a0 < W && !row[a0]) a0++;
            if (a0 == W) continue;
            while (!row[a1]) a1--;
            if (a0 < tx0) tx0 = a0;
            if (a1 > tx1) tx1 = a1;
            if (r < ty0) ty0 = r;
            ty1 = r;
        }
        int top = b->h - (dy0 + H);
        if (tx1 < 0) { g_tc_bytes -= (size_t)W * H; free(e->mask); e->mask = NULL; e->W = e->H = 0; e->ox = e->oy = 0; return true; }
        int nW = tx1 - tx0 + 1, nH = ty1 - ty0 + 1;
        uint8_t *tight = malloc((size_t)nW * nH);
        if (tight) {
            for (int r = 0; r < nH; r++) memcpy(tight + (size_t)r * nW, e->mask + (size_t)(ty0 + r) * W + tx0, (size_t)nW);
            g_tc_bytes -= (size_t)W * H; g_tc_bytes += (size_t)nW * nH;
            free(e->mask); e->mask = tight;
            dx0 += tx0; top += ty0; W = nW; H = nH;
        }
        e->W = W; e->H = H; e->ox = dx0 - ix; e->oy = top - iy;
        if (argb >> 24) mask_blend(v, e->mask, dx0, b->h - top, W, H, argb);
        /* a bound on what is kept: past 48 MB, the cache starts again */
        if (g_tc_bytes > (48u << 20)) for (int i = 0; i < TCACHE_SLOTS; i++) if (&g_tc[i] != e) tc_free(&g_tc[i]);
        return true;
    }
    return false;
}

static void draw_text(gcanvas *v, jobj *s, int start, int end, float x, float y, const int32_t *p, gface *g, gshader *sh)
{
    if (!sh && text_cached(v, s, start, end, x, y, p, g)) return;
    CFStringRef str = cfstr(s, start, end);
    CTLineRef line = make_line(str, p, g);
    CFRelease(str);
    float w = line_width(line, p);
    if (p[P_ALIGN] == 1) x -= w / 2;
    else if (p[P_ALIGN] == 2) x -= w;
    if (!sh && text_mask(v, line, x, y, w, p)) { CFRelease(line); return; }
    CGContextRef c = cg(v);
    bool layered;
    if (paint_begin(c, p, &layered)) {
        float sx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1, skew = pf(p, P_SKEWX);
        CGContextSetTextMatrix(c, CGAffineTransformMake(sx, 0, -skew, -1, 0, 0));
        int style = p[P_STYLE];
        bool bold = (p[P_FLAGS] & PF_FAKEBOLD) != 0;
        if (bold || style != 0) {
            CGContextSetLineWidth(c, style != 0 ? pf(p, P_STROKE) : pf(p, P_TEXTSIZE) / 24);
            CGContextSetTextDrawingMode(c, style == 1 ? kCGTextStroke : kCGTextFillStroke);
        } else CGContextSetTextDrawingMode(c, kCGTextFill);
        CGFloat asc = 0, desc = 0;
        CTLineGetTypographicBounds(line, &asc, &desc, NULL);
        CGRect area = CGRectMake(x - 2, y - asc - 2, w + 4, asc + desc + 4);
        if (sh) {
            CGContextSaveGState(c);
            CGContextSetTextDrawingMode(c, kCGTextClip);
            CGContextSetTextPosition(c, x, y);
            CTLineDraw(line, c);
            shader_fill(c, sh, (float)((uint32_t)p[P_COLOR] >> 24) / 255.0f);
            CGContextRestoreGState(c);
        } else {
            CGContextSetTextPosition(c, x, y);
            CTLineDraw(line, c);
        }
        if (p[P_FLAGS] & (PF_UNDERLINE | PF_STRIKE)) {
            float ts = pf(p, P_TEXTSIZE), th = ts / 18 > 1 ? ts / 18 : 1;
            if (p[P_FLAGS] & PF_UNDERLINE) CGContextFillRect(c, CGRectMake(x, y + ts / 9, w, th));
            if (p[P_FLAGS] & PF_STRIKE) CGContextFillRect(c, CGRectMake(x, y - ts * 0.3f, w, th));
        }
        struct text_clip tcl = { line, x, y };
        paint_end_clip(c, p, layered, area, clip_text, &tcl);
    }
    CFRelease(line);
}

/* ================================================================== the natives (husk.Gfx) */

#define NAT(fn) static bool fn(jobj *self, const jvalue *a, jvalue *ret)
static jvalue J(int64_t j) { jvalue v; v.j = j; return v; }
static jvalue Ii(int32_t i) { jvalue v; v.j = (uint32_t)i; return v; }
static jvalue Ff(float f) { jvalue v; v.j = 0; v.f = f; return v; }
static jvalue Ll(void *p) { jvalue v; v.j = 0; v.l = p; return v; }
#define H(T, k) ((T *)(uintptr_t)a[k].j)
static void *arr_data(jobj *x) { return x && x->kind == TL_K_PRIM_ARRAY ? x->arr.data : NULL; }
static uint32_t arr_len(jobj *x) { return x && x->kind == TL_K_PRIM_ARRAY ? x->arr.len : 0; }

/* ---- Bitmap */
NAT(G_bmNew) { (void)self; *ret = J((int64_t)(uintptr_t)bm_new(a[0].i, a[1].i)); return true; }
NAT(G_bmFree) { (void)self; (void)ret; bm_free(H(gbitmap, 0)); return true; }
NAT(G_bmErase)
{
    (void)self; (void)ret;
    gbitmap *b = H(gbitmap, 0);
    if (!b) return true;
    uint8_t px[4]; from_argb(px, (uint32_t)a[1].i);
    uint32_t v; memcpy(&v, px, 4);
    int w = b->w; size_t stride = b->stride; uint8_t *base = b->px;
    par_rows((int64_t)b->w * b->h, 0, b->h, ^(int j0, int j1) {
        if (!v && stride == (size_t)w * 4) { memset(base + (size_t)j0 * stride, 0, (size_t)(j1 - j0) * stride); return; }
        for (int j = j0; j < j1; j++) { uint32_t *d = (uint32_t *)(base + (size_t)j * stride); for (int i = 0; i < w; i++) d[i] = v; }
    });
    return true;
}
NAT(G_bmGetPixel)
{
    (void)self;
    gbitmap *b = H(gbitmap, 0); int x = a[1].i, y = a[2].i;
    *ret = Ii(b && x >= 0 && y >= 0 && x < b->w && y < b->h ? (int32_t)to_argb(b->px + (size_t)y * b->stride + (size_t)x * 4) : 0);
    return true;
}
NAT(G_bmSetPixel)
{
    (void)self; (void)ret;
    gbitmap *b = H(gbitmap, 0); int x = a[1].i, y = a[2].i;
    if (b && x >= 0 && y >= 0 && x < b->w && y < b->h) from_argb(b->px + (size_t)y * b->stride + (size_t)x * 4, (uint32_t)a[3].i);
    return true;
}
/* (long bm, int[] px, int offset, int stride, int x, int y, int w, int h, boolean set) */
NAT(G_bmPixels)
{
    (void)self; (void)ret;
    gbitmap *b = H(gbitmap, 0);
    int32_t *px = arr_data(a[1].l); uint32_t len = arr_len(a[1].l);
    int off = a[2].i, stride = a[3].i, x = a[4].i, y = a[5].i, w = a[6].i, h = a[7].i;
    bool set = a[8].z;
    if (!b || !px || x < 0 || y < 0 || x + w > b->w || y + h > b->h) return dvm_throw("java/lang/IllegalArgumentException", "pixels out of bounds");
    for (int r = 0; r < h; r++) {
        int64_t base = off + (int64_t)r * stride;
        if (base < 0 || base + w > len) return dvm_throw("java/lang/ArrayIndexOutOfBoundsException", "pixels");
        uint8_t *row = b->px + (size_t)(y + r) * b->stride + (size_t)x * 4;
        for (int k = 0; k < w; k++) {
            if (set) from_argb(row + 4 * k, (uint32_t)px[base + k]);
            else px[base + k] = (int32_t)to_argb(row + 4 * k);
        }
    }
    return true;
}
/* raw premultiplied bytes, for copyPixelsToBuffer / copyPixelsFromBuffer (long bm, byte[] or a buffer's address, boolean set) */
NAT(G_bmRaw)
{
    (void)self; (void)ret;
    gbitmap *b = H(gbitmap, 0);
    uint8_t *d = (uint8_t *)(uintptr_t)a[1].j;
    if (!b || !d) return true;
    if (a[2].z) memcpy(b->px, d, b->stride * (size_t)b->h); else memcpy(d, b->px, b->stride * (size_t)b->h);
    return true;
}
NAT(G_bmAddress) { (void)self; gbitmap *b = H(gbitmap, 0); *ret = J(b ? (int64_t)(uintptr_t)b->px : 0); return true; }

/* (byte[] data, int offset, int length, int sample, int[] outWH, boolean boundsOnly) -> long */
NAT(G_bmDecode)
{
    (void)self;
    uint8_t *d = arr_data(a[0].l); uint32_t len = arr_len(a[0].l);
    int off = a[1].i, n = a[2].i, sample = a[3].i;
    int32_t *wh = arr_data(a[4].l);
    *ret = J(0);
    if (!d || off < 0 || n <= 0 || (uint32_t)off + (uint32_t)n > len) return true;
    CFDataRef data = CFDataCreate(NULL, d + off, n);
    CGImageSourceRef src = CGImageSourceCreateWithData(data, NULL);
    CFRelease(data);
    if (!src) return true;
    if (a[5].z) {
        CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(src, 0, NULL);
        if (props && wh) {
            int w = 0, h = 0;
            CFNumberRef nw = CFDictionaryGetValue(props, kCGImagePropertyPixelWidth), nh = CFDictionaryGetValue(props, kCGImagePropertyPixelHeight);
            if (nw) CFNumberGetValue(nw, kCFNumberIntType, &w);
            if (nh) CFNumberGetValue(nh, kCFNumberIntType, &h);
            wh[0] = w / (sample > 1 ? sample : 1); wh[1] = h / (sample > 1 ? sample : 1);
        }
        if (props) CFRelease(props);
        CFRelease(src);
        return true;
    }
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (!img) return true;
    gbitmap *b = bm_from_image(img, sample);
    CGImageRelease(img);
    if (b && wh) { wh[0] = b->w; wh[1] = b->h; }
    *ret = J((int64_t)(uintptr_t)b);
    return true;
}
/* (long bm, int format 0 jpeg 1 png 2 webp, int quality) -> byte[] */
NAT(G_bmCompress)
{
    (void)self;
    gbitmap *b = H(gbitmap, 0);
    *ret = Ll(NULL);
    if (!b) return true;
    CGImageRef img = bm_image(b);
    CFMutableDataRef out = CFDataCreateMutable(NULL, 0);
    CFStringRef type = a[1].i == 0 ? CFSTR("public.jpeg") : CFSTR("public.png");
    CGImageDestinationRef dst = CGImageDestinationCreateWithData(out, type, 1, NULL);
    if (dst) {
        float q = a[2].i / 100.0f;
        CFNumberRef qn = CFNumberCreate(NULL, kCFNumberFloatType, &q);
        const void *k[] = { kCGImageDestinationLossyCompressionQuality }; const void *v[] = { qn };
        CFDictionaryRef opt = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CGImageDestinationAddImage(dst, img, opt);
        CGImageDestinationFinalize(dst);
        CFRelease(opt); CFRelease(qn); CFRelease(dst);
        jobj *arr = tl_jni_new_prim_array('B', (uint32_t)CFDataGetLength(out));
        memcpy(arr->arr.data, CFDataGetBytePtr(out), (size_t)CFDataGetLength(out));
        *ret = Ll(arr);
    }
    CFRelease(out);
    CGImageRelease(img);
    return true;
}
/* (long src, int x, int y, int w, int h, float[] matrix or null, boolean filter) -> long: a new bitmap of the region, transformed */
NAT(G_bmCopy)
{
    (void)self;
    gbitmap *s = H(gbitmap, 0);
    int x = a[1].i, y = a[2].i, w = a[3].i, h = a[4].i;
    float *m = arr_data(a[5].l);
    *ret = J(0);
    if (!s || w <= 0 || h <= 0) return true;
    CGAffineTransform t = m ? CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5]) : CGAffineTransformIdentity;
    CGRect box = CGRectApplyAffineTransform(CGRectMake(0, 0, w, h), t);
    int nw = (int)lrint(box.size.width), nh = (int)lrint(box.size.height);
    gbitmap *d = bm_new(nw > 0 ? nw : 1, nh > 0 ? nh : 1);
    if (!d) return true;
    CGImageRef full = bm_image(s);
    CGImageRef sub = CGImageCreateWithImageInRect(full, CGRectMake(x, y, w, h));
    CGContextRef c = d->ctx;
    CGContextSaveGState(c);
    CGContextTranslateCTM(c, 0, d->h); CGContextScaleCTM(c, 1, -1);           /* y down */
    CGContextTranslateCTM(c, -box.origin.x, -box.origin.y);
    CGContextConcatCTM(c, t);
    CGContextSetInterpolationQuality(c, a[6].z ? kCGInterpolationHigh : kCGInterpolationNone);
    CGContextTranslateCTM(c, 0, h); CGContextScaleCTM(c, 1, -1);
    CGContextDrawImage(c, CGRectMake(0, 0, w, h), sub);
    CGContextRestoreGState(c);
    CGImageRelease(sub); CGImageRelease(full);
    *ret = J((int64_t)(uintptr_t)d);
    return true;
}

/* ---- Canvas */
NAT(G_cvNew) { (void)self; gbitmap *b = H(gbitmap, 0); *ret = J(b ? (int64_t)(uintptr_t)cv_new(b) : 0); return true; }
NAT(G_cvFree) { (void)self; (void)ret; cv_free(H(gcanvas, 0)); return true; }
NAT(G_cvSave) { (void)self; gcanvas *v = H(gcanvas, 0); *ret = Ii(v ? cv_save(v, false, 1, CGRectNull) : 1); return true; }
/* (long, l, t, r, b, int alpha) */
NAT(G_cvSaveLayer)
{
    (void)self;
    gcanvas *v = H(gcanvas, 0);
    CGRect r = CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f);
    *ret = Ii(v ? cv_save(v, true, a[5].i / 255.0f, r) : 1);
    return true;
}
NAT(G_cvSaveLayerMode)
{
    (void)self;
    gcanvas *v = H(gcanvas, 0);
    CGRect r = CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f);
    *ret = Ii(v ? cv_save_mode(v, true, a[5].i / 255.0f, r, a[6].i) : 1);
    return true;
}
NAT(G_cvRestore) { (void)self; (void)ret; gcanvas *v = H(gcanvas, 0); if (v) cv_restore(v); return true; }
NAT(G_cvRestoreTo) { (void)self; (void)ret; gcanvas *v = H(gcanvas, 0); int n = a[1].i; if (v) while (v->depth + 1 > n && v->depth > 0) cv_restore(v); return true; }
NAT(G_cvSaveCount) { (void)self; gcanvas *v = H(gcanvas, 0); *ret = Ii(v ? v->depth + 1 : 1); return true; }
/* (long, float[6] affine: sx kx tx ky sy ty, boolean set) */
NAT(G_cvMatrix)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); float *m = arr_data(a[1].l);
    if (!v || !m) return true;
    CGAffineTransform t = CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5]);
    if (a[2].z) {
        CGAffineTransform cur = CGContextGetCTM(v->c);
        CGContextConcatCTM(v->c, CGAffineTransformInvert(cur));
        CGContextConcatCTM(v->c, CGAffineTransformConcat(t, v->base));
    } else CGContextConcatCTM(v->c, t);
    return true;
}
NAT(G_cvGetMatrix)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); float *m = arr_data(a[1].l);
    if (!v || !m) return true;
    CGAffineTransform t = CGAffineTransformConcat(CGContextGetCTM(v->c), CGAffineTransformInvert(v->base));
    m[0] = (float)t.a; m[1] = (float)t.c; m[2] = (float)t.tx; m[3] = (float)t.b; m[4] = (float)t.d; m[5] = (float)t.ty;
    return true;
}
/* (long, l, t, r, b, int op: 1 intersect, 0 difference) -> boolean */
NAT(G_cvClipRect)
{
    (void)self;
    gcanvas *v = H(gcanvas, 0);
    *ret = Ii(1);
    if (!v) return true;
    CGRect r = CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f);
    CGAffineTransform m = CGContextGetCTM(v->c);
    bool diff = a[5].i == 0, shaped = diff || fabs(m.b) > 1e-6 || fabs(m.c) > 1e-6;
    clip_record(v, NULL, r, false, diff);
    if (!diff) v->cb = CGRectIntersection(v->cb, CGRectStandardize(CGRectApplyAffineTransform(r, m)));
    if (shaped) { g_cs_shaped_rects++; CGPathRef rp = CGPathCreateWithRect(r, NULL); clip_mask_add(v, rp, false, diff); CGPathRelease(rp); }
    *ret = Ii(!CGRectIsNull(v->cb) && v->cb.size.width > 0 && v->cb.size.height > 0);
    return true;
}
/* (long, float[] path, int n, int fill (1 even-odd), int op) */
NAT(G_cvClipPath)
{
    (void)self;
    gcanvas *v = H(gcanvas, 0); float *f = arr_data(a[1].l);
    *ret = Ii(1);
    if (!v || !f) return true;
    CGPathRef p = build_path(f, a[2].i);
    bool diff = a[4].i == 0;
    clip_record(v, p, CGRectNull, a[3].i == 1, diff);
    if (!diff) {
        CGRect bb = CGPathIsEmpty(p) ? CGRectNull : CGPathGetPathBoundingBox(p);
        v->cb = CGRectIsNull(bb) ? CGRectNull : CGRectIntersection(v->cb, CGRectStandardize(CGRectApplyAffineTransform(bb, CGContextGetCTM(v->c))));
    }
    clip_mask_add(v, p, a[3].i == 1, diff);
    /* one rounded rectangle, rectangle or oval (Compose's shapes): worked out instead of filled */
    int n = a[2].i;
    if (!diff && n == 14 && (int)f[0] == OP_RRECT8) clip_mask_rrect(v, f[1], f[2], f[3], f[4], f + 5);
    else if (!diff && n == 8 && (int)f[0] == OP_RRECT) { float rad[8]; for (int i = 0; i < 8; i++) rad[i] = f[5 + (i & 1)]; clip_mask_rrect(v, f[1], f[2], f[3], f[4], rad); }
    else if (!diff && n == 6 && (int)f[0] == OP_RECT) { float rad[8] = { 0 }; clip_mask_rrect(v, f[1], f[2], f[3], f[4], rad); }
    else if (!diff && n == 6 && (int)f[0] == OP_OVAL) {
        float rad[8]; for (int i = 0; i < 8; i++) rad[i] = (i & 1) ? fabsf(f[4] - f[2]) / 2 : fabsf(f[3] - f[1]) / 2;
        clip_mask_rrect(v, f[1], f[2], f[3], f[4], rad);
    }
    CGPathRelease(p);
    *ret = Ii(!CGRectIsNull(v->cb) && v->cb.size.width > 0 && v->cb.size.height > 0);
    return true;
}
NAT(G_cvClipBounds)
{
    (void)self;
    gcanvas *v = H(gcanvas, 0); int32_t *o = arr_data(a[1].l);
    *ret = Ii(0);
    if (!v || !o) return true;
    CGRect r = clip_user(v);
    if (CGRectIsNull(r) || CGRectIsEmpty(r)) { o[0] = o[1] = o[2] = o[3] = 0; return true; }
    o[0] = (int32_t)floor(r.origin.x + 1e-3); o[1] = (int32_t)floor(r.origin.y + 1e-3);
    o[2] = (int32_t)ceil(r.origin.x + r.size.width - 1e-3); o[3] = (int32_t)ceil(r.origin.y + r.size.height - 1e-3);
    *ret = Ii(1);
    return true;
}
/* (long, int color, int mode) */
NAT(G_cvDrawColor)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v) return true;
    CGContextRef c = v->c;
    int mode = a[2].i;
    if (mode == 3 && blend_fill(v, clip_user(v), (uint32_t)a[1].i, false)) return true;
    cv_flush(v);
    CGContextSaveGState(c);
    CGRect all = CGContextGetClipBoundingBox(c);
    if (mode == 0) CGContextClearRect(c, all);
    else {
        CGColorRef col = color_of((uint32_t)a[1].i, 1);
        CGContextSetFillColorWithColor(c, col);
        CGColorRelease(col);
        CGContextSetBlendMode(c, blend_of(mode));
        CGContextFillRect(c, all);
    }
    CGContextRestoreGState(c);
    return true;
}
static int64_t g_gs_fast, g_gs_cg;      /* HUSK_FRAME_STATS: shaded draws done here, given to CoreGraphics */
static bool grad_fill(gcanvas *v, CGRect r, gshader *s, uint32_t argb, bool aa);
static bool ring_fill(gcanvas *v, CGRect outer, CGRect inner, gshader *sh, uint32_t argb, bool aa);
/* shapes: (long, l, t, r, b, [extra...], int[] paint, long shader) */
static void shape(gcanvas *v, CGPathRef p, jobj *paint, int64_t shader)
{
    if (!shader && mask_draw(v, p, paint_of(paint), false)) return;
    if (shader) g_gs_cg++;
    draw_path(cg(v), p, paint_of(paint), (gshader *)(uintptr_t)shader, false);
}
/* HUSK_FRAME_STATS: drawRect by the way it went, with its time */
enum { DR_FILL, DR_FILL_CG, DR_GRAD, DR_STROKE, DR_SH_KIND, DR_SH_PAINT, DR_SH_LAYER, DR_PAINT, DR_COUNT };
static const char *const k_dr_name[DR_COUNT] = { "colour here", "colour to CoreGraphics", "gradient here", "outline here", "shader not linear",
    "shader, paint not plain", "shader in a layer or turned", "paint not plain" };
static int64_t g_dr_n[DR_COUNT], g_dr_ns[DR_COUNT], g_dr_px[DR_COUNT];
static int g_dr_last_xfer = -1, g_dr_last_cf = -1, g_dr_last_kind = -1;
static int draw_rect(gcanvas *v, CGRect r, jobj *paint, int64_t shader)
{
    const int32_t *p = paint_of(paint);
    bool plain = p[P_STYLE] == 0 && p[P_CFMODE] < 0 && pf(p, P_SHRAD) <= 0 && p[P_XFER] == 3;
    if (!plain) { g_dr_last_xfer = p[P_XFER]; g_dr_last_cf = p[P_CFMODE]; }
    if (!shader && plain) {
        if (blend_fill(v, r, (uint32_t)p[P_COLOR], (p[P_FLAGS] & PF_AA) != 0)) return DR_FILL;
        /* the common case, without a path */
        cv_flush(v);
        CGContextSaveGState(v->c);
        CGColorRef cc = color_of((uint32_t)p[P_COLOR], 1);
        CGContextSetFillColorWithColor(v->c, cc);
        CGColorRelease(cc);
        CGContextSetShouldAntialias(v->c, (p[P_FLAGS] & PF_AA) != 0);
        CGContextFillRect(v->c, r);
        CGContextRestoreGState(v->c);
        return DR_FILL_CG;
    }
    gshader *sh = (gshader *)(uintptr_t)shader;
    if (sh && plain && grad_fill(v, r, sh, (uint32_t)p[P_COLOR], (p[P_FLAGS] & PF_AA) != 0)) return DR_GRAD;
    /* stroked, mitred (a rectangle's corners are right angles: within any miter limit Android takes): the outline's band */
    float sw = pf(p, P_STROKE);
    if (p[P_STYLE] != 0 && p[P_JOIN] == 0 && sw > 0 && p[P_CFMODE] < 0 && pf(p, P_SHRAD) <= 0 && p[P_XFER] == 3) {
        CGRect rs = CGRectStandardize(r);
        CGRect outer = CGRectInset(rs, -sw / 2, -sw / 2), inner = CGRectInset(rs, sw / 2, sw / 2);
        if (p[P_STYLE] == 2 || inner.size.width <= 0 || inner.size.height <= 0 || CGRectIsNull(inner)) inner = CGRectZero;
        if (ring_fill(v, outer, inner, sh, (uint32_t)p[P_COLOR], (p[P_FLAGS] & PF_AA) != 0)) return DR_STROKE;
    }
    int why = !sh ? DR_PAINT : !plain ? DR_SH_PAINT : sh->kind != SH_LINEAR ? DR_SH_KIND : DR_SH_LAYER;
    if (sh) g_dr_last_kind = sh->kind;
    CGPathRef path = CGPathCreateWithRect(r, NULL);
    shape(v, path, paint, shader);
    CGPathRelease(path);
    return why;
}
NAT(G_cvDrawRect)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v) return true;
    CGRect r = CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f);
    int64_t t0 = now_ns();
    int k = draw_rect(v, r, a[5].l, a[6].j);
    g_dr_n[k]++; g_dr_ns[k] += now_ns() - t0;
    CGAffineTransform m = CGContextGetCTM(v->c);
    g_dr_px[k] += (int64_t)fabs(r.size.width * r.size.height * (m.a * m.d - m.b * m.c));
    return true;
}
/* ---- grad_fill: a rectangle filled with a linear gradient, as blend_fill fills one with a colour (Compose's scrims and
 * fades, Brush.verticalGradient over the whole screen: CoreGraphics draws the gradient and blends it a pixel at a time, some
 * 3 ms a screen). t is affine in device space, so a row is a start and a step; the colours come from a table of 256. */
static const uint32_t *grad_lut(gshader *s, int alpha)
{
    if (!s->lut) { s->lut = malloc(256 * sizeof(uint32_t)); if (!s->lut) return NULL; s->lut_alpha = -1; }
    if (s->lut_alpha != alpha) {
        for (int i = 0; i < 256; i++) { uint32_t inv; s->lut[i] = premul(sweep_at(s, i / 255.0f), alpha / 255.0f, &inv); }
        s->lut_alpha = alpha;
    }
    return s->lut;
}
static inline int grad_index(float t, int tile)
{
    if (tile == 1) t -= floorf(t);
    else if (tile == 2) { t = fabsf(t); float w = t - 2 * floorf(t / 2); t = w > 1 ? 2 - w : w; }
    if (!(t > 0)) return 0;             /* NaN too */
    if (t >= 1) return 255;
    return (int)(t * 255 + 0.5f);
}
static bool grad_fill(gcanvas *v, CGRect r, gshader *s, uint32_t argb, bool aa)
{
    CGAffineTransform m;
    if (s->kind != SH_LINEAR || !blend_ok(v, &m)) return false;
    gbitmap *b = v->bm;
    /* device (x right, y down from the top, pixel centres) to the gradient's space */
    CGAffineTransform full = CGAffineTransformConcat(s->local, m);
    full = CGAffineTransformConcat(full, CGAffineTransformMake(1, 0, 0, -1, 0, b->h));
    if (fabs(full.a * full.d - full.b * full.c) < 1e-12) return false;
    CGAffineTransform inv = CGAffineTransformInvert(full);
    float gx = s->x1 - s->x0, gy = s->y1 - s->y0, len2 = gx * gx + gy * gy;
    if (len2 < 1e-9f) return false;
    /* t(x, y) = ((inv(x, y)) - p0) . g / |g|^2 = tx * x + ty * y + t0 */
    float tx = (float)(inv.a * gx + inv.b * gy) / len2, ty = (float)(inv.c * gx + inv.d * gy) / len2;
    float t0 = (float)((inv.tx - s->x0) * gx + (inv.ty - s->y0) * gy) / len2;
    int alpha = (int)(argb >> 24);
    const uint32_t *lut = grad_lut(s, alpha);
    if (!lut) return false;
    CGRect d = CGRectStandardize(CGRectApplyAffineTransform(CGRectStandardize(r), m));
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip)) return true;
    d = CGRectIntersection(d, CGRectStandardize(CGRectApplyAffineTransform(clip, m)));
    d = CGRectIntersection(d, CGRectMake(0, 0, b->w, b->h));
    if (CGRectIsNull(d) || d.size.width <= 0 || d.size.height <= 0 || !alpha) return true;
    double x0 = d.origin.x, x1 = x0 + d.size.width, y0 = b->h - (d.origin.y + d.size.height), y1 = b->h - d.origin.y;
    if (!aa) { x0 = ceil(x0 - 0.5); x1 = ceil(x1 - 0.5); y0 = ceil(y0 - 0.5); y1 = ceil(y1 - 0.5); }
    int ix0 = (int)floor(x0), ix1 = (int)ceil(x1), iy0 = (int)floor(y0), iy1 = (int)ceil(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > b->w) ix1 = b->w;
    if (iy1 > b->h) iy1 = b->h;
    if (ix0 >= ix1 || iy0 >= iy1) return true;
    int mx0 = ix0, mx1 = ix1, my0 = iy0, my1 = iy1;
    bool masked = clip_need(v, ix0, ix1, iy0, iy1);
    if (masked) { if (!clip_box(v, &mx0, &mx1, &my0, &my1)) return true; }
    else g_cc_on = false;
    int tile = s->tile;
    /* the first and last columns' coverage, where a pixel is partly covered */
    uint32_t cl8 = (uint32_t)lrintf((float)(fmin(ix0 + 1, x1) - fmax(ix0, x0)) * 255);
    uint32_t cr8 = (uint32_t)lrintf((float)(fmin(ix1, x1) - fmax(ix1 - 1, x0)) * 255);
    if (cl8 > 255) cl8 = 255;
    if (cr8 > 255) cr8 = 255;
    if (ix1 - ix0 == 1 && cr8 < cl8) cl8 = cr8;
    int fx0 = mx0, fx1 = mx1;               /* the columns wholly covered across */
    if (cl8 < 255 && ix0 >= mx0) fx0 = ix0 + 1;
    if (cr8 < 255 && ix1 - 1 < mx1 && ix1 - 1 >= fx0) fx1 = ix1 - 1;
    /* a gradient along y alone (the usual scrim) is one colour a row */
    bool rowwise = fabsf(tx) * (float)(ix1 - ix0) < 0.5f / 255;
    par_rows((int64_t)(mx1 - mx0) * (my1 - my0), my0, my1, ^(int j0_, int j1_) {
    for (int j = j0_; j < j1_; j++) {
        float cy = (float)(fmin(j + 1, y1) - fmax(j, y0));
        if (cy <= 0) continue;
        uint32_t cy8 = cy >= 0.999f ? 255 : (uint32_t)lrintf(cy * 255);
        uint32_t *row = (uint32_t *)(b->px + (size_t)j * b->stride);
        const uint8_t *mk = clip_at(v, mx0, j);
        const uint8_t *m0 = mk ? mk - mx0 : NULL;
        float tj = t0 + ty * (j + 0.5f);
        if (rowwise && !m0) {
            uint32_t c = lut[grad_index(tj + tx * (mx0 + 0.5f), tile)];
            if (cy8 != 255) c = scale_px(c, cy8);
            if (fx1 > fx0) blend_run(row + fx0, fx1 - fx0, c, 255 - (c >> 24));
        } else {
            float t = tj + tx * (fx0 + 0.5f);
            for (int i = fx0; i < fx1; i++, t += tx) {
                uint32_t c = lut[grad_index(t, tile)];
                uint32_t k = cy8;
                if (m0) k = (k * m0[i] + 127) / 255;
                if (k != 255) c = scale_px(c, k);
                row[i] = over(row[i], c);
            }
        }
        for (int e = 0; e < 2; e++) {
            int i = e ? ix1 - 1 : ix0;
            uint32_t ce = e ? cr8 : cl8;
            if (ce >= 255 || i < mx0 || i >= mx1 || (e && i == ix0)) continue;
            uint32_t k = (cy8 * ce + 127) / 255;
            if (m0) k = (k * m0[i] + 127) / 255;
            row[i] = over(row[i], scale_px(lut[grad_index(tj + tx * (i + 0.5f), tile)], k));
        }
    }
    });
    g_gs_fast++;
    return true;
}
/* ---- ring_fill: a rectangle's outline, the outer rectangle less the inner, in a colour or a linear gradient (a stroked rectangle
 * with mitred corners: Compose's Modifier.border round a whole screen, DepCalc's gaming mode's turning rainbow every frame, which
 * CoreGraphics filled through a mask of the outline the size of the screen). A box filter's coverage of a rectangle is its rows'
 * times its columns', and the inner lies in the outer: a pixel's is the outer's less the inner's, so only the band is visited. */
static inline float span_cover(int i, double a, double b) { double c = fmin(i + 1, b) - fmax(i, a); return c > 0 ? (float)c : 0; }
static bool ring_fill(gcanvas *v, CGRect outer, CGRect inner, gshader *sh, uint32_t argb, bool aa)
{
    CGAffineTransform m;
    if ((sh && sh->kind != SH_LINEAR) || !blend_ok(v, &m)) return false;
    gbitmap *b = v->bm;
    float tx = 0, ty = 0, t0 = 0;
    const uint32_t *lut = NULL;
    int alpha = (int)(argb >> 24);
    if (sh) {
        CGAffineTransform full = CGAffineTransformConcat(CGAffineTransformConcat(sh->local, m), CGAffineTransformMake(1, 0, 0, -1, 0, b->h));
        if (fabs(full.a * full.d - full.b * full.c) < 1e-12) return false;
        CGAffineTransform inv = CGAffineTransformInvert(full);
        float gx = sh->x1 - sh->x0, gy = sh->y1 - sh->y0, len2 = gx * gx + gy * gy;
        if (len2 < 1e-9f) return false;
        tx = (float)(inv.a * gx + inv.b * gy) / len2; ty = (float)(inv.c * gx + inv.d * gy) / len2;
        t0 = (float)((inv.tx - sh->x0) * gx + (inv.ty - sh->y0) * gy) / len2;
        if (!(lut = grad_lut(sh, alpha))) return false;
    }
    uint32_t inv_unused, s_full = premul(argb, 1, &inv_unused);
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip) || !alpha) return true;
    CGRect cd = CGRectStandardize(CGRectApplyAffineTransform(clip, m));
    CGRect bounds = CGRectIntersection(cd, CGRectMake(0, 0, b->w, b->h));
    /* both rectangles in device space, cut to the clip's bounds, rows from the top */
    double o[4], n[4] = { 0, 0, 0, 0 };     /* x0 x1 y0 y1 */
    CGRect od = CGRectIntersection(CGRectStandardize(CGRectApplyAffineTransform(CGRectStandardize(outer), m)), bounds);
    if (CGRectIsNull(od) || od.size.width <= 0 || od.size.height <= 0) return true;
    o[0] = od.origin.x; o[1] = o[0] + od.size.width; o[2] = b->h - (od.origin.y + od.size.height); o[3] = b->h - od.origin.y;
    bool hole = inner.size.width > 0 && inner.size.height > 0;
    if (hole) {
        CGRect id = CGRectIntersection(CGRectStandardize(CGRectApplyAffineTransform(CGRectStandardize(inner), m)), bounds);
        hole = !CGRectIsNull(id) && id.size.width > 0 && id.size.height > 0;
        if (hole) { n[0] = id.origin.x; n[1] = n[0] + id.size.width; n[2] = b->h - (id.origin.y + id.size.height); n[3] = b->h - id.origin.y; }
    }
    if (!aa) for (int k = 0; k < 4; k++) { o[k] = ceil(o[k] - 0.5); n[k] = ceil(n[k] - 0.5); }
    int ix0 = (int)floor(o[0]), ix1 = (int)ceil(o[1]), iy0 = (int)floor(o[2]), iy1 = (int)ceil(o[3]);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > b->w) ix1 = b->w;
    if (iy1 > b->h) iy1 = b->h;
    if (ix0 >= ix1 || iy0 >= iy1) return true;
    int mx0 = ix0, mx1 = ix1, my0 = iy0, my1 = iy1;
    if (clip_need(v, ix0, ix1, iy0, iy1)) { if (!clip_box(v, &mx0, &mx1, &my0, &my1)) return true; }
    else g_cc_on = false;
    /* each column's coverage by the outer rectangle and by the hole, worked out once: a row's is theirs times its own */
    static float *cov; static int cov_cap;
    int W = mx1 - mx0;
    if (2 * W > cov_cap) { free(cov); cov_cap = 2 * W + 256; cov = malloc(sizeof(float) * (size_t)cov_cap); if (!cov) { cov_cap = 0; return false; } }
    float *co = cov - mx0, *ci = cov + W - mx0;
    for (int i = mx0; i < mx1; i++) { co[i] = span_cover(i, o[0], o[1]); ci[i] = hole ? span_cover(i, n[0], n[1]) : 0; }
    /* the hole's wholly covered columns: a row that is wholly in the hole leaves them alone */
    int hx0 = hole ? (int)ceil(n[0]) : 0, hx1 = hole ? (int)floor(n[1]) : 0;
    if (hx0 < mx0) hx0 = mx0;
    if (hx1 > mx1) hx1 = mx1;
    int tile = sh ? sh->tile : 0;
    const double *O = o, *N = n;
    par_rows((int64_t)(mx1 - mx0) * (my1 - my0) / 8, my0, my1, ^(int j0_, int j1_) {
    for (int j = j0_; j < j1_; j++) {
        float cyo = span_cover(j, O[2], O[3]);
        if (cyo <= 0) continue;
        float cyi = hole ? span_cover(j, N[2], N[3]) : 0;
        uint32_t *row = (uint32_t *)(b->px + (size_t)j * b->stride);
        const uint8_t *mk = clip_at(v, mx0, j);
        const uint8_t *m0 = mk ? mk - mx0 : NULL;
        float tj = t0 + ty * (j + 0.5f);
        /* a row in the hole top to bottom skips its middle */
        bool skip = hole && cyi >= 0.999f && cyo >= 0.999f && hx1 > hx0;
        for (int seg = 0; seg < (skip ? 2 : 1); seg++) {
            int a = skip ? (seg ? hx1 : mx0) : mx0, z = skip ? (seg ? mx1 : hx0) : mx1;
            float t = tj + tx * (a + 0.5f);
            for (int i = a; i < z; i++, t += tx) {
                float c = co[i] * cyo - ci[i] * cyi;
                if (c <= 0.002f) continue;
                uint32_t k = c >= 0.998f ? 255 : (uint32_t)(c * 255 + 0.5f);
                if (m0) k = (k * m0[i] + 127) / 255;
                if (!k) continue;
                uint32_t px = lut ? lut[grad_index(t, tile)] : s_full;
                row[i] = over(row[i], k == 255 ? px : scale_px(px, k));
            }
        }
    }
    });
    g_gs_fast++;
    return true;
}
/* ---- rrect_fill: a rounded rectangle filled as blend_fill fills a rectangle, its corners' coverage from the distance to
 * their ellipses (Compose's buttons, circles and cards; CoreGraphics blends a translucent one a pixel at a time) */
static bool rrect_fill(gcanvas *v, CGRect r, float rx, float ry, uint32_t argb, bool aa)
{
    CGAffineTransform m;
    if (!blend_ok(v, &m)) return false;
    gbitmap *b = v->bm;
    CGRect d = CGRectStandardize(CGRectApplyAffineTransform(CGRectStandardize(r), m));
    float srx = (float)fabs(rx * m.a), sry = (float)fabs(ry * m.d);
    float x0 = (float)d.origin.x, x1 = x0 + (float)d.size.width;
    float y0 = (float)(b->h - (d.origin.y + d.size.height)), y1 = (float)(b->h - d.origin.y);
    if (srx > (x1 - x0) / 2) srx = (x1 - x0) / 2;
    if (sry > (y1 - y0) / 2) sry = (y1 - y0) / 2;
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip) || !(argb >> 24)) return true;
    CGRect cd = CGRectStandardize(CGRectApplyAffineTransform(clip, m));
    /* the pixels the shape may touch, inside the clip (its edges at pixel centres) */
    int ix0 = (int)floor(fmax(x0, ceil(cd.origin.x - 0.5))), ix1 = (int)ceil(fmin(x1, ceil(cd.origin.x + cd.size.width - 0.5)));
    int iy0 = (int)floor(fmax(y0, ceil(b->h - (cd.origin.y + cd.size.height) - 0.5))), iy1 = (int)ceil(fmin(y1, ceil(b->h - cd.origin.y - 0.5)));
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > b->w) ix1 = b->w;
    if (iy1 > b->h) iy1 = b->h;
    if (ix0 >= ix1 || iy0 >= iy1) return true;
    uint32_t inv_full, s_full = premul(argb, 1, &inv_full);
    int X0 = ix0, X1 = ix1, Y0 = iy0, Y1 = iy1;
    if (clip_need(v, ix0, ix1, iy0, iy1)) { if (!clip_box(v, &X0, &X1, &Y0, &Y1)) return true; }
    else g_cc_on = false;
    /* Each row: the pixels it wholly covers (one coverage, the row's: blended as a run), the ones it may partly cover (each its
     * own, from the distance to the corner's ellipse), none past them. A circle is corner rows top to bottom: it was a square
     * root and a division a pixel, over all of it (Compose's round buttons, 20 of them on a calculator). */
    par_rows((int64_t)(X1 - X0) * (Y1 - Y0), Y0, Y1, ^(int j0_, int j1_) {
    for (int j = j0_; j < j1_; j++) {
        uint32_t *row = (uint32_t *)(b->px + (size_t)j * b->stride);
        const uint8_t *mk = clip_at(v, X0, j);
        const uint8_t *m0 = mk ? mk - X0 : NULL;
        float py = j + 0.5f;
        float cy = clamp01(fminf(py - y0, y1 - py) + 0.5f);
        if (!aa) cy = cy >= 0.5f ? 1 : 0;
        if (cy <= 0) continue;
        int o0 = X0, o1 = X1, f0, f1;
        bool top = py < y0 + sry, bot = py > y1 - sry;
        if (!(top || bot) || srx <= 0 || sry <= 0) { f0 = (int)ceilf(x0); f1 = (int)floorf(x1); }
        else {
            /* the ellipse's half width where the row is farthest from its centre (wholly covered within) and nearest (none past) */
            float ecy = top ? y0 + sry : y1 - sry;
            float yfar = top ? fmaxf((float)j, y0) : fminf((float)(j + 1), y1), ynear = top ? fminf((float)(j + 1), ecy) : fmaxf((float)j, ecy);
            float df = (yfar - ecy) / sry, dn = (ynear - ecy) / sry;
            float wf = df * df < 1 ? srx * sqrtf(1 - df * df) : 0, wn = dn * dn < 1 ? srx * sqrtf(1 - dn * dn) : 0;
            f0 = (int)ceilf(x0 + srx - wf); f1 = (int)floorf(x1 - srx + wf);
            int n0 = (int)floorf(x0 + srx - wn) - 1, n1 = (int)ceilf(x1 - srx + wn) + 1;
            if (o0 < n0) o0 = n0;
            if (o1 > n1) o1 = n1;
        }
        if (o0 >= o1) continue;
        if (f0 < o0) f0 = o0;
        if (f1 > o1) f1 = o1;
        if (f1 <= f0) f0 = f1 = o1;
        /* the middle */
        if (f1 > f0) {
            uint32_t cinv, cs;
            if (cy >= 0.999f) { cs = s_full; cinv = inv_full; } else cs = premul(argb, cy, &cinv);
            if (!m0) blend_run(row + f0, f1 - f0, cs, cinv);
            else for (int q = f0; q < f1; q++) row[q] = over(row[q], scale_px(cs, m0[q]));
        }
        /* the edges, a pixel at a time */
        for (int e = 0; e < 2; e++) {
            int a0 = e ? f1 : o0, a1 = e ? o1 : f0;
            for (int q = a0; q < a1; q++) {
                float c = rrect_cover(q + 0.5f, py, x0, y0, x1, y1, srx, sry);
                if (!aa) c = c >= 0.5f ? 1 : 0;
                if (c <= 0) continue;
                uint32_t c8 = c >= 0.999f ? 255 : (uint32_t)lrintf(c * 255);
                if (m0) c8 = (c8 * m0[q] + 127) / 255;
                if (c8) row[q] = over(row[q], c8 == 255 ? s_full : scale_px(s_full, c8));
            }
        }
    }
    });
    return true;
}

NAT(G_cvDrawRRect)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v) return true;
    const int32_t *pp = paint_of(a[7].l);
    if (!a[8].j && pp[P_STYLE] == 0 && pp[P_CFMODE] < 0 && pf(pp, P_SHRAD) <= 0 && pp[P_XFER] == 3
        && rrect_fill(v, CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f), a[5].f, a[6].f, (uint32_t)pp[P_COLOR], (pp[P_FLAGS] & PF_AA) != 0))
        return true;
    float rad[8]; for (int i = 0; i < 8; i++) rad[i] = (i & 1) ? a[6].f : a[5].f;
    CGMutablePathRef p = CGPathCreateMutable();
    add_rrect8(p, NULL, a[1].f, a[2].f, a[3].f, a[4].f, rad, false);
    shape(v, p, a[7].l, a[8].j);
    CGPathRelease(p);
    return true;
}
NAT(G_cvDrawOval)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v) return true;
    CGPathRef p = CGPathCreateWithEllipseInRect(CGRectMake(a[1].f, a[2].f, a[3].f - a[1].f, a[4].f - a[2].f), NULL);
    shape(v, p, a[5].l, a[6].j);
    CGPathRelease(p);
    return true;
}
/* (long, l, t, r, b, float start, float sweep, boolean useCenter, int[] paint, long shader) */
NAT(G_cvDrawArc)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v) return true;
    CGMutablePathRef p = CGPathCreateMutable();
    float l = a[1].f, t = a[2].f, r = a[3].f, b = a[4].f;
    if (a[7].z) {
        CGPathMoveToPoint(p, NULL, (l + r) / 2, (t + b) / 2);
        add_arc(p, l, t, r, b, a[5].f, a[6].f, false);
        CGPathCloseSubpath(p);
    } else add_arc(p, l, t, r, b, a[5].f, a[6].f, true);
    shape(v, p, a[8].l, a[9].j);
    CGPathRelease(p);
    return true;
}
/* (long, float[] path, int n, int fill, int[] paint, long shader) */
NAT(G_cvDrawPath)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); float *f = arr_data(a[1].l);
    if (!v || !f) return true;
    CGPathRef p = build_path(f, a[2].i);
    if (a[5].j || !mask_draw(v, p, paint_of(a[4].l), a[3].i == 1))
        draw_path(cg(v), p, paint_of(a[4].l), (gshader *)(uintptr_t)a[5].j, a[3].i == 1);
    CGPathRelease(p);
    return true;
}
/* (long, float[] pts, int offset, int count (floats), int[] paint, long shader, boolean points) */
NAT(G_cvDrawLines)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); float *f = arr_data(a[1].l);
    if (!v || !f) return true;
    int off = a[2].i, n = a[3].i;
    if (off < 0 || (uint32_t)(off + n) > arr_len(a[1].l)) return true;
    const int32_t *p = paint_of(a[4].l);
    int32_t q[P_COUNT]; memcpy(q, p, sizeof(q));
    q[P_STYLE] = 1;
    CGMutablePathRef path = CGPathCreateMutable();
    if (a[6].z) {
        float sw = pf(p, P_STROKE) > 0 ? pf(p, P_STROKE) : 1;
        q[P_STYLE] = 0;
        for (int i = off; i + 1 < off + n; i += 2) {
            if (p[P_CAP] == 1) CGPathAddEllipseInRect(path, NULL, CGRectMake(f[i] - sw / 2, f[i + 1] - sw / 2, sw, sw));
            else CGPathAddRect(path, NULL, CGRectMake(f[i] - sw / 2, f[i + 1] - sw / 2, sw, sw));
        }
    } else for (int i = off; i + 3 < off + n; i += 4) { CGPathMoveToPoint(path, NULL, f[i], f[i + 1]); CGPathAddLineToPoint(path, NULL, f[i + 2], f[i + 3]); }
    draw_path(cg(v), path, q, (gshader *)(uintptr_t)a[5].j, false);
    CGPathRelease(path);
    return true;
}
/* (long, long bm, sl, st, sr, sb, dl, dt, dr, db, int[] paint or null) */
/* ---- blit: a bitmap (or a part) drawn to a rectangle of the canvas's bitmap by Husk, not by CoreGraphics, which costs
 * 4 ns a pixel on a phone (CGImage made, scaled, blended): DepCalc's tiled wallpaper alone took 36 ms a frame. Where
 * blend_ok allows, with a plain paint; each pixel whose centre the rectangle covers is the source's there, bilinear when
 * the paint filters (Compose's FilterQuality.Low), so tiles laid edge to edge leave no seam. */
static inline uint32_t bilerp(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, uint32_t fx, uint32_t fy)
{
    /* weights out of 256, two channels a lane */
    uint32_t w11 = (fx * fy + 128) >> 8, w10 = fx - w11, w01 = fy - w11, w00 = 256 - fx - fy + w11;
    uint32_t rb = (p00 & 0x00FF00FFu) * w00 + (p10 & 0x00FF00FFu) * w10 + (p01 & 0x00FF00FFu) * w01 + (p11 & 0x00FF00FFu) * w11;
    uint32_t ag = ((p00 >> 8) & 0x00FF00FFu) * w00 + ((p10 >> 8) & 0x00FF00FFu) * w10 + ((p01 >> 8) & 0x00FF00FFu) * w01 + ((p11 >> 8) & 0x00FF00FFu) * w11;
    rb += 0x00800080u; ag += 0x00800080u;
    return ((rb >> 8) & 0x00FF00FFu) | (ag & 0xFF00FF00u);
}
/* HUSK_FRAME_STATS (husk.DrawStats via Gfx.blitStats): bitmaps drawn here, how, and why the rest went to CoreGraphics */
enum { BS_HERE, BS_EXACT_PX, BS_SCALED_PX, BS_NS, BS_PAINT, BS_LAYER, BS_CLIP, BS_TURNED, BS_SHRUNK, BS_COUNT };
static int64_t g_bs[BS_COUNT];
static int64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
static bool blit_(gcanvas *v, gbitmap *src, float sl, float st, float sr, float sb, float dl, float dt, float dr, float db,
                  const int32_t *p);
static bool blit(gcanvas *v, gbitmap *src, float sl, float st, float sr, float sb, float dl, float dt, float dr, float db,
                 const int32_t *p)
{
    int64_t t0 = now_ns();
    bool r = blit_(v, src, sl, st, sr, sb, dl, dt, dr, db, p);
    if (r) { g_bs[BS_HERE]++; g_bs[BS_NS] += now_ns() - t0; }
    return r;
}
/* a bitmap scaled once, kept for the next draws of it at that size (a tiled wallpaper draws one tile tens of times a frame,
 * every frame): its pixels checked again, by a hash, once a frame */
typedef struct gscache {
    float sl, st, sr, sb;
    int w, h;
    bool flipx, flipy, filter;
    uint64_t hash, frame;
    uint32_t *px;
} gscache;
static uint64_t g_frame = 1;          /* frames presented (G_present) */
static pthread_t g_presenter;         /* the thread presenting them: within one of its frames a bitmap is taken to stay as it was */
static void bm_cache_free(gbitmap *b) { if (b->cache) { free(b->cache->px); free(b->cache); b->cache = NULL; } }
static uint64_t src_hash(const gbitmap *s, int x0, int y0, int x1, int y1)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int y = y0; y < y1; y++) {
        const uint32_t *r = (const uint32_t *)(s->px + (size_t)y * s->stride);
        uint64_t a = 0, b = 0;
        for (int x = x0; x < x1; x++) { a += r[x]; b ^= (uint64_t)r[x] << (x & 31); }
        h = (h ^ a) * 0x100000001b3ull; h = (h ^ b) * 0x100000001b3ull;
    }
    return h;
}
/* source pixels for the destination's W x H, bilinear or nearest; NULL without memory */
static const uint32_t *scaled(gbitmap *src, float sl, float st, float sr, float sb, int W, int H, bool flipx, bool flipy, bool filter)
{
    int hx0 = (int)floor(sl), hy0 = (int)floor(st), hx1 = (int)ceil(sr), hy1 = (int)ceil(sb);
    if (hx0 < 0) hx0 = 0;
    if (hy0 < 0) hy0 = 0;
    if (hx1 > src->w) hx1 = src->w;
    if (hy1 > src->h) hy1 = src->h;
    gscache *k = src->cache;
    bool same = k && k->sl == sl && k->st == st && k->sr == sr && k->sb == sb && k->w == W && k->h == H
             && k->flipx == flipx && k->flipy == flipy && k->filter == filter;
    /* other threads (an app's GL or video one, posting frames into the same bitmap) change a bitmap more often than that */
    if (same && k->frame == g_frame && pthread_equal(pthread_self(), g_presenter)) return k->px;
    uint64_t hash = src_hash(src, hx0, hy0, hx1, hy1);
    if (same && k->hash == hash) { k->frame = g_frame; return k->px; }
    if (!k) { k = src->cache = calloc(1, sizeof *k); if (!k) return NULL; }
    if (k->w * k->h != W * H || !k->px) { free(k->px); k->px = malloc((size_t)W * H * 4); if (!k->px) { k->w = k->h = 0; return NULL; } }
    k->sl = sl; k->st = st; k->sr = sr; k->sb = sb; k->w = W; k->h = H; k->flipx = flipx; k->flipy = flipy; k->filter = filter;
    k->hash = hash; k->frame = g_frame;
    double kx = (sr - sl) / W, ky = (sb - st) / H;
    int smaxx = src->w - 1, smaxy = src->h - 1;
    uint32_t *out = k->px;
    /* Much smaller, filtered: each pixel the average of the source pixels it covers (a box filter), as Android's scaled draws
     * look. Telegram's blurs draw a screen's worth shrunk 4 or 8 times every frame; CoreGraphics' filtering took 4 to 6 ms of it. */
    if (filter && (kx > 1.5 || ky > 1.5)) {
        int32_t *xa = malloc((size_t)(W + 1) * sizeof *xa);
        if (!xa) return NULL;
        for (int c = 0; c <= W; c++) {
            int x = (int)lrint(sl + c * kx);
            xa[c] = x < hx0 ? hx0 : x > hx1 ? hx1 : x;
        }
        par_rows((int64_t)(hx1 - hx0) * (hy1 - hy0), 0, H, ^(int r0, int r1) {
            for (int r = r0; r < r1; r++) {
                int rr = flipy ? H - 1 - r : r;
                int ya = (int)lrint(st + rr * ky), yb = (int)lrint(st + (rr + 1) * ky);
                if (ya < hy0) ya = hy0;
                if (yb > hy1) yb = hy1;
                if (yb <= ya) { yb = ya + 1; if (yb > hy1) { yb = hy1; ya = yb - 1; } }
                uint32_t *o = out + (size_t)r * W;
                for (int c = 0; c < W; c++) {
                    int cc = flipx ? W - 1 - c : c;
                    int x0 = xa[cc], x1 = xa[cc + 1];
                    if (x1 <= x0) { x1 = x0 + 1; if (x1 > hx1) { x1 = hx1; x0 = x1 - 1; } }
                    uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
                    for (int y = ya; y < yb; y++) {
                        const uint32_t *row = (const uint32_t *)(src->px + (size_t)y * src->stride);
                        for (int x = x0; x < x1; x++) { uint32_t v = row[x]; s0 += v & 255; s1 += (v >> 8) & 255; s2 += (v >> 16) & 255; s3 += v >> 24; }
                    }
                    uint32_t n = (uint32_t)((x1 - x0) * (yb - ya)), h = n / 2;
                    o[c] = (s0 + h) / n | ((s1 + h) / n) << 8 | ((s2 + h) / n) << 16 | ((s3 + h) / n) << 24;
                }
            }
        });
        free(xa);
        return out;
    }
    int32_t *xi = malloc((size_t)W * sizeof *xi);
    uint8_t *xf = malloc((size_t)W);
    if (!xi || !xf) { free(xi); free(xf); return NULL; }
    for (int c = 0; c < W; c++) {
        double sx = sl + ((flipx ? W - 1 - c : c) + 0.5) * kx;
        if (!filter) { int x = (int)floor(sx); xi[c] = x < 0 ? 0 : x > smaxx ? smaxx : x; xf[c] = 0; continue; }
        double f = sx - 0.5, fl = floor(f);
        int x = (int)fl; uint32_t w = (uint32_t)lrint((f - fl) * 256);
        if (w >= 256) { x++; w = 0; }
        if (x < 0) { x = 0; w = 0; }
        if (x >= smaxx) { x = smaxx; w = 0; }
        xi[c] = x; xf[c] = (uint8_t)w;
    }
    par_rows((int64_t)W * H, 0, H, ^(int r0, int r1) {
        for (int r = r0; r < r1; r++) {
            double sy = st + ((flipy ? H - 1 - r : r) + 0.5) * ky;
            int y0; uint32_t fy = 0;
            if (!filter) { y0 = (int)floor(sy); y0 = y0 < 0 ? 0 : y0 > smaxy ? smaxy : y0; }
            else {
                double f = sy - 0.5, fl = floor(f);
                y0 = (int)fl; fy = (uint32_t)lrint((f - fl) * 256);
                if (fy >= 256) { y0++; fy = 0; }
                if (y0 < 0) { y0 = 0; fy = 0; }
                if (y0 >= smaxy) { y0 = smaxy; fy = 0; }
            }
            const uint32_t *s0 = (const uint32_t *)(src->px + (size_t)y0 * src->stride);
            const uint32_t *s1 = (const uint32_t *)(src->px + (size_t)(fy ? y0 + 1 : y0) * src->stride);
            uint32_t *o = out + (size_t)r * W;
            for (int c = 0; c < W; c++) {
                int x = xi[c], x1 = xf[c] ? x + 1 : x;
                o[c] = (!xf[c] && !fy) ? s0[x] : bilerp(s0[x], s0[x1], s1[x], s1[x1], xf[c], fy);
            }
        }
    });
    free(xi); free(xf);
    return k->px;
}
/* d = s over d, a row; s first times ga / 255 */
static void over_run(uint32_t *restrict d, const uint32_t *restrict s, int n, uint32_t ga)
{
    if (ga == 255) for (int i = 0; i < n; i++) d[i] = over(d[i], s[i]);
    else for (int i = 0; i < n; i++) d[i] = over(d[i], scale_px(s[i], ga));
}
/* the same through a colour filter, as icons are tinted (Compose's ColorFilter.tint): cp the filter colour premultiplied,
 * ca its alpha; mode 5 SRC_IN (the colour where the source is), 9 SRC_ATOP (laid over it), 13 MULTIPLY / MODULATE */
static void over_run_cf(uint32_t *restrict d, const uint32_t *restrict s, int n, uint32_t ga, int mode, uint32_t cp, uint32_t ca)
{
    /* a loop for each mode, the branch outside: they vectorise */
    if (mode == 5) {
        if (ga != 255) cp = scale_px(cp, ga);
        for (int i = 0; i < n; i++) d[i] = over(d[i], scale_px(cp, s[i] >> 24));
    } else if (mode == 9) {
        for (int i = 0; i < n; i++) {
            uint32_t t = scale_px(cp, s[i] >> 24) + scale_px(s[i], 255 - ca);
            d[i] = over(d[i], ga != 255 ? scale_px(t, ga) : t);
        }
    } else {
        for (int i = 0; i < n; i++) {
            /* each channel of the source times the colour's */
            uint32_t px = s[i];
            uint32_t r = ((px & 255) * (cp & 255) + 127) / 255, g = (((px >> 8) & 255) * ((cp >> 8) & 255) + 127) / 255;
            uint32_t b = (((px >> 16) & 255) * ((cp >> 16) & 255) + 127) / 255, a = ((px >> 24) * ca + 127) / 255;
            uint32_t t = r | g << 8 | b << 16 | a << 24;
            d[i] = over(d[i], ga != 255 ? scale_px(t, ga) : t);
        }
    }
}
static bool blit_(gcanvas *v, gbitmap *src, float sl, float st, float sr, float sb, float dl, float dt, float dr, float db,
                  const int32_t *p)
{
    int cf = p[P_CFMODE];
    if (src == v->bm || (cf >= 0 && cf != 5 && cf != 9 && cf != 13) || p[P_XFER] != 3 || pf(p, P_SHRAD) > 0) { g_bs[BS_PAINT]++; return false; }
    CGAffineTransform m;
    if (v->layers) { g_bs[BS_LAYER]++; return false; }
    if (v->complex || v->depth >= 256) { g_bs[BS_CLIP]++; return false; }
    if (!blend_ok(v, &m)) { g_bs[BS_TURNED]++; return false; }
    gbitmap *b = v->bm;
    uint32_t ga = (uint32_t)p[P_COLOR] >> 24;
    if (!ga) return true;
    /* the destination's corners in the bitmap's memory (rows from the top) */
    CGPoint c0 = CGPointApplyAffineTransform(CGPointMake(dl, dt), m), c1 = CGPointApplyAffineTransform(CGPointMake(dr, db), m);
    double mx0 = c0.x, mx1 = c1.x, my0 = b->h - c0.y, my1 = b->h - c1.y;
    if (fabs(mx1 - mx0) < 1e-6 || fabs(my1 - my0) < 1e-6) return true;
    bool filter = (p[P_FLAGS] & PF_FILTER) != 0;
    double kx = (sr - sl) / (mx1 - mx0), ky = (sb - st) / (my1 - my0);
    /* much smaller: scaled() averages the pixels each covers (it was CoreGraphics' filtering, BS_SHRUNK) */
    /* one to one on whole pixels: the source itself; else the source scaled to the destination's size, laid on whole pixels
     * (half a pixel at most from where it would be: nothing to see, and the scaled copy stays good while it moves) */
    bool exact = fabs(kx - 1) < 1e-6 && fabs(ky - 1) < 1e-6
              && fabs((sl - mx0) - lrint(sl - mx0)) < 1e-4 && fabs((st - my0) - lrint(st - my0)) < 1e-4
              && fabs(sl - lrint(sl)) < 1e-4 && fabs(st - lrint(st)) < 1e-4;
    int bx0 = (int)lrint(fmin(mx0, mx1)), by0 = (int)lrint(fmin(my0, my1));
    int W = (int)lrint(fabs(mx1 - mx0)), H = (int)lrint(fabs(my1 - my0));
    const uint32_t *img; size_t istride;
    if (exact) {
        int x = (int)lrint(sl), y = (int)lrint(st);
        if (x < 0 || y < 0 || x + W > src->w || y + H > src->h) return false;
        img = (const uint32_t *)(src->px + (size_t)y * src->stride) + x; istride = src->stride / 4;
    } else {
        if (W <= 0 || H <= 0) return true;
        img = scaled(src, sl, st, sr, sb, W, H, mx1 < mx0, my1 < my0, filter); istride = (size_t)W;
        if (!img) return false;
    }
    g_bs[exact ? BS_EXACT_PX : BS_SCALED_PX] += (int64_t)W * H;
    /* the clip: the pixels whose centres it covers */
    CGRect clip = clip_user(v);
    if (CGRectIsNull(clip)) return true;
    CGRect cd = CGRectStandardize(CGRectApplyAffineTransform(clip, m));
    int ix0 = (int)ceil(cd.origin.x - 0.5), ix1 = (int)ceil(cd.origin.x + cd.size.width - 0.5);
    int iy0 = (int)ceil(b->h - (cd.origin.y + cd.size.height) - 0.5), iy1 = (int)ceil(b->h - cd.origin.y - 0.5);
    if (ix0 < bx0) ix0 = bx0;
    if (iy0 < by0) iy0 = by0;
    if (ix1 > bx0 + W) ix1 = bx0 + W;
    if (iy1 > by0 + H) iy1 = by0 + H;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > b->w) ix1 = b->w;
    if (iy1 > b->h) iy1 = b->h;
    uint32_t ca = 0, cp = cf >= 0 ? premul((uint32_t)p[P_CFCOLOR], 1, &ca) : 0;
    ca = 255 - ca;                          /* premul gave 255 - alpha */
    if (clip_need(v, ix0, ix1, iy0, iy1)) {
        if (!clip_box(v, &ix0, &ix1, &iy0, &iy1)) return true;
        par_rows((int64_t)(ix1 - ix0) * (iy1 - iy0), iy0, iy1, ^(int j0_, int j1_) {
        uint32_t *tmp = malloc((size_t)(ix1 - ix0) * 4);
        if (!tmp) return;
        for (int j = j0_; j < j1_; j++) {
            /* the row as it would blend, then each pixel through the mask */
            const uint32_t *sr0 = img + (size_t)(j - by0) * istride + (ix0 - bx0);
            const uint8_t *mk = clip_at(v, ix0, j);
            uint32_t *d = (uint32_t *)(b->px + (size_t)j * b->stride) + ix0;
            if (!mk) {
                if (cf < 0) over_run(d, sr0, ix1 - ix0, ga); else over_run_cf(d, sr0, ix1 - ix0, ga, cf, cp, ca);
                continue;
            }
            for (int i = 0; i < ix1 - ix0; i++) tmp[i] = 0;
            if (cf < 0) over_run(tmp, sr0, ix1 - ix0, ga); else over_run_cf(tmp, sr0, ix1 - ix0, ga, cf, cp, ca);
            for (int i = 0; i < ix1 - ix0; i++) d[i] = over(d[i], scale_px(tmp[i], mk[i]));
        }
        free(tmp);
        });
        return true;
    }
    par_rows((int64_t)(ix1 - ix0) * (iy1 - iy0), iy0, iy1, ^(int j0_, int j1_) {
    for (int j = j0_; j < j1_; j++) {
        uint32_t *d = (uint32_t *)(b->px + (size_t)j * b->stride) + ix0;
        const uint32_t *sr0 = img + (size_t)(j - by0) * istride + (ix0 - bx0);
        if (cf < 0) over_run(d, sr0, ix1 - ix0, ga);
        else over_run_cf(d, sr0, ix1 - ix0, ga, cf, cp, ca);
    }
    });
    return true;
}

void k_gfx_report(long frames);
/* (long[] out, long frames): the blit counters (BS_*), then zero; and the natives' costs to the log */
NAT(G_blitStats)
{
    (void)self; (void)ret;
    int64_t *o = arr_data(a[0].l);
    if (o) for (int i = 0; i < BS_COUNT; i++) { o[i] = g_bs[i]; g_bs[i] = 0; }
    k_gfx_report(a[1].j);               /* gfx_jni.c (gen-jni.py): the costliest natives */
    double fr = a[1].j > 0 ? (double)a[1].j : 1;
    tl_log_line("FrameStats: clip per frame: %.1f masks (%.2f Mpx, %.2f ms), %.1f of them turned or left-out rectangles; CoreGraphics given clips %.1f times (%.1f clips, %.2f ms)",
                g_cs_masks / fr, g_cs_mask_px / fr / 1e6, g_cs_mask_ns / fr / 1e6, g_cs_shaped_rects / fr, g_cs_flushes / fr, g_cs_flushed_ops / fr, g_cs_flush_ns / fr / 1e6);
    g_cs_masks = g_cs_mask_px = g_cs_mask_ns = g_cs_flushes = g_cs_flushed_ops = g_cs_flush_ns = g_cs_shaped_rects = 0;
    tl_log_line("FrameStats: text per frame: %.1f from the cache (%.2f Mpx), %.1f laid out, %.1f colour glyphs, %.1f not cacheable",
                g_tc_hit / fr, g_tc_px / fr / 1e6, g_tc_miss / fr, g_tc_color / fr, g_tc_skip / fr);
    g_tc_hit = g_tc_miss = g_tc_color = g_tc_skip = g_tc_px = 0;
    tl_log_line("FrameStats: shaders per frame: %.1f gradients filled here, %.1f shaded shapes given to CoreGraphics", g_gs_fast / fr, g_gs_cg / fr);
    g_gs_fast = g_gs_cg = 0;
    char line[1024]; int at = snprintf(line, sizeof(line), "FrameStats: drawRect per frame:");
    for (int k = 0; k < DR_COUNT; k++) {
        if (g_dr_n[k] && at < (int)sizeof(line))
            at += snprintf(line + at, sizeof(line) - at, " %s %.1fx %.2fMpx %.2fms;", k_dr_name[k], g_dr_n[k] / fr, g_dr_px[k] / fr / 1e6, g_dr_ns[k] / fr / 1e6);
        g_dr_n[k] = g_dr_ns[k] = g_dr_px[k] = 0;
    }
    tl_log_line("%s last odd paint: xfer %d, filter mode %d; shader kind %d", line, g_dr_last_xfer, g_dr_last_cf, g_dr_last_kind);
    return true;
}
NAT(G_cvDrawBitmap)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); gbitmap *b = H(gbitmap, 1);
    if (!v || !b) return true;
    const int32_t *p = paint_of(a[10].l);
    int32_t q[P_COUNT]; memcpy(q, p, sizeof(q));
    if (!a[10].l) q[P_FLAGS] |= PF_FILTER;
    float sl = a[2].f, st = a[3].f, sr = a[4].f, sb = a[5].f, dl = a[6].f, dt = a[7].f, dr = a[8].f, db = a[9].f;
    if (sr - sl <= 0 || sb - st <= 0 || dr - dl == 0 || db - dt == 0) return true;
    if (blit(v, b, sl, st, sr, sb, dl, dt, dr, db, q)) return true;
    CGImageRef full = bm_image(b);
    bool whole = sl == 0 && st == 0 && sr == b->w && sb == b->h;
    CGImageRef img = whole ? full : CGImageCreateWithImageInRect(full, CGRectMake(sl, st, sr - sl, sb - st));
    CGContextRef c = cg(v);
    bool layered;
    if (img && paint_begin(c, q, &layered)) {
        CGContextSetFillColorWithColor(c, CGColorGetConstantColor(kCGColorBlack));
        CGContextSetAlpha(c, (float)((uint32_t)q[P_COLOR] >> 24) / 255.0f);
        CGContextSaveGState(c);
        CGContextTranslateCTM(c, dl, db); CGContextScaleCTM(c, 1, -1);
        CGContextDrawImage(c, CGRectMake(0, 0, dr - dl, db - dt), img);
        CGContextRestoreGState(c);
        struct image_clip icl = { img, CGAffineTransformMake(1, 0, 0, -1, dl, db), CGRectMake(0, 0, dr - dl, db - dt) };
        paint_end_clip(c, q, layered, CGRectMake(dl, dt, dr - dl, db - dt), clip_image, &icl);
    }
    if (img && !whole) CGImageRelease(img);
    CGImageRelease(full);
    return true;
}
/* (long, long bm, float[6] matrix, int[] paint or null) */
NAT(G_cvDrawBitmapM)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0); gbitmap *b = H(gbitmap, 1); float *m = arr_data(a[2].l);
    if (!v || !b || !m) return true;
    const int32_t *p = paint_of(a[3].l);
    int32_t q[P_COUNT]; memcpy(q, p, sizeof(q));
    if (!a[3].l) q[P_FLAGS] |= PF_FILTER;
    CGImageRef img = bm_image(b);
    CGContextRef c = cg(v);
    bool layered;
    if (paint_begin(c, q, &layered)) {
        CGContextSetAlpha(c, (float)((uint32_t)q[P_COLOR] >> 24) / 255.0f);
        CGContextSaveGState(c);
        CGContextConcatCTM(c, CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5]));
        CGContextTranslateCTM(c, 0, b->h); CGContextScaleCTM(c, 1, -1);
        CGContextDrawImage(c, CGRectMake(0, 0, b->w, b->h), img);
        CGContextRestoreGState(c);
        struct image_clip icl = { img, CGAffineTransformConcat(CGAffineTransformMake(1, 0, 0, -1, 0, b->h), CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5])), CGRectMake(0, 0, b->w, b->h) };
        paint_end_clip(c, q, layered, CGRectApplyAffineTransform(CGRectMake(0, 0, b->w, b->h), CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5])), clip_image, &icl);
    }
    CGImageRelease(img);
    return true;
}
/* (long, String, int start, int end, float x, float y, int[] paint, long typeface, long shader) */
NAT(G_cvDrawText)
{
    (void)self; (void)ret;
    gcanvas *v = H(gcanvas, 0);
    if (!v || !a[1].l) return true;
    draw_text(v, a[1].l, a[2].i, a[3].i, a[4].f, a[5].f, paint_of(a[6].l), (gface *)(uintptr_t)a[7].j, (gshader *)(uintptr_t)a[8].j);
    return true;
}

/* ---- text measuring: (String, int start, int end, int[] paint, long typeface, ...) */
/* txMeasure's answers kept: Compose measures the same labels every frame (60 a frame on DepCalc's calculator), and CoreText laid
 * each out again. Keyed by the characters and what of the paint sets a width: size, scale, skew, spacing, flags, the face. */
typedef struct { uint64_t h; int32_t key[6]; int64_t face; uint16_t *chars; int n; float w; } mcache;
static mcache g_mc[2048];
static pthread_mutex_t g_mc_lock = PTHREAD_MUTEX_INITIALIZER;
NAT(G_txMeasure)
{
    (void)self;
    int32_t len = 0;
    const uint16_t *all = a[0].l ? tl_dvm_string_chars(a[0].l, &len) : NULL;
    int st = a[1].i, en = a[2].i;
    const int32_t *pp = paint_of(a[3].l);
    mcache *e = NULL; uint64_t mh = 0;
    if (all && st >= 0 && en <= len && en > st && en - st <= 256) {
        int32_t key[6] = { pp[P_TEXTSIZE], pp[P_SCALEX], pp[P_SKEWX], pp[P_LSPACE], pp[P_FLAGS], 0 };
        uint64_t h = 1469598103934665603ull ^ (uint64_t)a[4].j;
        for (int i = st; i < en; i++) h = (h ^ all[i]) * 1099511628211ull;
        for (int i = 0; i < 5; i++) h = (h ^ (uint32_t)key[i]) * 1099511628211ull;
        e = &g_mc[h & 2047];
        pthread_mutex_lock(&g_mc_lock);
        bool hit = e->chars && e->h == h && e->n == en - st && e->face == a[4].j && !memcmp(e->key, key, sizeof key)
            && !memcmp(e->chars, all + st, (size_t)(en - st) * 2);
        float hw = e->w;
        if (!hit) {
            free(e->chars);
            e->chars = malloc((size_t)(en - st) * 2);
            if (e->chars) { memcpy(e->chars, all + st, (size_t)(en - st) * 2); e->h = h; e->n = en - st; e->face = a[4].j; memcpy(e->key, key, sizeof key); e->w = NAN; }
        }
        pthread_mutex_unlock(&g_mc_lock);
        if (hit && !isnan(hw)) { *ret = Ff(hw); return true; }
        if (!hit && !e->chars) e = NULL;
        mh = h;
    }
    CFStringRef s = cfstr(a[0].l, a[1].i, a[2].i);
    const int32_t *p = paint_of(a[3].l);
    CTLineRef line = make_line(s, p, (gface *)(uintptr_t)a[4].j);
    float w = line_width(line, p);
    if (e) { pthread_mutex_lock(&g_mc_lock); if (e->h == mh) e->w = w; pthread_mutex_unlock(&g_mc_lock); }
    *ret = Ff(w);
    CFRelease(line); CFRelease(s);
    return true;
}
/* (int[] paint, long typeface, float[] out: top ascent descent bottom leading) -> float spacing */
NAT(G_txMetrics)
{
    (void)self;
    const int32_t *p = paint_of(a[0].l);
    CTFontRef f = sized((gface *)(uintptr_t)a[1].j, pf(p, P_TEXTSIZE));
    float *o = arr_data(a[2].l);
    float asc = (float)CTFontGetAscent(f), desc = (float)CTFontGetDescent(f), lead = (float)CTFontGetLeading(f);
    CGRect bb = CTFontGetBoundingBox(f);
    if (o && arr_len(a[2].l) >= 5) {
        o[0] = -(float)fmax(bb.origin.y + bb.size.height, asc);
        o[1] = -asc; o[2] = desc;
        o[3] = (float)fmax(-bb.origin.y, desc);
        o[4] = lead;
    }
    *ret = Ff(asc + desc + lead);
    CFRelease(f);
    return true;
}
/* (String, int start, int end, int[] paint, long typeface, int[] outRect) */
NAT(G_txBounds)
{
    (void)self; (void)ret;
    CFStringRef s = cfstr(a[0].l, a[1].i, a[2].i);
    const int32_t *p = paint_of(a[3].l);
    CTLineRef line = make_line(s, p, (gface *)(uintptr_t)a[4].j);
    int32_t *o = arr_data(a[5].l);
    if (o) {
        CGRect r = CTLineGetImageBounds(line, NULL);
        if (CGRectIsNull(r) || CGRectIsEmpty(r)) { o[0] = o[1] = o[2] = o[3] = 0; }
        else {
            float sx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1;
            o[0] = (int32_t)floor(r.origin.x * sx); o[2] = (int32_t)ceil((r.origin.x + r.size.width) * sx);
            o[1] = (int32_t)floor(-(r.origin.y + r.size.height)); o[3] = (int32_t)ceil(-r.origin.y);
        }
    }
    CFRelease(line); CFRelease(s);
    return true;
}
/* (String, int start, int end, float maxWidth, int[] paint, long typeface, boolean words) -> chars that fit */
NAT(G_txBreak)
{
    (void)self;
    CFStringRef s = cfstr(a[0].l, a[1].i, a[2].i);
    const int32_t *p = paint_of(a[4].l);
    CFAttributedStringRef as = make_attributed(s, p, (gface *)(uintptr_t)a[5].j);
    CTTypesetterRef ts = CTTypesetterCreateWithAttributedString(as);
    float sx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1;
    double w = a[3].f / sx;
    CFIndex n = a[6].z ? CTTypesetterSuggestLineBreak(ts, 0, w) : CTTypesetterSuggestClusterBreak(ts, 0, w);
    *ret = Ii((int32_t)n);
    CFRelease(ts); CFRelease(as); CFRelease(s);
    return true;
}
/* (String, int start, int end, int[] paint, long typeface, float[] out) -> advances per UTF-16 unit */
NAT(G_txWidths)
{
    (void)self; (void)ret;
    CFStringRef s = cfstr(a[0].l, a[1].i, a[2].i);
    const int32_t *p = paint_of(a[3].l);
    float *o = arr_data(a[5].l);
    CFIndex n = CFStringGetLength(s);
    if (o && (CFIndex)arr_len(a[5].l) >= n) {
        CTLineRef line = make_line(s, p, (gface *)(uintptr_t)a[4].j);
        float sx = pf(p, P_SCALEX) > 0 ? pf(p, P_SCALEX) : 1;
        double prev = 0;
        for (CFIndex i = 0; i < n; i++) {
            double x = CTLineGetOffsetForStringIndex(line, i + 1, NULL);
            o[i] = (float)((x - prev) * sx);
            prev = x;
        }
        CFRelease(line);
    }
    CFRelease(s);
    return true;
}

/* ---- Typeface */
NAT(G_tfCreate) { (void)self; const char *fam = a[0].l ? tl_jni_string(a[0].l) : NULL; *ret = J((int64_t)(uintptr_t)face_family(fam, a[1].i)); return true; }
NAT(G_tfDefault) { (void)self; (void)a; *ret = J((int64_t)(uintptr_t)default_face()); return true; }
NAT(G_tfDerive)
{
    (void)self;
    gface *g = H(gface, 0);
    if (!g) g = default_face();
    int style = a[1].i, weight = a[2].i;
    *ret = J((int64_t)(uintptr_t)face_make(g->base, style, weight > 0 ? weight : ((style & 1) ? 700 : 400)));
    return true;
}
/* (byte[] data) -> long, 0 when it is not a font */
NAT(G_tfFromData)
{
    (void)self;
    uint8_t *d = arr_data(a[0].l); uint32_t n = arr_len(a[0].l);
    *ret = J(0);
    if (!d || !n) return true;
    CFDataRef data = CFDataCreate(NULL, d, n);
    CTFontDescriptorRef desc = CTFontManagerCreateFontDescriptorFromData(data);
    CFRelease(data);
    if (!desc) return true;
    CTFontRef f = CTFontCreateWithFontDescriptor(desc, 12, NULL);
    CFRelease(desc);
    gface *g = calloc(1, sizeof(*g));
    g->base = f; g->weight = 400;
    *ret = J((int64_t)(uintptr_t)g);
    return true;
}
NAT(G_tfWeight) { (void)self; gface *g = H(gface, 0); *ret = Ii(g ? g->weight : 400); return true; }

/* ---- shaders */
static gshader *grad_shader(int kind, jobj *colors, jobj *pos, int tile)
{
    int n = (int)arr_len(colors);
    const float *p = pos && (int)arr_len(pos) >= n ? arr_data(pos) : NULL;
    return shader_new(kind, arr_data(colors), p, n, tile);
}
/* (x0, y0, x1, y1, int[] colors, float[] pos, int tile) */
NAT(G_shLinear)
{
    (void)self;
    gshader *s = grad_shader(SH_LINEAR, a[4].l, a[5].l, a[6].i);
    s->x0 = a[0].f; s->y0 = a[1].f; s->x1 = a[2].f; s->y1 = a[3].f;
    *ret = J((int64_t)(uintptr_t)s);
    return true;
}
/* (cx, cy, r, int[] colors, float[] pos, int tile) */
NAT(G_shRadial)
{
    (void)self;
    gshader *s = grad_shader(SH_RADIAL, a[3].l, a[4].l, a[5].i);
    s->x0 = a[0].f; s->y0 = a[1].f; s->r = a[2].f;
    *ret = J((int64_t)(uintptr_t)s);
    return true;
}
/* (cx, cy, int[] colors, float[] pos) */
NAT(G_shSweep)
{
    (void)self;
    gshader *s = grad_shader(SH_SWEEP, a[2].l, a[3].l, 0);
    s->x0 = a[0].f; s->y0 = a[1].f;
    *ret = J((int64_t)(uintptr_t)s);
    return true;
}
/* (long bm, int tile) */
NAT(G_shBitmap)
{
    (void)self;
    gbitmap *b = H(gbitmap, 0);
    gshader *s = calloc(1, sizeof(*s));
    s->kind = SH_BITMAP; s->tile = a[1].i; s->local = CGAffineTransformIdentity;
    if (b) { s->image = bm_image(b); s->iw = b->w; s->ih = b->h; }
    *ret = J((int64_t)(uintptr_t)s);
    return true;
}
NAT(G_shMatrix)
{
    (void)self; (void)ret;
    gshader *s = H(gshader, 0); float *m = arr_data(a[1].l);
    if (s) s->local = m ? CGAffineTransformMake(m[0], m[3], m[1], m[4], m[2], m[5]) : CGAffineTransformIdentity;
    return true;
}
NAT(G_shFree)
{
    (void)self; (void)ret;
    gshader *s = H(gshader, 0);
    if (!s) return true;
    if (s->grad) CGGradientRelease(s->grad);
    if (s->image) CGImageRelease(s->image);
    free(s->colors); free(s->pos); free(s->lut); free(s);
    return true;
}

/* ---- Path queries: (float[] path, int n, ...) */
/* husk-port: (float[] a, int na, float[] b, int nb, int op, int evenOdd) -> float[] ops of a op b (Path.Op order: difference,
 * intersect, union, xor, reverse difference), CoreGraphics' boolean operations, or null where they are not (before iOS 16) */
static void put_op(float **o, int *n, int *cap, int op, int k, const CGPoint *pt)
{
    if (*n + 1 + 2 * k > *cap) { *cap = (*cap + 1 + 2 * k) * 2; *o = realloc(*o, (size_t)*cap * sizeof **o); }
    (*o)[(*n)++] = (float)op;
    for (int i = 0; i < k; i++) { (*o)[(*n)++] = (float)pt[i].x; (*o)[(*n)++] = (float)pt[i].y; }
}
typedef struct { float *o; int n, cap; } ops_out;
static void path_to_ops(void *info, const CGPathElement *e)
{
    ops_out *w = info;
    switch (e->type) {
    case kCGPathElementMoveToPoint: put_op(&w->o, &w->n, &w->cap, 0, 1, e->points); break;
    case kCGPathElementAddLineToPoint: put_op(&w->o, &w->n, &w->cap, 1, 1, e->points); break;
    case kCGPathElementAddQuadCurveToPoint: put_op(&w->o, &w->n, &w->cap, 2, 2, e->points); break;
    case kCGPathElementAddCurveToPoint: put_op(&w->o, &w->n, &w->cap, 3, 3, e->points); break;
    case kCGPathElementCloseSubpath: put_op(&w->o, &w->n, &w->cap, 4, 0, NULL); break;
    }
}
NAT(G_pathOp)
{
    (void)self;
    *ret = Ll(NULL);
    float *fa = arr_data(a[0].l), *fb = arr_data(a[2].l);
    if (!fa || !fb) return true;
    if (__builtin_available(macOS 13.0, iOS 16.0, *)) {
        CGPathRef pa = build_path(fa, a[1].i), pb = build_path(fb, a[3].i), r = NULL;
        bool eo = a[5].i != 0;
        switch (a[4].i) {
        case 0: r = CGPathCreateCopyBySubtractingPath(pa, pb, eo); break;
        case 1: r = CGPathCreateCopyByIntersectingPath(pa, pb, eo); break;
        case 2: r = CGPathCreateCopyByUnioningPath(pa, pb, eo); break;
        case 3: r = CGPathCreateCopyBySymmetricDifferenceOfPath(pa, pb, eo); break;
        case 4: r = CGPathCreateCopyBySubtractingPath(pb, pa, eo); break;
        }
        CGPathRelease(pa); CGPathRelease(pb);
        if (!r) return true;
        ops_out w = { NULL, 0, 0 };
        CGPathApply(r, &w, path_to_ops);
        CGPathRelease(r);
        jobj *out = tl_jni_new_prim_array('F', (uint32_t)w.n);
        if (w.n) memcpy(out->arr.data, w.o, (size_t)w.n * sizeof *w.o);
        free(w.o);
        *ret = Ll(out);
    }
    return true;
}
NAT(G_pathBounds)
{
    (void)self; (void)ret;
    float *f = arr_data(a[0].l), *o = arr_data(a[2].l);
    if (!f || !o) return true;
    CGPathRef p = build_path(f, a[1].i);
    CGRect r = CGPathIsEmpty(p) ? CGRectZero : CGPathGetPathBoundingBox(p);
    o[0] = (float)r.origin.x; o[1] = (float)r.origin.y; o[2] = (float)(r.origin.x + r.size.width); o[3] = (float)(r.origin.y + r.size.height);
    CGPathRelease(p);
    return true;
}
NAT(G_pathContains)
{
    (void)self;
    float *f = arr_data(a[0].l);
    *ret = Ii(0);
    if (!f) return true;
    CGPathRef p = build_path(f, a[1].i);
    *ret = Ii(CGPathContainsPoint(p, NULL, CGPointMake(a[3].f, a[4].f), a[2].i == 1));
    CGPathRelease(p);
    return true;
}
/* (float[] path, int n, float[] out) -> length of the first contour, and out[0..1] its first point (for PathMeasure) */
NAT(G_pathLength)
{
    (void)self;
    float *f = arr_data(a[0].l);
    *ret = Ff(0);
    if (!f) return true;
    CGPathRef p = build_path(f, a[1].i);
    /* flatten by dashing-free stroke approximation: sample the path's elements */
    __block struct st { CGPoint last, first; double len; bool started; } st = { 0 };
    CGPathApplyWithBlock(p, ^(const CGPathElement *e) {
        CGPoint *pt = e->points;
        switch (e->type) {
        case kCGPathElementMoveToPoint: if (!st.started) { st.first = pt[0]; st.started = true; } st.last = pt[0]; break;
        case kCGPathElementAddLineToPoint: st.len += hypot(pt[0].x - st.last.x, pt[0].y - st.last.y); st.last = pt[0]; break;
        case kCGPathElementAddQuadCurveToPoint: case kCGPathElementAddCurveToPoint: {
            int np = e->type == kCGPathElementAddQuadCurveToPoint ? 2 : 3;
            CGPoint p0 = st.last, prev = p0;
            for (int i = 1; i <= 16; i++) {
                double t = i / 16.0, u = 1 - t; CGPoint q;
                if (np == 2) { q.x = u * u * p0.x + 2 * u * t * pt[0].x + t * t * pt[1].x; q.y = u * u * p0.y + 2 * u * t * pt[0].y + t * t * pt[1].y; }
                else { q.x = u * u * u * p0.x + 3 * u * u * t * pt[0].x + 3 * u * t * t * pt[1].x + t * t * t * pt[2].x; q.y = u * u * u * p0.y + 3 * u * u * t * pt[0].y + 3 * u * t * t * pt[1].y + t * t * t * pt[2].y; }
                st.len += hypot(q.x - prev.x, q.y - prev.y); prev = q;
            }
            st.last = pt[np - 1]; break; }
        case kCGPathElementCloseSubpath: st.len += hypot(st.first.x - st.last.x, st.first.y - st.last.y); st.last = st.first; break;
        }
    });
    *ret = Ff((float)st.len);
    CGPathRelease(p);
    return true;
}

/* ================================================================== the frame on screen */

/* A few buffers the frames go round: the host holds the one it shows (and Core Animation the one before, until it lets go)
 * while the next is drawn into another; nothing is copied twice, nor waits on the other side's lock while it copies. */
#define NFB 4
static struct {
    pthread_mutex_t lock;
    uint8_t *buf[NFB]; int bw[NFB], bh[NFB];
    int refs[NFB];                  /* the host's holds on each */
    int cur;                        /* the latest frame's buffer, -1 none */
    atomic_bool fresh;
    atomic_bool any;
} S = { .lock = PTHREAD_MUTEX_INITIALIZER, .cur = -1 };

/* husk.Native.present(long bitmap): the window's views, drawn; the host puts them on screen. */
NAT(G_present)
{
    (void)self; (void)ret;
    g_frame++;
    g_presenter = pthread_self();
    gbitmap *b = H(gbitmap, 0);
    if (!b) { atomic_store(&S.any, false); atomic_store(&S.fresh, true); return true; }
    pthread_mutex_lock(&S.lock);
    int k = -1;
    for (int i = 0; i < NFB && k < 0; i++) if (i != S.cur && !S.refs[i]) k = i;
    if (k < 0) { pthread_mutex_unlock(&S.lock); return true; }     /* all held: the host is behind, this frame is skipped */
    if (S.bw[k] != b->w || S.bh[k] != b->h) { free(S.buf[k]); S.buf[k] = malloc((size_t)b->w * 4 * (size_t)b->h); S.bw[k] = b->w; S.bh[k] = b->h; }
    uint8_t *dst = S.buf[k];
    pthread_mutex_unlock(&S.lock);
    if (!dst) return true;
    /* no one takes a buffer that is neither the latest nor held: it is filled without the lock */
    size_t row = (size_t)b->w * 4, stride = b->stride; const uint8_t *src = b->px;
    par_rows((int64_t)b->w * b->h, 0, b->h, ^(int j0, int j1) {
        if (stride == row) memcpy(dst + (size_t)j0 * row, src + (size_t)j0 * row, (size_t)(j1 - j0) * row);
        else for (int j = j0; j < j1; j++) memcpy(dst + (size_t)j * row, src + (size_t)j * stride, row);
    });
    pthread_mutex_lock(&S.lock);
    S.cur = k;
    pthread_mutex_unlock(&S.lock);
    atomic_store(&S.any, true);
    atomic_store(&S.fresh, true);
    return true;
}

/* For the host: when there is a new frame, its pixels (w * 4 bytes a row), held until tl_ui_frame_release(token) */
bool tl_ui_frame_hold(const uint8_t **px, int *w, int *h, bool *any, int *token)
{
    if (!atomic_exchange(&S.fresh, false)) return false;
    pthread_mutex_lock(&S.lock);
    int k = S.cur;
    if (k >= 0) { S.refs[k]++; *px = S.buf[k]; *w = S.bw[k]; *h = S.bh[k]; } else { *px = NULL; *w = *h = 0; }
    *any = atomic_load(&S.any) && k >= 0;
    *token = k;
    pthread_mutex_unlock(&S.lock);
    return true;
}
void tl_ui_frame_release(int token)
{
    if (token < 0 || token >= NFB) return;
    pthread_mutex_lock(&S.lock);
    if (S.refs[token] > 0) S.refs[token]--;
    pthread_mutex_unlock(&S.lock);
}
/* the same, one at a time (the GL thread, the test host) */
static int g_held = -1;
bool tl_ui_frame_take(const uint8_t **px, int *w, int *h, bool *any) { return tl_ui_frame_hold(px, w, h, any, &g_held); }
void tl_ui_frame_done(void) { tl_ui_frame_release(g_held); g_held = -1; }
bool tl_ui_has_frame(void) { return atomic_load(&S.any); }

static const struct { const char *name, *sig; dvm_native_fn fn; } k_gfx[] = {
    { "bmNew", "(II)J", G_bmNew },
    { "bmFree", "(J)V", G_bmFree },
    { "bmErase", "(JI)V", G_bmErase },
    { "bmGetPixel", "(JII)I", G_bmGetPixel },
    { "bmSetPixel", "(JIII)V", G_bmSetPixel },
    { "bmPixels", "(J[IIIIIIIZ)V", G_bmPixels },
    { "bmRaw", "(JJZ)V", G_bmRaw },
    { "bmAddress", "(J)J", G_bmAddress },
    { "bmDecode", "([BIII[IZ)J", G_bmDecode },
    { "bmCompress", "(JII)[B", G_bmCompress },
    { "bmCopy", "(JIIII[FZ)J", G_bmCopy },
    { "cvNew", "(J)J", G_cvNew },
    { "cvFree", "(J)V", G_cvFree },
    { "cvSave", "(J)I", G_cvSave },
    { "cvSaveLayer", "(JFFFFI)I", G_cvSaveLayer }, { "cvSaveLayerMode", "(JFFFFII)I", G_cvSaveLayerMode },
    { "cvRestore", "(J)V", G_cvRestore },
    { "cvRestoreTo", "(JI)V", G_cvRestoreTo },
    { "cvSaveCount", "(J)I", G_cvSaveCount },
    { "cvMatrix", "(J[FZ)V", G_cvMatrix },
    { "cvGetMatrix", "(J[F)V", G_cvGetMatrix },
    { "cvClipRect", "(JFFFFI)Z", G_cvClipRect },
    { "cvClipPath", "(J[FIII)Z", G_cvClipPath },
    { "cvClipBounds", "(J[I)Z", G_cvClipBounds },
    { "cvDrawColor", "(JII)V", G_cvDrawColor },
    { "cvDrawRect", "(JFFFF[IJ)V", G_cvDrawRect },
    { "cvDrawRRect", "(JFFFFFF[IJ)V", G_cvDrawRRect },
    { "cvDrawOval", "(JFFFF[IJ)V", G_cvDrawOval },
    { "cvDrawArc", "(JFFFFFFZ[IJ)V", G_cvDrawArc },
    { "cvDrawPath", "(J[FII[IJ)V", G_cvDrawPath },
    { "cvDrawLines", "(J[FII[IJZ)V", G_cvDrawLines },
    { "cvDrawBitmap", "(JJFFFFFFFF[I)V", G_cvDrawBitmap },
    { "cvDrawBitmapM", "(JJ[F[I)V", G_cvDrawBitmapM },
    { "cvDrawText", "(JLjava/lang/String;IIFF[IJJ)V", G_cvDrawText },
    { "txMeasure", "(Ljava/lang/String;II[IJ)F", G_txMeasure },
    { "txMetrics", "([IJ[F)F", G_txMetrics },
    { "txBounds", "(Ljava/lang/String;II[IJ[I)V", G_txBounds },
    { "txBreak", "(Ljava/lang/String;IIF[IJZ)I", G_txBreak },
    { "txWidths", "(Ljava/lang/String;II[IJ[F)V", G_txWidths },
    { "tfCreate", "(Ljava/lang/String;I)J", G_tfCreate },
    { "tfDefault", "()J", G_tfDefault },
    { "tfDerive", "(JII)J", G_tfDerive },
    { "tfFromData", "([B)J", G_tfFromData },
    { "tfWeight", "(J)I", G_tfWeight },
    { "shLinear", "(FFFF[I[FI)J", G_shLinear },
    { "shRadial", "(FFF[I[FI)J", G_shRadial },
    { "shSweep", "(FF[I[F)J", G_shSweep },
    { "shBitmap", "(JI)J", G_shBitmap },
    { "shMatrix", "(J[F)V", G_shMatrix },
    { "shFree", "(J)V", G_shFree },
    { "pathBounds", "([FI[F)V", G_pathBounds }, { "pathOp", "([FI[FIII)[F", G_pathOp }, { "blitStats", "([JJ)V", G_blitStats },
    { "pathContains", "([FIIFF)Z", G_pathContains },
    { "pathLength", "([FI[F)F", G_pathLength },
    { "present", "(J)V", G_present },
    { NULL, NULL, NULL },
};

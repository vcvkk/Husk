/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What CoreGraphics' drawing costs in each pixel format a screen-sized bitmap could have (CI prints it). */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void bench(const char *name, CGColorSpaceRef cs, uint32_t info)
{
    int w = 1179, h = 2556;
    void *px = calloc((size_t)w * h, 4);
    CGContextRef c = CGBitmapContextCreate(px, w, h, 8, (size_t)w * 4, cs, info);
    CGFloat opaque[4] = { 0.1, 0.5, 0.2, 1 }, half[4] = { 0, 0, 0, 0.5 };
    CGColorRef co = CGColorCreate(cs, opaque), ch = CGColorCreate(cs, half);
    struct { const char *what; CGColorRef col; bool aa; CGRect r; } t[] = {
        { "opaque fill, aa", co, true, CGRectMake(0, 0, w, h) },
        { "opaque fill, no aa", co, false, CGRectMake(0, 0, w, h) },
        { "opaque fill, fractional, aa", co, true, CGRectMake(0.5f, 0.25f, w - 1, h - 1) },
        { "50% black fill", ch, true, CGRectMake(0, 0, w, h) },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        CGContextSetFillColorWithColor(c, t[i].col);
        CGContextSetShouldAntialias(c, t[i].aa);
        double t0 = now();
        for (int k = 0; k < 20; k++) CGContextFillRect(c, t[i].r);
        printf("cg-bench %-22s %-28s %6.2f ms\n", name, t[i].what, (now() - t0) / 20 * 1e3);
    }
    double t0 = now();
    for (int k = 0; k < 20; k++) memset(px, 0, (size_t)w * h * 4);
    printf("cg-bench %-22s %-28s %6.2f ms\n", name, "memset (erase)", (now() - t0) / 20 * 1e3);
    t0 = now();
    for (int k = 0; k < 20; k++) { CGImageRef img = CGBitmapContextCreateImage(c); CGContextDrawImage(c, CGRectMake(0, 0, w, h), img); CGImageRelease(img); }
    printf("cg-bench %-22s %-28s %6.2f ms\n", name, "image drawn over itself", (now() - t0) / 20 * 1e3);
    CGColorRelease(co); CGColorRelease(ch); CGContextRelease(c); free(px);
}

int main(void)
{
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB), dev = CGColorSpaceCreateDeviceRGB();
    bench("RGBA sRGB (now)", srgb, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    bench("BGRA sRGB", srgb, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    bench("RGBA device", dev, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    bench("BGRA device", dev, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    return 0;
}

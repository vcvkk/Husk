/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * An app's own native libraries (lib/arm64-v8a/*.so in its APK) under ART: husk.NativeLibs loads them through the guest linker
 * (port/native: Husk's main line's loader and bionic, the library mapped into executable memory or, without JIT, the dylib made
 * from it ahead of time), runs their JNI_OnLoad, and gives ART their Java_... functions when it looks a native method up.
 *
 * ART calls their native methods as it calls any (AAPCS64: what the libraries were built for). They call back through the JNIEnv,
 * where one thing differs: a variadic call (CallIntMethod(env, obj, mid, ...)) from AAPCS64 code passes its arguments in
 * registers, from Darwin code on the stack, and a va_list (CallIntMethodV) is a structure in one and a pointer in the other. So
 * those 62 functions of the table are replaced, for every thread, by entries that see where the call came from (the return
 * address: in a guest library or not) and read the arguments the caller's way, by the method's shorty, for the runtime's
 * jvalue-array function (...A). Every other function is the runtime's own: their arguments are passed alike.
 */
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "husk-framework.h"
#include "../../native/tl/husk-tl-ld.h"
#include "../../native/tl/husk-tl-va.h"

void tl_log_line(const char *fmt, ...);
/* ART (port/art/patches/0013) */
void art_husk_set_native_lookup(void *(*fn)(const char *, const char *));
const char *art_husk_method_shorty(jmethodID mid);
const struct JNINativeInterface *art_husk_jni_table(void);
void art_husk_set_jni_table(const struct JNINativeInterface *table);

static const struct JNINativeInterface *R;          /* the runtime's own table */
static struct JNINativeInterface T;                 /* it, with the variadic entries replaced */

static inline bool from_guest(const void *ra) { return ra && tl_ld_lib_of(ra) != NULL; }

/* ---- the arguments, by the shorty (its first letter the return type) */

/* a guest's: AAPCS64, registers first (tl_va_list), floats promoted to double */
static void args_guest(const char *shorty, tl_va_list *ap, jvalue *a)
{
    for (int i = 0; shorty[i + 1]; i++) {
        switch (shorty[i + 1]) {
            case 'F': a[i].f = (float)tl_va_arg_f64(ap); break;
            case 'D': a[i].d = tl_va_arg_f64(ap); break;
            case 'J': a[i].j = (jlong)tl_va_arg_u64(ap); break;
            case 'L': a[i].l = (jobject)(uintptr_t)tl_va_arg_u64(ap); break;
            default: a[i].j = 0; a[i].i = (jint)tl_va_arg_u64(ap); break;
        }
    }
}
/* Darwin's: each variadic argument in an 8-byte stack slot, floats promoted to double */
static void args_host(const char *shorty, const uint64_t *sp, jvalue *a)
{
    for (int i = 0; shorty[i + 1]; i++, sp++) {
        switch (shorty[i + 1]) {
            case 'F': { double d; memcpy(&d, sp, 8); a[i].f = (float)d; break; }
            case 'D': memcpy(&a[i].d, sp, 8); break;
            case 'J': a[i].j = (jlong)*sp; break;
            case 'L': a[i].l = (jobject)(uintptr_t)*sp; break;
            default: a[i].j = 0; a[i].i = (jint)*sp; break;
        }
    }
}

/* ---- the variadic entries: a stub saves x0-x7 and q0-q7 (as AAPCS64's va_start would), the caller's stack and return address,
 * calls the entry's C part with them, and returns what it left in x0 and q0 */
typedef struct gframe {
    uint64_t gp[8];
    unsigned __int128 fp[8];
    void *stack;                                    /* the caller's stack pointer: where stack arguments start */
    void *ra;                                       /* where the call came from */
} gframe;
_Static_assert(__builtin_offsetof(gframe, fp) == 64 && __builtin_offsetof(gframe, stack) == 192 && __builtin_offsetof(gframe, ra) == 200, "gframe");
__asm__(".text\n.p2align 2\n_hj_common:\n"
        "  sub sp, sp, #224\n"
        "  stp x0, x1, [sp, #0]\n  stp x2, x3, [sp, #16]\n  stp x4, x5, [sp, #32]\n  stp x6, x7, [sp, #48]\n"
        "  stp q0, q1, [sp, #64]\n  stp q2, q3, [sp, #96]\n  stp q4, q5, [sp, #128]\n  stp q6, q7, [sp, #160]\n"
        "  add x9, sp, #224\n  stp x9, x30, [sp, #192]\n"
        "  stp x29, x30, [sp, #208]\n  add x29, sp, #208\n"
        "  mov x0, sp\n  blr x16\n"
        "  ldr x0, [sp, #0]\n  ldr q0, [sp, #64]\n"
        "  ldp x29, x30, [sp, #208]\n  add sp, sp, #224\n  ret\n");
#define HJ_STUB(sym, impl) \
    __asm__(".text\n.p2align 2\n_" #sym ":\n  adrp x16, _" #impl "@PAGE\n  add x16, x16, _" #impl "@PAGEOFF\n  b _hj_common\n")

/* the arguments of the call in f, after `named` fixed ones, the method's */
static const char *frame_args(gframe *f, int named, jmethodID m, jvalue *a)
{
    const char *shorty = art_husk_method_shorty(m);
    if (!shorty) return NULL;
    if (from_guest(f->ra)) { tl_va_list ap; tl_va_start((tl_va_frame *)f, named, 0, &ap); args_guest(shorty, &ap, a); }
    else args_host(shorty, (const uint64_t *)f->stack, a);
    return shorty;
}
static inline void put_ret(gframe *f, const void *v, size_t n, bool fp)
{
    if (fp) { f->fp[0] = 0; memcpy(&f->fp[0], v, n); }
    else { uint64_t x = 0; memcpy(&x, v, n); f->gp[0] = x; }
}
void hj_CallObjectMethod_c(gframe *f);
HJ_STUB(hj_CallObjectMethod, hj_CallObjectMethod_c);
void hj_CallObjectMethod(void);
void hj_CallObjectMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jobject r = R->CallObjectMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jobject hj_CallObjectMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallObjectMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallObjectMethodA(env, o, m, a);
}
void hj_CallNonvirtualObjectMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualObjectMethod, hj_CallNonvirtualObjectMethod_c);
void hj_CallNonvirtualObjectMethod(void);
void hj_CallNonvirtualObjectMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jobject r = R->CallNonvirtualObjectMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jobject hj_CallNonvirtualObjectMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualObjectMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualObjectMethodA(env, o, c, m, a);
}
void hj_CallStaticObjectMethod_c(gframe *f);
HJ_STUB(hj_CallStaticObjectMethod, hj_CallStaticObjectMethod_c);
void hj_CallStaticObjectMethod(void);
void hj_CallStaticObjectMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jobject r = R->CallStaticObjectMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jobject hj_CallStaticObjectMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticObjectMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticObjectMethodA(env, c, m, a);
}
void hj_CallBooleanMethod_c(gframe *f);
HJ_STUB(hj_CallBooleanMethod, hj_CallBooleanMethod_c);
void hj_CallBooleanMethod(void);
void hj_CallBooleanMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jboolean r = R->CallBooleanMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jboolean hj_CallBooleanMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallBooleanMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallBooleanMethodA(env, o, m, a);
}
void hj_CallNonvirtualBooleanMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualBooleanMethod, hj_CallNonvirtualBooleanMethod_c);
void hj_CallNonvirtualBooleanMethod(void);
void hj_CallNonvirtualBooleanMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jboolean r = R->CallNonvirtualBooleanMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jboolean hj_CallNonvirtualBooleanMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualBooleanMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualBooleanMethodA(env, o, c, m, a);
}
void hj_CallStaticBooleanMethod_c(gframe *f);
HJ_STUB(hj_CallStaticBooleanMethod, hj_CallStaticBooleanMethod_c);
void hj_CallStaticBooleanMethod(void);
void hj_CallStaticBooleanMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jboolean r = R->CallStaticBooleanMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jboolean hj_CallStaticBooleanMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticBooleanMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticBooleanMethodA(env, c, m, a);
}
void hj_CallByteMethod_c(gframe *f);
HJ_STUB(hj_CallByteMethod, hj_CallByteMethod_c);
void hj_CallByteMethod(void);
void hj_CallByteMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jbyte r = R->CallByteMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jbyte hj_CallByteMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallByteMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallByteMethodA(env, o, m, a);
}
void hj_CallNonvirtualByteMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualByteMethod, hj_CallNonvirtualByteMethod_c);
void hj_CallNonvirtualByteMethod(void);
void hj_CallNonvirtualByteMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jbyte r = R->CallNonvirtualByteMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jbyte hj_CallNonvirtualByteMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualByteMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualByteMethodA(env, o, c, m, a);
}
void hj_CallStaticByteMethod_c(gframe *f);
HJ_STUB(hj_CallStaticByteMethod, hj_CallStaticByteMethod_c);
void hj_CallStaticByteMethod(void);
void hj_CallStaticByteMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jbyte r = R->CallStaticByteMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jbyte hj_CallStaticByteMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticByteMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticByteMethodA(env, c, m, a);
}
void hj_CallCharMethod_c(gframe *f);
HJ_STUB(hj_CallCharMethod, hj_CallCharMethod_c);
void hj_CallCharMethod(void);
void hj_CallCharMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jchar r = R->CallCharMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jchar hj_CallCharMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallCharMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallCharMethodA(env, o, m, a);
}
void hj_CallNonvirtualCharMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualCharMethod, hj_CallNonvirtualCharMethod_c);
void hj_CallNonvirtualCharMethod(void);
void hj_CallNonvirtualCharMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jchar r = R->CallNonvirtualCharMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jchar hj_CallNonvirtualCharMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualCharMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualCharMethodA(env, o, c, m, a);
}
void hj_CallStaticCharMethod_c(gframe *f);
HJ_STUB(hj_CallStaticCharMethod, hj_CallStaticCharMethod_c);
void hj_CallStaticCharMethod(void);
void hj_CallStaticCharMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jchar r = R->CallStaticCharMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jchar hj_CallStaticCharMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticCharMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticCharMethodA(env, c, m, a);
}
void hj_CallShortMethod_c(gframe *f);
HJ_STUB(hj_CallShortMethod, hj_CallShortMethod_c);
void hj_CallShortMethod(void);
void hj_CallShortMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jshort r = R->CallShortMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jshort hj_CallShortMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallShortMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallShortMethodA(env, o, m, a);
}
void hj_CallNonvirtualShortMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualShortMethod, hj_CallNonvirtualShortMethod_c);
void hj_CallNonvirtualShortMethod(void);
void hj_CallNonvirtualShortMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jshort r = R->CallNonvirtualShortMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jshort hj_CallNonvirtualShortMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualShortMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualShortMethodA(env, o, c, m, a);
}
void hj_CallStaticShortMethod_c(gframe *f);
HJ_STUB(hj_CallStaticShortMethod, hj_CallStaticShortMethod_c);
void hj_CallStaticShortMethod(void);
void hj_CallStaticShortMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jshort r = R->CallStaticShortMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jshort hj_CallStaticShortMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticShortMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticShortMethodA(env, c, m, a);
}
void hj_CallIntMethod_c(gframe *f);
HJ_STUB(hj_CallIntMethod, hj_CallIntMethod_c);
void hj_CallIntMethod(void);
void hj_CallIntMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jint r = R->CallIntMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jint hj_CallIntMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallIntMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallIntMethodA(env, o, m, a);
}
void hj_CallNonvirtualIntMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualIntMethod, hj_CallNonvirtualIntMethod_c);
void hj_CallNonvirtualIntMethod(void);
void hj_CallNonvirtualIntMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jint r = R->CallNonvirtualIntMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jint hj_CallNonvirtualIntMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualIntMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualIntMethodA(env, o, c, m, a);
}
void hj_CallStaticIntMethod_c(gframe *f);
HJ_STUB(hj_CallStaticIntMethod, hj_CallStaticIntMethod_c);
void hj_CallStaticIntMethod(void);
void hj_CallStaticIntMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jint r = R->CallStaticIntMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jint hj_CallStaticIntMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticIntMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticIntMethodA(env, c, m, a);
}
void hj_CallLongMethod_c(gframe *f);
HJ_STUB(hj_CallLongMethod, hj_CallLongMethod_c);
void hj_CallLongMethod(void);
void hj_CallLongMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jlong r = R->CallLongMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jlong hj_CallLongMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallLongMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallLongMethodA(env, o, m, a);
}
void hj_CallNonvirtualLongMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualLongMethod, hj_CallNonvirtualLongMethod_c);
void hj_CallNonvirtualLongMethod(void);
void hj_CallNonvirtualLongMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jlong r = R->CallNonvirtualLongMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jlong hj_CallNonvirtualLongMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualLongMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualLongMethodA(env, o, c, m, a);
}
void hj_CallStaticLongMethod_c(gframe *f);
HJ_STUB(hj_CallStaticLongMethod, hj_CallStaticLongMethod_c);
void hj_CallStaticLongMethod(void);
void hj_CallStaticLongMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jlong r = R->CallStaticLongMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jlong hj_CallStaticLongMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticLongMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticLongMethodA(env, c, m, a);
}
void hj_CallFloatMethod_c(gframe *f);
HJ_STUB(hj_CallFloatMethod, hj_CallFloatMethod_c);
void hj_CallFloatMethod(void);
void hj_CallFloatMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jfloat r = R->CallFloatMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jfloat hj_CallFloatMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallFloatMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallFloatMethodA(env, o, m, a);
}
void hj_CallNonvirtualFloatMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualFloatMethod, hj_CallNonvirtualFloatMethod_c);
void hj_CallNonvirtualFloatMethod(void);
void hj_CallNonvirtualFloatMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jfloat r = R->CallNonvirtualFloatMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jfloat hj_CallNonvirtualFloatMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualFloatMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualFloatMethodA(env, o, c, m, a);
}
void hj_CallStaticFloatMethod_c(gframe *f);
HJ_STUB(hj_CallStaticFloatMethod, hj_CallStaticFloatMethod_c);
void hj_CallStaticFloatMethod(void);
void hj_CallStaticFloatMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jfloat r = R->CallStaticFloatMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jfloat hj_CallStaticFloatMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticFloatMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticFloatMethodA(env, c, m, a);
}
void hj_CallDoubleMethod_c(gframe *f);
HJ_STUB(hj_CallDoubleMethod, hj_CallDoubleMethod_c);
void hj_CallDoubleMethod(void);
void hj_CallDoubleMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jdouble r = R->CallDoubleMethodA(env, o, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jdouble hj_CallDoubleMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallDoubleMethodV(env, o, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallDoubleMethodA(env, o, m, a);
}
void hj_CallNonvirtualDoubleMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualDoubleMethod, hj_CallNonvirtualDoubleMethod_c);
void hj_CallNonvirtualDoubleMethod(void);
void hj_CallNonvirtualDoubleMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    jdouble r = R->CallNonvirtualDoubleMethodA(env, o, c, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jdouble hj_CallNonvirtualDoubleMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallNonvirtualDoubleMethodV(env, o, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallNonvirtualDoubleMethodA(env, o, c, m, a);
}
void hj_CallStaticDoubleMethod_c(gframe *f);
HJ_STUB(hj_CallStaticDoubleMethod, hj_CallStaticDoubleMethod_c);
void hj_CallStaticDoubleMethod(void);
void hj_CallStaticDoubleMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jdouble r = R->CallStaticDoubleMethodA(env, c, m, a);
    put_ret(f, &r, sizeof r, true);
}
static jdouble hj_CallStaticDoubleMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { return R->CallStaticDoubleMethodV(env, c, m, ap); }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->CallStaticDoubleMethodA(env, c, m, a);
}
void hj_CallVoidMethod_c(gframe *f);
HJ_STUB(hj_CallVoidMethod, hj_CallVoidMethod_c);
void hj_CallVoidMethod(void);
void hj_CallVoidMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    R->CallVoidMethodA(env, o, m, a);
}
static void hj_CallVoidMethodV(JNIEnv *env, jobject o, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { R->CallVoidMethodV(env, o, m, ap); return; }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    R->CallVoidMethodA(env, o, m, a);
}
void hj_CallNonvirtualVoidMethod_c(gframe *f);
HJ_STUB(hj_CallNonvirtualVoidMethod, hj_CallNonvirtualVoidMethod_c);
void hj_CallNonvirtualVoidMethod(void);
void hj_CallNonvirtualVoidMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jobject o = (jobject)f->gp[1]; jclass c = (jclass)f->gp[2]; jmethodID m = (jmethodID)f->gp[3];
    jvalue a[256]; frame_args(f, 4, m, a);
    R->CallNonvirtualVoidMethodA(env, o, c, m, a);
}
static void hj_CallNonvirtualVoidMethodV(JNIEnv *env, jobject o, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { R->CallNonvirtualVoidMethodV(env, o, c, m, ap); return; }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    R->CallNonvirtualVoidMethodA(env, o, c, m, a);
}
void hj_CallStaticVoidMethod_c(gframe *f);
HJ_STUB(hj_CallStaticVoidMethod, hj_CallStaticVoidMethod_c);
void hj_CallStaticVoidMethod(void);
void hj_CallStaticVoidMethod_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    R->CallStaticVoidMethodA(env, c, m, a);
}
static void hj_CallStaticVoidMethodV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) { R->CallStaticVoidMethodV(env, c, m, ap); return; }
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    R->CallStaticVoidMethodA(env, c, m, a);
}
void hj_NewObject_c(gframe *f);
HJ_STUB(hj_NewObject, hj_NewObject_c);
void hj_NewObject(void);
void hj_NewObject_c(gframe *f)
{
    JNIEnv *env = (JNIEnv *)f->gp[0]; jclass c = (jclass)f->gp[1]; jmethodID m = (jmethodID)f->gp[2];
    jvalue a[256]; frame_args(f, 3, m, a);
    jobject r = R->NewObjectA(env, c, m, a);
    put_ret(f, &r, sizeof r, false);
}
static jobject hj_NewObjectV(JNIEnv *env, jclass c, jmethodID m, va_list ap)
{
    if (!from_guest(__builtin_return_address(0))) return R->NewObjectV(env, c, m, ap);
    const char *shorty = art_husk_method_shorty(m);
    jvalue a[256];
    if (shorty) args_guest(shorty, (tl_va_list *)(void *)ap, a);
    return R->NewObjectA(env, c, m, a);
}

static void install_table(void)
{
    R = art_husk_jni_table();
    T = *R;
    T.CallObjectMethod = (void *)hj_CallObjectMethod; T.CallObjectMethodV = hj_CallObjectMethodV;
    T.CallNonvirtualObjectMethod = (void *)hj_CallNonvirtualObjectMethod; T.CallNonvirtualObjectMethodV = hj_CallNonvirtualObjectMethodV;
    T.CallStaticObjectMethod = (void *)hj_CallStaticObjectMethod; T.CallStaticObjectMethodV = hj_CallStaticObjectMethodV;
    T.CallBooleanMethod = (void *)hj_CallBooleanMethod; T.CallBooleanMethodV = hj_CallBooleanMethodV;
    T.CallNonvirtualBooleanMethod = (void *)hj_CallNonvirtualBooleanMethod; T.CallNonvirtualBooleanMethodV = hj_CallNonvirtualBooleanMethodV;
    T.CallStaticBooleanMethod = (void *)hj_CallStaticBooleanMethod; T.CallStaticBooleanMethodV = hj_CallStaticBooleanMethodV;
    T.CallByteMethod = (void *)hj_CallByteMethod; T.CallByteMethodV = hj_CallByteMethodV;
    T.CallNonvirtualByteMethod = (void *)hj_CallNonvirtualByteMethod; T.CallNonvirtualByteMethodV = hj_CallNonvirtualByteMethodV;
    T.CallStaticByteMethod = (void *)hj_CallStaticByteMethod; T.CallStaticByteMethodV = hj_CallStaticByteMethodV;
    T.CallCharMethod = (void *)hj_CallCharMethod; T.CallCharMethodV = hj_CallCharMethodV;
    T.CallNonvirtualCharMethod = (void *)hj_CallNonvirtualCharMethod; T.CallNonvirtualCharMethodV = hj_CallNonvirtualCharMethodV;
    T.CallStaticCharMethod = (void *)hj_CallStaticCharMethod; T.CallStaticCharMethodV = hj_CallStaticCharMethodV;
    T.CallShortMethod = (void *)hj_CallShortMethod; T.CallShortMethodV = hj_CallShortMethodV;
    T.CallNonvirtualShortMethod = (void *)hj_CallNonvirtualShortMethod; T.CallNonvirtualShortMethodV = hj_CallNonvirtualShortMethodV;
    T.CallStaticShortMethod = (void *)hj_CallStaticShortMethod; T.CallStaticShortMethodV = hj_CallStaticShortMethodV;
    T.CallIntMethod = (void *)hj_CallIntMethod; T.CallIntMethodV = hj_CallIntMethodV;
    T.CallNonvirtualIntMethod = (void *)hj_CallNonvirtualIntMethod; T.CallNonvirtualIntMethodV = hj_CallNonvirtualIntMethodV;
    T.CallStaticIntMethod = (void *)hj_CallStaticIntMethod; T.CallStaticIntMethodV = hj_CallStaticIntMethodV;
    T.CallLongMethod = (void *)hj_CallLongMethod; T.CallLongMethodV = hj_CallLongMethodV;
    T.CallNonvirtualLongMethod = (void *)hj_CallNonvirtualLongMethod; T.CallNonvirtualLongMethodV = hj_CallNonvirtualLongMethodV;
    T.CallStaticLongMethod = (void *)hj_CallStaticLongMethod; T.CallStaticLongMethodV = hj_CallStaticLongMethodV;
    T.CallFloatMethod = (void *)hj_CallFloatMethod; T.CallFloatMethodV = hj_CallFloatMethodV;
    T.CallNonvirtualFloatMethod = (void *)hj_CallNonvirtualFloatMethod; T.CallNonvirtualFloatMethodV = hj_CallNonvirtualFloatMethodV;
    T.CallStaticFloatMethod = (void *)hj_CallStaticFloatMethod; T.CallStaticFloatMethodV = hj_CallStaticFloatMethodV;
    T.CallDoubleMethod = (void *)hj_CallDoubleMethod; T.CallDoubleMethodV = hj_CallDoubleMethodV;
    T.CallNonvirtualDoubleMethod = (void *)hj_CallNonvirtualDoubleMethod; T.CallNonvirtualDoubleMethodV = hj_CallNonvirtualDoubleMethodV;
    T.CallStaticDoubleMethod = (void *)hj_CallStaticDoubleMethod; T.CallStaticDoubleMethodV = hj_CallStaticDoubleMethodV;
    T.CallVoidMethod = (void *)hj_CallVoidMethod; T.CallVoidMethodV = hj_CallVoidMethodV;
    T.CallNonvirtualVoidMethod = (void *)hj_CallNonvirtualVoidMethod; T.CallNonvirtualVoidMethodV = hj_CallNonvirtualVoidMethodV;
    T.CallStaticVoidMethod = (void *)hj_CallStaticVoidMethod; T.CallStaticVoidMethodV = hj_CallStaticVoidMethodV;
    T.NewObject = (void *)hj_NewObject; T.NewObjectV = hj_NewObjectV;
    art_husk_set_jni_table(&T);
}

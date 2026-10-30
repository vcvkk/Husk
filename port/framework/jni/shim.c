/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The framework natives' interface on JNI (shim.h). */
#include "shim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local shim_call *t_call;

void shim_begin(shim_call *c, JNIEnv *env)
{
    c->env = env;
    c->objs = NULL;
    c->outer = t_call;
    t_call = c;
}

JNIEnv *shim_env(void) { return t_call ? t_call->env : NULL; }

static jobj *new_jobj(void)
{
    jobj *o = calloc(1, sizeof *o);
    o->next = t_call->objs;
    t_call->objs = o;
    return o;
}

/* An array's elements, to read and write in place (get), then written back and let go */
static void *elements(JNIEnv *env, jarray a, char elem, bool get, ...)
{
    void *p = NULL;
    if (!get) { va_list ap; va_start(ap, get); p = va_arg(ap, void *); va_end(ap); }
#define E(c, T, N) case c: if (get) return (*env)->Get##N##ArrayElements(env, (T)a, NULL); \
                           (*env)->Release##N##ArrayElements(env, (T)a, p, 0); return NULL;
    switch (elem) {
    E('I', jintArray, Int) E('F', jfloatArray, Float) E('B', jbyteArray, Byte) E('J', jlongArray, Long)
    E('S', jshortArray, Short) E('C', jcharArray, Char) E('Z', jbooleanArray, Boolean) E('D', jdoubleArray, Double)
    }
#undef E
    return NULL;
}

jobj *shim_in(jobject ref, char kind)
{
    if (!ref) return NULL;
    JNIEnv *env = t_call->env;
    jobj *o = new_jobj();
    o->ref = ref;
    /* the classes told apart, looked up once (every object parameter of every native call came here: a GL call with an array
     * did ten FindClass) */
    static jclass k_string, k_objs, k_prim[8];
    static const struct { const char *desc; char elem; } prims[] = {
        { "[I", 'I' }, { "[F", 'F' }, { "[B", 'B' }, { "[J", 'J' }, { "[S", 'S' }, { "[C", 'C' }, { "[Z", 'Z' }, { "[D", 'D' },
    };
    if (!k_string) {
        jclass t;
        for (size_t i = 0; i < 8; i++) { t = (*env)->FindClass(env, prims[i].desc); k_prim[i] = (*env)->NewGlobalRef(env, t); (*env)->DeleteLocalRef(env, t); }
        t = (*env)->FindClass(env, "[Ljava/lang/Object;"); k_objs = (*env)->NewGlobalRef(env, t); (*env)->DeleteLocalRef(env, t);
        t = (*env)->FindClass(env, "java/lang/String"); k_string = (*env)->NewGlobalRef(env, t); (*env)->DeleteLocalRef(env, t);
    }
    if ((*env)->IsInstanceOf(env, ref, k_string)) {
        o->kind = TL_K_STRING;
        return o;
    }
    (void)kind;
    if ((*env)->IsInstanceOf(env, ref, k_objs)) {
        o->kind = TL_K_OBJ_ARRAY;
        o->oarr.len = (uint32_t)(*env)->GetArrayLength(env, (jarray)ref);
        o->oarr.v = calloc(o->oarr.len ? o->oarr.len : 1, sizeof *o->oarr.v);
        for (uint32_t i = 0; i < o->oarr.len; i++)
            o->oarr.v[i] = shim_in((*env)->GetObjectArrayElement(env, (jobjectArray)ref, (jsize)i), 'L');
        return o;
    }
    for (size_t i = 0; i < 8; i++) {
        if ((*env)->IsInstanceOf(env, ref, k_prim[i])) {
            o->kind = TL_K_PRIM_ARRAY;
            o->arr.elem = prims[i].elem;
            o->arr.len = (uint32_t)(*env)->GetArrayLength(env, (jarray)ref);
            o->arr.data = elements(env, (jarray)ref, o->arr.elem, true);
            break;
        }
    }
    return o;
}

/* An array a native made (tl_jni_new_prim_array) becomes a Java array here. */
jobject shim_out(jobj *o)
{
    if (!o) return NULL;
    if (o->ref) return o->ref;
    JNIEnv *env = t_call->env;
    if (o->kind == TL_K_PRIM_ARRAY) {
        jarray a = NULL;
        switch (o->arr.elem) {
        case 'B': a = (*env)->NewByteArray(env, (jsize)o->arr.len);
                  (*env)->SetByteArrayRegion(env, a, 0, (jsize)o->arr.len, o->arr.data); break;
        case 'I': a = (*env)->NewIntArray(env, (jsize)o->arr.len);
                  (*env)->SetIntArrayRegion(env, a, 0, (jsize)o->arr.len, o->arr.data); break;
        case 'F': a = (*env)->NewFloatArray(env, (jsize)o->arr.len);
                  (*env)->SetFloatArrayRegion(env, a, 0, (jsize)o->arr.len, o->arr.data); break;
        }
        return a;
    }
    return NULL;
}

void shim_end(shim_call *c)
{
    JNIEnv *env = c->env;
    for (jobj *o = c->objs, *next; o; o = next) {
        next = o->next;
        if (o->kind == TL_K_PRIM_ARRAY && o->ref) elements(env, (jarray)o->ref, o->arr.elem, false, o->arr.data);
        else if (o->kind == TL_K_PRIM_ARRAY) free(o->arr.data);
        if (o->kind == TL_K_OBJ_ARRAY) free(o->oarr.v);
        if (o->utf8) (*env)->ReleaseStringUTFChars(env, (jstring)o->ref, o->utf8);
        if (o->utf16) (*env)->ReleaseStringChars(env, (jstring)o->ref, o->utf16);
        free(o);
    }
    t_call = c->outer;
}

bool shim_register(JNIEnv *env, const char *cls, const JNINativeMethod *m, int n)
{
    jclass c = (*env)->FindClass(env, cls);
    if (!c) { (*env)->ExceptionClear(env); tl_log_line("husk-port: no class %s for its natives", cls); return false; }
    bool ok = (*env)->RegisterNatives(env, c, m, n) == 0;
    if (!ok) {
        /* ART takes all or none: one method the class lacks (a framework jar older than the natives) and none are
           registered. Each on its own then, so only what is missing is missing. */
        (*env)->ExceptionClear(env);
        for (int i = 0; i < n; i++)
            if ((*env)->RegisterNatives(env, c, &m[i], 1) != 0) {
                (*env)->ExceptionClear(env);
                tl_log_line("husk-port: %s has no %s%s for its native", cls, m[i].name, m[i].signature);
            }
    }
    (*env)->DeleteLocalRef(env, c);
    return ok;
}

/* ---- what the natives call */

void tl_log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

const char *tl_jni_string(jobj *s)
{
    if (!s || s->kind != TL_K_STRING) return NULL;
    if (!s->utf8) s->utf8 = (*t_call->env)->GetStringUTFChars(t_call->env, (jstring)s->ref, NULL);
    return s->utf8;
}

const uint16_t *tl_dvm_string_chars(jobj *s, int32_t *len)
{
    if (!s || s->kind != TL_K_STRING) return NULL;
    if (!s->utf16) {
        s->utf16 = (*t_call->env)->GetStringChars(t_call->env, (jstring)s->ref, NULL);
        s->utf16_len = (*t_call->env)->GetStringLength(t_call->env, (jstring)s->ref);
    }
    *len = s->utf16_len;
    return s->utf16;
}

jobj *tl_jni_wrap(jobject ref)
{
    if (!ref) return NULL;
    jobj *o = new_jobj();
    o->ref = ref;
    return o;
}

jobj *tl_jni_new_string(const char *utf8)
{
    JNIEnv *env = shim_env();
    return env && utf8 ? tl_jni_wrap((*env)->NewStringUTF(env, utf8)) : NULL;
}

jobj *tl_jni_new_prim_array(char elem, uint32_t len)
{
    jobj *o = new_jobj();
    size_t size = elem == 'B' || elem == 'Z' ? 1 : elem == 'S' || elem == 'C' ? 2 : elem == 'J' || elem == 'D' ? 8 : 4;
    o->kind = TL_K_PRIM_ARRAY;
    o->arr.elem = elem;
    o->arr.len = len;
    o->arr.data = calloc(len ? len : 1, size);
    return o;
}

jvalue tl_jni_get_field(jobj *o, const char *name, const char *sig)
{
    jvalue v = { 0 };
    JNIEnv *env = shim_env();
    if (!env || !o || !o->ref) return v;
    jclass c = (*env)->GetObjectClass(env, o->ref);
    jfieldID f = (*env)->GetFieldID(env, c, name, sig);
    if (f && sig[0] == 'J') v.j = (*env)->GetLongField(env, o->ref, f);
    else if (f && sig[0] == 'I') v.i = (*env)->GetIntField(env, o->ref, f);
    (*env)->DeleteLocalRef(env, c);
    return v;
}

bool tl_jni_pending(void) { JNIEnv *env = shim_env(); return env && (*env)->ExceptionCheck(env); }
void tl_jni_clear(void) { JNIEnv *env = shim_env(); if (env) (*env)->ExceptionClear(env); }

bool dvm_throw(const char *cls, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    JNIEnv *env = shim_env();
    if (env) {
        jclass c = (*env)->FindClass(env, cls);
        if (c) (*env)->ThrowNew(env, c, msg);
    }
    return false;
}

/* tl_path_resolve: the guest linker's bionic's (port/native/tl/husk-tl-bionic-io.c), which resolves Android's paths for the
 * app's libraries too; with no system fonts folder set it gives a /system path back as it is, and gfx.c takes CoreText's then */

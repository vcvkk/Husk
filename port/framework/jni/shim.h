/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The interface the framework's natives were written against in Husk's main line (its Dalvik
 * interpreter's: husk-tl-dvm-internal.h), on JNI, so that they compile here as they are.
 *
 * A native there is bool fn(jobj *self, const jvalue *args, jvalue *ret): parameters in order, one
 * jvalue each, objects as jobj pointers, false when it threw. Here each runs inside a JNI method
 * that gen-jni.py writes from the native's table: it makes a jobj for every object parameter
 * (an array's elements pinned, a string's characters fetched on demand), calls the native, and
 * then gives back what the jobjs hold. A jobj lives as long as that call.
 */
#ifndef HUSK_PORT_FRAMEWORK_SHIM_H
#define HUSK_PORT_FRAMEWORK_SHIM_H
#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TL_K_OBJECT = 0, TL_K_PRIM_ARRAY = 1, TL_K_STRING = 2 };

typedef struct jobj {
    int kind;
    jobject ref;                     /* the JNI reference: a parameter's, or a returned array's */
    struct { uint32_t len; void *data; char elem; } arr;
    const char *utf8;                /* a string's, once asked for */
    const jchar *utf16;
    jsize utf16_len;
    struct jobj *next;               /* the call's jobjs, given back at its end */
} jobj;

typedef bool (*dvm_native_fn)(jobj *self, const jvalue *args, jvalue *ret);

/* the JNI method a native runs in */
typedef struct shim_call {
    JNIEnv *env;
    jobj *objs;                      /* every jobj of the call */
    struct shim_call *outer;
} shim_call;

void shim_begin(shim_call *c, JNIEnv *env);
jobj *shim_in(jobject o, char kind);   /* kind: the parameter's descriptor letter ('[', 'L') */
jobject shim_out(jobj *o);             /* a returned jobj as a local reference */
void shim_end(shim_call *c);           /* arrays' elements written back, strings let go */
JNIEnv *shim_env(void);
/* RegisterNatives for one class from a gen-jni.py table */
bool shim_register(JNIEnv *env, const char *cls, const JNINativeMethod *m, int n);

/* ---- what the natives call */
void tl_log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *tl_jni_string(jobj *s);                          /* UTF-8 */
const uint16_t *tl_dvm_string_chars(jobj *s, int32_t *len);  /* UTF-16 */
jobj *tl_jni_new_prim_array(char elem, uint32_t len);
jvalue tl_jni_get_field(jobj *o, const char *name, const char *sig);
bool tl_jni_pending(void);
void tl_jni_clear(void);
bool dvm_throw(const char *cls, const char *fmt, ...) __attribute__((format(printf, 2, 3)));   /* always false */
/* a path the app sees (/system/fonts/...) as one on this system, or NULL */
const char *tl_path_resolve(const char *path, char *buf, size_t n);

#endif

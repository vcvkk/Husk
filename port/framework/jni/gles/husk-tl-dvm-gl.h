/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What the generated GLES natives (gles/*.c, Husk's main line's, made by its gen/gles20.py and gen/gles.py) use, on JNI through
 * shim.h: GL functions through ANGLE, and Java arrays and java.nio buffers as pointers (gl.c). */
#ifndef HUSK_PORT_GLES_GL_H
#define HUSK_PORT_GLES_GL_H
#include "shim.h"

typedef struct { const char *name, *sig; dvm_native_fn fn; } gl_native;

void *gl_fn(const char *name);
const char *gl_str(jobj *s);                    /* UTF-8; NULL for null */
void *gl_array(jobj *arr, int32_t offset);      /* the elements from offset, or NULL */
void *gl_buffer(jobj *buffer);                  /* a java.nio buffer's elements from its position, or NULL */

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What the main line's native-library layer (tl/) takes from its host, here: the parts its app provides (EGL and Vulkan through
 * ANGLE and MoltenVK, OpenSL ES, its Java world's JNI, StikDebug's JIT) are not part of this port yet, and answer "none".
 *
 * Its Java world's JNI (tl_jni_*, over the main line's own objects) is a different thing from the framework's natives' shim
 * (port/framework/jni/shim.h, over ART): build.sh renames the layer's calls to tln_*, which are these.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"
#include "husk-tl-internal.h"
#include "husk-tl-jni.h"

const tl_bionic_entry tl_tab_egl[] = { TL_END };
const tl_bionic_entry tl_tab_opensles[] = { TL_END };
const tl_bionic_entry tl_tab_jvm[] = { TL_END };
void *tl_egl_resolve(const char *name) { (void)name; return NULL; }
void *tl_vk_resolve(const char *name) { (void)name; return NULL; }
bool tl_vk_available(void) { return false; }

void (*tl_cocos_audio_hook)(const int16_t *samples, int frames, int channels, int rate);
/* the JIT region StikDebug (or the debugged process itself) made at launch (ios/husk-ios-jit.c, husk_app_jit_prewarm) */
typedef struct { uint8_t *rw_addr, *rx_addr; size_t size; } husk_mapping;
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
husk_mapping *husk_ios_jit_get_mapping(void);
#else
static husk_mapping *husk_ios_jit_get_mapping(void) { return NULL; }   /* a Mac makes its own (husk-tl-xmem.c) */
#endif
tl_dual_mapping *tl_find_stikdebug_prewarmed(void)
{
    husk_mapping *m = husk_ios_jit_get_mapping();
    if (!m || !m->rw_addr || !m->rx_addr) return NULL;
    static tl_dual_mapping d;
    d.rw_addr = m->rw_addr; d.rx_addr = m->rx_addr; d.size = m->size;
    return &d;
}
bool tl_assetstream_stored(const char *name, int *fd, int64_t *off, int64_t *len) { (void)name; (void)fd; (void)off; (void)len; return false; }
int tl_dns_servers(char out[][64], int max) { (void)out; (void)max; return 0; }

/* Android's bitmaps to native code (jnigraphics, tl_AndroidBitmap_*): the framework's, over its own bitmaps (gfx.c) */

/* the layer's own Java world: none here */
jvalue tln_jni_call(jobj *o, const char *name, const char *sig, const jvalue *args) { (void)o; (void)name; (void)sig; (void)args; jvalue v; memset(&v, 0, sizeof v); return v; }
jvalue tln_jni_get_field(jobj *o, const char *name, const char *sig) { (void)o; (void)name; (void)sig; jvalue v; memset(&v, 0, sizeof v); return v; }
jobj *tln_jni_ref(jobj *o) { return o; }
bool tln_jni_pending(void) { return false; }
void tln_jni_clear(void) {}

/* the framework's (port/framework/jni/shim.c) where it is linked in; this one for the layer on its own */
__attribute__((weak)) void tl_log_line(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The VM's life cycle, strings, exceptions, and the native methods of the boot
 * library (src/dvm/bootlib). The boot library is Java wherever Java can do the
 * job; what is here is what cannot be written in Java -- object identity, class
 * metadata, array copies, the bits of floating-point numbers, output.
 */
#include "dvm-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef HUSK_DVM_ZIP
#include "../translation-layer/husk-tl-internal.h"
#endif

void dvm_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("dvm: fatal: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    abort();
}

/* ------------------------------------------------------------- strings */

DvmArray *dvm_string_chars(DvmVM *vm, DvmObject *s)
{
    return s ? (DvmArray *)DVM_OBJ(DVM_FIELDS(s)[vm->slot_string_value]) : NULL;
}

DvmObject *dvm_new_string_utf16(DvmVM *vm, const uint16_t *chars, int32_t len)
{
    DvmArray *a = dvm_alloc_array(vm, vm->c_char_array, len);
    if (!a) return NULL;
    if (len) memcpy(a->data, chars, (size_t)len * 2);
    DvmObject *s = dvm_alloc_object(vm, vm->c_string);
    if (!s) return NULL;
    DVM_FIELDS(s)[vm->slot_string_value] = DVM_REF(a);
    return s;
}

/* UTF-8 and MUTF-8 both: a 4-byte sequence becomes a surrogate pair, a 3-byte surrogate stays one char. */
static int32_t decode_utf8(const char *src, uint16_t *out)
{
    const uint8_t *p = (const uint8_t *)src;
    int32_t n = 0;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xf0 && p[0] && p[1] && p[2]) {
            c = ((c & 0x07) << 18) | ((p[0] & 0x3f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            p += 3;
            c -= 0x10000;
            if (out) { out[n] = (uint16_t)(0xd800 + (c >> 10)); out[n + 1] = (uint16_t)(0xdc00 + (c & 0x3ff)); }
            n += 2;
            continue;
        }
        if (c >= 0xe0 && p[0] && p[1]) { c = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f); p += 2; }
        else if (c >= 0xc0 && p[0]) { c = ((c & 0x1f) << 6) | (p[0] & 0x3f); p += 1; }
        if (out) out[n] = (uint16_t)c;
        n++;
    }
    return n;
}

DvmObject *dvm_new_string_mutf8(DvmVM *vm, const char *s)
{
    int32_t n = decode_utf8(s, NULL);
    uint16_t stackbuf[256], *buf = n <= 256 ? stackbuf : malloc((size_t)n * 2);
    decode_utf8(s, buf);
    DvmObject *r = dvm_new_string_utf16(vm, buf, n);
    if (buf != stackbuf) free(buf);
    return r;
}

DvmObject *dvm_new_string(DvmVM *vm, const char *utf8) { return dvm_new_string_mutf8(vm, utf8); }

static char *utf16_to_utf8(const uint16_t *c, int32_t n, size_t *outlen)
{
    char *out = malloc((size_t)n * 3 + 1), *w = out;
    for (int32_t i = 0; i < n; i++) {
        uint32_t u = c[i];
        if (u >= 0xd800 && u < 0xdc00 && i + 1 < n && c[i + 1] >= 0xdc00 && c[i + 1] < 0xe000) {
            u = 0x10000 + ((u - 0xd800) << 10) + (c[i + 1] - 0xdc00);
            i++;
            *w++ = (char)(0xf0 | (u >> 18)); *w++ = (char)(0x80 | ((u >> 12) & 0x3f));
            *w++ = (char)(0x80 | ((u >> 6) & 0x3f)); *w++ = (char)(0x80 | (u & 0x3f));
        } else if (u < 0x80) *w++ = (char)u;
        else if (u < 0x800) { *w++ = (char)(0xc0 | (u >> 6)); *w++ = (char)(0x80 | (u & 0x3f)); }
        else { *w++ = (char)(0xe0 | (u >> 12)); *w++ = (char)(0x80 | ((u >> 6) & 0x3f)); *w++ = (char)(0x80 | (u & 0x3f)); }
    }
    *w = 0;
    if (outlen) *outlen = (size_t)(w - out);
    return out;
}

char *dvm_string_utf8(DvmVM *vm, DvmObject *s)
{
    DvmArray *a = dvm_string_chars(vm, s);
    if (!s) return NULL;
    if (!a) return strdup("");
    return utf16_to_utf8((const uint16_t *)a->data, a->length, NULL);
}

/* ------------------------------------------------------------- exceptions */

static int throw_nesting;

void dvm_throw_object(DvmVM *vm, DvmObject *ex) { vm->exception = ex; }

void dvm_throw(DvmVM *vm, const char *desc, const char *fmt, ...)
{
    char msg[1024] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
    }
    if (throw_nesting > 4) dvm_fatal("exceptions while throwing %s: %s", desc, msg);
    DvmClass *c = dvm_lookup_class(vm, desc);
    if (!c) dvm_fatal("%s (not in the boot library): %s", desc, msg);

    /* Constructing the exception runs Java, which must work even at the stack limit. */
    throw_nesting++;
    vm->exception = NULL;
    vm->depth_limit += 64;
    vm->reg_limit += 1u << 14;
    DvmObject *o = NULL;
    if (dvm_init_class(vm, c) && (o = dvm_alloc_object(vm, c))) {
        DvmObject *s = fmt ? dvm_new_string(vm, msg) : NULL;
        DvmMethod *ctor = dvm_find_declared_method(c, "<init>", "(Ljava/lang/String;)V");
        if (ctor) dvm_callv(vm, ctor, NULL, DVM_REF(o), DVM_REF(s));
        else if ((ctor = dvm_find_declared_method(c, "<init>", "()V"))) dvm_callv(vm, ctor, NULL, DVM_REF(o));
    }
    vm->depth_limit -= 64;
    vm->reg_limit -= 1u << 14;
    throw_nesting--;
    if (!vm->exception) vm->exception = o;
}

/* The backtrace is a long[] of (method, pc) pairs; names are made only if asked for. */
bool dvm_fill_in_stack_trace(DvmVM *vm, DvmObject *t)
{
    DvmFrame *f = vm->frame;
    /* skip the natives and Java methods that are filling it in, and the exception's constructors */
    while (f && (!strcmp(f->method->name, "nativeFillInStackTrace") || !strcmp(f->method->name, "fillInStackTrace"))) f = f->prev;
    while (f && !strcmp(f->method->name, "<init>") && dvm_is_subclass(f->method->clazz, vm->c_throwable)) f = f->prev;
    int n = 0;
    for (DvmFrame *g = f; g; g = g->prev) n++;
    DvmArray *a = dvm_alloc_array_of(vm, "[J", n * 2);
    if (!a) return false;
    int64_t *e = (int64_t *)a->data;
    for (int i = 0; f; f = f->prev, i++) { e[2 * i] = (int64_t)(uintptr_t)f->method; e[2 * i + 1] = f->pc; }
    DVM_FIELDS(t)[vm->slot_throwable_backtrace] = DVM_REF(a);
    return true;
}

static int line_for_pc(const DvmMethod *m, uint32_t pc)
{
    /* code_item.debug_info_off -> debug_info_item: the line program */
    if (!m->insns) return -1;
    const uint8_t *ci = (const uint8_t *)m->insns - 16;
    uint32_t off;
    memcpy(&off, ci + 8, 4);
    DvmDex *dex = m->clazz->dex;
    if (!off || off >= dex->size) return -1;
    const uint8_t *p = dex->data + off;
    int32_t line = (int32_t)dvm_uleb(&p);
    uint32_t nparams = dvm_uleb(&p);
    for (uint32_t i = 0; i < nparams; i++) dvm_uleb(&p);
    uint32_t addr = 0;
    int32_t best = -1;
    for (;;) {
        uint8_t op = *p++;
        switch (op) {
        case 0x00: return best;
        case 0x01: addr += dvm_uleb(&p); break;
        case 0x02: line += dvm_sleb(&p); break;
        case 0x03: dvm_uleb(&p); dvm_uleb(&p); dvm_uleb(&p); break;
        case 0x04: dvm_uleb(&p); dvm_uleb(&p); dvm_uleb(&p); dvm_uleb(&p); break;
        case 0x05: case 0x06: dvm_uleb(&p); break;
        case 0x07: case 0x08: break;
        case 0x09: dvm_uleb(&p); break;
        default: {
            int adj = op - 0x0a;
            line += -4 + (adj % 15);
            addr += adj / 15;
            if (addr > pc) return best;
            best = line;
            break;
        }
        }
    }
}

static void frame_string(const DvmMethod *m, uint32_t pc, char *out, size_t n)
{
    char cls[256];
    size_t w = 0;
    for (const char *s = m->clazz->descriptor + 1; *s && *s != ';' && w + 1 < sizeof(cls); s++) cls[w++] = *s == '/' ? '.' : *s;
    cls[w] = 0;
    const char *src = NULL;
    if (m->clazz->class_def >= 0) {
        uint32_t si;
        memcpy(&si, m->clazz->dex->class_defs + 32 * m->clazz->class_def + 16, 4);
        if (si != 0xffffffffu) src = dvm_dex_string(m->clazz->dex, si);
    }
    if (m->access & ACC_NATIVE) { snprintf(out, n, "%s.%s(Native Method)", cls, m->name); return; }
    int line = line_for_pc(m, pc);
    if (src && line >= 0) snprintf(out, n, "%s.%s(%s:%d)", cls, m->name, src, line);
    else snprintf(out, n, "%s.%s(%s)", cls, m->name, src ? src : "Unknown Source");
}

void dvm_print_exception(DvmVM *vm, DvmObject *ex)
{
    vm->exception = NULL;
    DvmMethod *pst = dvm_find_method(ex->clazz, "printStackTrace", "()V");
    if (pst && dvm_callv(vm, pst, NULL, DVM_REF(ex))) return;
    vm->exception = NULL;
    fprintf(stderr, "%s (printStackTrace failed)\n", ex->clazz->descriptor);
}

/* ------------------------------------------------------------- natives */

#define A_I(i)  ((int32_t)args[i])
#define A_J(i)  ((int64_t)args[i])
#define A_O(i)  DVM_OBJ(args[i])
#define RET_I(x) (*ret = (uint32_t)(int32_t)(x))
#define RET_O(x) (*ret = DVM_REF(x))

static float a_f(DvmReg r) { float f; uint32_t u = (uint32_t)r; memcpy(&f, &u, 4); return f; }
static double a_d(DvmReg r) { double d; memcpy(&d, &r, 8); return d; }
static DvmReg r_d(double d) { DvmReg r; memcpy(&r, &d, 8); return r; }

static DvmClass *class_of(DvmVM *vm, DvmObject *class_obj) { return (DvmClass *)(uintptr_t)DVM_FIELDS(class_obj)[vm->slot_class_vm]; }

static bool Object_getClass(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmObject *o = dvm_class_object(vm, A_O(0)->clazz);
    if (!o) return false;
    RET_O(o);
    return true;
}
static bool Object_hashCode(DvmVM *vm, DvmReg *args, DvmReg *ret) { RET_I(dvm_identity_hash(vm, A_O(0))); return true; }
static bool Object_clone(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmObject *o = A_O(0);
    DvmClass *cl = dvm_lookup_class(vm, "Ljava/lang/Cloneable;");
    if (!dvm_is_array(o->clazz) && !(cl && dvm_instance_of(o, cl))) {
        dvm_throw(vm, "Ljava/lang/CloneNotSupportedException;", "Class %s doesn't implement Cloneable", o->clazz->descriptor);
        return false;
    }
    DvmObject *c = dvm_clone(vm, o);
    if (!c) return false;
    RET_O(c);
    return true;
}
static bool nop(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; (void)args; (void)ret; return true; }

static void class_name(const DvmClass *c, char *out, size_t n)
{
    static const struct { char t; const char *name; } prims[] = {
        { 'Z', "boolean" }, { 'B', "byte" }, { 'C', "char" }, { 'S', "short" }, { 'I', "int" },
        { 'J', "long" }, { 'F', "float" }, { 'D', "double" }, { 'V', "void" },
    };
    if (c->prim && !dvm_is_array(c)) {
        for (size_t i = 0; i < sizeof(prims) / sizeof(prims[0]); i++)
            if (prims[i].t == c->prim) { snprintf(out, n, "%s", prims[i].name); return; }
    }
    const char *d = c->descriptor;
    size_t w = 0;
    if (d[0] == 'L') d++;
    for (; *d && w + 1 < n; d++) {
        if (*d == ';' && c->descriptor[0] == 'L') break;
        out[w++] = *d == '/' ? '.' : *d;
    }
    out[w] = 0;
}

static bool Class_getName(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    char name[512];
    class_name(class_of(vm, A_O(0)), name, sizeof(name));
    DvmObject *s = dvm_new_string(vm, name);
    if (!s) return false;
    RET_O(s);
    return true;
}
static bool Class_isInterface(DvmVM *vm, DvmReg *args, DvmReg *ret) { RET_I(dvm_is_interface(class_of(vm, A_O(0)))); return true; }
static bool Class_isArray(DvmVM *vm, DvmReg *args, DvmReg *ret) { RET_I(dvm_is_array(class_of(vm, A_O(0)))); return true; }
static bool Class_isPrimitive(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmClass *c = class_of(vm, A_O(0));
    RET_I(c->prim && !dvm_is_array(c));
    return true;
}
static bool Class_getSuperclass(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmClass *c = class_of(vm, A_O(0));
    DvmObject *o = (c->super && !dvm_is_interface(c)) ? dvm_class_object(vm, c->super) : NULL;
    RET_O(o);
    return true;
}
static bool Class_getComponentType(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmClass *c = class_of(vm, A_O(0));
    RET_O(c->component ? dvm_class_object(vm, c->component) : NULL);
    return true;
}
static bool Class_isInstance(DvmVM *vm, DvmReg *args, DvmReg *ret) { RET_I(dvm_instance_of(A_O(1), class_of(vm, A_O(0)))); return true; }
static bool Class_isAssignableFrom(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    if (!A_O(1)) { dvm_throw(vm, "Ljava/lang/NullPointerException;", NULL); return false; }
    RET_I(dvm_is_subclass(class_of(vm, A_O(1)), class_of(vm, A_O(0))));
    return true;
}
static bool Class_forName(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    char *name = dvm_string_utf8(vm, A_O(0));
    if (!name) { dvm_throw(vm, "Ljava/lang/NullPointerException;", NULL); return false; }
    size_t l = strlen(name);
    char *desc = malloc(l + 3);
    if (name[0] == '[') strcpy(desc, name);
    else snprintf(desc, l + 3, "L%s;", name);
    for (char *p = desc; *p; p++) if (*p == '.') *p = '/';
    DvmClass *c = dvm_lookup_class(vm, desc);
    free(desc);
    if (!c) { dvm_throw(vm, "Ljava/lang/ClassNotFoundException;", "%s", name); free(name); return false; }
    free(name);
    if (!dvm_init_class(vm, c)) return false;
    RET_O(dvm_class_object(vm, c));
    return true;
}
static bool Class_getPrimitiveClass(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    static const char *names[] = { "boolean", "byte", "char", "short", "int", "long", "float", "double", "void" };
    static const char types[] = "ZBCSIJFDV";
    char *n = dvm_string_utf8(vm, A_O(0));
    DvmClass *c = NULL;
    for (int i = 0; n && i < 9; i++) if (!strcmp(n, names[i])) c = dvm_primitive_class(vm, types[i]);
    free(n);
    RET_O(c ? dvm_class_object(vm, c) : NULL);
    return true;
}
static bool Class_getEnumConstantsShared(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmClass *c = class_of(vm, A_O(0));
    if (!(c->access & ACC_ENUM)) { RET_O(NULL); return true; }
    char proto[600];
    snprintf(proto, sizeof(proto), "()[%s", c->descriptor);
    DvmMethod *values = dvm_find_declared_method(c, "values", proto);
    if (!values) { RET_O(NULL); return true; }
    if (!dvm_init_class(vm, c)) return false;
    return dvm_call(vm, values, NULL, ret);
}

static bool Arrays_newArray0(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmClass *comp = class_of(vm, A_O(0));
    if (comp->prim == 'V') { dvm_throw(vm, "Ljava/lang/IllegalArgumentException;", "void array"); return false; }
    DvmArray *a = dvm_alloc_array(vm, dvm_array_class_of(vm, comp), A_I(1));
    if (!a) return false;
    RET_O(a);
    return true;
}

static bool Array_newArray(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    if (!A_O(0)) { dvm_throw(vm, "Ljava/lang/NullPointerException;", "component type"); return false; }
    DvmClass *comp = class_of(vm, A_O(0));
    if (comp->prim == 'V' && !dvm_is_array(comp)) { dvm_throw(vm, "Ljava/lang/IllegalArgumentException;", "void array"); return false; }
    DvmArray *a = dvm_alloc_array(vm, dvm_array_class_of(vm, comp), A_I(1));
    if (!a) return false;
    RET_O(a);
    return true;
}
static bool Array_arrayClassOf(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmObject *o = dvm_class_object(vm, dvm_array_class_of(vm, class_of(vm, A_O(0))));
    if (!o) return false;
    RET_O(o);
    return true;
}
static bool Array_getLength0(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmObject *o = A_O(0);
    if (!o) { dvm_throw(vm, "Ljava/lang/NullPointerException;", NULL); return false; }
    if (!dvm_is_array(o->clazz)) { dvm_throw(vm, "Ljava/lang/IllegalArgumentException;", "Argument is not an array"); return false; }
    RET_I(((DvmArray *)o)->length);
    return true;
}

static bool System_arraycopy(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)ret;
    DvmArray *src = (DvmArray *)A_O(0), *dst = (DvmArray *)A_O(2);
    int32_t sp = A_I(1), dp = A_I(3), n = A_I(4);
    if (!src || !dst) { dvm_throw(vm, "Ljava/lang/NullPointerException;", "%s == null", src ? "dst" : "src"); return false; }
    DvmClass *sc = src->obj.clazz, *dc = dst->obj.clazz;
    if (!dvm_is_array(sc) || !dvm_is_array(dc)) {
        dvm_throw(vm, "Ljava/lang/ArrayStoreException;", "%s of type %s is not an array", dvm_is_array(sc) ? "destination" : "source",
                  dvm_is_array(sc) ? dc->descriptor : sc->descriptor);
        return false;
    }
    if (sc->prim != dc->prim) {
        dvm_throw(vm, "Ljava/lang/ArrayStoreException;", "Incompatible types: src=%s, dst=%s", sc->descriptor, dc->descriptor);
        return false;
    }
    if (sp < 0 || dp < 0 || n < 0 || (int64_t)sp + n > src->length || (int64_t)dp + n > dst->length) {
        dvm_throw(vm, "Ljava/lang/ArrayIndexOutOfBoundsException;",
                  "src.length=%d srcPos=%d dst.length=%d dstPos=%d length=%d", src->length, sp, dst->length, dp, n);
        return false;
    }
    size_t es = sc->elem_size;
    if (sc->prim == 'L' && !dvm_is_subclass(sc->component, dc->component)) {
        DvmObject **s = (DvmObject **)src->data, **d = (DvmObject **)dst->data;
        for (int32_t i = 0; i < n; i++) {
            DvmObject *o = s[sp + i];
            if (o && !dvm_instance_of(o, dc->component)) {
                dvm_throw(vm, "Ljava/lang/ArrayStoreException;", "source[%d] of type %s cannot be stored in destination array of type %s",
                          sp + i, o->clazz->descriptor, dc->descriptor);
                return false;
            }
            d[dp + i] = o;
        }
        return true;
    }
    memmove(dst->data + (size_t)dp * es, src->data + (size_t)sp * es, (size_t)n * es);
    return true;
}
static bool System_nanoTime(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)vm; (void)args;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    *ret = (DvmReg)((int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec);
    return true;
}
static bool System_currentTimeMillis(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)vm; (void)args;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    *ret = (DvmReg)((int64_t)ts.tv_sec * 1000ll + ts.tv_nsec / 1000000);
    return true;
}
static bool System_identityHashCode(DvmVM *vm, DvmReg *args, DvmReg *ret) { RET_I(A_O(0) ? dvm_identity_hash(vm, A_O(0)) : 0); return true; }
static bool System_gc(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)args; (void)ret; dvm_gc(vm); return true; }
static bool System_exit(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)ret;
    vm->exit_status = A_I(0);
    vm->exiting = true;
    if (vm->exit_jmp) longjmp(*vm->exit_jmp, 1);
    exit(A_I(0));
}

static bool PrintStream_writeString(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)ret;
    DvmArray *a = dvm_string_chars(vm, A_O(1));
    if (!a) return true;
    size_t len;
    char *s = utf16_to_utf8((const uint16_t *)a->data, a->length, &len);
    FILE *f = A_I(0) == 2 ? stderr : stdout;
    if (f == stderr) fflush(stdout);
    fwrite(s, 1, len, f);
    free(s);
    return true;
}

static bool Throwable_nativeFillInStackTrace(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    (void)ret;
    return dvm_fill_in_stack_trace(vm, A_O(0));
}
static bool Throwable_nativeGetStackTrace(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    DvmArray *bt = (DvmArray *)A_O(0);
    int n = bt ? bt->length / 2 : 0;
    DvmArray *out = dvm_alloc_array_of(vm, "[Ljava/lang/String;", n);
    if (!out) return false;
    for (int i = 0; i < n; i++) {
        int64_t *e = (int64_t *)bt->data;
        char buf[1024];
        frame_string((DvmMethod *)(uintptr_t)e[2 * i], (uint32_t)e[2 * i + 1], buf, sizeof(buf));
        DvmObject *s = dvm_new_string(vm, buf);
        if (!s) return false;
        ((DvmObject **)out->data)[i] = s;
    }
    RET_O(out);
    return true;
}

static bool Float_floatToRawIntBits(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = (uint32_t)args[0]; return true; }
static bool Float_intBitsToFloat(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = (uint32_t)args[0]; return true; }
static bool Double_doubleToRawLongBits(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = args[0]; return true; }
static bool Double_longBitsToDouble(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = args[0]; return true; }

/* Java's Double.toString and Float.toString: the shortest digits that read back
 * as the same value, in plain notation for 1e-3 <= |x| < 1e7 and E notation otherwise. */
static void java_fp_string(double x, bool is_float, char *out, size_t n)
{
    if (x != x) { snprintf(out, n, "NaN"); return; }
    if (isinf(x)) { snprintf(out, n, x > 0 ? "Infinity" : "-Infinity"); return; }
    if (x == 0) { snprintf(out, n, signbit(x) ? "-0.0" : "0.0"); return; }
    char buf[64];
    int maxp = is_float ? 9 : 17;
    /* Java considers two digits when one would do, and takes the closer: 4.9E-324, not 5.0E-324 */
    for (int p = 2; p <= maxp; p++) {
        snprintf(buf, sizeof(buf), "%.*e", p - 1, x);
        double back = strtod(buf, NULL);
        if (is_float ? (float)back == (float)x : back == x) break;
    }
    /* buf: [-]d[.ddd]e[+-]XX */
    char digits[32];
    int nd = 0, exp10;
    bool neg = buf[0] == '-';
    const char *p = buf + neg;
    for (; *p && *p != 'e'; p++) if (*p != '.') digits[nd++] = *p;
    digits[nd] = 0;
    exp10 = atoi(p + 1);
    while (nd > 1 && digits[nd - 1] == '0') digits[--nd] = 0;
    char *w = out;
    size_t left = n;
#define PUT(...) do { int k = snprintf(w, left, __VA_ARGS__); if (k > 0 && (size_t)k < left) { w += k; left -= (size_t)k; } } while (0)
    if (neg) PUT("-");
    double ax = fabs(x);
    if (ax >= 1e-3 && ax < 1e7) {
        if (exp10 >= 0) {
            for (int i = 0; i <= exp10; i++) PUT("%c", i < nd ? digits[i] : '0');
            PUT(".");
            if (nd > exp10 + 1) PUT("%s", digits + exp10 + 1); else PUT("0");
        } else {
            PUT("0.");
            for (int i = 0; i < -exp10 - 1; i++) PUT("0");
            PUT("%s", digits);
        }
    } else {
        PUT("%c.", digits[0]);
        if (nd > 1) PUT("%s", digits + 1); else PUT("0");
        PUT("E%d", exp10);
    }
#undef PUT
}

static bool Double_toString(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    char buf[64];
    java_fp_string(a_d(args[0]), false, buf, sizeof(buf));
    DvmObject *s = dvm_new_string(vm, buf);
    if (!s) return false;
    RET_O(s);
    return true;
}
static bool Float_toString(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    char buf[64];
    java_fp_string(a_f(args[0]), true, buf, sizeof(buf));
    DvmObject *s = dvm_new_string(vm, buf);
    if (!s) return false;
    RET_O(s);
    return true;
}
static bool Double_parseDouble(DvmVM *vm, DvmReg *args, DvmReg *ret)
{
    char *s = dvm_string_utf8(vm, A_O(0));
    if (!s) { dvm_throw(vm, "Ljava/lang/NullPointerException;", NULL); return false; }
    char *end;
    const char *b = s;
    while (*b == ' ') b++;
    double d = strtod(b, &end);
    while (*end == ' ' || *end == 'd' || *end == 'D' || *end == 'f' || *end == 'F') end++;
    bool ok = end != b && !*end;
    if (!ok) dvm_throw(vm, "Ljava/lang/NumberFormatException;", "For input string: \"%s\"", s);
    free(s);
    *ret = r_d(d);
    return ok;
}

#define MATH1(name, expr) static bool Math_##name(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; double x = a_d(args[0]); *ret = r_d(expr); return true; }
MATH1(sqrt, sqrt(x)) MATH1(sin, sin(x)) MATH1(cos, cos(x)) MATH1(tan, tan(x)) MATH1(asin, asin(x)) MATH1(acos, acos(x))
MATH1(atan, atan(x)) MATH1(exp, exp(x)) MATH1(log, log(x)) MATH1(log10, log10(x)) MATH1(floor, floor(x))
MATH1(ceil, ceil(x)) MATH1(rint, rint(x)) MATH1(cbrt, cbrt(x))
static bool Math_atan2(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = r_d(atan2(a_d(args[0]), a_d(args[2]))); return true; }
static bool Math_pow(DvmVM *vm, DvmReg *args, DvmReg *ret) { (void)vm; *ret = r_d(pow(a_d(args[0]), a_d(args[2]))); return true; }

static const DvmNative natives[] = {
    { "Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;", Object_getClass },
    { "Ljava/lang/Object;", "hashCode", "()I", Object_hashCode },
    { "Ljava/lang/Object;", "internalClone", "()Ljava/lang/Object;", Object_clone },
    { "Ljava/lang/Object;", "notify", "()V", nop },
    { "Ljava/lang/Object;", "notifyAll", "()V", nop },
    { "Ljava/lang/Class;", "getName", "()Ljava/lang/String;", Class_getName },
    { "Ljava/lang/Class;", "isInterface", "()Z", Class_isInterface },
    { "Ljava/lang/Class;", "isArray", "()Z", Class_isArray },
    { "Ljava/lang/Class;", "isPrimitive", "()Z", Class_isPrimitive },
    { "Ljava/lang/Class;", "getSuperclass", "()Ljava/lang/Class;", Class_getSuperclass },
    { "Ljava/lang/Class;", "getComponentType", "()Ljava/lang/Class;", Class_getComponentType },
    { "Ljava/lang/Class;", "isInstance", "(Ljava/lang/Object;)Z", Class_isInstance },
    { "Ljava/lang/Class;", "isAssignableFrom", "(Ljava/lang/Class;)Z", Class_isAssignableFrom },
    { "Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", Class_forName },
    { "Ljava/lang/Class;", "getPrimitiveClass", "(Ljava/lang/String;)Ljava/lang/Class;", Class_getPrimitiveClass },
    { "Ljava/lang/Class;", "getEnumConstantsShared", "()[Ljava/lang/Object;", Class_getEnumConstantsShared },
    { "Ljava/util/Arrays;", "newArray0", "(Ljava/lang/Class;I)[Ljava/lang/Object;", Arrays_newArray0 },
    { "Ljava/lang/reflect/Array;", "newArray", "(Ljava/lang/Class;I)Ljava/lang/Object;", Array_newArray },
    { "Ljava/lang/reflect/Array;", "arrayClassOf", "(Ljava/lang/Class;)Ljava/lang/Class;", Array_arrayClassOf },
    { "Ljava/lang/reflect/Array;", "getLength0", "(Ljava/lang/Object;)I", Array_getLength0 },
    { "Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", System_arraycopy },
    { "Ljava/lang/System;", "nanoTime", "()J", System_nanoTime },
    { "Ljava/lang/System;", "currentTimeMillis", "()J", System_currentTimeMillis },
    { "Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", System_identityHashCode },
    { "Ljava/lang/System;", "gc", "()V", System_gc },
    { "Ljava/lang/System;", "exit", "(I)V", System_exit },
    { "Ljava/io/PrintStream;", "writeString", "(ILjava/lang/String;)V", PrintStream_writeString },
    { "Ljava/lang/Throwable;", "nativeFillInStackTrace", "()V", Throwable_nativeFillInStackTrace },
    { "Ljava/lang/Throwable;", "nativeGetStackTrace", "(Ljava/lang/Object;)[Ljava/lang/String;", Throwable_nativeGetStackTrace },
    { "Ljava/lang/Float;", "floatToRawIntBits", "(F)I", Float_floatToRawIntBits },
    { "Ljava/lang/Float;", "intBitsToFloat", "(I)F", Float_intBitsToFloat },
    { "Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", Float_toString },
    { "Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", Double_doubleToRawLongBits },
    { "Ljava/lang/Double;", "longBitsToDouble", "(J)D", Double_longBitsToDouble },
    { "Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;", Double_toString },
    { "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", Double_parseDouble },
    { "Ljava/lang/Math;", "sqrt", "(D)D", Math_sqrt },
    { "Ljava/lang/Math;", "sin", "(D)D", Math_sin },
    { "Ljava/lang/Math;", "cos", "(D)D", Math_cos },
    { "Ljava/lang/Math;", "tan", "(D)D", Math_tan },
    { "Ljava/lang/Math;", "asin", "(D)D", Math_asin },
    { "Ljava/lang/Math;", "acos", "(D)D", Math_acos },
    { "Ljava/lang/Math;", "atan", "(D)D", Math_atan },
    { "Ljava/lang/Math;", "exp", "(D)D", Math_exp },
    { "Ljava/lang/Math;", "log", "(D)D", Math_log },
    { "Ljava/lang/Math;", "log10", "(D)D", Math_log10 },
    { "Ljava/lang/Math;", "floor", "(D)D", Math_floor },
    { "Ljava/lang/Math;", "ceil", "(D)D", Math_ceil },
    { "Ljava/lang/Math;", "rint", "(D)D", Math_rint },
    { "Ljava/lang/Math;", "cbrt", "(D)D", Math_cbrt },
    { "Ljava/lang/Math;", "atan2", "(DD)D", Math_atan2 },
    { "Ljava/lang/Math;", "pow", "(DD)D", Math_pow },
};

DvmNativeFn dvm_find_native(const char *cls, const char *name, const char *proto)
{
    for (size_t i = 0; i < sizeof(natives) / sizeof(natives[0]); i++)
        if (!strcmp(natives[i].cls, cls) && !strcmp(natives[i].name, name) && !strcmp(natives[i].proto, proto))
            return natives[i].fn;
    return NULL;
}

/* ------------------------------------------------------------- the VM */

DvmVM *dvm_create(void)
{
    DvmVM *vm = calloc(1, sizeof(*vm));
    vm->reg_cap = 1u << 20;
    vm->reg_stack = calloc(vm->reg_cap, sizeof(DvmReg));
    vm->reg_limit = vm->reg_cap - (1u << 16);
    vm->depth_limit = DVM_MAX_DEPTH;
    vm->gc_threshold = 16u << 20;
    vm->stack_hi = dvm_thread_stack_top();
    const char *stress = getenv("DVM_GC_STRESS");
    if (stress) vm->gc_stress = (uint32_t)atoi(stress);
    return vm;
}

void dvm_destroy(DvmVM *vm)
{
    if (!vm) return;
    dvm_heap_free_all(vm);
    /* array classes share Object's vtable: free each owner's while every class still exists */
    for (int h = 0; h < DVM_CLASS_HASH; h++)
        for (DvmClass *c = vm->class_hash[h]; c; c = c->hash_next)
            if (c->vtable && !dvm_is_array(c)) free(c->vtable);
    for (int h = 0; h < DVM_CLASS_HASH; h++)
        for (DvmClass *c = vm->class_hash[h], *n; c; c = n) {
            n = c->hash_next;
            free(c->sfields); free(c->ifields); free(c->methods); free(c->statics);
            free(c->ref_slots); free(c->static_ref_slots); free(c->ifaces);
            if (c->class_def < 0) free((void *)c->descriptor);
            free(c);
        }
    for (int i = 0; i < vm->n_dexes; i++) {
        DvmDex *d = vm->dexes[i];
        for (uint32_t p = 0; p < d->n_protos; p++) free(d->protos[p]);
        free(d->protos); free(d->strings); free(d->types); free(d->fields); free(d->methods); free(d->def_index);
        free(d->data);
        free(d);
    }
    free(vm->dexes);
    free(vm->reg_stack);
    free(vm);
}

bool dvm_add_dex_memory(DvmVM *vm, const void *data, size_t size, char *err, size_t errlen)
{
    uint8_t *copy = malloc(size);
    memcpy(copy, data, size);
    if (!dvm_dex_open(vm, copy, size, err, errlen)) { free(copy); return false; }
    return true;
}

bool dvm_add_dex_file(DvmVM *vm, const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "%s: %s", path, strerror(errno)); return false; }
    uint8_t magic[4] = { 0 };
    size_t got = fread(magic, 1, 4, f);
    if (got == 4 && !memcmp(magic, "PK", 2)) {
        fclose(f);
#ifdef HUSK_DVM_ZIP
        tl_zip z;
        if (!tl_zip_open(&z, path, err, errlen)) return false;
        bool any = false;
        for (int i = 1;; i++) {
            char name[32];
            if (i == 1) snprintf(name, sizeof(name), "classes.dex");
            else snprintf(name, sizeof(name), "classes%d.dex", i);
            const tl_zip_entry *e = tl_zip_find(&z, name);
            if (!e) break;
            const uint8_t *data; size_t len; bool owned;
            if (!tl_zip_data(&z, e, 512u << 20, &data, &len, &owned, err, errlen)) { tl_zip_close(&z); return false; }
            bool ok = dvm_add_dex_memory(vm, data, len, err, errlen);
            if (owned) free((void *)data);
            if (!ok) { tl_zip_close(&z); return false; }
            any = true;
        }
        tl_zip_close(&z);
        if (!any) { snprintf(err, errlen, "%s has no classes.dex", path); return false; }
        return true;
#else
        snprintf(err, errlen, "%s: built without ZIP support", path);
        return false;
#endif
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size > 0 ? (size_t)size : 1);
    if (size <= 0 || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f); free(data);
        snprintf(err, errlen, "%s: could not read", path);
        return false;
    }
    fclose(f);
    if (!dvm_dex_open(vm, data, (size_t)size, err, errlen)) { free(data); return false; }
    return true;
}

static bool boot(DvmVM *vm)
{
    if (vm->c_string) return true;
    vm->c_object = dvm_lookup_class(vm, "Ljava/lang/Object;");
    vm->c_class = dvm_lookup_class(vm, "Ljava/lang/Class;");
    vm->c_string = dvm_lookup_class(vm, "Ljava/lang/String;");
    vm->c_throwable = dvm_lookup_class(vm, "Ljava/lang/Throwable;");
    vm->c_char_array = dvm_lookup_class(vm, "[C");
    if (!vm->c_object || !vm->c_class || !vm->c_string || !vm->c_throwable) {
        fprintf(stderr, "dvm: the boot library is missing java.lang.Object, Class, String or Throwable\n");
        return false;
    }
    DvmField *f;
    if (!(f = dvm_find_field(vm->c_string, "value", "[C", false))) { fprintf(stderr, "dvm: String has no char[] value\n"); return false; }
    vm->slot_string_value = f->slot;
    if (!(f = dvm_find_field(vm->c_class, "vmClass", "J", false))) { fprintf(stderr, "dvm: Class has no long vmClass\n"); return false; }
    vm->slot_class_vm = f->slot;
    if (!(f = dvm_find_field(vm->c_throwable, "backtrace", "Ljava/lang/Object;", false))) { fprintf(stderr, "dvm: Throwable has no Object backtrace\n"); return false; }
    vm->slot_throwable_backtrace = f->slot;
    DvmClass *oom = dvm_lookup_class(vm, "Ljava/lang/OutOfMemoryError;");
    if (oom) vm->oom = dvm_alloc_object(vm, oom);
    return true;
}

int dvm_run_main(DvmVM *vm, const char *class_name, int argc, const char **argv)
{
    if (!boot(vm)) return 2;
    char desc[512];
    if (class_name[0] == 'L' && strchr(class_name, ';')) snprintf(desc, sizeof(desc), "%s", class_name);
    else snprintf(desc, sizeof(desc), "L%s;", class_name);
    for (char *p = desc; *p; p++) if (*p == '.') *p = '/';

    jmp_buf exit_jmp;
    vm->exit_jmp = &exit_jmp;
    DvmFrame *frame0 = vm->frame;
    size_t regs0 = vm->reg_top;
    int depth0 = vm->depth;
    if (setjmp(exit_jmp)) {
        vm->frame = frame0; vm->reg_top = regs0; vm->depth = depth0;
        vm->exit_jmp = NULL;
        fflush(stdout);
        return vm->exit_status;
    }

    int status = 0;
    DvmClass *c = dvm_find_class(vm, desc);
    DvmMethod *m = NULL;
    if (c && dvm_init_class(vm, c)) {
        m = dvm_find_declared_method(c, "main", "([Ljava/lang/String;)V");
        if (!m || !(m->access & ACC_STATIC))
            dvm_throw(vm, "Ljava/lang/NoSuchMethodError;", "no static main(String[]) in %s", class_name);
    }
    if (m && !vm->exception) {
        DvmArray *a = dvm_alloc_array_of(vm, "[Ljava/lang/String;", argc);
        for (int i = 0; a && i < argc; i++) {
            DvmObject *s = dvm_new_string(vm, argv[i]);
            if (!s) { a = NULL; break; }
            ((DvmObject **)a->data)[i] = s;
        }
        if (a) {
            DvmReg arg = DVM_REF(a);
            dvm_call(vm, m, &arg, NULL);
        }
    }
    if (vm->exception) {
        DvmObject *ex = vm->exception;
        fflush(stdout);
        fprintf(stderr, "Exception in thread \"main\" ");
        dvm_print_exception(vm, ex);
        status = 1;
    }
    fflush(stdout);
    fflush(stderr);
    vm->exit_jmp = NULL;
    return status;
}

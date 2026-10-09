/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The interpreter.
 *
 * One C call per Java call: a frame's registers come from the VM's register
 * stack, and its DvmFrame sits on the C stack, linked for stack traces and the
 * collector. A throw sets vm->exception and goes to the handler search; an
 * instruction that may throw never advances pc first, so pc is the throwing
 * instruction when the search runs. A method with no handler for it returns
 * false, and its caller searches in turn.
 */
#include "dvm-internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dvm_arg_slots(const DvmMethod *m)
{
    int n = (m->access & ACC_STATIC) ? 0 : 1;
    for (const char *p = m->shorty + 1; *p; p++) n += (*p == 'J' || *p == 'D') ? 2 : 1;
    return n;
}

/* ------------------------------------------------------------- arithmetic */

static inline float  r2f(DvmReg r) { float f; uint32_t u = (uint32_t)r; memcpy(&f, &u, 4); return f; }
static inline double r2d(DvmReg r) { double d; memcpy(&d, &r, 8); return d; }
static inline DvmReg f2r(float f)  { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline DvmReg d2r(double d) { DvmReg r; memcpy(&r, &d, 8); return r; }
static inline DvmReg i2r(int32_t i) { return (uint32_t)i; }

static int32_t f2i(double x)
{
    if (x != x) return 0;
    if (x >= 2147483647.0) return INT32_MAX;
    if (x <= -2147483648.0) return INT32_MIN;
    return (int32_t)x;
}

static int64_t f2l(double x)
{
    if (x != x) return 0;
    if (x >= 9223372036854775807.0) return INT64_MAX;
    if (x <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)x;
}

/* k is the opcode's offset from 0x90 (the 23x binops) -- the 2addr and literal forms map onto it. */
static bool binop(DvmVM *vm, int k, DvmReg a, DvmReg b, DvmReg *out)
{
    int32_t ia = (int32_t)a, ib = (int32_t)b;
    int64_t la = (int64_t)a, lb = (int64_t)b;
    switch (k) {
    case 0x00: *out = i2r((int32_t)((uint32_t)ia + (uint32_t)ib)); return true;
    case 0x01: *out = i2r((int32_t)((uint32_t)ia - (uint32_t)ib)); return true;
    case 0x02: *out = i2r((int32_t)((uint32_t)ia * (uint32_t)ib)); return true;
    case 0x03: case 0x04:
        if (ib == 0) { dvm_throw(vm, "Ljava/lang/ArithmeticException;", "divide by zero"); return false; }
        if (ia == INT32_MIN && ib == -1) { *out = i2r(k == 0x03 ? INT32_MIN : 0); return true; }
        *out = i2r(k == 0x03 ? ia / ib : ia % ib); return true;
    case 0x05: *out = i2r(ia & ib); return true;
    case 0x06: *out = i2r(ia | ib); return true;
    case 0x07: *out = i2r(ia ^ ib); return true;
    case 0x08: *out = i2r((int32_t)((uint32_t)ia << (ib & 31))); return true;
    case 0x09: *out = i2r(ia >> (ib & 31)); return true;
    case 0x0a: *out = i2r((int32_t)((uint32_t)ia >> (ib & 31))); return true;
    case 0x0b: *out = (DvmReg)((uint64_t)la + (uint64_t)lb); return true;
    case 0x0c: *out = (DvmReg)((uint64_t)la - (uint64_t)lb); return true;
    case 0x0d: *out = (DvmReg)((uint64_t)la * (uint64_t)lb); return true;
    case 0x0e: case 0x0f:
        if (lb == 0) { dvm_throw(vm, "Ljava/lang/ArithmeticException;", "divide by zero"); return false; }
        if (la == INT64_MIN && lb == -1) { *out = (DvmReg)(k == 0x0e ? INT64_MIN : 0); return true; }
        *out = (DvmReg)(k == 0x0e ? la / lb : la % lb); return true;
    case 0x10: *out = (DvmReg)(la & lb); return true;
    case 0x11: *out = (DvmReg)(la | lb); return true;
    case 0x12: *out = (DvmReg)(la ^ lb); return true;
    /* long shifts take an int shift count */
    case 0x13: *out = (DvmReg)((uint64_t)la << (ib & 63)); return true;
    case 0x14: *out = (DvmReg)(la >> (ib & 63)); return true;
    case 0x15: *out = (DvmReg)((uint64_t)la >> (ib & 63)); return true;
    case 0x16: *out = f2r(r2f(a) + r2f(b)); return true;
    case 0x17: *out = f2r(r2f(a) - r2f(b)); return true;
    case 0x18: *out = f2r(r2f(a) * r2f(b)); return true;
    case 0x19: *out = f2r(r2f(a) / r2f(b)); return true;
    case 0x1a: *out = f2r(fmodf(r2f(a), r2f(b))); return true;
    case 0x1b: *out = d2r(r2d(a) + r2d(b)); return true;
    case 0x1c: *out = d2r(r2d(a) - r2d(b)); return true;
    case 0x1d: *out = d2r(r2d(a) * r2d(b)); return true;
    case 0x1e: *out = d2r(r2d(a) / r2d(b)); return true;
    case 0x1f: *out = d2r(fmod(r2d(a), r2d(b))); return true;
    }
    return true;
}

static DvmReg unop(int op, DvmReg a)
{
    switch (op) {
    case 0x7b: return i2r((int32_t)(0u - (uint32_t)a));
    case 0x7c: return i2r(~(int32_t)a);
    case 0x7d: return (DvmReg)(0ull - a);
    case 0x7e: return ~a;
    case 0x7f: return f2r(-r2f(a));
    case 0x80: return d2r(-r2d(a));
    case 0x81: return (DvmReg)(int64_t)(int32_t)a;
    case 0x82: return f2r((float)(int32_t)a);
    case 0x83: return d2r((double)(int32_t)a);
    case 0x84: return i2r((int32_t)a);
    case 0x85: return f2r((float)(int64_t)a);
    case 0x86: return d2r((double)(int64_t)a);
    case 0x87: return i2r(f2i(r2f(a)));
    case 0x88: return (DvmReg)f2l(r2f(a));
    case 0x89: return d2r((double)r2f(a));
    case 0x8a: return i2r(f2i(r2d(a)));
    case 0x8b: return (DvmReg)f2l(r2d(a));
    case 0x8c: return f2r((float)r2d(a));
    case 0x8d: return i2r((int8_t)a);
    case 0x8e: return i2r((uint16_t)a);
    case 0x8f: return i2r((int16_t)a);
    }
    return 0;
}

static int32_t fcmp(double x, double y, int nan)
{
    if (x != x || y != y) return nan;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* ------------------------------------------------------------- exceptions */

static void java_name(const char *desc, char *out, size_t n)
{
    size_t w = 0;
    if (desc[0] == 'L') {
        for (const char *s = desc + 1; *s && *s != ';' && w + 1 < n; s++) out[w++] = *s == '/' ? '.' : *s;
    } else {
        for (const char *s = desc; *s && w + 1 < n; s++) out[w++] = *s == '/' ? '.' : *s;
    }
    out[w] = 0;
}

static bool find_handler(DvmVM *vm, DvmMethod *m, uint32_t pc, DvmObject *ex, uint32_t *target)
{
    for (uint16_t t = 0; t < m->tries_size; t++) {
        const uint8_t *ti = m->tries + 8 * t;
        uint32_t start; uint16_t count, off;
        memcpy(&start, ti, 4); memcpy(&count, ti + 4, 2); memcpy(&off, ti + 6, 2);
        if (pc < start || pc >= start + count) continue;
        const uint8_t *p = m->handlers + off;
        int32_t size = dvm_sleb(&p);
        for (int32_t i = 0; i < (size < 0 ? -size : size); i++) {
            uint32_t type = dvm_uleb(&p), addr = dvm_uleb(&p);
            DvmClass *c = m->clazz->dex->types[type];
            if (!c) c = dvm_lookup_class(vm, dvm_dex_type(m->clazz->dex, type));
            if (c && dvm_instance_of(ex, c)) { *target = addr; return true; }
        }
        if (size <= 0) { *target = dvm_uleb(&p); return true; }
        return false;
    }
    return false;
}

/* ------------------------------------------------------------- calls */

static bool interpret(DvmVM *vm, DvmMethod *m, DvmReg *args, DvmReg *retp);

bool dvm_call(DvmVM *vm, DvmMethod *m, DvmReg *args, DvmReg *ret)
{
    DvmReg dummy;
    if (!ret) ret = &dummy;
    *ret = 0;
    if (m->access & ACC_ABSTRACT) {
        dvm_throw(vm, "Ljava/lang/AbstractMethodError;", "abstract method \"%s.%s%s\"", m->clazz->descriptor, m->name, m->proto);
        return false;
    }
    if (m->access & ACC_NATIVE) {
        if (!m->native) m->native = dvm_find_native(m->clazz->descriptor, m->name, m->proto);
        if (!m->native) {
            dvm_throw(vm, "Ljava/lang/UnsatisfiedLinkError;", "No implementation found for %s.%s%s",
                      m->clazz->descriptor, m->name, m->proto);
            return false;
        }
        if (vm->depth >= vm->depth_limit) { dvm_throw(vm, "Ljava/lang/StackOverflowError;", "stack size %d", vm->depth); return false; }
        DvmFrame fr = { vm->frame, m, 0, NULL };
        vm->frame = &fr;
        vm->depth++;
        bool ok = m->native(vm, args, ret);
        vm->frame = fr.prev;
        vm->depth--;
        if (ok && vm->exception) ok = false;
        return ok;
    }
    if (!m->insns) {
        dvm_throw(vm, "Ljava/lang/VerifyError;", "%s.%s%s has no code", m->clazz->descriptor, m->name, m->proto);
        return false;
    }
    return interpret(vm, m, args, ret);
}

bool dvm_callv(DvmVM *vm, DvmMethod *m, DvmReg *ret, ...)
{
    DvmReg args[64];
    int n = dvm_arg_slots(m);
    if (n > 64) n = 64;
    va_list ap;
    va_start(ap, ret);
    for (int i = 0; i < n; i++) args[i] = va_arg(ap, DvmReg);
    va_end(ap);
    return dvm_call(vm, m, args, ret);
}

/* ------------------------------------------------------------- the loop */

#define vA4   ((inst >> 8) & 0xf)
#define vB4   (inst >> 12)
#define vAA   (inst >> 8)
#define W(n)  (insns[pc + (n)])
#define S16(n) ((int32_t)(int16_t)W(n))
#define U32(n) ((uint32_t)W(n) | ((uint32_t)W((n) + 1) << 16))
#define OBJ(r) DVM_OBJ(v[r])
#define THROW() goto handle_exception
#define NPE(what) do { dvm_throw(vm, "Ljava/lang/NullPointerException;", "%s", what); THROW(); } while (0)

static bool interpret(DvmVM *vm, DvmMethod *m, DvmReg *args, DvmReg *retp)
{
    uint32_t nregs = m->regs < m->ins ? m->ins : m->regs;
    if (vm->depth >= vm->depth_limit || vm->reg_top + nregs > vm->reg_limit) {
        dvm_throw(vm, "Ljava/lang/StackOverflowError;", "stack size %d", vm->depth);
        return false;
    }
    DvmReg *v = vm->reg_stack + vm->reg_top;
    vm->reg_top += nregs;
    memset(v, 0, nregs * sizeof(DvmReg));
    if (m->ins && args) memcpy(v + nregs - m->ins, args, m->ins * sizeof(DvmReg));

    DvmFrame fr = { vm->frame, m, 0, NULL };
    vm->frame = &fr;
    vm->depth++;

    DvmDex *dex = m->clazz->dex;
    const uint16_t *insns = m->insns;
    const uint32_t size = m->insns_size;
    uint32_t pc = 0;
    DvmReg res = 0;
    bool ok = true;

    for (;;) {
        if (pc >= size) {
            dvm_throw(vm, "Ljava/lang/VerifyError;", "fell off the end of %s.%s", m->clazz->descriptor, m->name);
            THROW();
        }
        fr.pc = pc;
        uint16_t inst = insns[pc];
        uint8_t op = inst & 0xff;
        switch (op) {
        case 0x00: pc += 1; break;

        /* moves */
        case 0x01: case 0x04: case 0x07: v[vA4] = v[vB4]; pc += 1; break;
        case 0x02: case 0x05: case 0x08: v[vAA] = v[W(1)]; pc += 2; break;
        case 0x03: case 0x06: case 0x09: v[W(1)] = v[W(2)]; pc += 3; break;
        case 0x0a: case 0x0b: case 0x0c: v[vAA] = res; pc += 1; break;
        case 0x0d: v[vAA] = DVM_REF(fr.caught); fr.caught = NULL; pc += 1; break;

        /* returns */
        case 0x0e: *retp = 0; goto done;
        case 0x0f: *retp = (uint32_t)v[vAA]; goto done;
        case 0x10: case 0x11: *retp = v[vAA]; goto done;

        /* constants */
        case 0x12: v[vA4] = i2r((int32_t)((uint32_t)inst << 16) >> 28); pc += 1; break;
        case 0x13: v[vAA] = i2r(S16(1)); pc += 2; break;
        case 0x14: v[vAA] = U32(1); pc += 3; break;
        case 0x15: v[vAA] = (uint32_t)W(1) << 16; pc += 2; break;
        case 0x16: v[vAA] = (DvmReg)(int64_t)S16(1); pc += 2; break;
        case 0x17: v[vAA] = (DvmReg)(int64_t)(int32_t)U32(1); pc += 3; break;
        case 0x18: v[vAA] = (DvmReg)U32(1) | ((DvmReg)U32(3) << 32); pc += 5; break;
        case 0x19: v[vAA] = (DvmReg)W(1) << 48; pc += 2; break;
        case 0x1a: case 0x1b: {
            DvmObject *s = dvm_resolve_string(vm, dex, op == 0x1a ? W(1) : U32(1));
            if (!s) THROW();
            v[vAA] = DVM_REF(s);
            pc += op == 0x1a ? 2 : 3;
            break;
        }
        case 0x1c: {
            DvmClass *c = dvm_resolve_type(vm, dex, W(1));
            if (!c) THROW();
            DvmObject *o = dvm_class_object(vm, c);
            if (!o) THROW();
            v[vAA] = DVM_REF(o);
            pc += 2;
            break;
        }

        /* monitors: one thread, so only the null check */
        case 0x1d: case 0x1e:
            if (!OBJ(vAA)) NPE("monitor on a null object");
            pc += 1;
            break;

        /* types */
        case 0x1f: {
            DvmObject *o = OBJ(vAA);
            if (o) {
                DvmClass *c = dvm_resolve_type(vm, dex, W(1));
                if (!c) THROW();
                if (!dvm_instance_of(o, c)) {
                    char a[256], b[256];
                    java_name(o->clazz->descriptor, a, sizeof(a));
                    java_name(c->descriptor, b, sizeof(b));
                    dvm_throw(vm, "Ljava/lang/ClassCastException;", "%s cannot be cast to %s", a, b);
                    THROW();
                }
            }
            pc += 2;
            break;
        }
        case 0x20: {
            DvmObject *o = OBJ(vB4);
            int32_t r = 0;
            if (o) {
                DvmClass *c = dvm_resolve_type(vm, dex, W(1));
                if (!c) THROW();
                r = dvm_instance_of(o, c);
            }
            v[vA4] = i2r(r);
            pc += 2;
            break;
        }
        case 0x21: {
            DvmObject *o = OBJ(vB4);
            if (!o) NPE("Attempt to get length of null array");
            v[vA4] = i2r(((DvmArray *)o)->length);
            pc += 1;
            break;
        }
        case 0x22: {
            DvmClass *c = dvm_resolve_type(vm, dex, W(1));
            if (!c) THROW();
            if (c->access & (ACC_ABSTRACT | ACC_INTERFACE)) {
                dvm_throw(vm, "Ljava/lang/InstantiationError;", "%s", c->descriptor);
                THROW();
            }
            if (!dvm_init_class(vm, c)) THROW();
            DvmObject *o = dvm_alloc_object(vm, c);
            if (!o) THROW();
            v[vAA] = DVM_REF(o);
            pc += 2;
            break;
        }
        case 0x23: {
            DvmClass *c = dvm_resolve_type(vm, dex, W(1));
            if (!c) THROW();
            DvmArray *a = dvm_alloc_array(vm, c, (int32_t)v[vB4]);
            if (!a) THROW();
            v[vA4] = DVM_REF(a);
            pc += 2;
            break;
        }
        case 0x24: case 0x25: {
            DvmClass *c = dvm_resolve_type(vm, dex, W(1));
            if (!c) THROW();
            uint32_t n = op == 0x24 ? vB4 : vAA;
            DvmArray *a = dvm_alloc_array(vm, c, (int32_t)n);
            if (!a) THROW();
            for (uint32_t i = 0; i < n; i++) {
                uint32_t r = op == 0x25 ? W(2) + i
                           : i == 4 ? vA4 : (W(2) >> (4 * i)) & 0xf;
                if (c->prim == 'L') ((DvmObject **)a->data)[i] = OBJ(r);
                else ((int32_t *)a->data)[i] = (int32_t)v[r];
            }
            res = DVM_REF(a);
            pc += 3;
            break;
        }
        case 0x26: {
            DvmArray *a = (DvmArray *)OBJ(vAA);
            if (!a) NPE("fill-array-data on a null array");
            const uint16_t *p = insns + pc + (int32_t)U32(1);
            uint16_t width = p[1];
            uint32_t n = (uint32_t)p[2] | ((uint32_t)p[3] << 16);
            if ((int64_t)n > a->length) {
                dvm_throw(vm, "Ljava/lang/ArrayIndexOutOfBoundsException;", "length=%d; index=%u", a->length, n - 1);
                THROW();
            }
            memcpy(a->data, p + 4, (size_t)n * width);
            pc += 3;
            break;
        }
        case 0x27: {
            DvmObject *e = OBJ(vAA);
            if (!e) NPE("throw with null exception");
            vm->exception = e;
            THROW();
        }

        /* branches */
        case 0x28: pc += (int32_t)(int8_t)(inst >> 8); break;
        case 0x29: pc += S16(1); break;
        case 0x2a: pc += (int32_t)U32(1); break;
        case 0x2b: {
            const uint16_t *p = insns + pc + (int32_t)U32(1);
            uint16_t n = p[1];
            int32_t first = (int32_t)((uint32_t)p[2] | ((uint32_t)p[3] << 16));
            int64_t idx = (int64_t)(int32_t)v[vAA] - first;
            if (idx >= 0 && idx < n) {
                const uint16_t *t = p + 4 + 2 * idx;
                pc += (int32_t)((uint32_t)t[0] | ((uint32_t)t[1] << 16));
            } else pc += 3;
            break;
        }
        case 0x2c: {
            const uint16_t *p = insns + pc + (int32_t)U32(1);
            uint16_t n = p[1];
            int32_t key = (int32_t)v[vAA];
            const uint16_t *keys = p + 2, *targets = p + 2 + 2 * n;
            uint32_t next = pc + 3;
            for (uint16_t i = 0; i < n; i++) {
                int32_t k = (int32_t)((uint32_t)keys[2 * i] | ((uint32_t)keys[2 * i + 1] << 16));
                if (k == key) { next = pc + (int32_t)((uint32_t)targets[2 * i] | ((uint32_t)targets[2 * i + 1] << 16)); break; }
                if (k > key) break;
            }
            pc = next;
            break;
        }

        /* compares */
        case 0x2d: case 0x2e: {
            uint16_t w = W(1);
            v[vAA] = i2r(fcmp(r2f(v[w & 0xff]), r2f(v[w >> 8]), op == 0x2d ? -1 : 1));
            pc += 2;
            break;
        }
        case 0x2f: case 0x30: {
            uint16_t w = W(1);
            v[vAA] = i2r(fcmp(r2d(v[w & 0xff]), r2d(v[w >> 8]), op == 0x2f ? -1 : 1));
            pc += 2;
            break;
        }
        case 0x31: {
            uint16_t w = W(1);
            int64_t a = (int64_t)v[w & 0xff], b = (int64_t)v[w >> 8];
            v[vAA] = i2r(a < b ? -1 : a > b ? 1 : 0);
            pc += 2;
            break;
        }
        case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: {
            DvmReg ra = v[vA4], rb = v[vB4];
            int32_t a = (int32_t)ra, b = (int32_t)rb;
            bool t;
            switch (op) {
            case 0x32: t = ra == rb; break;
            case 0x33: t = ra != rb; break;
            case 0x34: t = a < b; break;
            case 0x35: t = a >= b; break;
            case 0x36: t = a > b; break;
            default:   t = a <= b; break;
            }
            pc += t ? S16(1) : 2;
            break;
        }
        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: {
            DvmReg ra = v[vAA];
            int32_t a = (int32_t)ra;
            bool t;
            switch (op) {
            case 0x38: t = ra == 0; break;
            case 0x39: t = ra != 0; break;
            case 0x3a: t = a < 0; break;
            case 0x3b: t = a >= 0; break;
            case 0x3c: t = a > 0; break;
            default:   t = a <= 0; break;
            }
            pc += t ? S16(1) : 2;
            break;
        }

        /* arrays */
        case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
        case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: {
            uint16_t w = W(1);
            DvmArray *a = (DvmArray *)OBJ(w & 0xff);
            int32_t i = (int32_t)v[w >> 8];
            if (!a) NPE(op < 0x4b ? "Attempt to read from null array" : "Attempt to write to null array");
            if (i < 0 || i >= a->length) {
                dvm_throw(vm, "Ljava/lang/ArrayIndexOutOfBoundsException;", "length=%d; index=%d", a->length, i);
                THROW();
            }
            uint8_t *at = a->data + (size_t)i * a->obj.clazz->elem_size;
            DvmReg *r = &v[vAA];
            switch (op) {
            case 0x44: { int32_t x; memcpy(&x, at, 4); *r = (uint32_t)x; break; }
            case 0x45: memcpy(r, at, 8); break;
            case 0x46: { DvmObject *x; memcpy(&x, at, 8); *r = DVM_REF(x); break; }
            case 0x47: *r = at[0]; break;
            case 0x48: *r = i2r((int8_t)at[0]); break;
            case 0x49: { uint16_t x; memcpy(&x, at, 2); *r = x; break; }
            case 0x4a: { int16_t x; memcpy(&x, at, 2); *r = i2r(x); break; }
            case 0x4b: { uint32_t x = (uint32_t)*r; memcpy(at, &x, 4); break; }
            case 0x4c: memcpy(at, r, 8); break;
            case 0x4d: {
                DvmObject *x = DVM_OBJ(*r);
                if (x && !dvm_instance_of(x, a->obj.clazz->component)) {
                    char n1[256], n2[256];
                    java_name(x->clazz->descriptor, n1, sizeof(n1));
                    java_name(a->obj.clazz->descriptor, n2, sizeof(n2));
                    dvm_throw(vm, "Ljava/lang/ArrayStoreException;", "%s cannot be stored in an array of type %s", n1, n2);
                    THROW();
                }
                memcpy(at, &x, 8);
                break;
            }
            case 0x4e: case 0x4f: at[0] = (uint8_t)*r; break;
            case 0x50: case 0x51: { uint16_t x = (uint16_t)*r; memcpy(at, &x, 2); break; }
            }
            pc += 2;
            break;
        }

        /* instance fields */
        case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
        case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
            DvmField *f = dvm_resolve_field(vm, dex, W(1), false);
            if (!f) THROW();
            DvmObject *o = OBJ(vB4);
            if (!o) {
                dvm_throw(vm, "Ljava/lang/NullPointerException;", "Attempt to %s field '%s %s' on a null object reference",
                          op < 0x59 ? "read from" : "write to", f->type, f->name);
                THROW();
            }
            if (op < 0x59) v[vA4] = DVM_FIELDS(o)[f->slot];
            else DVM_FIELDS(o)[f->slot] = v[vA4];
            pc += 2;
            break;
        }

        /* static fields */
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
        case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: {
            DvmField *f = dvm_resolve_field(vm, dex, W(1), true);
            if (!f) THROW();
            if (!dvm_init_class(vm, f->clazz)) THROW();
            if (op < 0x67) v[vAA] = f->clazz->statics[f->slot];
            else f->clazz->statics[f->slot] = v[vAA];
            pc += 2;
            break;
        }

        /* calls */
        case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72:
        case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: {
            bool range = op >= 0x74;
            int kind = range ? op - 0x74 : op - 0x6e;     /* virtual super direct static interface */
            DvmReg argbuf[5];
            DvmReg *a;
            if (range) a = &v[W(2)];
            else {
                uint32_t n = vB4;
                uint16_t r = W(2);
                for (uint32_t i = 0; i < n && i < 4; i++) argbuf[i] = v[(r >> (4 * i)) & 0xf];
                if (n == 5) argbuf[4] = v[vA4];
                a = argbuf;
            }
            DvmMethod *t = dvm_resolve_method(vm, dex, W(1));
            if (!t) THROW();
            DvmMethod *target = t;
            if (kind == 3) {
                if (!dvm_init_class(vm, t->clazz)) THROW();
            } else {
                DvmObject *self = DVM_OBJ(a[0]);
                if (!self) {
                    dvm_throw(vm, "Ljava/lang/NullPointerException;", "Attempt to invoke %s method '%s' on a null object reference",
                              kind == 4 || dvm_is_interface(t->clazz) ? "interface" : "virtual", t->name);
                    THROW();
                }
                if (kind == 0) {
                    if (t->vtable_index >= 0) target = self->clazz->vtable[t->vtable_index];
                    else if (dvm_is_interface(t->clazz)) target = dvm_find_interface_target(self->clazz, t);
                } else if (kind == 1) {
                    DvmClass *sup = m->clazz->super;
                    if (!dvm_is_interface(t->clazz) && sup && t->vtable_index >= 0 && t->vtable_index < sup->vtable_len)
                        target = sup->vtable[t->vtable_index];
                } else if (kind == 4) {
                    target = dvm_find_interface_target(self->clazz, t);
                }
                if (!target) {
                    dvm_throw(vm, "Ljava/lang/IncompatibleClassChangeError;", "%s does not implement %s.%s",
                              self->clazz->descriptor, t->clazz->descriptor, t->name);
                    THROW();
                }
            }
            res = 0;
            if (!dvm_call(vm, target, a, &res)) THROW();
            pc += 3;
            break;
        }

        /* unary and conversions */
        case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: case 0x80: case 0x81: case 0x82:
        case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a:
        case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f:
            v[vA4] = unop(op, v[vB4]);
            pc += 1;
            break;

        default:
            if (op >= 0x90 && op <= 0xaf) {
                uint16_t w = W(1);
                DvmReg out;
                if (!binop(vm, op - 0x90, v[w & 0xff], v[w >> 8], &out)) THROW();
                v[vAA] = out;
                pc += 2;
            } else if (op >= 0xb0 && op <= 0xcf) {
                DvmReg out;
                if (!binop(vm, op - 0xb0, v[vA4], v[vB4], &out)) THROW();
                v[vA4] = out;
                pc += 1;
            } else if (op >= 0xd0 && op <= 0xe2) {
                static const int8_t map[] = { 0x00, -1, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,       /* lit16 */
                                              0x00, -1, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,       /* lit8 */
                                              0x08, 0x09, 0x0a };
                DvmReg src, lit, out;
                uint32_t dst;
                if (op <= 0xd7) { dst = vA4; src = v[vB4]; lit = i2r(S16(1)); }
                else { uint16_t w = W(1); dst = vAA; src = v[w & 0xff]; lit = i2r((int8_t)(w >> 8)); }
                int k = map[op - 0xd0];
                if (k < 0) out = i2r((int32_t)((uint32_t)lit - (uint32_t)src));          /* rsub */
                else if (!binop(vm, k, src, lit, &out)) THROW();
                v[dst] = out;
                pc += 2;
            } else if (op >= 0xfa) {
                dvm_throw(vm, "Ljava/lang/UnsupportedOperationException;",
                          "%s is not supported yet", op <= 0xfb ? "invoke-polymorphic" : op <= 0xfd ? "invoke-custom" :
                          op == 0xfe ? "const-method-handle" : "const-method-type");
                THROW();
            } else {
                dvm_throw(vm, "Ljava/lang/VerifyError;", "bad opcode 0x%02x in %s.%s at %u", op, m->clazz->descriptor, m->name, pc);
                THROW();
            }
            break;
        }
        continue;

    handle_exception: {
            DvmObject *ex = vm->exception;
            uint32_t h;
            if (ex && find_handler(vm, m, pc, ex, &h)) {
                fr.caught = ex;
                vm->exception = NULL;
                pc = h;
                continue;
            }
            ok = false;
            goto done;
        }
    }

done:
    vm->frame = fr.prev;
    vm->depth--;
    vm->reg_top -= nregs;
    return ok;
}

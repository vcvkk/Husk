/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Classes: loading from DEX, linking (field layout, vtables), initialisation,
 * and resolving the DEX's references to classes, methods, fields and strings.
 *
 * Dispatch:
 *   - invoke-virtual goes through the vtable. A class's vtable is its
 *     superclass's, with overrides put in place and new methods appended, then
 *     any interface method still missing (a default method, or an abstract one
 *     that throws AbstractMethodError if called) appended after those.
 *   - invoke-interface finds the receiver class's vtable entry with the same
 *     name and proto, remembered per call target for the last receiver class.
 */
#include "dvm-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static uint32_t desc_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h & (DVM_CLASS_HASH - 1);
}

static DvmClass *hash_get(DvmVM *vm, const char *desc)
{
    for (DvmClass *c = vm->class_hash[desc_hash(desc)]; c; c = c->hash_next)
        if (!strcmp(c->descriptor, desc)) return c;
    return NULL;
}

static void hash_put(DvmVM *vm, DvmClass *c)
{
    uint32_t h = desc_hash(c->descriptor);
    c->hash_next = vm->class_hash[h];
    vm->class_hash[h] = c;
}

static int prim_size(char t)
{
    switch (t) {
    case 'Z': case 'B': return 1;
    case 'C': case 'S': return 2;
    case 'I': case 'F': return 4;
    case 'J': case 'D': return 8;
    default: return 8;               /* references */
    }
}

DvmClass *dvm_primitive_class(DvmVM *vm, char type)
{
    char desc[2] = { type, 0 };
    DvmClass *c = hash_get(vm, desc);
    if (c) return c;
    if (!strchr("ZBCSIJFDV", type)) return NULL;
    c = calloc(1, sizeof(*c));
    c->descriptor = strdup(desc);
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    c->prim = type;
    c->class_def = -1;
    c->status = CLS_INITIALIZED;
    hash_put(vm, c);
    return c;
}

DvmClass *dvm_array_class_of(DvmVM *vm, DvmClass *component)
{
    size_t l = strlen(component->descriptor);
    char *desc = malloc(l + 2);
    desc[0] = '[';
    memcpy(desc + 1, component->descriptor, l + 1);
    DvmClass *c = hash_get(vm, desc);
    if (c) { free(desc); return c; }
    DvmClass *obj = dvm_lookup_class(vm, "Ljava/lang/Object;");
    if (!obj) dvm_fatal("java.lang.Object is missing from the boot library");
    c = calloc(1, sizeof(*c));
    c->descriptor = desc;
    c->super = obj;
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    c->class_def = -1;
    c->component = component;
    c->prim = (component->prim && !dvm_is_array(component)) ? component->prim : 'L';   /* an array of arrays holds references */
    c->elem_size = (uint8_t)prim_size(c->prim);
    c->vtable = obj->vtable;
    c->vtable_len = obj->vtable_len;
    DvmClass *cl = dvm_lookup_class(vm, "Ljava/lang/Cloneable;");
    DvmClass *se = dvm_lookup_class(vm, "Ljava/io/Serializable;");
    c->ifaces = calloc(2, sizeof(DvmClass *));
    if (cl) c->ifaces[c->n_ifaces++] = cl;
    if (se) c->ifaces[c->n_ifaces++] = se;
    c->status = CLS_INITIALIZED;
    hash_put(vm, c);
    return c;
}

/* ------------------------------------------------------------- lookup */

DvmMethod *dvm_find_declared_method(DvmClass *c, const char *name, const char *proto)
{
    for (int i = 0; i < c->n_methods; i++) {
        DvmMethod *m = &c->methods[i];
        if (!strcmp(m->name, name) && (!proto || !strcmp(m->proto, proto))) return m;
    }
    return NULL;
}

static DvmMethod *find_in_ifaces(DvmClass *c, const char *name, const char *proto)
{
    for (int i = 0; i < c->n_ifaces; i++) {
        DvmMethod *m = dvm_find_declared_method(c->ifaces[i], name, proto);
        if (m) return m;
        m = find_in_ifaces(c->ifaces[i], name, proto);
        if (m) return m;
    }
    return NULL;
}

DvmMethod *dvm_find_method(DvmClass *c, const char *name, const char *proto)
{
    for (DvmClass *k = c; k; k = k->super) {
        DvmMethod *m = dvm_find_declared_method(k, name, proto);
        if (m) return m;
    }
    for (DvmClass *k = c; k; k = k->super) {
        DvmMethod *m = find_in_ifaces(k, name, proto);
        if (m) return m;
    }
    return NULL;
}

static DvmField *declared_field(DvmClass *c, const char *name, const char *type, bool is_static)
{
    DvmField *f = is_static ? c->sfields : c->ifields;
    int n = is_static ? c->n_sfields : c->n_ifields;
    for (int i = 0; i < n; i++)
        if (!strcmp(f[i].name, name) && (!type || !strcmp(f[i].type, type))) return &f[i];
    return NULL;
}

static DvmField *static_in_ifaces(DvmClass *c, const char *name, const char *type)
{
    for (int i = 0; i < c->n_ifaces; i++) {
        DvmField *f = declared_field(c->ifaces[i], name, type, true);
        if (!f) f = static_in_ifaces(c->ifaces[i], name, type);
        if (f) return f;
    }
    return NULL;
}

DvmField *dvm_find_field(DvmClass *c, const char *name, const char *type, bool is_static)
{
    for (DvmClass *k = c; k; k = k->super) {
        DvmField *f = declared_field(k, name, type, is_static);
        if (f) return f;
        if (is_static && (f = static_in_ifaces(k, name, type))) return f;
    }
    return NULL;
}

DvmMethod *dvm_find_interface_target(DvmClass *c, DvmMethod *im)
{
    if (im->ic_class == c) return im->ic_target;
    DvmMethod *found = NULL;
    for (int i = 0; i < c->vtable_len; i++) {
        DvmMethod *m = c->vtable[i];
        if (!strcmp(m->name, im->name) && !strcmp(m->proto, im->proto)) { found = m; break; }
    }
    if (found) { im->ic_class = c; im->ic_target = found; }
    return found;
}

static bool implements(const DvmClass *c, const DvmClass *iface)
{
    for (const DvmClass *k = c; k; k = k->super)
        for (int i = 0; i < k->n_ifaces; i++)
            if (k->ifaces[i] == iface || implements(k->ifaces[i], iface)) return true;
    return false;
}

bool dvm_is_subclass(const DvmClass *c, const DvmClass *of)
{
    if (c == of) return true;
    if (c->prim && !dvm_is_array(c)) return false;
    if (!of->super && !of->prim && !dvm_is_interface(of)) return true;   /* java.lang.Object */
    if (dvm_is_interface(of)) return implements(c, of);
    if (dvm_is_array(of)) {
        if (!dvm_is_array(c)) return false;
        if (of->prim != 'L' || c->prim != 'L') return c->component == of->component;
        return dvm_is_subclass(c->component, of->component);
    }
    for (const DvmClass *k = c->super; k; k = k->super)
        if (k == of) return true;
    return false;
}

bool dvm_instance_of(const DvmObject *o, const DvmClass *c)
{
    return o && dvm_is_subclass(o->clazz, c);
}

/* ------------------------------------------------------------- loading */

static void layout_fields(DvmClass *c)
{
    int base = c->super ? c->super->instance_slots : 0;
    int nrefs = c->super ? c->super->n_ref_slots : 0;
    for (int i = 0; i < c->n_ifields; i++) if (dvm_is_ref_type(c->ifields[i].type)) nrefs++;
    c->ref_slots = calloc(nrefs + 1, sizeof(uint32_t));
    if (c->super && c->super->n_ref_slots)
        memcpy(c->ref_slots, c->super->ref_slots, c->super->n_ref_slots * sizeof(uint32_t));
    c->n_ref_slots = c->super ? c->super->n_ref_slots : 0;
    for (int i = 0; i < c->n_ifields; i++) {
        c->ifields[i].slot = (uint32_t)(base + i);
        if (dvm_is_ref_type(c->ifields[i].type)) c->ref_slots[c->n_ref_slots++] = (uint32_t)(base + i);
    }
    c->instance_slots = base + c->n_ifields;

    c->statics = calloc(c->n_sfields + 1, sizeof(DvmReg));
    c->static_ref_slots = calloc(c->n_sfields + 1, sizeof(uint32_t));
    for (int i = 0; i < c->n_sfields; i++) {
        c->sfields[i].slot = (uint32_t)i;
        if (dvm_is_ref_type(c->sfields[i].type)) c->static_ref_slots[c->n_static_ref_slots++] = (uint32_t)i;
    }
}

static bool vtable_has(DvmMethod **vt, int n, const DvmMethod *m, int *at)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(vt[i]->name, m->name) && !strcmp(vt[i]->proto, m->proto)) { *at = i; return true; }
    return false;
}

static void add_iface_methods(DvmClass *iface, DvmMethod ***vt, int *n, int *cap)
{
    for (int i = iface->n_direct; i < iface->n_methods; i++) {
        DvmMethod *m = &iface->methods[i];
        int at;
        if (vtable_has(*vt, *n, m, &at)) {
            /* a default method fills a slot an abstract class left abstract */
            if (((*vt)[at]->access & ACC_ABSTRACT) && !(m->access & ACC_ABSTRACT)) (*vt)[at] = m;
            continue;
        }
        if (*n == *cap) { *cap = *cap * 2 + 8; *vt = realloc(*vt, *cap * sizeof(DvmMethod *)); }
        (*vt)[(*n)++] = m;
    }
    for (int i = 0; i < iface->n_ifaces; i++) add_iface_methods(iface->ifaces[i], vt, n, cap);
}

static void build_vtable(DvmClass *c)
{
    if (dvm_is_interface(c)) {
        for (int i = 0; i < c->n_methods; i++) c->methods[i].vtable_index = -1;
        return;
    }
    int n = c->super ? c->super->vtable_len : 0, cap = n + (c->n_methods - c->n_direct) + 8;
    DvmMethod **vt = malloc(cap * sizeof(DvmMethod *));
    if (n) memcpy(vt, c->super->vtable, n * sizeof(DvmMethod *));
    for (int i = 0; i < c->n_direct; i++) c->methods[i].vtable_index = -1;
    for (int i = c->n_direct; i < c->n_methods; i++) {
        DvmMethod *m = &c->methods[i];
        int at;
        if (vtable_has(vt, n, m, &at)) { vt[at] = m; m->vtable_index = at; continue; }
        if (n == cap) { cap = cap * 2 + 8; vt = realloc(vt, cap * sizeof(DvmMethod *)); }
        m->vtable_index = n;
        vt[n++] = m;
    }
    for (DvmClass *k = c; k; k = k->super)
        for (int i = 0; i < k->n_ifaces; i++) add_iface_methods(k->ifaces[i], &vt, &n, &cap);
    c->vtable = vt;
    c->vtable_len = n;
}

static int arg_slots_of(const char *shorty, bool is_static)
{
    int n = is_static ? 0 : 1;
    for (const char *p = shorty + 1; *p; p++) n += (*p == 'J' || *p == 'D') ? 2 : 1;
    return n;
}

static void read_method(DvmClass *c, DvmMethod *m, uint32_t midx, uint32_t access, uint32_t code_off)
{
    DvmDex *dex = c->dex;
    const uint8_t *mi = dex->method_ids + 8 * midx;
    m->clazz = c;
    m->name = dvm_dex_string(dex, rd32(mi + 4));
    m->proto = dvm_dex_proto(dex, rd16(mi + 2));
    m->shorty = dvm_dex_shorty(dex, rd16(mi + 2));
    m->access = access;
    m->ins = (uint16_t)arg_slots_of(m->shorty, access & ACC_STATIC);
    if (code_off && code_off + 16 <= dex->size) {
        const uint8_t *ci = dex->data + code_off;
        m->regs = rd16(ci);
        m->ins = rd16(ci + 2);
        m->outs = rd16(ci + 4);
        m->tries_size = rd16(ci + 6);
        m->insns_size = rd32(ci + 12);
        if ((uint64_t)code_off + 16 + 2ull * m->insns_size > dex->size) { m->insns_size = 0; return; }
        m->insns = (const uint16_t *)(ci + 16);
        if (m->tries_size) {
            const uint8_t *t = ci + 16 + 2 * m->insns_size;
            if (m->insns_size & 1) t += 2;
            m->tries = t;
            m->handlers = t + 8 * m->tries_size;
        }
    }
    if (access & ACC_NATIVE) m->native = dvm_find_native(c->descriptor, m->name, m->proto);
}

static DvmClass *load_class(DvmVM *vm, DvmDex *dex, int def, const char *desc, bool throw_)
{
    const uint8_t *cd = dex->class_defs + 32 * def;
    DvmClass *c = calloc(1, sizeof(*c));
    c->descriptor = desc;
    c->dex = dex;
    c->class_def = def;
    c->access = rd32(cd + 4);

    uint32_t super_idx = rd32(cd + 8);
    if (super_idx != 0xffffffffu) {
        DvmClass *s = throw_ ? dvm_find_class(vm, dvm_dex_type(dex, super_idx))
                             : dvm_lookup_class(vm, dvm_dex_type(dex, super_idx));
        if (!s) { free(c); return NULL; }
        if (dvm_is_interface(s)) {
            if (throw_) dvm_throw(vm, "Ljava/lang/IncompatibleClassChangeError;", "superclass of %s is an interface", desc);
            free(c); return NULL;
        }
        c->super = s;
    }
    uint32_t ifoff = rd32(cd + 12);
    if (ifoff && ifoff + 4 <= dex->size) {
        uint32_t n = rd32(dex->data + ifoff);
        if (ifoff + 4 + 2ull * n > dex->size) n = 0;
        c->ifaces = calloc(n + 1, sizeof(DvmClass *));
        for (uint32_t i = 0; i < n; i++) {
            const char *id = dvm_dex_type(dex, rd16(dex->data + ifoff + 4 + 2 * i));
            DvmClass *k = throw_ ? dvm_find_class(vm, id) : dvm_lookup_class(vm, id);
            if (!k) { free(c->ifaces); free(c); return NULL; }
            c->ifaces[c->n_ifaces++] = k;
        }
    }

    uint32_t data_off = rd32(cd + 24);
    if (data_off && data_off < dex->size) {
        const uint8_t *p = dex->data + data_off;
        uint32_t ns = dvm_uleb(&p), ni = dvm_uleb(&p), nd = dvm_uleb(&p), nv = dvm_uleb(&p);
        c->sfields = calloc(ns + 1, sizeof(DvmField));
        c->ifields = calloc(ni + 1, sizeof(DvmField));
        c->methods = calloc(nd + nv + 1, sizeof(DvmMethod));
        for (int pass = 0; pass < 2; pass++) {
            uint32_t n = pass ? ni : ns, idx = 0;
            DvmField *fs = pass ? c->ifields : c->sfields;
            for (uint32_t i = 0; i < n; i++) {
                idx += dvm_uleb(&p);
                uint32_t acc = dvm_uleb(&p);
                if (idx >= dex->n_fields) continue;
                const uint8_t *fi = dex->field_ids + 8 * idx;
                DvmField *f = &fs[pass ? c->n_ifields++ : c->n_sfields++];
                f->clazz = c;
                f->type = dvm_dex_type(dex, rd16(fi + 2));
                f->name = dvm_dex_string(dex, rd32(fi + 4));
                f->access = acc;
            }
        }
        for (int pass = 0; pass < 2; pass++) {
            uint32_t n = pass ? nv : nd, idx = 0;
            for (uint32_t i = 0; i < n; i++) {
                idx += dvm_uleb(&p);
                uint32_t acc = dvm_uleb(&p), code = dvm_uleb(&p);
                if (idx >= dex->n_methods) continue;
                read_method(c, &c->methods[c->n_methods++], idx, acc, code);
            }
            if (!pass) c->n_direct = c->n_methods;
        }
    }

    layout_fields(c);
    build_vtable(c);
    c->status = CLS_LINKED;
    hash_put(vm, c);
    return c;
}

static DvmClass *find_class(DvmVM *vm, const char *desc, bool throw_)
{
    DvmClass *c = hash_get(vm, desc);
    if (c) return c;
    if (desc[0] == '[') {
        DvmClass *comp = find_class(vm, desc + 1, throw_);
        return comp ? dvm_array_class_of(vm, comp) : NULL;
    }
    if (desc[0] && !desc[1]) {
        c = dvm_primitive_class(vm, desc[0]);
        if (c) return c;
    }
    if (desc[0] == 'L') {
        for (int i = 0; i < vm->n_dexes; i++) {
            int def = dvm_dex_find_class_def(vm->dexes[i], desc);
            if (def >= 0) {
                /* the descriptor string lives in the DEX, as long as the class does */
                const char *d = dvm_dex_type(vm->dexes[i], rd32(vm->dexes[i]->class_defs + 32 * def));
                return load_class(vm, vm->dexes[i], def, d, throw_);
            }
        }
    }
    if (throw_) {
        /* Java names the class with dots */
        char name[512];
        size_t n = 0;
        const char *s = desc[0] == 'L' ? desc + 1 : desc;
        for (; *s && *s != ';' && n < sizeof(name) - 1; s++) name[n++] = *s == '/' ? '.' : *s;
        name[n] = 0;
        dvm_throw(vm, "Ljava/lang/NoClassDefFoundError;", "%s", name);
    }
    return NULL;
}

DvmClass *dvm_find_class(DvmVM *vm, const char *desc) { return find_class(vm, desc, true); }
DvmClass *dvm_lookup_class(DvmVM *vm, const char *desc) { return find_class(vm, desc, false); }

/* ------------------------------------------------------------- initialisation */

static bool apply_static_values(DvmVM *vm, DvmClass *c)
{
    if (c->class_def < 0) return true;
    uint32_t off = rd32(c->dex->class_defs + 32 * c->class_def + 28);
    if (!off || off >= c->dex->size) return true;
    const uint8_t *p = c->dex->data + off;
    uint32_t n = dvm_uleb(&p);
    for (uint32_t i = 0; i < n && (int)i < c->n_sfields; i++) {
        uint8_t h = *p++;
        int type = h & 0x1f, arg = h >> 5, size = arg + 1;
        uint64_t raw = 0;
        DvmReg v = 0;
        if (type == 0x1e || type == 0x1f) {             /* null, boolean: no bytes */
            v = type == 0x1f ? (DvmReg)arg : 0;
            c->statics[i] = v;
            continue;
        }
        if (type == 0x1c || type == 0x1d) {             /* arrays and annotations in a static initial value: not emitted by compilers */
            dvm_throw(vm, "Ljava/lang/UnsupportedOperationException;", "encoded array/annotation static value in %s", c->descriptor);
            return false;
        }
        for (int b = 0; b < size; b++) raw |= (uint64_t)p[b] << (8 * b);
        p += size;
        switch (type) {
        case 0x00: v = (uint32_t)(int32_t)(int8_t)raw; break;
        case 0x02:
        case 0x04: v = (uint32_t)(int32_t)((int64_t)(raw << (64 - 8 * size)) >> (64 - 8 * size)); break;
        case 0x03: v = (uint32_t)raw; break;
        case 0x06: v = (uint64_t)((int64_t)(raw << (64 - 8 * size)) >> (64 - 8 * size)); break;
        case 0x10: v = (uint32_t)(raw << (32 - 8 * size)); break;
        case 0x11: v = raw << (64 - 8 * size); break;
        case 0x17: {
            DvmObject *s = dvm_resolve_string(vm, c->dex, (uint32_t)raw);
            if (!s) return false;
            v = DVM_REF(s);
            break;
        }
        case 0x18: {
            DvmClass *k = dvm_resolve_type(vm, c->dex, (uint32_t)raw);
            if (!k) return false;
            v = DVM_REF(dvm_class_object(vm, k));
            break;
        }
        default: v = 0; break;
        }
        c->statics[i] = v;
    }
    return true;
}

bool dvm_init_class(DvmVM *vm, DvmClass *c)
{
    if (c->status == CLS_INITIALIZED || c->status == CLS_INITIALIZING) return true;
    if (c->status == CLS_ERROR) {
        dvm_throw(vm, "Ljava/lang/NoClassDefFoundError;", "Could not initialize class %s", c->descriptor);
        return false;
    }
    c->status = CLS_INITIALIZING;
    if (c->super && !dvm_init_class(vm, c->super)) { c->status = CLS_ERROR; return false; }
    if (!apply_static_values(vm, c)) { c->status = CLS_ERROR; return false; }
    DvmMethod *clinit = dvm_find_declared_method(c, "<clinit>", "()V");
    if (clinit && !dvm_call(vm, clinit, NULL, NULL)) {
        c->status = CLS_ERROR;
        DvmObject *ex = vm->exception;
        DvmClass *error = dvm_lookup_class(vm, "Ljava/lang/Error;");
        if (error && !dvm_instance_of(ex, error)) {
            vm->exception = NULL;
            DvmClass *eiie = dvm_find_class(vm, "Ljava/lang/ExceptionInInitializerError;");
            DvmMethod *ctor = eiie ? dvm_find_declared_method(eiie, "<init>", "(Ljava/lang/Throwable;)V") : NULL;
            if (!ctor) { vm->exception = ex; return false; }
            DvmObject *w = dvm_alloc_object(vm, eiie);
            if (!w) return false;
            DvmReg args[2] = { DVM_REF(w), DVM_REF(ex) };
            if (dvm_call(vm, ctor, args, NULL)) dvm_throw_object(vm, w);
        }
        return false;
    }
    c->status = CLS_INITIALIZED;
    return true;
}

/* ------------------------------------------------------------- resolution */

DvmClass *dvm_resolve_type(DvmVM *vm, DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_types) { dvm_throw(vm, "Ljava/lang/VerifyError;", "type index %u", idx); return NULL; }
    if (dex->types[idx]) return dex->types[idx];
    DvmClass *c = dvm_find_class(vm, dvm_dex_type(dex, idx));
    if (c) dex->types[idx] = c;
    return c;
}

DvmMethod *dvm_resolve_method(DvmVM *vm, DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_methods) { dvm_throw(vm, "Ljava/lang/VerifyError;", "method index %u", idx); return NULL; }
    if (dex->methods[idx]) return dex->methods[idx];
    const uint8_t *mi = dex->method_ids + 8 * idx;
    DvmClass *c = dvm_resolve_type(vm, dex, rd16(mi));
    if (!c) return NULL;
    const char *name = dvm_dex_string(dex, rd32(mi + 4));
    const char *proto = dvm_dex_proto(dex, rd16(mi + 2));
    DvmMethod *m = dvm_find_method(c, name, proto);
    if (!m) {
        dvm_throw(vm, "Ljava/lang/NoSuchMethodError;", "%s.%s%s", c->descriptor, name, proto);
        return NULL;
    }
    dex->methods[idx] = m;
    return m;
}

DvmField *dvm_resolve_field(DvmVM *vm, DvmDex *dex, uint32_t idx, bool is_static)
{
    if (idx >= dex->n_fields) { dvm_throw(vm, "Ljava/lang/VerifyError;", "field index %u", idx); return NULL; }
    if (dex->fields[idx]) return dex->fields[idx];
    const uint8_t *fi = dex->field_ids + 8 * idx;
    DvmClass *c = dvm_resolve_type(vm, dex, rd16(fi));
    if (!c) return NULL;
    const char *name = dvm_dex_string(dex, rd32(fi + 4));
    const char *type = dvm_dex_type(dex, rd16(fi + 2));
    DvmField *f = dvm_find_field(c, name, type, is_static);
    if (!f) {
        dvm_throw(vm, "Ljava/lang/NoSuchFieldError;", "%s field %s.%s %s", is_static ? "static" : "instance",
                  c->descriptor, name, type);
        return NULL;
    }
    dex->fields[idx] = f;
    return f;
}

DvmObject *dvm_resolve_string(DvmVM *vm, DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_strings) { dvm_throw(vm, "Ljava/lang/VerifyError;", "string index %u", idx); return NULL; }
    if (dex->strings[idx]) return dex->strings[idx];
    DvmObject *s = dvm_new_string_mutf8(vm, dvm_dex_string(dex, idx));
    if (s) dex->strings[idx] = s;
    return s;
}

DvmObject *dvm_class_object(DvmVM *vm, DvmClass *c)
{
    if (c->class_obj) return c->class_obj;
    if (!vm->c_class) dvm_fatal("java.lang.Class is missing from the boot library");
    DvmObject *o = dvm_alloc_object(vm, vm->c_class);
    if (!o) return NULL;
    DVM_FIELDS(o)[vm->slot_class_vm] = (DvmReg)(uintptr_t)c;
    c->class_obj = o;
    return o;
}

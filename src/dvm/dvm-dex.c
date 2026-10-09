/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DEX files: the header's tables, checked against the file's size before
 * anything uses them, and an index from class descriptor to class_def.
 */
#include "dvm-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

uint32_t dvm_uleb(const uint8_t **p)
{
    const uint8_t *s = *p;
    uint32_t r = 0;
    for (int i = 0; i < 5; i++) {
        r |= (uint32_t)(s[i] & 0x7f) << (7 * i);
        if (!(s[i] & 0x80)) { *p = s + i + 1; return r; }
    }
    *p = s + 5;
    return r;
}

int32_t dvm_sleb(const uint8_t **p)
{
    const uint8_t *s = *p;
    int32_t r = 0;
    int i = 0, shift = 0;
    uint8_t b;
    do {
        b = s[i++];
        r |= (int32_t)((uint32_t)(b & 0x7f) << shift);
        shift += 7;
    } while ((b & 0x80) && i < 5);
    if (shift < 32 && (b & 0x40)) r |= (int32_t)(~0u << shift);
    *p = s + i;
    return r;
}

const char *dvm_dex_string(const DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_strings) return "";
    const uint8_t *p = dex->data + dex->string_ids[idx];
    dvm_uleb(&p);                                   /* utf16 length */
    return (const char *)p;
}

const char *dvm_dex_type(const DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_types) return "";
    return dvm_dex_string(dex, dex->type_ids[idx]);
}

const char *dvm_dex_shorty(const DvmDex *dex, uint32_t idx)
{
    return dvm_dex_string(dex, rd32(dex->proto_ids + 12 * idx));
}

/* "(params)return", made once per proto. */
const char *dvm_dex_proto(DvmDex *dex, uint32_t idx)
{
    if (idx >= dex->n_protos) return "()V";
    if (dex->protos[idx]) return dex->protos[idx];
    const uint8_t *pi = dex->proto_ids + 12 * idx;
    const char *ret = dvm_dex_type(dex, rd32(pi + 4));
    uint32_t params_off = rd32(pi + 8);
    size_t len = 3 + strlen(ret);
    uint32_t n = 0;
    const uint8_t *list = NULL;
    if (params_off && params_off + 4 <= dex->size) {
        n = rd32(dex->data + params_off);
        list = dex->data + params_off + 4;
        if (params_off + 4 + 2ull * n > dex->size) n = 0;
        for (uint32_t i = 0; i < n; i++) len += strlen(dvm_dex_type(dex, rd16(list + 2 * i)));
    }
    char *s = malloc(len), *w = s;
    *w++ = '(';
    for (uint32_t i = 0; i < n; i++) {
        const char *t = dvm_dex_type(dex, rd16(list + 2 * i));
        size_t l = strlen(t);
        memcpy(w, t, l); w += l;
    }
    *w++ = ')';
    strcpy(w, ret);
    dex->protos[idx] = s;
    return s;
}

static uint32_t str_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

int dvm_dex_find_class_def(const DvmDex *dex, const char *descriptor)
{
    if (!dex->def_index_cap) return -1;
    uint32_t mask = dex->def_index_cap - 1;
    for (uint32_t i = str_hash(descriptor) & mask;; i = (i + 1) & mask) {
        int32_t d = dex->def_index[i];
        if (d < 0) return -1;
        if (!strcmp(dvm_dex_type(dex, rd32(dex->class_defs + 32 * d)), descriptor)) return d;
    }
}

bool dvm_dex_open(DvmVM *vm, uint8_t *data, size_t size, char *err, size_t errlen)
{
#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); return false; } while (0)
    if (size < 0x70 || memcmp(data, "dex\n", 4)) FAIL("not a DEX file");
    if (rd32(data + 0x28) != 0x12345678) FAIL("DEX is not little-endian");
    if (rd32(data + 0x20) > size) FAIL("DEX is truncated");

    DvmDex *d = calloc(1, sizeof(*d));
    d->data = data;
    d->size = size;
    struct { uint32_t *n; uint32_t off_at; uint32_t esz; const void **table; } t[] = {
        { &d->n_strings,    0x38, 4,  (const void **)&d->string_ids },
        { &d->n_types,      0x40, 4,  (const void **)&d->type_ids },
        { &d->n_protos,     0x48, 12, (const void **)&d->proto_ids },
        { &d->n_fields,     0x50, 8,  (const void **)&d->field_ids },
        { &d->n_methods,    0x58, 8,  (const void **)&d->method_ids },
        { &d->n_class_defs, 0x60, 32, (const void **)&d->class_defs },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint32_t n = rd32(data + t[i].off_at), off = rd32(data + t[i].off_at + 4);
        if (n && (uint64_t)off + (uint64_t)n * t[i].esz > size) { free(d); FAIL("DEX table out of range"); }
        *t[i].n = n;
        *t[i].table = data + off;
    }
    for (uint32_t i = 0; i < d->n_strings; i++)
        if (d->string_ids[i] >= size) { free(d); FAIL("DEX string out of range"); }

    d->strings = calloc(d->n_strings + 1, sizeof(*d->strings));
    d->types = calloc(d->n_types + 1, sizeof(*d->types));
    d->fields = calloc(d->n_fields + 1, sizeof(*d->fields));
    d->methods = calloc(d->n_methods + 1, sizeof(*d->methods));
    d->protos = calloc(d->n_protos + 1, sizeof(*d->protos));

    uint32_t cap = 16;
    while (cap < d->n_class_defs * 2) cap <<= 1;
    d->def_index_cap = cap;
    d->def_index = malloc(cap * sizeof(int32_t));
    memset(d->def_index, 0xff, cap * sizeof(int32_t));
    for (uint32_t c = 0; c < d->n_class_defs; c++) {
        const char *desc = dvm_dex_type(d, rd32(d->class_defs + 32 * c));
        if (dvm_dex_find_class_def(d, desc) >= 0) continue;   /* first definition wins */
        for (uint32_t i = str_hash(desc) & (cap - 1);; i = (i + 1) & (cap - 1))
            if (d->def_index[i] < 0) { d->def_index[i] = (int32_t)c; break; }
    }

    vm->dexes = realloc(vm->dexes, (vm->n_dexes + 1) * sizeof(*vm->dexes));
    vm->dexes[vm->n_dexes++] = d;
    return true;
#undef FAIL
}

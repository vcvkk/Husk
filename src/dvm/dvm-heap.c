/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The heap: a non-moving mark-sweep collector.
 *
 * Every object is its own allocation, recorded in an open-addressed set. The set
 * is how the collector recognises a pointer: Dalvik registers carry no types, and
 * native code holds objects in C locals, so the register stack and the C stack
 * are scanned conservatively -- any word equal to the address of a live object
 * keeps it. Everything else is precise: static fields and instance fields by the
 * class's reference slots, arrays of references element by element, the DEX
 * files' resolved strings, class objects, the pending exception, the exceptions
 * handlers were entered with, and objects C code registered as globals.
 *
 * Objects never move, so a conservative root is never a problem to update.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "dvm-internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GC_MIN_THRESHOLD (16u << 20)

static size_t object_size(const DvmObject *o)
{
    const DvmClass *c = o->clazz;
    if (dvm_is_array(c)) return sizeof(DvmArray) + (size_t)((const DvmArray *)o)->length * c->elem_size;
    return sizeof(DvmObject) + (size_t)c->instance_slots * sizeof(DvmReg);
}

static size_t ptr_hash(const void *p, size_t mask) { return (size_t)(((uintptr_t)p >> 3) * 0x9E3779B97F4A7C15ull) & mask; }

static void set_insert(DvmVM *vm, DvmObject *o)
{
    if ((vm->objs_count + 1) * 2 > vm->objs_cap) {
        size_t cap = vm->objs_cap ? vm->objs_cap * 2 : 4096;
        DvmObject **t = calloc(cap, sizeof(*t));
        for (size_t i = 0; i < vm->objs_cap; i++) {
            DvmObject *e = vm->objs[i];
            if (!e) continue;
            size_t j = ptr_hash(e, cap - 1);
            while (t[j]) j = (j + 1) & (cap - 1);
            t[j] = e;
        }
        free(vm->objs);
        vm->objs = t;
        vm->objs_cap = cap;
    }
    size_t j = ptr_hash(o, vm->objs_cap - 1);
    while (vm->objs[j]) j = (j + 1) & (vm->objs_cap - 1);
    vm->objs[j] = o;
    vm->objs_count++;
    uintptr_t a = (uintptr_t)o;
    if (!vm->heap_lo || a < vm->heap_lo) vm->heap_lo = a;
    if (a > vm->heap_hi) vm->heap_hi = a;
}

static DvmObject *set_find(const DvmVM *vm, uintptr_t a)
{
    if (a < vm->heap_lo || a > vm->heap_hi || (a & 7) || !vm->objs_cap) return NULL;
    size_t j = ptr_hash((void *)a, vm->objs_cap - 1);
    for (DvmObject *e; (e = vm->objs[j]); j = (j + 1) & (vm->objs_cap - 1))
        if ((uintptr_t)e == a) return e;
    return NULL;
}

/* ----------------------------------------------------------------- marking */

static void mark(DvmVM *vm, DvmObject *o)
{
    if (!o || o->gc) return;
    o->gc = 1;
    if (vm->mark_top == vm->mark_cap) {
        vm->mark_cap = vm->mark_cap ? vm->mark_cap * 2 : 1024;
        vm->mark_stack = realloc(vm->mark_stack, vm->mark_cap * sizeof(DvmObject *));
    }
    vm->mark_stack[vm->mark_top++] = o;
}

/* Reads whole stacks on purpose, red zones included. */
__attribute__((no_sanitize("address")))
static void mark_conservative(DvmVM *vm, const void *lo, const void *hi)
{
    const uintptr_t *p = (const uintptr_t *)(((uintptr_t)lo + 7) & ~(uintptr_t)7);
    for (; (const void *)(p + 1) <= hi; p++) {
        DvmObject *o = set_find(vm, *p);
        if (o) mark(vm, o);
    }
}

static void drain(DvmVM *vm)
{
    while (vm->mark_top) {
        DvmObject *o = vm->mark_stack[--vm->mark_top];
        const DvmClass *c = o->clazz;
        if (dvm_is_array(c)) {
            if (c->prim == 'L') {
                DvmArray *a = (DvmArray *)o;
                DvmObject **e = (DvmObject **)a->data;
                for (int32_t i = 0; i < a->length; i++) mark(vm, e[i]);
            }
        } else {
            DvmReg *f = DVM_FIELDS(o);
            for (int i = 0; i < c->n_ref_slots; i++) mark(vm, DVM_OBJ(f[c->ref_slots[i]]));
        }
    }
}

static void __attribute__((noinline)) scan_c_stack(DvmVM *vm)
{
    /* Callee-saved registers may hold the only copy of a pointer: setjmp spills them.
     * The buffer is scanned on its own because a sanitizer may put locals on a heap "fake stack". */
    jmp_buf regs;
    setjmp(regs);
    mark_conservative(vm, &regs, (const char *)&regs + sizeof(regs));
    mark_conservative(vm, __builtin_frame_address(0), (const void *)vm->stack_hi);
}

void dvm_gc(DvmVM *vm)
{
    if (vm->gc_running) return;
    vm->gc_running = true;

    for (int h = 0; h < DVM_CLASS_HASH; h++)
        for (DvmClass *c = vm->class_hash[h]; c; c = c->hash_next) {
            mark(vm, c->class_obj);
            for (int i = 0; i < c->n_static_ref_slots; i++) mark(vm, DVM_OBJ(c->statics[c->static_ref_slots[i]]));
        }
    for (int d = 0; d < vm->n_dexes; d++)
        for (uint32_t i = 0; i < vm->dexes[d]->n_strings; i++) mark(vm, vm->dexes[d]->strings[i]);
    mark(vm, vm->exception);
    mark(vm, vm->oom);
    for (size_t i = 0; i < vm->n_globals; i++) mark(vm, vm->globals[i]);
    for (DvmFrame *f = vm->frame; f; f = f->prev) mark(vm, f->caught);
    mark_conservative(vm, vm->reg_stack, vm->reg_stack + vm->reg_top);
    scan_c_stack(vm);
    drain(vm);

    /* sweep, and rebuild the set from the survivors */
    DvmObject **old = vm->objs;
    size_t cap = vm->objs_cap;
    vm->objs = NULL; vm->objs_cap = 0; vm->objs_count = 0; vm->heap_lo = vm->heap_hi = 0;
    size_t live = 0;
    for (size_t i = 0; i < cap; i++) {
        DvmObject *o = old[i];
        if (!o) continue;
        if (o->gc) { o->gc = 0; live += object_size(o); set_insert(vm, o); }
        else free(o);
    }
    free(old);
    vm->heap_bytes = live;
    vm->bytes_since_gc = 0;
    vm->gc_threshold = live * 2 > GC_MIN_THRESHOLD ? live * 2 : GC_MIN_THRESHOLD;
    vm->gc_count++;
    vm->gc_running = false;
}

/* ------------------------------------------------------------- allocation */

static void *heap_alloc(DvmVM *vm, size_t size)
{
    if (vm->bytes_since_gc + size > vm->gc_threshold || (vm->gc_stress && ++vm->gc_stress_count % vm->gc_stress == 0)) dvm_gc(vm);
    void *p = calloc(1, size);
    if (!p) { dvm_gc(vm); p = calloc(1, size); }
    if (!p) { dvm_throw_object(vm, vm->oom); return NULL; }
    set_insert(vm, p);
    vm->heap_bytes += size;
    vm->bytes_since_gc += size;
    return p;
}

DvmObject *dvm_alloc_object(DvmVM *vm, DvmClass *c)
{
    DvmObject *o = heap_alloc(vm, sizeof(DvmObject) + (size_t)c->instance_slots * sizeof(DvmReg));
    if (o) o->clazz = c;
    return o;
}

DvmArray *dvm_alloc_array(DvmVM *vm, DvmClass *ac, int32_t length)
{
    if (length < 0) {
        dvm_throw(vm, "Ljava/lang/NegativeArraySizeException;", "%d", length);
        return NULL;
    }
    if ((uint64_t)length * ac->elem_size > (1ull << 31)) { dvm_throw_object(vm, vm->oom); return NULL; }
    DvmArray *a = heap_alloc(vm, sizeof(DvmArray) + (size_t)length * ac->elem_size);
    if (!a) return NULL;
    a->obj.clazz = ac;
    a->length = length;
    return a;
}

DvmArray *dvm_alloc_array_of(DvmVM *vm, const char *desc, int32_t length)
{
    DvmClass *ac = dvm_find_class(vm, desc);
    return ac ? dvm_alloc_array(vm, ac, length) : NULL;
}

DvmObject *dvm_clone(DvmVM *vm, DvmObject *o)
{
    size_t size = object_size(o);
    DvmObject *c = heap_alloc(vm, size);
    if (!c) return NULL;
    memcpy(c, o, size);
    c->gc = 0;
    c->hash = 0;
    return c;
}

uint32_t dvm_identity_hash(DvmVM *vm, DvmObject *o)
{
    if (!o->hash) {
        uint32_t x = (vm->next_hash += 0x9E3779B9u);
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15;
        o->hash = (x & 0x7fffffff) | 1;
    }
    return o->hash;
}

void dvm_add_global(DvmVM *vm, DvmObject *o)
{
    if (vm->n_globals == vm->globals_cap) {
        vm->globals_cap = vm->globals_cap * 2 + 16;
        vm->globals = realloc(vm->globals, vm->globals_cap * sizeof(DvmObject *));
    }
    vm->globals[vm->n_globals++] = o;
}

void dvm_heap_free_all(DvmVM *vm)
{
    for (size_t i = 0; i < vm->objs_cap; i++) free(vm->objs[i]);
    free(vm->objs);
    free(vm->mark_stack);
    free(vm->globals);
    vm->objs = NULL;
    vm->objs_cap = vm->objs_count = 0;
}

size_t dvm_heap_objects(const DvmVM *vm) { return vm->objs_count; }
size_t dvm_heap_bytes(const DvmVM *vm) { return vm->heap_bytes; }

/* The top of the calling thread's C stack: the collector scans from where it is up to here. */
uintptr_t dvm_thread_stack_top(void)
{
#ifdef __APPLE__
    return (uintptr_t)pthread_get_stackaddr_np(pthread_self());
#else
    pthread_attr_t attr;
    void *addr = NULL;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
    }
    return (uintptr_t)addr + size;
#endif
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HUSK_DVM_INTERNAL_H
#define HUSK_DVM_INTERNAL_H

#include "dvm.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct DvmClass DvmClass;
typedef struct DvmMethod DvmMethod;
typedef struct DvmField DvmField;
typedef struct DvmObject DvmObject;
typedef struct DvmArray DvmArray;
typedef struct DvmDex DvmDex;
typedef struct DvmFrame DvmFrame;

/*
 * A Dalvik register, and a field slot. Every value lives in 64 bits: an int or a
 * float in the low 32 with the high 32 zero, a long or a double in all of it, a
 * reference as the pointer. A wide value sits whole in its first register; the
 * second register of the pair is never read. Ints are always written
 * zero-extended, so a 64-bit compare is right for both ints and references.
 */
typedef uint64_t DvmReg;

/* Every native method: arguments in register form (this first, a wide value in one
 * slot followed by an unused one). Returns false with an exception pending. */
typedef bool (*DvmNativeFn)(DvmVM *vm, DvmReg *args, DvmReg *ret);

enum {
    ACC_PUBLIC = 0x1, ACC_PRIVATE = 0x2, ACC_PROTECTED = 0x4, ACC_STATIC = 0x8, ACC_FINAL = 0x10,
    ACC_SYNCHRONIZED = 0x20, ACC_VOLATILE = 0x40, ACC_BRIDGE = 0x40, ACC_TRANSIENT = 0x80,
    ACC_VARARGS = 0x80, ACC_NATIVE = 0x100, ACC_INTERFACE = 0x200, ACC_ABSTRACT = 0x400,
    ACC_STRICT = 0x800, ACC_SYNTHETIC = 0x1000, ACC_ANNOTATION = 0x2000, ACC_ENUM = 0x4000,
    ACC_CONSTRUCTOR = 0x10000,
};

struct DvmObject {
    DvmClass *clazz;
    uint32_t gc;                 /* mark bit */
    uint32_t hash;               /* identity hash; 0 until asked for */
};

struct DvmArray {
    DvmObject obj;
    int32_t length;
    uint32_t pad;
    uint8_t data[];              /* 8-aligned */
};

#define DVM_FIELDS(o) ((DvmReg *)((DvmObject *)(o) + 1))

struct DvmField {
    DvmClass *clazz;
    const char *name;
    const char *type;            /* descriptor */
    uint32_t access;
    uint32_t slot;               /* into the object's fields or the class's statics */
};

struct DvmMethod {
    DvmClass *clazz;
    const char *name;
    const char *proto;           /* "(I[Ljava/lang/String;)V" */
    const char *shorty;
    uint32_t access;
    uint16_t regs, ins, outs;    /* ins counts `this` */
    uint32_t insns_size;
    const uint16_t *insns;
    uint16_t tries_size;
    const uint8_t *tries;        /* try_item[] */
    const uint8_t *handlers;     /* encoded_catch_handler_list */
    int vtable_index;            /* -1: not dispatched through a vtable */
    DvmNativeFn native;
    /* invoke-interface's inline cache: the last receiver class and what it chose */
    DvmClass *ic_class;
    DvmMethod *ic_target;
};

enum { CLS_LOADED, CLS_LINKED, CLS_INITIALIZING, CLS_INITIALIZED, CLS_ERROR };

struct DvmClass {
    const char *descriptor;
    DvmClass *super;
    DvmClass **ifaces;           /* directly declared */
    int n_ifaces;
    uint32_t access;
    DvmDex *dex;
    int class_def;               /* -1 for array and primitive classes */
    int status;

    DvmField *sfields, *ifields;
    int n_sfields, n_ifields;
    DvmMethod *methods;          /* direct, then virtual */
    int n_methods, n_direct;
    DvmMethod **vtable;
    int vtable_len;

    DvmReg *statics;
    int instance_slots;          /* including the superclasses' */
    uint32_t *ref_slots;         /* instance slots holding references */
    int n_ref_slots;
    uint32_t *static_ref_slots;
    int n_static_ref_slots;

    DvmClass *component;         /* arrays */
    char prim;                   /* 'I'... for a primitive class; the element type char for an array */
    uint8_t elem_size;           /* arrays */
    DvmObject *class_obj;        /* its java.lang.Class, made on demand */
    DvmClass *hash_next;
};

struct DvmDex {
    uint8_t *data;
    size_t size;
    uint32_t n_strings, n_types, n_protos, n_fields, n_methods, n_class_defs;
    const uint32_t *string_ids;
    const uint32_t *type_ids;
    const uint8_t *proto_ids;    /* 12 bytes each */
    const uint8_t *field_ids;    /* 8 bytes each */
    const uint8_t *method_ids;   /* 8 bytes each */
    const uint8_t *class_defs;   /* 32 bytes each */

    DvmObject **strings;         /* resolved const-strings: GC roots */
    DvmClass **types;
    DvmField **fields;
    DvmMethod **methods;
    char **protos;
    /* descriptor -> class_def, open addressing */
    int32_t *def_index;
    uint32_t def_index_cap;
};

struct DvmFrame {
    DvmFrame *prev;
    DvmMethod *method;
    uint32_t pc;
    DvmObject *caught;           /* the exception a handler was entered with */
};

typedef struct DvmNative {
    const char *cls, *name, *proto;
    DvmNativeFn fn;
} DvmNative;

#define DVM_CLASS_HASH 4096
#define DVM_MAX_DEPTH 3000

struct DvmVM {
    DvmDex **dexes;
    int n_dexes;
    DvmClass *class_hash[DVM_CLASS_HASH];

    DvmObject *exception;        /* pending */
    DvmFrame *frame;             /* innermost */
    int depth, depth_limit;
    size_t reg_limit;            /* reg_cap less a reserve for constructing StackOverflowError */

    /* registers of all live frames, bump-allocated */
    DvmReg *reg_stack;
    size_t reg_cap, reg_top;

    /* the heap: every object, in a set the collector also uses to recognise pointers */
    DvmObject **objs;
    size_t objs_cap, objs_count;
    uintptr_t heap_lo, heap_hi;
    size_t heap_bytes, bytes_since_gc, gc_threshold;
    DvmObject **mark_stack;
    size_t mark_cap, mark_top;
    uintptr_t stack_hi;          /* the C stack's top, for conservative scanning */
    bool gc_running;
    uint64_t gc_count;
    uint32_t gc_stress, gc_stress_count;   /* DVM_GC_STRESS=N: collect every N allocations */

    /* objects C code keeps across calls */
    DvmObject **globals;
    size_t n_globals, globals_cap;

    uint32_t next_hash;

    /* well-known classes and slots */
    DvmClass *c_object, *c_string, *c_class, *c_throwable, *c_char_array;
    uint32_t slot_string_value, slot_class_vm, slot_throwable_backtrace;
    DvmObject *oom;              /* preallocated */

    bool exiting;
    int exit_status;
    jmp_buf *exit_jmp;
};

/* dex.c */
bool dvm_dex_open(DvmVM *vm, uint8_t *data, size_t size, char *err, size_t errlen);
const char *dvm_dex_string(const DvmDex *dex, uint32_t idx);
const char *dvm_dex_type(const DvmDex *dex, uint32_t idx);
const char *dvm_dex_proto(DvmDex *dex, uint32_t idx);
const char *dvm_dex_shorty(const DvmDex *dex, uint32_t idx);
int dvm_dex_find_class_def(const DvmDex *dex, const char *descriptor);
uint32_t dvm_uleb(const uint8_t **p);
int32_t dvm_sleb(const uint8_t **p);

/* class.c */
DvmClass *dvm_find_class(DvmVM *vm, const char *descriptor);       /* throws NoClassDefFoundError */
DvmClass *dvm_lookup_class(DvmVM *vm, const char *descriptor);     /* never throws; NULL if absent */
DvmClass *dvm_array_class_of(DvmVM *vm, DvmClass *component);
bool dvm_init_class(DvmVM *vm, DvmClass *c);
bool dvm_is_subclass(const DvmClass *c, const DvmClass *of);       /* assignability */
bool dvm_instance_of(const DvmObject *o, const DvmClass *c);
DvmMethod *dvm_find_method(DvmClass *c, const char *name, const char *proto);
DvmMethod *dvm_find_declared_method(DvmClass *c, const char *name, const char *proto);
DvmField *dvm_find_field(DvmClass *c, const char *name, const char *type, bool is_static);
DvmMethod *dvm_find_interface_target(DvmClass *c, DvmMethod *im);
DvmClass *dvm_resolve_type(DvmVM *vm, DvmDex *dex, uint32_t idx);
DvmMethod *dvm_resolve_method(DvmVM *vm, DvmDex *dex, uint32_t idx);
DvmField *dvm_resolve_field(DvmVM *vm, DvmDex *dex, uint32_t idx, bool is_static);
DvmObject *dvm_resolve_string(DvmVM *vm, DvmDex *dex, uint32_t idx);
DvmObject *dvm_class_object(DvmVM *vm, DvmClass *c);
DvmClass *dvm_primitive_class(DvmVM *vm, char type);
static inline bool dvm_is_interface(const DvmClass *c) { return (c->access & ACC_INTERFACE) != 0; }
static inline bool dvm_is_array(const DvmClass *c) { return c->descriptor[0] == '['; }
static inline bool dvm_is_ref_type(const char *t) { return t[0] == 'L' || t[0] == '['; }

/* heap.c */
uintptr_t dvm_thread_stack_top(void);
DvmObject *dvm_alloc_object(DvmVM *vm, DvmClass *c);
DvmArray *dvm_alloc_array(DvmVM *vm, DvmClass *array_class, int32_t length);
DvmArray *dvm_alloc_array_of(DvmVM *vm, const char *array_descriptor, int32_t length);
DvmObject *dvm_clone(DvmVM *vm, DvmObject *o);
uint32_t dvm_identity_hash(DvmVM *vm, DvmObject *o);
void dvm_add_global(DvmVM *vm, DvmObject *o);
void dvm_heap_free_all(DvmVM *vm);

/* interp.c */
bool dvm_call(DvmVM *vm, DvmMethod *m, DvmReg *args, DvmReg *ret);
bool dvm_callv(DvmVM *vm, DvmMethod *m, DvmReg *ret, ...);  /* args as DvmReg, count from the method */
int dvm_arg_slots(const DvmMethod *m);

/* string helpers (native.c) */
DvmObject *dvm_new_string(DvmVM *vm, const char *utf8);
DvmObject *dvm_new_string_utf16(DvmVM *vm, const uint16_t *chars, int32_t len);
DvmObject *dvm_new_string_mutf8(DvmVM *vm, const char *mutf8);
char *dvm_string_utf8(DvmVM *vm, DvmObject *s);     /* malloc'd; NULL for null */
DvmArray *dvm_string_chars(DvmVM *vm, DvmObject *s);

/* exceptions (native.c) */
void dvm_throw(DvmVM *vm, const char *descriptor, const char *fmt, ...);
void dvm_throw_object(DvmVM *vm, DvmObject *ex);
bool dvm_fill_in_stack_trace(DvmVM *vm, DvmObject *throwable);
void dvm_print_exception(DvmVM *vm, DvmObject *ex);
DvmNativeFn dvm_find_native(const char *cls, const char *name, const char *proto);

#define DVM_OBJ(r)  ((DvmObject *)(uintptr_t)(r))
#define DVM_REF(o)  ((DvmReg)(uintptr_t)(o))

void dvm_fatal(const char *fmt, ...) __attribute__((noreturn));

#endif

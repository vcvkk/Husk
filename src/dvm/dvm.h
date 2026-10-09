/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Dalvik VM of husk-port.
 *
 * Runs DEX bytecode in-process: classes are loaded from DEX files in order (the
 * boot library first, then the app), linked with vtables, initialised on first
 * use, and their methods run by an interpreter. Exceptions are Java's own: a
 * throw unwinds through try/catch tables and across calls. Memory is a
 * non-moving mark-sweep heap.
 *
 * What it does not do yet, and says so where it matters: more than one thread
 * (monitors are checked for null and otherwise do nothing), invoke-polymorphic
 * and invoke-custom (UnsupportedOperationException), JNI.
 */
#ifndef HUSK_DVM_H
#define HUSK_DVM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DvmVM DvmVM;

DvmVM *dvm_create(void);
void dvm_destroy(DvmVM *vm);

/* Add a .dex file, or an .apk/.jar's classes*.dex. Earlier files win a class name. */
bool dvm_add_dex_file(DvmVM *vm, const char *path, char *err, size_t errlen);
bool dvm_add_dex_memory(DvmVM *vm, const void *data, size_t size, char *err, size_t errlen);

/*
 * Run `static void main(String[])` of a class ("com.example.Main" or
 * "Lcom/example/Main;"). Returns 0, or 1 if an exception escaped (printed to
 * stderr the way a JVM prints it), or the status System.exit was given.
 */
int dvm_run_main(DvmVM *vm, const char *class_name, int argc, const char **argv);

/* Collect now. Statistics, for tests. */
void dvm_gc(DvmVM *vm);
size_t dvm_heap_objects(const DvmVM *vm);
size_t dvm_heap_bytes(const DvmVM *vm);

#ifdef __cplusplus
}
#endif

#endif /* HUSK_DVM_H */

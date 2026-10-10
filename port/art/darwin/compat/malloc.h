/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HUSK_COMPAT_MALLOC_H
#define HUSK_COMPAT_MALLOC_H
#include <malloc/malloc.h>
#include <stdlib.h>
static inline void* memalign(size_t alignment, size_t size) {
    void* p = NULL;
    return posix_memalign(&p, alignment < sizeof(void*) ? sizeof(void*) : alignment, size) == 0 ? p : NULL;
}
#define malloc_usable_size(p) malloc_size(p)
#endif

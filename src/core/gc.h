/*
* Copyright (c) 2024 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_GC_H
#define JANET_GC_H

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#endif

/* The metadata header associated with an allocated block of memory */
#define janet_gc_header(mem) ((JanetGCObject *)(mem))

#define JANET_MEM_TYPEBITS 0xFF
#define JANET_MEM_REACHABLE 0x100
#define JANET_MEM_DISABLED 0x200

#define janet_gc_settype(m, t) ((janet_gc_header(m)->flags |= (0xFF & (t))))
#define janet_gc_type(m) (janet_gc_header(m)->flags & 0xFF)

#define janet_gc_mark(m) (janet_gc_header(m)->flags |= JANET_MEM_REACHABLE)
#define janet_gc_reachable(m) (janet_gc_header(m)->flags & JANET_MEM_REACHABLE)

/* Memory types for the GC. Different from JanetType to include funcenv and funcdef. */
enum JanetMemoryType {
    JANET_MEMORY_NONE,
    JANET_MEMORY_STRING,
    JANET_MEMORY_SYMBOL,
    JANET_MEMORY_ARRAY,
    JANET_MEMORY_TUPLE,
    JANET_MEMORY_TABLE,
    JANET_MEMORY_STRUCT,
    JANET_MEMORY_FIBER,
    JANET_MEMORY_BUFFER,
    JANET_MEMORY_FUNCTION,
    JANET_MEMORY_ABSTRACT,
    JANET_MEMORY_FUNCENV,
    JANET_MEMORY_FUNCDEF,
    JANET_MEMORY_THREADED_ABSTRACT,
    JANET_MEMORY_TABLE_WEAKK,
    JANET_MEMORY_TABLE_WEAKV,
    JANET_MEMORY_TABLE_WEAKKV,
    JANET_MEMORY_ARRAY_WEAK
};

/* To allocate collectable memory, one must call janet_alloc, initialize the memory,
 * and then call when janet_enablegc when it is initialized and reachable by the gc (on the JANET stack) */
void *janet_gcalloc(enum JanetMemoryType type, size_t size);

#endif

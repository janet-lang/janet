/*
* Copyright (c) 2017 Calvin Rose
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

#ifndef DST_GC_H
#define DST_GC_H

#include <dst/dst.h>

/* The metadata header associated with an allocated block of memory */
#define dst_gc_header(mem) ((DstGCMemoryHeader *)(mem) - 1)

#define DST_MEM_TYPEBITS 0xFF
#define DST_MEM_REACHABLE 0x100
#define DST_MEM_DISABLED 0x200

#define dst_gc_settype(m, t) ((dst_gc_header(m)->flags |= (0xFF & (t))))
#define dst_gc_type(m) (dst_gc_header(m)->flags & 0xFF)

#define dst_gc_mark(m) (dst_gc_header(m)->flags |= DST_MEM_REACHABLE)
#define dst_gc_unmark(m) (dst_gc_header(m)->flags &= ~DST_MEM_COLOR)
#define dst_gc_reachable(m) (dst_gc_header(m)->flags & DST_MEM_REACHABLE)

// #define dst_gclock() (dst_vm_gc_suspend++)
// #define dst_gcunlock(lock) (dst_vm_gc_suspend = lock)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct DstGCMemoryHeader DstGCMemoryHeader;
struct DstGCMemoryHeader {
    DstGCMemoryHeader *next;
    uint32_t flags;
};

/* Memory types for the GC. Different from DstType to include funcenv and funcdef. */
enum DstMemoryType {
    DST_MEMORY_NONE,
    DST_MEMORY_STRING,
    DST_MEMORY_SYMBOL,
    DST_MEMORY_ARRAY,
    DST_MEMORY_TUPLE,
    DST_MEMORY_TABLE,
    DST_MEMORY_STRUCT,
    DST_MEMORY_FIBER,
    DST_MEMORY_BUFFER,
    DST_MEMORY_FUNCTION,
    DST_MEMORY_ABSTRACT,
    DST_MEMORY_FUNCENV,
    DST_MEMORY_FUNCDEF
};

/* To allocate collectable memory, one must calk dst_alloc, initialize the memory,
 * and then call when dst_enablegc when it is initailize and reachable by the gc (on the DST stack) */
void *dst_gcalloc(enum DstMemoryType type, size_t size);

#endif

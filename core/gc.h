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

#ifndef DST_GC_H_defined
#define DST_GC_H_defined

#include "internal.h"

/* The metadata header associated with an allocated block of memory */
#define gc_header(mem) ((GCMemoryHeader *)(mem) - 1)

#define DST_MEM_REACHABLE 0x100
#define DST_MEM_DISABLED 0x200

#define gc_settype(m, t) ((gc_header(m)->flags |= (0xFF & (t))))
#define gc_type(m) (gc_header(m)->flags & 0xFF)

#define gc_mark(m) (gc_header(m)->flags |= DST_MEM_REACHABLE)
#define gc_unmark(m) (gc_header(m)->flags &= ~DST_MEM_COLOR)
#define gc_reachable(m) (gc_header(m)->flags & DST_MEM_REACHABLE)


/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader *next;
    uint32_t flags;
};

/* Memory types for the GC. Different from DstType to include funcenv and funcdef. */
typedef enum DstMemoryType DstMemoryType;
enum DstMemoryType {
    DST_MEMORY_NONE,
    DST_MEMORY_STRING,
    DST_MEMORY_ARRAY,
    DST_MEMORY_TUPLE,
    DST_MEMORY_TABLE,
    DST_MEMORY_STRUCT,
    DST_MEMORY_THREAD,
    DST_MEMORY_BUFFER,
    DST_MEMORY_FUNCTION,
    DST_MEMORY_USERDATA,
    DST_MEMORY_FUNCENV,
    DST_MEMORY_FUNCDEF
}

/* Prevent GC from freeing some memory. */
#define dst_disablegc(m) (gc_header(m)->flags |= DST_MEM_DISABLED, (m))

/* To allocate collectable memory, one must calk dst_alloc, initialize the memory,
 * and then call when dst_enablegc when it is initailize and reachable by the gc (on the DST stack) */
void *dst_alloc(Dst *vm, DstMemoryType type, size_t size);
#define dst_enablegc(m) (gc_header(m)->flags &= ~DST_MEM_DISABLED, (m))

/* When doing C interop, it is often needed to disable GC on a value.
 * This is needed when a garbage collection could occur in the middle
 * of a c function. This could happen, for example, if one calls back
 * into dst inside of a c function. The pin and unpin functions toggle
 * garbage collection on a value when needed. Note that no dst functions
 * will call gc when you don't want it to. GC only happens automatically
 * in the interpreter loop. */
void dst_pin(DstValue x);
void dst_unpin(DstValue x);

/* Specific types can also be pinned and unpinned as well. */
#define dst_pin_table dst_disablegc
#define dst_pin_array dst_disablegc
#define dst_pin_buffer dst_disablegc
#define dst_pin_function dst_disablegc
#define dst_pin_thread dst_disablegc
#define dst_pin_string(s) dst_disablegc(dst_string_raw(s))
#define dst_pin_symbol(s) dst_disablegc(dst_string_raw(s))
#define dst_pin_tuple(s) dst_disablegc(dst_tuple_raw(s))
#define dst_pin_struct(s) dst_disablegc(dst_struct_raw(s))
#define dst_pin_userdata(s) dst_disablegc(dst_userdata_header(s))

#define dst_unpin_table dst_enablegc
#define dst_unpin_array dst_enablegc
#define dst_unpin_buffer dst_enablegc
#define dst_unpin_function dst_enablegc
#define dst_unpin_thread dst_enablegc
#define dst_unpin_string(s) dst_enablegc(dst_string_raw(s))
#define dst_unpin_symbol(s) dst_enablegc(dst_string_raw(s))
#define dst_unpin_tuple(s) dst_enablegc(dst_tuple_raw(s))
#define dst_unpin_struct(s) dst_enablegc(dst_struct_raw(s))
#define dst_unpin_userdata(s) dst_enablegc(dst_userdata_header(s))

void dst_mark(DstValue x);
void dst_sweep(Dst *vm);

/* Collect some memory */
void dst_collect(Dst *vm);

/* Clear all memory. */
void dst_clear_memory(Dst *vm);

/* Run garbage collection if needed */
#define dst_maybe_collect(Dst *vm) do {\
    if (vm->nextCollection >= vm->memoryInterval) dst_collect(vm); } while (0)

#endif

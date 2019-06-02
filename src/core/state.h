/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_STATE_H_defined
#define JANET_STATE_H_defined

#include <stdint.h>

/* The VM state. Rather than a struct that is passed
 * around, the vm state is global for simplicity. If
 * at some point a global state object, or context,
 * is required to be passed around, this is what would
 * be in it. However, thread local global variables for interpreter
 * state should allow easy multi-threading. */

/* How many VM stacks have been entered */
extern JANET_THREAD_LOCAL int janet_vm_stackn;

/* The current running fiber on the current thread.
 * Set and unset by janet_run. */
extern JANET_THREAD_LOCAL JanetFiber *janet_vm_fiber;

/* The current pointer to the inner most jmp_buf. The current
 * return point for panics. */
extern JANET_THREAD_LOCAL jmp_buf *janet_vm_jmp_buf;
extern JANET_THREAD_LOCAL Janet *janet_vm_return_reg;

/* The global registry for c functions. Used to store meta-data
 * along with otherwise bare c function pointers. */
extern JANET_THREAD_LOCAL JanetTable *janet_vm_registry;

/* Immutable value cache */
extern JANET_THREAD_LOCAL const uint8_t **janet_vm_cache;
extern JANET_THREAD_LOCAL uint32_t janet_vm_cache_capacity;
extern JANET_THREAD_LOCAL uint32_t janet_vm_cache_count;
extern JANET_THREAD_LOCAL uint32_t janet_vm_cache_deleted;

/* Garbage collection */
extern JANET_THREAD_LOCAL void *janet_vm_blocks;
extern JANET_THREAD_LOCAL uint32_t janet_vm_gc_interval;
extern JANET_THREAD_LOCAL uint32_t janet_vm_next_collection;
extern JANET_THREAD_LOCAL int janet_vm_gc_suspend;

/* GC roots */
extern JANET_THREAD_LOCAL Janet *janet_vm_roots;
extern JANET_THREAD_LOCAL uint32_t janet_vm_root_count;
extern JANET_THREAD_LOCAL uint32_t janet_vm_root_capacity;

/* Scratch memory */
extern JANET_THREAD_LOCAL void **janet_scratch_mem;
extern JANET_THREAD_LOCAL size_t janet_scratch_cap;
extern JANET_THREAD_LOCAL size_t janet_scratch_len;

#endif /* JANET_STATE_H_defined */

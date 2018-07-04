/*
* Copyright (c) 2018 Calvin Rose
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

#ifndef DST_STATE_H_defined
#define DST_STATE_H_defined

#include <stdint.h>

/* The VM state. Rather than a struct that is passed
 * around, the vm state is global for simplicity. If
 * at some point a a global state object, or context,
 * is required to be passed around, this is waht would
 * be in it. However, thread local globals for interpreter
 * state should allow easy multithreading. */

/* How many VM stacks have been entered */
extern DST_THREAD_LOCAL int dst_vm_stackn;

/* The current running fiber on the current thread.
 * Set and unset by dst_run. */
extern DST_THREAD_LOCAL DstFiber *dst_vm_fiber;

/* The global registry for c functions. Used to store metadata
 * along with otherwise bare c function pointers. */
extern DST_THREAD_LOCAL DstTable *dst_vm_registry;

/* Immutable value cache */
extern DST_THREAD_LOCAL const uint8_t **dst_vm_cache;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_capacity;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_count;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_deleted;

/* Garbage collection */
extern DST_THREAD_LOCAL void *dst_vm_blocks;
extern DST_THREAD_LOCAL uint32_t dst_vm_gc_interval;
extern DST_THREAD_LOCAL uint32_t dst_vm_next_collection;
extern DST_THREAD_LOCAL int dst_vm_gc_suspend;

/* GC roots */
extern DST_THREAD_LOCAL Dst *dst_vm_roots;
extern DST_THREAD_LOCAL uint32_t dst_vm_root_count;
extern DST_THREAD_LOCAL uint32_t dst_vm_root_capacity;

#endif /* DST_STATE_H_defined */

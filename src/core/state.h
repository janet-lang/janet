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

#ifndef JANET_STATE_H_defined
#define JANET_STATE_H_defined

#include <stdint.h>

/* The VM state. Rather than a struct that is passed
 * around, the vm state is global for simplicity. If
 * at some point a a global state object, or context,
 * is required to be passed around, this is waht would
 * be in it. However, thread local globals for interpreter
 * state should allow easy multithreading. */

/* How many VM stacks have been entered */
extern JANET_THREAD_LOCAL int janet_vm_stackn;

/* The current running fiber on the current thread.
 * Set and unset by janet_run. */
extern JANET_THREAD_LOCAL JanetFiber *janet_vm_fiber;

/* The global registry for c functions. Used to store metadata
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

extern JANET_THREAD_LOCAL Janet *janet_vm_gc_marklist;
extern JANET_THREAD_LOCAL size_t janet_vm_gc_marklist_count;
extern JANET_THREAD_LOCAL size_t janet_vm_gc_marklist_capacity;
extern JANET_THREAD_LOCAL size_t janet_vm_gc_marklist_rootcount;

#endif /* JANET_STATE_H_defined */

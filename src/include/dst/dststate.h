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

#ifndef DST_STATE_H_defined
#define DST_STATE_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "dstconfig.h"
#include "dsttypes.h"

/* Names of all of the types */
extern const char *dst_type_names[16];

/* The VM state. Rather than a struct that is passed
 * around, the vm state is global for simplicity. */

/* How many VM stacks have been entered */
extern DST_THREAD_LOCAL int dst_vm_stackn;

/* Garbage collection */
extern DST_THREAD_LOCAL void *dst_vm_blocks;
extern DST_THREAD_LOCAL uint32_t dst_vm_gc_interval;
extern DST_THREAD_LOCAL uint32_t dst_vm_next_collection;
extern DST_THREAD_LOCAL int dst_vm_gc_suspend;

/* Immutable value cache */
extern DST_THREAD_LOCAL const uint8_t **dst_vm_cache;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_capacity;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_count;
extern DST_THREAD_LOCAL uint32_t dst_vm_cache_deleted;

/* GC roots */
extern DST_THREAD_LOCAL Dst *dst_vm_roots;
extern DST_THREAD_LOCAL uint32_t dst_vm_root_count;
extern DST_THREAD_LOCAL uint32_t dst_vm_root_capacity;

#ifdef __cplusplus
}
#endif

#endif /* DST_STATE_H_defined */

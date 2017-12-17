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

#include <dst/dst.h>
#include "compile.h"

void dst_compile_slotpool_init(DstSlotPool *pool) {
    pool->s = NULL;
    pool->count = 0;
    pool->free = 0;
    pool->cap = 0;
}

void dst_compile_slotpool_deinit(DstSlotPool *pool) {
    free(pool->s);
    pool->s = NULL;
    pool->cap = 0;
    pool->count = 0;
    pool->free = 0;
}

void dst_compile_slotpool_extend(DstSlotPool *pool, int32_t extra) {
    int32_t i;
    int32_t newcount = pool->count + extra;
    if (newcount > pool->cap) {
        int32_t newcap = 2 * newcount;
        pool->s = realloc(pool->s, newcap * sizeof(DstSlot));
        if (NULL == pool->s) {
            DST_OUT_OF_MEMORY;
        }
        pool->cap = newcap;
    }
    /* Mark all new slots as free */
    for (i = pool->count; i < newcount; i++) {
        pool->s[i].flags = 0;
    }
    pool->count = newcount;
}

DstSlot *dst_compile_slotpool_alloc(DstSlotPool *pool) {
    int32_t oldcount = pool->count;
    int32_t newcount = oldcount == 0xF0 ? 0x101 : oldcount + 1;
    int32_t index = newcount - 1;
    while (pool->free < pool->count) {
        if (!(pool->s[pool->free].flags & DST_SLOT_NOTEMPTY)) {
            return pool->s + pool->free; 
        }
        pool->free++;
    }
    dst_compile_slotpool_extend(pool, newcount - oldcount);
    pool->s[index].flags = DST_SLOT_NOTEMPTY;
    pool->s[index].index = index;
    return pool->s + index;
}

void dst_compile_slotpool_freeindex(DstSlotPool *pool, int32_t index) {
    if (index > 0 && index < pool->count) {
        pool->s[index].flags = 0;
        if (index < pool->free)
            pool->free = index;
    }
}

void dst_compile_slotpool_free(DstSlotPool *pool, DstSlot *s) {
    DstSlot *oldfree = pool->s + pool->free;
    if (s >= pool->s && s < (pool->s + pool->count)) {
        if (s < oldfree) {
            pool->free = s - pool->s;
        }
        s->flags = 0;
    }
}


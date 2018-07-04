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

/* Implements a simple first fit register allocator for the compiler. */

#ifndef DST_REGALLOC_H
#define DST_REGALLOC_H

#include <stdint.h>

/* Placeholder for allocating temporary registers */
typedef enum {
    DSTC_REGTEMP_0,
    DSTC_REGTEMP_1,
    DSTC_REGTEMP_2,
    DSTC_REGTEMP_3,
    DSTC_REGTEMP_4,
    DSTC_REGTEMP_5,
    DSTC_REGTEMP_6,
    DSTC_REGTEMP_7
} DstcRegisterTemp;

typedef struct {
    uint32_t *chunks;
    int32_t count; /* number of chunks in chunks */
    int32_t capacity; /* amount allocated for chunks */
    int32_t max; /* The maximum allocated register so far */
    int32_t regtemps; /* Hold which tempregistered are alloced. */
} DstcRegisterAllocator;

void dstc_regalloc_init(DstcRegisterAllocator *ra);
void dstc_regalloc_deinit(DstcRegisterAllocator *ra);

int32_t dstc_regalloc_1(DstcRegisterAllocator *ra);
void dstc_regalloc_free(DstcRegisterAllocator *ra, int32_t reg);
int32_t dstc_regalloc_temp(DstcRegisterAllocator *ra, DstcRegisterTemp nth);
void dstc_regalloc_freetemp(DstcRegisterAllocator *ra, int32_t reg, DstcRegisterTemp nth);
void dstc_regalloc_clone(DstcRegisterAllocator *dest, DstcRegisterAllocator *src);
void dstc_regalloc_touch(DstcRegisterAllocator *ra, int32_t reg);

/* Mutli-slot allocation disabled */
/*
int32_t dstc_regalloc_n(DstcRegisterAllocator *ra, int32_t n);
int32_t dstc_regalloc_call(DstcRegisterAllocator *ra, int32_t callee, int32_t nargs);
void dstc_regalloc_freerange(DstcRegisterAllocator *ra, int32_t regstart, int32_t n);
*/

#endif

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
} DstcRegisterAllocator;

void dstc_regalloc_init(DstcRegisterAllocator *ra);
void dstc_regalloc_deinit(DstcRegisterAllocator *ra);

int32_t dstc_regalloc_1(DstcRegisterAllocator *ra);
void dstc_regalloc_free(DstcRegisterAllocator *ra, int32_t reg);
void dstc_regalloc_freerange(DstcRegisterAllocator *ra, int32_t regstart, int32_t n);
int32_t dstc_regalloc_temp(DstcRegisterAllocator *ra, DstcRegisterTemp nth);
int32_t dstc_regalloc_n(DstcRegisterAllocator *ra, int32_t n);
int32_t dstc_regalloc_call(DstcRegisterAllocator *ra, int32_t callee, int32_t nargs);
void dstc_regalloc_clone(DstcRegisterAllocator *dest, DstcRegisterAllocator *src);
void dstc_regalloc_touch(DstcRegisterAllocator *ra, int32_t reg);

/* Test code */
/*
#include <stdio.h>
static void printreg(DstcRegisterAllocator *ra) {
    printf("count=%d, cap=%d, max=%d\n", ra->count, ra->capacity, ra->max);
    for (int row = 0; row < ra->count; row++) {
        uint32_t chunk = ra->chunks[row];
        putc('[', stdout);
        for (int i = 0; i < 32; i++) {
            putc(
                (chunk & (1 << i))
                    ? '*'
                    : '.', stdout);
        }
        putc(']', stdout);
        putc('\n', stdout);
    }
    putc('\n', stdout);
}

static void runtest(void) {
    DstcRegisterAllocator ra, rb;
    dstc_regalloc_init(&ra);
    int32_t a = dstc_regalloc_1(&ra);
    int32_t b = dstc_regalloc_1(&ra);
    int32_t c = dstc_regalloc_1(&ra);
    int32_t d = dstc_regalloc_1(&ra);
    int32_t e = dstc_regalloc_1(&ra);
    printreg(&ra);
    dstc_regalloc_free(&ra, b);
    dstc_regalloc_free(&ra, d);
    printreg(&ra);
    int32_t x = dstc_regalloc_n(&ra, 32);
    printreg(&ra);
    dstc_regalloc_1(&ra);
    printreg(&ra);
    int32_t y = dstc_regalloc_n(&ra, 101);
    printreg(&ra);
    dstc_regalloc_clone(&rb, &ra);
    printreg(&rb);
    dstc_regalloc_deinit(&ra);
    dstc_regalloc_deinit(&rb);
}
*/

#endif

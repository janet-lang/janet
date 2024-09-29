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

/* Implements a simple first fit register allocator for the compiler. */

#ifndef JANET_REGALLOC_H
#define JANET_REGALLOC_H

#include <stdint.h>

/* Placeholder for allocating temporary registers */
typedef enum {
    JANETC_REGTEMP_0,
    JANETC_REGTEMP_1,
    JANETC_REGTEMP_2,
    JANETC_REGTEMP_3,
    JANETC_REGTEMP_4,
    JANETC_REGTEMP_5,
    JANETC_REGTEMP_6,
    JANETC_REGTEMP_7
} JanetcRegisterTemp;

typedef struct {
    uint32_t *chunks;
    int32_t count; /* number of chunks in chunks */
    int32_t capacity; /* amount allocated for chunks */
    int32_t max; /* The maximum allocated register so far */
    int32_t regtemps; /* Hold which temp. registers are allocated. */
} JanetcRegisterAllocator;

void janetc_regalloc_init(JanetcRegisterAllocator *ra);
void janetc_regalloc_deinit(JanetcRegisterAllocator *ra);

int32_t janetc_regalloc_1(JanetcRegisterAllocator *ra);
void janetc_regalloc_free(JanetcRegisterAllocator *ra, int32_t reg);
int32_t janetc_regalloc_temp(JanetcRegisterAllocator *ra, JanetcRegisterTemp nth);
void janetc_regalloc_freetemp(JanetcRegisterAllocator *ra, int32_t reg, JanetcRegisterTemp nth);
void janetc_regalloc_clone(JanetcRegisterAllocator *dest, JanetcRegisterAllocator *src);
void janetc_regalloc_touch(JanetcRegisterAllocator *ra, int32_t reg);
int janetc_regalloc_check(JanetcRegisterAllocator *ra, int32_t reg);

#endif

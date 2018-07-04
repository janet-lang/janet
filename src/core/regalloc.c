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

#include <dst/dst.h>
#include "regalloc.h"

void dstc_regalloc_init(DstcRegisterAllocator *ra) {
    ra->chunks = NULL;
    ra->count = 0;
    ra->capacity = 0;
    ra->max = 0;
    ra->regtemps = 0;
}

void dstc_regalloc_deinit(DstcRegisterAllocator *ra) {
    free(ra->chunks);
}

/* Fallbacks for when ctz not available */
#ifdef __GNUC__
#define count_trailing_zeros(x) __builtin_ctz(x)
#define count_trailing_ones(x) __builtin_ctz(~(x))
#else
static int32_t count_trailing_ones(uint32_t x) {
    int32_t ret = 0;
    while (x & 1) {
        ret++;
        x >>= 1;
    }
    return ret;
}
#define count_trailing_zeros(x) count_trailing_ones(~(x))
#endif

/* Get ith bit */
#define ithbit(I) ((uint32_t)1 << (I))

/* Get N bits */
#define nbits(N) (ithbit(N) - 1)

/* Copy a regsiter allocator */
void dstc_regalloc_clone(DstcRegisterAllocator *dest, DstcRegisterAllocator *src) {
    size_t size;
    dest->count = src->count;
    dest->capacity = src->capacity;
    dest->max = src->max;
    size = sizeof(uint32_t) * dest->capacity;
    dest->chunks = malloc(size);
    dest->regtemps = 0;
    if (!dest->chunks) {
        DST_OUT_OF_MEMORY;
    }
    memcpy(dest->chunks, src->chunks, size);
}

/* Allocate one more chunk in chunks */
static void pushchunk(DstcRegisterAllocator *ra) {
    /* Registers 240-255 are always allocated (reserved) */
    uint32_t chunk = ra->count == 7 ? 0xFFFF0000 : 0;
    int32_t newcount = ra->count + 1;
    if (newcount > ra->capacity) {
        int32_t newcapacity = newcount * 2;
        ra->chunks = realloc(ra->chunks, newcapacity * sizeof(uint32_t));
        if (!ra->chunks) {
            DST_OUT_OF_MEMORY;
        }
        ra->capacity = newcapacity;
    }
    ra->chunks[ra->count] = chunk;
    ra->count = newcount;
}

/* Reallocate a given register */
void dstc_regalloc_touch(DstcRegisterAllocator *ra, int32_t reg) {
    int32_t chunk = reg >> 5;
    int32_t bit = reg & 0x1F;
    while (chunk >= ra->count) pushchunk(ra);
    ra->chunks[chunk] |= ithbit(bit);
}

/* Allocate one register. */
int32_t dstc_regalloc_1(DstcRegisterAllocator *ra) {
    /* Get the nth bit in the array */
    int32_t bit, chunk, nchunks, reg;
    bit = -1;
    nchunks = ra->count;
    for (chunk = 0; chunk < nchunks; chunk++) {
        uint32_t block = ra->chunks[chunk];
        if (block == 0xFFFFFFFF) continue;
        bit = count_trailing_ones(block);
        break;
    }
    /* No reg found */
    if (bit == -1) {
        pushchunk(ra);
        bit = 0;
        chunk = nchunks;
    }
    /* set the bit at index bit in chunk */
    ra->chunks[chunk] |= ithbit(bit);
    reg = (chunk << 5) + bit;
    if (reg > ra->max)
        ra->max = reg;
    return reg;
}

/* Free a register. The register must have been previously allocated
 * without being freed. */
void dstc_regalloc_free(DstcRegisterAllocator *ra, int32_t reg) {
    int32_t chunk = reg >> 5;
    int32_t bit = reg & 0x1F;
    ra->chunks[chunk] &= ~ithbit(bit);
}

/* Get a register that will fit in 8 bits (< 256). Do not call this
 * twice with the same value of nth without calling dstc_regalloc_free
 * on the returned register before. */
int32_t dstc_regalloc_temp(DstcRegisterAllocator *ra, DstcRegisterTemp nth) {
    int32_t oldmax = ra->max;
    if (ra->regtemps & (1 << nth)) {
        dst_exit("regtemp already allocated");
    }
    ra->regtemps |= 1 << nth;
    int32_t reg = dstc_regalloc_1(ra);
    if (reg > 0xFF) {
        reg = 0xF0 + nth;
        ra->max = (reg > oldmax) ? reg : oldmax;
    }
    return reg;
}

void dstc_regalloc_freetemp(DstcRegisterAllocator *ra, int32_t reg, DstcRegisterTemp nth) {
    ra->regtemps &= ~(1 << nth);
    if (reg < 0xF0)
        dstc_regalloc_free(ra, reg);
}

/* Disable multi-slot allocation for now. */

/*
static int32_t checkrange(DstcRegisterAllocator *ra, int32_t start, int32_t end) {
    int32_t startchunk = start / 32;
    int32_t endchunk = end / 32;
    for (int32_t chunk = startchunk; chunk <= endchunk; chunk++) {
        while (ra->count <= chunk) pushchunk(ra);
        uint32_t mask = 0xFFFFFFFF;
        if (chunk == startchunk)
            mask &= ~nbits(start & 0x1F);
        if (chunk == endchunk)
            mask &= nbits(end & 0x1F);
        uint32_t block = ra->chunks[chunk];
        uint32_t masking = mask & block;
        if (masking) {
            int32_t nextbit = (block == 0xFFFFFFFF)
                ? 32
                : count_trailing_zeros(masking) + 1;
            return chunk * 32 + nextbit;
        }
    }
    return -1;
}

static void markrange(DstcRegisterAllocator *ra, int32_t start, int32_t end) {
    int32_t startchunk = start / 32;
    int32_t endchunk = end / 32;
    for (int32_t chunk = startchunk; chunk <= endchunk; chunk++) {
        uint32_t mask = 0xFFFFFFFF;
        if (chunk == startchunk)
            mask &= ~nbits(start & 0x1F);
        if (chunk == endchunk)
            mask &= nbits(end & 0x1F);
        ra->chunks[chunk] |= mask;
    }
}

void dstc_regalloc_freerange(DstcRegisterAllocator *ra, int32_t start, int32_t n) {
    int32_t end = start + n - 1;
    int32_t startchunk = start / 32;
    int32_t endchunk = end / 32;
    for (int32_t chunk = startchunk; chunk <= endchunk; chunk++) {
        uint32_t mask = 0;
        if (chunk == startchunk)
            mask |= nbits(start & 0x1F);
        if (chunk == endchunk)
            mask |= ~nbits(end & 0x1F);
        ra->chunks[chunk] &= mask;
    }
}

int32_t dstc_regalloc_n(DstcRegisterAllocator *ra, int32_t n) {
    int32_t start = 0, end = 0, next = 0;
    while (next >= 0) {
        start = next;
        end = start + n - 1;
        next = checkrange(ra, start, end);
    }
    markrange(ra, start, end);
    if (end > ra->max)
        ra->max = end;
    return start;
}

int32_t dstc_regalloc_call(DstcRegisterAllocator *ra, int32_t callee, int32_t nargs) {
    if (checkrange(ra, callee, callee + nargs) < 0) {
        markrange(ra, callee + 1, callee + nargs);
        return callee;
    }
    return dstc_regalloc_n(ra, nargs + 1);
}

*/

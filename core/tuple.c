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
#include "symcache.h"

/* Create a new empty tuple of the given size. This will return memory
 * which should be filled with DstValues. The memory will not be collected until
 * dst_tuple_end is called. */
DstValue *dst_tuple_begin(int32_t length) {
    char *data = dst_alloc(DST_MEMORY_TUPLE, 2 * sizeof(int32_t) + length * sizeof(DstValue));
    DstValue *tuple = (DstValue *)(data + (2 * sizeof(int32_t)));
    dst_tuple_length(tuple) = length;
    dst_tuple_hash(tuple) = 0;
    return tuple;
}

/* Finish building a tuple */
const DstValue *dst_tuple_end(DstValue *tuple) {
    return (const DstValue *)tuple;
}

/* Build a tuple with n values */
const DstValue *dst_tuple_n(DstValue *values, int32_t n) {
    DstValue *t = dst_tuple_begin(n);
    memcpy(t, values, sizeof(DstValue) * n);
    return dst_tuple_end(t);
}

/* Check if two tuples are equal */
int dst_tuple_equal(const DstValue *lhs, const DstValue *rhs) {
    int32_t index;
    int32_t llen = dst_tuple_length(lhs);
    int32_t rlen = dst_tuple_length(rhs);
    int32_t lhash = dst_tuple_hash(lhs);
    int32_t rhash = dst_tuple_hash(rhs);
    if (llen != rlen)
        return 0;
    if (lhash == 0)
        lhash = dst_tuple_hash(lhs) = dst_array_calchash(lhs, llen);
    if (rhash == 0)
        rhash = dst_tuple_hash(rhs) = dst_array_calchash(rhs, rlen);
    if (lhash != rhash)
        return 0;
    for (index = 0; index < llen; index++) {
        if (!dst_equals(lhs[index], rhs[index]))
            return 0;
    }
    return 1;
}

/* Compare tuples */
int dst_tuple_compare(const DstValue *lhs, const DstValue *rhs) {
    int32_t i;
    int32_t llen = dst_tuple_length(lhs);
    int32_t rlen = dst_tuple_length(rhs);
    int32_t count = llen < rlen ? llen : rlen;
    for (i = 0; i < count; ++i) {
        int comp = dst_compare(lhs[i], rhs[i]);
        if (comp != 0) return comp;
    }
    if (llen < rlen)
        return -1;
    else if (llen > rlen)
        return 1;
    return 0;
}

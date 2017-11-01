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

#include "internal.h"
#include "cache.h"
#include "wrap.h"

/* Create a new empty tuple of the given size. This will return memory
 * which should be filled with DstValues. The memory will not be collected until
 * dst_tuple_end is called. */
DstValue *dst_tuple_begin(Dst *vm, uint32_t length) {
    char *data = dst_alloc(vm, DST_MEMORY_TUPLE, 2 * sizeof(uint32_t) + length * sizeof(DstValue));
    DstValue *tuple = (DstValue *)(data + (2 * sizeof(uint32_t)));
    dst_tuple_length(tuple) = length;
    return tuple;
}

/* Finish building a tuple */
const DstValue *dst_tuple_end(Dst *vm, DstValue *tuple) {
    dst_tuple_hash(tuple) = dst_calchash_array(tuple, dst_tuple_length(tuple));
    return dst_cache_add(vm, dst_wrap_tuple((const DstValue *) tuple)).as.tuple;
}

const DstValue *dst_tuple_n(Dst *vm, DstValue *values, uint32_t n) {
    DstValue *t = dst_tuple_begin(vm, n);
    memcpy(t, values, sizeof(DstValue) * n);
    return dst_tuple_end(vm, t);
}

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
#include <dst/dstcorelib.h>
#include "symcache.h"
#include "gc.h"
#include "util.h"

/* Create a new empty tuple of the given size. This will return memory
 * which should be filled with Dsts. The memory will not be collected until
 * dst_tuple_end is called. */
Dst *dst_tuple_begin(int32_t length) {
    char *data = dst_gcalloc(DST_MEMORY_TUPLE, 4 * sizeof(int32_t) + length * sizeof(Dst));
    Dst *tuple = (Dst *)(data + (4 * sizeof(int32_t)));
    dst_tuple_length(tuple) = length;
    dst_tuple_sm_line(tuple) = 0;
    dst_tuple_sm_col(tuple) = 0;
    return tuple;
}

/* Finish building a tuple */
const Dst *dst_tuple_end(Dst *tuple) {
    dst_tuple_hash(tuple) = dst_array_calchash(tuple, dst_tuple_length(tuple));
    return (const Dst *)tuple;
}

/* Build a tuple with n values */
const Dst *dst_tuple_n(Dst *values, int32_t n) {
    Dst *t = dst_tuple_begin(n);
    memcpy(t, values, sizeof(Dst) * n);
    return dst_tuple_end(t);
}

/* Check if two tuples are equal */
int dst_tuple_equal(const Dst *lhs, const Dst *rhs) {
    int32_t index;
    int32_t llen = dst_tuple_length(lhs);
    int32_t rlen = dst_tuple_length(rhs);
    int32_t lhash = dst_tuple_hash(lhs);
    int32_t rhash = dst_tuple_hash(rhs);
    if (lhash == 0)
        lhash = dst_tuple_hash(lhs) = dst_array_calchash(lhs, llen);
    if (rhash == 0)
        rhash = dst_tuple_hash(rhs) = dst_array_calchash(rhs, rlen);
    if (lhash != rhash)
        return 0;
    if (llen != rlen)
        return 0;
    for (index = 0; index < llen; index++) {
        if (!dst_equals(lhs[index], rhs[index]))
            return 0;
    }
    return 1;
}

/* Compare tuples */
int dst_tuple_compare(const Dst *lhs, const Dst *rhs) {
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

/* C Functions */

static int cfun_slice(DstArgs args) {
    const Dst *vals;
    int32_t len;
    Dst *ret;
    int32_t start, end;
    DST_MINARITY(args, 1);
    if (!dst_seq_view(args.v[0], &vals, &len)) DST_THROW(args, "expected array/tuple");
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (dst_checktype(args.v[1], DST_INTEGER)) {
        start = dst_unwrap_integer(args.v[1]);
    } else {
        DST_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (dst_checktype(args.v[2], DST_INTEGER)) {
        end = dst_unwrap_integer(args.v[2]);
    } else {
        DST_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        int32_t i, j;
        ret = dst_tuple_begin(end - start);
        for (j = 0, i = start; i < end; j++, i++) {
            ret[j] = vals[i];
        }
    } else {
        ret = dst_tuple_begin(0);
    }
    DST_RETURN_TUPLE(args, dst_tuple_end(ret));
}

static int cfun_prepend(DstArgs args) {
    const Dst *t;
    int32_t len;
    Dst *n;
    DST_FIXARITY(args, 2);
    if (!dst_seq_view(args.v[0], &t, &len)) DST_THROW(args, "expected tuple/array");
    n = dst_tuple_begin(len + 1);
    memcpy(n + 1, t, sizeof(Dst) * len);
    n[0] = args.v[1];
    DST_RETURN_TUPLE(args, dst_tuple_end(n));
}

static int cfun_append(DstArgs args) {
    const Dst *t;
    int32_t len;
    Dst *n;
    DST_FIXARITY(args, 2);
    if (!dst_seq_view(args.v[0], &t, &len)) DST_THROW(args, "expected tuple/array");
    n = dst_tuple_begin(len + 1);
    memcpy(n, t, sizeof(Dst) * len);
    n[len] = args.v[1];
    DST_RETURN_TUPLE(args, dst_tuple_end(n));
}

/* Load the tuple module */
int dst_lib_tuple(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_def(env, "tuple.slice", dst_wrap_cfunction(cfun_slice));
    dst_env_def(env, "tuple.append", dst_wrap_cfunction(cfun_append));
    dst_env_def(env, "tuple.prepend", dst_wrap_cfunction(cfun_prepend));
    return 0;
}

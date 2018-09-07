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

#include <janet/janet.h>
#include "symcache.h"
#include "gc.h"
#include "util.h"

/* Create a new empty tuple of the given size. This will return memory
 * which should be filled with Janets. The memory will not be collected until
 * janet_tuple_end is called. */
Janet *janet_tuple_begin(int32_t length) {
    char *data = janet_gcalloc(JANET_MEMORY_TUPLE, 4 * sizeof(int32_t) + length * sizeof(Janet));
    Janet *tuple = (Janet *)(data + (4 * sizeof(int32_t)));
    janet_tuple_length(tuple) = length;
    janet_tuple_sm_line(tuple) = 0;
    janet_tuple_sm_col(tuple) = 0;
    return tuple;
}

/* Finish building a tuple */
const Janet *janet_tuple_end(Janet *tuple) {
    janet_tuple_hash(tuple) = janet_array_calchash(tuple, janet_tuple_length(tuple));
    return (const Janet *)tuple;
}

/* Build a tuple with n values */
const Janet *janet_tuple_n(const Janet *values, int32_t n) {
    Janet *t = janet_tuple_begin(n);
    memcpy(t, values, sizeof(Janet) * n);
    return janet_tuple_end(t);
}

/* Check if two tuples are equal */
int janet_tuple_equal(const Janet *lhs, const Janet *rhs) {
    int32_t index;
    int32_t llen = janet_tuple_length(lhs);
    int32_t rlen = janet_tuple_length(rhs);
    int32_t lhash = janet_tuple_hash(lhs);
    int32_t rhash = janet_tuple_hash(rhs);
    if (lhash == 0)
        lhash = janet_tuple_hash(lhs) = janet_array_calchash(lhs, llen);
    if (rhash == 0)
        rhash = janet_tuple_hash(rhs) = janet_array_calchash(rhs, rlen);
    if (lhash != rhash)
        return 0;
    if (llen != rlen)
        return 0;
    for (index = 0; index < llen; index++) {
        if (!janet_equals(lhs[index], rhs[index]))
            return 0;
    }
    return 1;
}

/* Compare tuples */
int janet_tuple_compare(const Janet *lhs, const Janet *rhs) {
    int32_t i;
    int32_t llen = janet_tuple_length(lhs);
    int32_t rlen = janet_tuple_length(rhs);
    int32_t count = llen < rlen ? llen : rlen;
    for (i = 0; i < count; ++i) {
        int comp = janet_compare(lhs[i], rhs[i]);
        if (comp != 0) return comp;
    }
    if (llen < rlen)
        return -1;
    else if (llen > rlen)
        return 1;
    return 0;
}

/* C Functions */

static int cfun_slice(JanetArgs args) {
    const Janet *vals;
    int32_t len;
    Janet *ret;
    int32_t start, end;
    JANET_MINARITY(args, 1);
    if (!janet_indexed_view(args.v[0], &vals, &len)) JANET_THROW(args, "expected array/tuple");
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (janet_checktype(args.v[1], JANET_INTEGER)) {
        start = janet_unwrap_integer(args.v[1]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (janet_checktype(args.v[2], JANET_INTEGER)) {
        end = janet_unwrap_integer(args.v[2]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = janet_tuple_begin(end - start);
        memcpy(ret, vals + start, sizeof(Janet) * (end - start));
    } else {
        ret = janet_tuple_begin(0);
    }
    JANET_RETURN_TUPLE(args, janet_tuple_end(ret));
}

static int cfun_prepend(JanetArgs args) {
    const Janet *t;
    int32_t len;
    Janet *n;
    JANET_FIXARITY(args, 2);
    if (!janet_indexed_view(args.v[0], &t, &len)) JANET_THROW(args, "expected tuple/array");
    n = janet_tuple_begin(len + 1);
    memcpy(n + 1, t, sizeof(Janet) * len);
    n[0] = args.v[1];
    JANET_RETURN_TUPLE(args, janet_tuple_end(n));
}

static int cfun_append(JanetArgs args) {
    const Janet *t;
    int32_t len;
    Janet *n;
    JANET_FIXARITY(args, 2);
    if (!janet_indexed_view(args.v[0], &t, &len)) JANET_THROW(args, "expected tuple/array");
    n = janet_tuple_begin(len + 1);
    memcpy(n, t, sizeof(Janet) * len);
    n[len] = args.v[1];
    JANET_RETURN_TUPLE(args, janet_tuple_end(n));
}

static const JanetReg cfuns[] = {
    {"tuple.slice", cfun_slice},
    {"tuple.append", cfun_append},
    {"tuple.prepend", cfun_prepend},
    {NULL, NULL}
};

/* Load the tuple module */
int janet_lib_tuple(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}

/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include <janet.h>
#include "symcache.h"
#include "gc.h"
#include "util.h"
#endif

/* Create a new empty tuple of the given size. This will return memory
 * which should be filled with Janets. The memory will not be collected until
 * janet_tuple_end is called. */
Janet *janet_tuple_begin(int32_t length) {
    size_t size = sizeof(JanetTupleHead) + (length * sizeof(Janet));
    JanetTupleHead *head = janet_gcalloc(JANET_MEMORY_TUPLE, size);
    head->sm_line = -1;
    head->sm_column = -1;
    head->length = length;
    return (Janet *)(head->data);
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

static Janet cfun_tuple_brackets(int32_t argc, Janet *argv) {
    const Janet *tup = janet_tuple_n(argv, argc);
    janet_tuple_flag(tup) |= JANET_TUPLE_FLAG_BRACKETCTOR;
    return janet_wrap_tuple(tup);
}

static Janet cfun_tuple_slice(int32_t argc, Janet *argv) {
    JanetView view = janet_getindexed(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    return janet_wrap_tuple(janet_tuple_n(view.items + range.start, range.end - range.start));
}

static Janet cfun_tuple_type(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const Janet *tup = janet_gettuple(argv, 0);
    if (janet_tuple_flag(tup) & JANET_TUPLE_FLAG_BRACKETCTOR) {
        return janet_ckeywordv("brackets");
    } else {
        return janet_ckeywordv("parens");
    }
}

static Janet cfun_tuple_sourcemap(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const Janet *tup = janet_gettuple(argv, 0);
    Janet contents[2];
    contents[0] = janet_wrap_integer(janet_tuple_head(tup)->sm_line);
    contents[1] = janet_wrap_integer(janet_tuple_head(tup)->sm_column);
    return janet_wrap_tuple(janet_tuple_n(contents, 2));
}

static Janet cfun_tuple_setmap(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    const Janet *tup = janet_gettuple(argv, 0);
    janet_tuple_head(tup)->sm_line = janet_getinteger(argv, 1);
    janet_tuple_head(tup)->sm_column = janet_getinteger(argv, 2);
    return argv[0];
}

static const JanetReg tuple_cfuns[] = {
    {
        "tuple/brackets", cfun_tuple_brackets,
        JDOC("(tuple/brackets & xs)\n\n"
             "Creates a new bracketed tuple containing the elements xs.")
    },
    {
        "tuple/slice", cfun_tuple_slice,
        JDOC("(tuple/slice arrtup [,start=0 [,end=(length arrtup)]])\n\n"
             "Take a sub sequence of an array or tuple from index start "
             "inclusive to index end exclusive. If start or end are not provided, "
             "they default to 0 and the length of arrtup respectively. "
             "'start' and 'end' can also be negative to indicate indexing "
             "from the end of the input. Note that index -1 is synonymous with "
             "index '(length arrtup)' to allow a full negative slice range. "
             "Returns the new tuple.")
    },
    {
        "tuple/type", cfun_tuple_type,
        JDOC("(tuple/type tup)\n\n"
             "Checks how the tuple was constructed. Will return the keyword "
             ":brackets if the tuple was parsed with brackets, and :parens "
             "otherwise. The two types of tuples will behave the same most of "
             "the time, but will print differently and be treated differently by "
             "the compiler.")
    },
    {
        "tuple/sourcemap", cfun_tuple_sourcemap,
        JDOC("(tuple/sourcemap tup)\n\n"
             "Returns the sourcemap metadata attached to a tuple, "
             " which is another tuple (line, column).")
    },
    {
        "tuple/setmap", cfun_tuple_setmap,
        JDOC("(tuple/setmap tup line column)\n\n"
             "Set the sourcemap metadata on a tuple. line and column indicate "
             "should be integers.")
    },
    {NULL, NULL, NULL}
};

/* Load the tuple module */
void janet_lib_tuple(JanetTable *env) {
    janet_core_cfuns(env, NULL, tuple_cfuns);
}

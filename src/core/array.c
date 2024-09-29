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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

#include <string.h>

static void janet_array_impl(JanetArray *array, int32_t capacity) {
    Janet *data = NULL;
    if (capacity > 0) {
        janet_vm.next_collection += capacity * sizeof(Janet);
        data = (Janet *) janet_malloc(sizeof(Janet) * (size_t) capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
        }
    }
    array->count = 0;
    array->capacity = capacity;
    array->data = data;
}

/* Creates a new array */
JanetArray *janet_array(int32_t capacity) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    janet_array_impl(array, capacity);
    return array;
}

/* Creates a new array with weak references */
JanetArray *janet_array_weak(int32_t capacity) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY_WEAK, sizeof(JanetArray));
    janet_array_impl(array, capacity);
    return array;
}

/* Creates a new array from n elements. */
JanetArray *janet_array_n(const Janet *elements, int32_t n) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    array->capacity = n;
    array->count = n;
    array->data = janet_malloc(sizeof(Janet) * (size_t) n);
    if (!array->data) {
        JANET_OUT_OF_MEMORY;
    }
    safe_memcpy(array->data, elements, sizeof(Janet) * n);
    return array;
}

/* Ensure the array has enough capacity for elements */
void janet_array_ensure(JanetArray *array, int32_t capacity, int32_t growth) {
    Janet *newData;
    Janet *old = array->data;
    if (capacity <= array->capacity) return;
    int64_t new_capacity = ((int64_t) capacity) * growth;
    if (new_capacity > INT32_MAX) new_capacity = INT32_MAX;
    capacity = (int32_t) new_capacity;
    newData = janet_realloc(old, capacity * sizeof(Janet));
    if (NULL == newData) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm.next_collection += (capacity - array->capacity) * sizeof(Janet);
    array->data = newData;
    array->capacity = capacity;
}

/* Set the count of an array. Extend with nil if needed. */
void janet_array_setcount(JanetArray *array, int32_t count) {
    if (count < 0)
        return;
    if (count > array->count) {
        int32_t i;
        janet_array_ensure(array, count, 1);
        for (i = array->count; i < count; i++) {
            array->data[i] = janet_wrap_nil();
        }
    }
    array->count = count;
}

/* Push a value to the top of the array */
void janet_array_push(JanetArray *array, Janet x) {
    if (array->count == INT32_MAX) {
        janet_panic("array overflow");
    }
    int32_t newcount = array->count + 1;
    janet_array_ensure(array, newcount, 2);
    array->data[array->count] = x;
    array->count = newcount;
}

/* Pop a value from the top of the array */
Janet janet_array_pop(JanetArray *array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        return janet_wrap_nil();
    }
}

/* Look at the last value in the array */
Janet janet_array_peek(JanetArray *array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        return janet_wrap_nil();
    }
}

/* C Functions */

JANET_CORE_FN(cfun_array_new,
              "(array/new capacity)",
              "Creates a new empty array with a pre-allocated capacity. The same as "
              "`(array)` but can be more efficient if the maximum size of an array is known.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    JanetArray *array = janet_array(cap);
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_weak,
              "(array/weak capacity)",
              "Creates a new empty array with a pre-allocated capacity and support for weak references. Similar to `array/new`.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    JanetArray *array = janet_array_weak(cap);
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_new_filled,
              "(array/new-filled count &opt value)",
              "Creates a new array of `count` elements, all set to `value`, which defaults to nil. Returns the new array.") {
    janet_arity(argc, 1, 2);
    int32_t count = janet_getnat(argv, 0);
    Janet x = (argc == 2) ? argv[1] : janet_wrap_nil();
    JanetArray *array = janet_array(count);
    for (int32_t i = 0; i < count; i++) {
        array->data[i] = x;
    }
    array->count = count;
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_fill,
              "(array/fill arr &opt value)",
              "Replace all elements of an array with `value` (defaulting to nil) without changing the length of the array. "
              "Returns the modified array.") {
    janet_arity(argc, 1, 2);
    JanetArray *array = janet_getarray(argv, 0);
    Janet x = (argc == 2) ? argv[1] : janet_wrap_nil();
    for (int32_t i = 0; i < array->count; i++) {
        array->data[i] = x;
    }
    return argv[0];
}

JANET_CORE_FN(cfun_array_pop,
              "(array/pop arr)",
              "Remove the last element of the array and return it. If the array is empty, will return nil. Modifies "
              "the input array.") {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    return janet_array_pop(array);
}

JANET_CORE_FN(cfun_array_peek,
              "(array/peek arr)",
              "Returns the last element of the array. Does not modify the array.") {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    return janet_array_peek(array);
}

JANET_CORE_FN(cfun_array_push,
              "(array/push arr & xs)",
              "Push all the elements of xs to the end of an array. Modifies the input array and returns it.") {
    janet_arity(argc, 1, -1);
    JanetArray *array = janet_getarray(argv, 0);
    if (INT32_MAX - argc + 1 <= array->count) {
        janet_panic("array overflow");
    }
    int32_t newcount = array->count - 1 + argc;
    janet_array_ensure(array, newcount, 2);
    if (argc > 1) memcpy(array->data + array->count, argv + 1, (size_t)(argc - 1) * sizeof(Janet));
    array->count = newcount;
    return argv[0];
}

JANET_CORE_FN(cfun_array_ensure,
              "(array/ensure arr capacity growth)",
              "Ensures that the memory backing the array is large enough for `capacity` "
              "items at the given rate of growth. `capacity` and `growth` must be integers. "
              "If the backing capacity is already enough, then this function does nothing. "
              "Otherwise, the backing memory will be reallocated so that there is enough space.") {
    janet_fixarity(argc, 3);
    JanetArray *array = janet_getarray(argv, 0);
    int32_t newcount = janet_getinteger(argv, 1);
    int32_t growth = janet_getinteger(argv, 2);
    if (newcount < 1) janet_panic("expected positive integer");
    janet_array_ensure(array, newcount, growth);
    return argv[0];
}

JANET_CORE_FN(cfun_array_slice,
              "(array/slice arrtup &opt start end)",
              "Takes a slice of array or tuple from `start` to `end`. The range is half open, "
              "[start, end). Indexes can also be negative, indicating indexing from the "
              "end of the array. By default, `start` is 0 and `end` is the length of the array. "
              "Note that if the range is negative, it is taken as (start, end] to allow a full "
              "negative slice range. Returns a new array.") {
    JanetView view = janet_getindexed(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    JanetArray *array = janet_array(range.end - range.start);
    if (array->data)
        memcpy(array->data, view.items + range.start, sizeof(Janet) * (range.end - range.start));
    array->count = range.end - range.start;
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_concat,
              "(array/concat arr & parts)",
              "Concatenates a variable number of arrays (and tuples) into the first argument, "
              "which must be an array. If any of the parts are arrays or tuples, their elements will "
              "be inserted into the array. Otherwise, each part in `parts` will be appended to `arr` in order. "
              "Return the modified array `arr`.") {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetArray *array = janet_getarray(argv, 0);
    for (i = 1; i < argc; i++) {
        switch (janet_type(argv[i])) {
            default:
                janet_array_push(array, argv[i]);
                break;
            case JANET_ARRAY:
            case JANET_TUPLE: {
                int32_t j, len = 0;
                const Janet *vals = NULL;
                janet_indexed_view(argv[i], &vals, &len);
                if (array->data == vals) {
                    int32_t newcount = array->count + len;
                    janet_array_ensure(array, newcount, 2);
                    janet_indexed_view(argv[i], &vals, &len);
                }
                for (j = 0; j < len; j++)
                    janet_array_push(array, vals[j]);
            }
            break;
        }
    }
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_join,
              "(array/join arr & parts)",
              "Join a variable number of arrays and tuples into the first argument, "
              "which must be an array. "
              "Return the modified array `arr`.") {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetArray *array = janet_getarray(argv, 0);
    for (i = 1; i < argc; i++) {
        int32_t j, len = 0;
        const Janet *vals = NULL;
        if (!janet_indexed_view(argv[i], &vals, &len)) {
            janet_panicf("expected indexed type for argument %d, got %v", i, argv[i]);
        }
        if (array->data == vals) {
            int32_t newcount = array->count + len;
            janet_array_ensure(array, newcount, 2);
            janet_indexed_view(argv[i], &vals, &len);
        }
        for (j = 0; j < len; j++)
            janet_array_push(array, vals[j]);
    }
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_array_insert,
              "(array/insert arr at & xs)",
              "Insert all `xs` into array `arr` at index `at`. `at` should be an integer between "
              "0 and the length of the array. A negative value for `at` will index backwards from "
              "the end of the array, inserting after the index such that inserting at -1 appends to "
              "the array. Returns the array.") {
    size_t chunksize, restsize;
    janet_arity(argc, 2, -1);
    JanetArray *array = janet_getarray(argv, 0);
    int32_t at = janet_getinteger(argv, 1);
    if (at < 0) {
        at = array->count + at + 1;
    }
    if (at < 0 || at > array->count)
        janet_panicf("insertion index %d out of range [0,%d]", at, array->count);
    chunksize = (argc - 2) * sizeof(Janet);
    restsize = (array->count - at) * sizeof(Janet);
    if (INT32_MAX - (argc - 2) < array->count) {
        janet_panic("array overflow");
    }
    janet_array_ensure(array, array->count + argc - 2, 2);
    if (restsize) {
        memmove(array->data + at + argc - 2,
                array->data + at,
                restsize);
    }
    safe_memcpy(array->data + at, argv + 2, chunksize);
    array->count += (argc - 2);
    return argv[0];
}

JANET_CORE_FN(cfun_array_remove,
              "(array/remove arr at &opt n)",
              "Remove up to `n` elements starting at index `at` in array `arr`. `at` can index from "
              "the end of the array with a negative index, and `n` must be a non-negative integer. "
              "By default, `n` is 1. "
              "Returns the array.") {
    janet_arity(argc, 2, 3);
    JanetArray *array = janet_getarray(argv, 0);
    int32_t at = janet_getinteger(argv, 1);
    int32_t n = 1;
    if (at < 0) {
        at = array->count + at;
    }
    if (at < 0 || at > array->count)
        janet_panicf("removal index %d out of range [0,%d]", at, array->count);
    if (argc == 3) {
        n = janet_getinteger(argv, 2);
        if (n < 0)
            janet_panicf("expected non-negative integer for argument n, got %v", argv[2]);
    }
    if (at + n > array->count) {
        n = array->count - at;
    }
    memmove(array->data + at,
            array->data + at + n,
            (array->count - at - n) * sizeof(Janet));
    array->count -= n;
    return argv[0];
}

JANET_CORE_FN(cfun_array_trim,
              "(array/trim arr)",
              "Set the backing capacity of an array to its current length. Returns the modified array.") {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    if (array->count) {
        if (array->count < array->capacity) {
            Janet *newData = janet_realloc(array->data, array->count * sizeof(Janet));
            if (NULL == newData) {
                JANET_OUT_OF_MEMORY;
            }
            array->data = newData;
            array->capacity = array->count;
        }
    } else {
        array->capacity = 0;
        janet_free(array->data);
        array->data = NULL;
    }
    return argv[0];
}

JANET_CORE_FN(cfun_array_clear,
              "(array/clear arr)",
              "Empties an array, setting it's count to 0 but does not free the backing capacity. "
              "Returns the modified array.") {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    array->count = 0;
    return argv[0];
}

/* Load the array module */
void janet_lib_array(JanetTable *env) {
    JanetRegExt array_cfuns[] = {
        JANET_CORE_REG("array/new", cfun_array_new),
        JANET_CORE_REG("array/weak", cfun_array_weak),
        JANET_CORE_REG("array/new-filled", cfun_array_new_filled),
        JANET_CORE_REG("array/fill", cfun_array_fill),
        JANET_CORE_REG("array/pop", cfun_array_pop),
        JANET_CORE_REG("array/peek", cfun_array_peek),
        JANET_CORE_REG("array/push", cfun_array_push),
        JANET_CORE_REG("array/ensure", cfun_array_ensure),
        JANET_CORE_REG("array/slice", cfun_array_slice),
        JANET_CORE_REG("array/concat", cfun_array_concat),
        JANET_CORE_REG("array/insert", cfun_array_insert),
        JANET_CORE_REG("array/remove", cfun_array_remove),
        JANET_CORE_REG("array/trim", cfun_array_trim),
        JANET_CORE_REG("array/clear", cfun_array_clear),
        JANET_CORE_REG("array/join", cfun_array_join),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, array_cfuns);
}

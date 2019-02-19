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
#include "gc.h"
#include "util.h"
#endif

#include <string.h>

/* Initializes an array */
JanetArray *janet_array_init(JanetArray *array, int32_t capacity) {
    Janet *data = NULL;
    if (capacity > 0) {
        data = (Janet *) malloc(sizeof(Janet) * capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
        }
    }
    array->count = 0;
    array->capacity = capacity;
    array->data = data;
    return array;
}

void janet_array_deinit(JanetArray *array) {
    free(array->data);
}

/* Creates a new array */
JanetArray *janet_array(int32_t capacity) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    return janet_array_init(array, capacity);
}

/* Creates a new array from n elements. */
JanetArray *janet_array_n(const Janet *elements, int32_t n) {
    JanetArray *array = janet_gcalloc(JANET_MEMORY_ARRAY, sizeof(JanetArray));
    array->capacity = n;
    array->count = n;
    array->data = malloc(sizeof(Janet) * n);
    if (!array->data) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(array->data, elements, sizeof(Janet) * n);
    return array;
}

/* Ensure the array has enough capacity for elements */
void janet_array_ensure(JanetArray *array, int32_t capacity, int32_t growth) {
    Janet *newData;
    Janet *old = array->data;
    if (capacity <= array->capacity) return;
    capacity *= growth;
    newData = realloc(old, capacity * sizeof(Janet));
    if (NULL == newData) {
        JANET_OUT_OF_MEMORY;
    }
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

static Janet cfun_array_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    JanetArray *array = janet_array(cap);
    return janet_wrap_array(array);
}

static Janet cfun_array_pop(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    return janet_array_pop(array);
}

static Janet cfun_array_peek(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetArray *array = janet_getarray(argv, 0);
    return janet_array_peek(array);
}

static Janet cfun_array_push(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    JanetArray *array = janet_getarray(argv, 0);
    int32_t newcount = array->count - 1 + argc;
    janet_array_ensure(array, newcount, 2);
    if (argc > 1) memcpy(array->data + array->count, argv + 1, (argc - 1) * sizeof(Janet));
    array->count = newcount;
    return argv[0];
}

static Janet cfun_array_ensure(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    JanetArray *array = janet_getarray(argv, 0);
    int32_t newcount = janet_getinteger(argv, 1);
    int32_t growth = janet_getinteger(argv, 2);
    if (newcount < 1) janet_panic("expected positive integer");
    janet_array_ensure(array, newcount, growth);
    return argv[0];
}

static Janet cfun_array_slice(int32_t argc, Janet *argv) {
    JanetRange range = janet_getslice(argc, argv);
    JanetView view = janet_getindexed(argv, 0);
    JanetArray *array = janet_array(range.end - range.start);
    memcpy(array->data, view.items + range.start, sizeof(Janet) * (range.end - range.start));
    array->count = range.end - range.start;
    return janet_wrap_array(array);
}

static Janet cfun_array_concat(int32_t argc, Janet *argv) {
    int32_t i;
    janet_arity(argc, 1, -1);
    JanetArray *array = janet_getarray(argv, 0);
    for (i = 1; i < argc; i++) {
        switch (janet_type(argv[i])) {
            default:
                janet_array_push(array, argv[i]);
                break;
            case JANET_ARRAY:
            case JANET_TUPLE:
                {
                    int32_t j, len;
                    const Janet *vals;
                    janet_indexed_view(argv[i], &vals, &len);
                    for (j = 0; j < len; j++)
                        janet_array_push(array, vals[j]);
                }
                break;
        }
    }
    return janet_wrap_array(array);
}

static Janet cfun_array_insert(int32_t argc, Janet *argv) {
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
    janet_array_ensure(array, array->count + argc - 2, 2);
    memmove(array->data + at + argc - 2,
            array->data + at,
            restsize);
    memcpy(array->data + at, argv + 2, chunksize);
    array->count += (argc - 2);
    return janet_wrap_array(array);
}

static const JanetReg array_cfuns[] = {
    {"array/new", cfun_array_new,
        JDOC("(array/new capacity)\n\n"
                "Creates a new empty array with a pre-allocated capacity. The same as "
                "(array) but can be more efficient if the maximum size of an array is known.")
    },
    {"array/pop", cfun_array_pop,
        JDOC("(array/pop arr)\n\n"
                "Remove the last element of the array and return it. If the array is empty, will return nil. Modifies "
                "the input array.")
    },
    {"array/peek", cfun_array_peek,
        JDOC("(array/peek arr)\n\n"
                "Returns the last element of the array. Does not modify the array.")
    },
    {"array/push", cfun_array_push,
        JDOC("(array/push arr x)\n\n"
                "Insert an element in the end of an array. Modifies the input array and returns it.")
    },
    {"array/ensure", cfun_array_ensure,
        JDOC("(array/ensure arr capacity)\n\n"
                "Ensures that the memory backing the array has enough memory for capacity "
                "items. Capacity must be an integer. If the backing capacity is already enough, "
                "then this function does nothing. Otherwise, the backing memory will be reallocated "
                "so that there is enough space.")
    },
    {"array/slice", cfun_array_slice,
        JDOC("(array/slice arrtup [, start=0 [, end=(length arrtup)]])\n\n"
                "Takes a slice of array or tuple from start to end. The range is half open, "
                "[start, end). Indexes can also be negative, indicating indexing from the end of the "
                "end of the array. By default, start is 0 and end is the length of the array. "
                "Returns a new array.")
    },
    {"array/concat", cfun_array_concat,
        JDOC("(array/concat arr & parts)\n\n"
                "Concatenates a variadic number of arrays (and tuples) into the first argument "
                "which must an array. If any of the parts are arrays or tuples, their elements will "
                "be inserted into the array. Otherwise, each part in parts will be appended to arr in order. "
                "Return the modified array arr.")
    },
    {"array/insert", cfun_array_insert,
        JDOC("(array/insert arr at & xs)\n\n"
                "Insert all of xs into array arr at index at. at should be an integer "
                "0 and the length of the array. A negative value for at will index from "
                "the end of the array, such that inserting at -1 appends to the array. "
                "Returns the array.")
    },
    {NULL, NULL, NULL}
};

/* Load the array module */
void janet_lib_array(JanetTable *env) {
    janet_core_cfuns(env, NULL, array_cfuns);
}

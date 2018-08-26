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
#include "gc.h"
#include <string.h>

/* Initializes an array */
DstArray *dst_array_init(DstArray *array, int32_t capacity) {
    Dst *data = NULL;
    if (capacity > 0) {
        data = (Dst *) malloc(sizeof(Dst) * capacity);
        if (NULL == data) {
            DST_OUT_OF_MEMORY;
        }
    }
    array->count = 0;
    array->capacity = capacity;
    array->data = data;
    return array;
}

void dst_array_deinit(DstArray *array) {
    free(array->data);
}

/* Creates a new array */
DstArray *dst_array(int32_t capacity) {
    DstArray *array = dst_gcalloc(DST_MEMORY_ARRAY, sizeof(DstArray));
    return dst_array_init(array, capacity);
}

/* Creates a new array from n elements. */
DstArray *dst_array_n(const Dst *elements, int32_t n) {
    DstArray *array = dst_gcalloc(DST_MEMORY_ARRAY, sizeof(DstArray));
    array->capacity = n;
    array->count = n;
    array->data = malloc(sizeof(Dst) * n);
    if (!array->data) {
        DST_OUT_OF_MEMORY;
    }
    memcpy(array->data, elements, sizeof(Dst) * n);
    return array;
}

/* Ensure the array has enough capacity for elements */
void dst_array_ensure(DstArray *array, int32_t capacity) {
    Dst *newData;
    Dst *old = array->data;
    if (capacity <= array->capacity) return;
    newData = realloc(old, capacity * sizeof(Dst));
    if (NULL == newData) {
        DST_OUT_OF_MEMORY;
    }
    array->data = newData;
    array->capacity = capacity;
}

/* Set the count of an array. Extend with nil if needed. */
void dst_array_setcount(DstArray *array, int32_t count) {
    if (count < 0)
        return;
    if (count > array->count) {
        int32_t i;
        dst_array_ensure(array, count);
        for (i = array->count; i < count; i++) {
            array->data[i] = dst_wrap_nil();
        }
    }
    array->count = count;
}

/* Push a value to the top of the array */
void dst_array_push(DstArray *array, Dst x) {
    int32_t newcount = array->count + 1;
    if (newcount >= array->capacity) {
        dst_array_ensure(array, newcount * 2);
    }
    array->data[array->count] = x;
    array->count = newcount;
}

/* Pop a value from the top of the array */
Dst dst_array_pop(DstArray *array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        return dst_wrap_nil();
    }
}

/* Look at the last value in the array */
Dst dst_array_peek(DstArray *array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        return dst_wrap_nil();
    }
}

/* C Functions */

static int cfun_new(DstArgs args) {
    int32_t cap;
    DstArray *array;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(cap, args, 0);
    array = dst_array(cap);
    DST_RETURN_ARRAY(args, array);
}

static int cfun_pop(DstArgs args) {
    DstArray *array;
    DST_FIXARITY(args, 1);
    DST_ARG_ARRAY(array, args, 0);
    DST_RETURN(args, dst_array_pop(array));
}

static int cfun_peek(DstArgs args) {
    DstArray *array;
    DST_FIXARITY(args, 1);
    DST_ARG_ARRAY(array, args, 0);
    DST_RETURN(args, dst_array_peek(array));
}

static int cfun_push(DstArgs args) {
    DstArray *array;
    int32_t newcount;
    DST_MINARITY(args, 1);
    DST_ARG_ARRAY(array, args, 0);
    newcount = array->count - 1 + args.n;
    dst_array_ensure(array, newcount);
    if (args.n > 1) memcpy(array->data + array->count, args.v + 1, (args.n - 1) * sizeof(Dst));
    array->count = newcount;
    DST_RETURN(args, args.v[0]);
}

static int cfun_setcount(DstArgs args) {
    DstArray *array;
    int32_t newcount;
    DST_FIXARITY(args, 2);
    DST_ARG_ARRAY(array, args, 0);
    DST_ARG_INTEGER(newcount, args, 1);
    if (newcount < 0) DST_THROW(args, "expected positive integer");
    dst_array_setcount(array, newcount);
    DST_RETURN(args, args.v[0]);
}

static int cfun_ensure(DstArgs args) {
    DstArray *array;
    int32_t newcount;
    DST_FIXARITY(args, 2);
    DST_ARG_ARRAY(array, args, 0);
    DST_ARG_INTEGER(newcount, args, 1);
    if (newcount < 0) DST_THROW(args, "expected positive integer");
    dst_array_ensure(array, newcount);
    DST_RETURN(args, args.v[0]);
}

static int cfun_slice(DstArgs args) {
    const Dst *vals;
    int32_t len;
    DstArray *ret;
    int32_t start, end;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 3);
    if (!dst_indexed_view(args.v[0], &vals, &len))
        DST_THROW(args, "expected array|tuple");
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
        ret = dst_array(end - start);
        for (j = 0, i = start; i < end; j++, i++) {
            ret->data[j] = vals[i];
        }
        ret->count = j;
    } else {
        ret = dst_array(0);
    }
    DST_RETURN_ARRAY(args, ret);
}

static int cfun_concat(DstArgs args) {
    int32_t i;
    DstArray *array;
    DST_MINARITY(args, 1);
    DST_ARG_ARRAY(array, args, 0);
    for (i = 1; i < args.n; i++) {
        switch (dst_type(args.v[i])) {
            default:
                dst_array_push(array, args.v[i]);
                break;
            case DST_ARRAY:
            case DST_TUPLE:
                {
                    int32_t j, len;
                    const Dst *vals;
                    dst_indexed_view(args.v[i], &vals, &len);
                    for (j = 0; j < len; j++)
                        dst_array_push(array, vals[j]);
                }
                break;
        }
    }
    DST_RETURN_ARRAY(args, array);
}

static const DstReg cfuns[] = {
    {"array.new", cfun_new},
    {"array.pop", cfun_pop},
    {"array.peek", cfun_peek},
    {"array.push", cfun_push},
    {"array.setcount", cfun_setcount},
    {"array.ensure", cfun_ensure},
    {"array.slice", cfun_slice},
    {"array.concat", cfun_concat},
    {NULL, NULL}
};

/* Load the array module */
int dst_lib_array(DstArgs args) {
    DstTable *env = dst_env(args);
    dst_cfuns(env, NULL, cfuns);
    return 0;
}

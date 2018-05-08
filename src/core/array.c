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
#include "gc.h"

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

static int cfun_pop(DstArgs args) {
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_ARRAY);
    return dst_return(args, dst_array_pop(dst_unwrap_array(args.v[0])));
}

static int cfun_peek(DstArgs args) {
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_ARRAY);
    return dst_return(args, dst_array_peek(dst_unwrap_array(args.v[0])));
}

static int cfun_push(DstArgs args) {
    DstArray *array;
    int32_t newcount;
    dst_minarity(args, 1);
    dst_check(args, 0, DST_ARRAY);
    array = dst_unwrap_array(args.v[0]);
    newcount = array->count - 1 + args.n;
    dst_array_ensure(array, newcount);
    if (args.n > 1) memcpy(array->data + array->count, args.v + 1, (args.n - 1) * sizeof(Dst));
    array->count = newcount;
    return dst_return(args, args.v[0]);
}

static int cfun_setcount(DstArgs args) {
    int32_t newcount;
    dst_fixarity(args, 2);
    dst_check(args, 0, DST_ARRAY);
    dst_check(args, 1, DST_INTEGER);
    newcount = dst_unwrap_integer(args.v[1]);
    if (newcount < 0) return dst_throw(args, "expected positive integer");
    dst_array_setcount(dst_unwrap_array(args.v[0]), newcount);
    return dst_return(args, args.v[0]);
}

static int cfun_ensure(DstArgs args) {
    int32_t newcount;
    dst_fixarity(args, 2);
    dst_check(args, 0, DST_ARRAY);
    dst_check(args, 1, DST_INTEGER);
    newcount = dst_unwrap_integer(args.v[1]);
    if (newcount < 0) return dst_throw(args, "expected positive integer");
    dst_array_ensure(dst_unwrap_array(args.v[0]), newcount);
    return dst_return(args, args.v[0]);
}

static int cfun_slice(DstArgs args) {
    const Dst *vals;
    int32_t len;
    DstArray *ret;
    int32_t start, end;
    dst_minarity(args, 1);
    dst_maxarity(args, 3);
    if (!dst_seq_view(args.v[0], &vals, &len))
        return dst_throw(args, "expected array|tuple");
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (dst_checktype(args.v[1], DST_INTEGER)) {
        start = dst_unwrap_integer(args.v[1]);
    } else {
        return dst_throw(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (dst_checktype(args.v[2], DST_INTEGER)) {
        end = dst_unwrap_integer(args.v[2]);
    } else {
        return dst_throw(args, "expected integer");
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
    return dst_return(args, dst_wrap_array(ret));
}

static int cfun_concat(DstArgs args) {
    int32_t i;
    DstArray *array;
    dst_minarity(args, 1);
    dst_check(args, 0, DST_ARRAY);
    array = dst_unwrap_array(args.v[0]);
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
                    dst_seq_view(args.v[i], &vals, &len);
                    for (j = 0; j < len; j++)
                        dst_array_push(array, vals[j]);
                }
                break;
        }
    }
    return dst_return(args, args.v[0]);
}

static const DstReg cfuns[] = {
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
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

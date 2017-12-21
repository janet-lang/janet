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

/* Iniializes an array */
DstArray *dst_array_init(DstArray *array, int32_t capacity) {
    DstValue *data = NULL;
    if (capacity > 0) {
        data = (DstValue *) malloc(sizeof(DstValue) * capacity);
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
    DstValue *newData;
    DstValue *old = array->data;
    if (capacity <= array->capacity) return;
    newData = realloc(old, capacity * sizeof(DstValue));
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
        dst_array_ensure(array, count + 1);
        dst_memempty(array->data + array->count, count - array->count);
    }
    array->count = count;
}

/* Push a value to the top of the array */
void dst_array_push(DstArray *array, DstValue x) {
    int32_t newcount = array->count + 1;
    if (newcount >= array->capacity) {
        dst_array_ensure(array, newcount * 2);
    }
    array->data[array->count] = x;
    array->count = newcount;
}

/* Pop a value from the top of the array */
DstValue dst_array_pop(DstArray *array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        return dst_wrap_nil();
    }
}

/* Look at the last value in the array */
DstValue dst_array_peek(DstArray *array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        return dst_wrap_nil();
    }
}

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

#include <janet.h>
#include <assert.h>

#include "tests.h"

int array_test() {

    int i;
    JanetArray *array1, *array2;

    array1 = janet_array(10);
    array2 = janet_array(0);

    janet_array_push(array1, janet_cstringv("one"));
    janet_array_push(array1, janet_cstringv("two"));
    janet_array_push(array1, janet_cstringv("three"));
    janet_array_push(array1, janet_cstringv("four"));
    janet_array_push(array1, janet_cstringv("five"));
    janet_array_push(array1, janet_cstringv("six"));
    janet_array_push(array1, janet_cstringv("seven"));

    assert(array1->count == 7);
    assert(array1->capacity >= 7);
    assert(janet_equals(array1->data[0], janet_cstringv("one")));

    janet_array_push(array2, janet_cstringv("one"));
    janet_array_push(array2, janet_cstringv("two"));
    janet_array_push(array2, janet_cstringv("three"));
    janet_array_push(array2, janet_cstringv("four"));
    janet_array_push(array2, janet_cstringv("five"));
    janet_array_push(array2, janet_cstringv("six"));
    janet_array_push(array2, janet_cstringv("seven"));

    for (i = 0; i < array2->count; i++) {
        assert(janet_equals(array1->data[i], array2->data[i]));
    }

    janet_array_pop(array1);
    janet_array_pop(array1);

    assert(array1->count == 5);

    return 0;
}

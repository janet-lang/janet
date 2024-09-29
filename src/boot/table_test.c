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

int table_test() {

    JanetTable *t1, *t2;

    t1 = janet_table(10);
    t2 = janet_table(0);

    janet_table_put(t1, janet_cstringv("hello"), janet_wrap_integer(2));
    janet_table_put(t1, janet_cstringv("akey"), janet_wrap_integer(5));
    janet_table_put(t1, janet_cstringv("box"), janet_wrap_boolean(0));
    janet_table_put(t1, janet_cstringv("square"), janet_cstringv("avalue"));

    assert(t1->count == 4);
    assert(t1->capacity >= t1->count);

    assert(janet_equals(janet_table_get(t1, janet_cstringv("hello")), janet_wrap_integer(2)));
    assert(janet_equals(janet_table_get(t1, janet_cstringv("akey")), janet_wrap_integer(5)));
    assert(janet_equals(janet_table_get(t1, janet_cstringv("box")), janet_wrap_boolean(0)));
    assert(janet_equals(janet_table_get(t1, janet_cstringv("square")), janet_cstringv("avalue")));

    janet_table_remove(t1, janet_cstringv("hello"));
    janet_table_put(t1, janet_cstringv("box"), janet_wrap_nil());

    assert(t1->count == 2);

    assert(janet_equals(janet_table_get(t1, janet_cstringv("hello")), janet_wrap_nil()));
    assert(janet_equals(janet_table_get(t1, janet_cstringv("box")), janet_wrap_nil()));

    janet_table_put(t2, janet_csymbolv("t2key1"), janet_wrap_integer(10));
    janet_table_put(t2, janet_csymbolv("t2key2"), janet_wrap_integer(100));
    janet_table_put(t2, janet_csymbolv("some key "), janet_wrap_integer(-2));
    janet_table_put(t2, janet_csymbolv("a thing"), janet_wrap_integer(10));

    assert(janet_equals(janet_table_get(t2, janet_csymbolv("t2key1")), janet_wrap_integer(10)));
    assert(janet_equals(janet_table_get(t2, janet_csymbolv("t2key2")), janet_wrap_integer(100)));

    assert(t2->count == 4);
    assert(janet_equals(janet_table_remove(t2, janet_csymbolv("t2key1")), janet_wrap_integer(10)));
    assert(t2->count == 3);
    assert(janet_equals(janet_table_remove(t2, janet_csymbolv("t2key2")), janet_wrap_integer(100)));
    assert(t2->count == 2);

    return 0;
}

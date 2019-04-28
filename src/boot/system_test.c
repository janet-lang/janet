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

#include <janet.h>
#include <assert.h>
#include <stdio.h>

#include "tests.h"

int system_test() {

#ifdef JANET_32
    assert(sizeof(void *) == 4);
#else
    assert(sizeof(void *) == 8);
#endif

    /* Reflexive testing and nanbox testing */
    assert(janet_equals(janet_wrap_nil(), janet_wrap_nil()));
    assert(janet_equals(janet_wrap_false(), janet_wrap_false()));
    assert(janet_equals(janet_wrap_true(), janet_wrap_true()));
    assert(janet_equals(janet_wrap_integer(1), janet_wrap_integer(1)));
    assert(janet_equals(janet_wrap_integer(INT32_MAX), janet_wrap_integer(INT32_MAX)));
    assert(janet_equals(janet_wrap_integer(-2), janet_wrap_integer(-2)));
    assert(janet_equals(janet_wrap_integer(INT32_MIN), janet_wrap_integer(INT32_MIN)));
    assert(janet_equals(janet_wrap_number(1.4), janet_wrap_number(1.4)));
    assert(janet_equals(janet_wrap_number(3.14159265), janet_wrap_number(3.14159265)));

    assert(NULL != &janet_wrap_nil);

    assert(janet_equals(janet_cstringv("a string."), janet_cstringv("a string.")));
    assert(janet_equals(janet_csymbolv("sym"), janet_csymbolv("sym")));

    return 0;
}

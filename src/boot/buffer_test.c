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

int buffer_test() {

    int i;
    JanetBuffer *buffer1, *buffer2;

    buffer1 = janet_buffer(100);
    buffer2 = janet_buffer(0);

    janet_buffer_push_cstring(buffer1, "hello, world!\n");

    janet_buffer_push_u8(buffer2, 'h');
    janet_buffer_push_u8(buffer2, 'e');
    janet_buffer_push_u8(buffer2, 'l');
    janet_buffer_push_u8(buffer2, 'l');
    janet_buffer_push_u8(buffer2, 'o');
    janet_buffer_push_u8(buffer2, ',');
    janet_buffer_push_u8(buffer2, ' ');
    janet_buffer_push_u8(buffer2, 'w');
    janet_buffer_push_u8(buffer2, 'o');
    janet_buffer_push_u8(buffer2, 'r');
    janet_buffer_push_u8(buffer2, 'l');
    janet_buffer_push_u8(buffer2, 'd');
    janet_buffer_push_u8(buffer2, '!');
    janet_buffer_push_u8(buffer2, '\n');

    assert(buffer1->count == buffer2->count);
    assert(buffer1->capacity >= buffer1->count);
    assert(buffer2->capacity >= buffer2->count);

    for (i = 0; i < buffer1->count; i++) {
        assert(buffer1->data[i] == buffer2->data[i]);
    }

    return 0;
}

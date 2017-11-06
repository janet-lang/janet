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

/* Wrapper functions wrap a data type that is used from C into a
 * dst value, which can then be used in dst internal functions. Use
 * these functions sparingly, as these function will let the programmer
 * leak memory, where as the stack based API ensures that all values can
 * be collected by the garbage collector. */

DstValue dst_wrap_nil() {
    DstValue y;
    y.type = DST_NIL;
    return y;
}

#define DST_WRAP_DEFINE(NAME, TYPE, DTYPE, UM)\
DstValue dst_wrap_##NAME(TYPE x) {\
    DstValue y;\
    y.type = DTYPE;\
    y.as.UM = x;\
    return y;\
}

DST_WRAP_DEFINE(real, double, DST_REAL, real)
DST_WRAP_DEFINE(integer, int64_t, DST_INTEGER, integer)
DST_WRAP_DEFINE(boolean, int, DST_BOOLEAN, boolean)
DST_WRAP_DEFINE(string, const uint8_t *, DST_STRING, string)
DST_WRAP_DEFINE(symbol, const uint8_t *, DST_SYMBOL, string)
DST_WRAP_DEFINE(array, DstArray *, DST_ARRAY, array)
DST_WRAP_DEFINE(tuple, const DstValue *, DST_TUPLE, tuple)
DST_WRAP_DEFINE(struct, const DstValue *, DST_STRUCT, st)
DST_WRAP_DEFINE(thread, DstFiber *, DST_FIBER, fiber)
DST_WRAP_DEFINE(buffer, DstBuffer *, DST_BUFFER, buffer)
DST_WRAP_DEFINE(function, DstFunction *, DST_FUNCTION, function)
DST_WRAP_DEFINE(cfunction, DstCFunction, DST_CFUNCTION, cfunction)
DST_WRAP_DEFINE(table, DstTable *, DST_TABLE, table)
DST_WRAP_DEFINE(userdata, void *, DST_USERDATA, pointer)

#undef DST_WRAP_DEFINE

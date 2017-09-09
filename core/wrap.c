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
#include "internal.h"

/* Wrapper functions wrap a data type that is used from C into a
 * gst value, which can then be used in gst. */

DstValue dst_wrap_nil() {
    GstValue y;
    y.type = GST_NIL;
    return y;
}

int dst_check_nil(Gst *vm, uint32_t i) {
    DstValue a = dst_arg(vm, i);
    return a.type == DST_NIL;
}

#define DST_WRAP_DEFINE(NAME, TYPE, DTYPE, UM)\
DstValue dst_wrap_##NAME(TYPE x) {\
    DstValue y;\
    y.type = DTYPE;\
    y.data.UM = x;\
    return y;\
}\
\
int dst_check_##NAME(Dst *vm, uint32_t i) {\
    return dst_arg(vm, i).type == DTYPE;\
}\

DST_WRAP_DEFINE(real, DstReal, DST_REAL, real)
DST_WRAP_DEFINE(integer, DstInteger, DST_INTEGER, integer)
DST_WRAP_DEFINE(boolean, int, DST_BOOLEAN, boolean)
DST_WRAP_DEFINE(string, const uint8_t *, DST_STRING, string)
DST_WRAP_DEFINE(symbol, const uint8_t *, DST_SYMBOL, string)
DST_WRAP_DEFINE(array, DstArray *, DST_ARRAY, array)
DST_WRAP_DEFINE(tuple, const DstValue *, DST_TUPLE, tuple)
DST_WRAP_DEFINE(struct, const DstValue *, DST_STRUCT, st)
DST_WRAP_DEFINE(thread, DstThread *, DST_THREAD, thread)
DST_WRAP_DEFINE(buffer, DstBuffer *, DST_BYTEBUFFER, buffer)
DST_WRAP_DEFINE(function, DstFunction *, DST_FUNCTION, function)
DST_WRAP_DEFINE(cfunction, DstCFunction, DST_CFUNCTION, cfunction)
DST_WRAP_DEFINE(table, DstTable *, DST_TABLE, table)
DST_WRAP_DEFINE(funcenv, DstFuncEnv *, DST_FUNCENV, env)
DST_WRAP_DEFINE(funcdef, DstFuncDef *, DST_FUNCDEF, def)

#undef DST_WRAP_DEFINE

DstValue dst_wrap_userdata(void *x) {
    DstValue ret;
    ret.type = DST_USERDATA;
    ret.data.pointer = x;
    return ret;
}

void *dst_check_userdata(Dst *vm, uint32_t i, const DstUserType *type) {
    DstValue x = dst_arg(vm, i);
    DstUserdataHeader *h;
    if (x.type != DST_USERDATA) return NULL;
    h = dst_udata_header(x.data.pointer);
    if (h->type != type) return NULL;
    return x.data.pointer;
}

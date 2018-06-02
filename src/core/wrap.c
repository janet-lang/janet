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

#ifdef DST_NANBOX

void *dst_nanbox_to_pointer(Dst x) {
    /* We need to do this shift to keep the higher bits of the pointer
     * the same as bit 47 as required by the x86 architecture. We may save
     * an instruction if we do x.u64 & DST_NANBOX_POINTERBITS, but this 0s
     * the high bits, and may make the pointer non-canocial on x86. If we switch
     * to 47 bit pointers (which is what userspace uses on Windows, we can use
     * the single mask rather than two shifts. */
#if defined (DST_NANBOX_47) || defined (DST_32)
    x.i64 &= DST_NANBOX_POINTERBITS;
#else
    x.i64 = (x.i64 << 16) >> 16;
#endif
    return (void *)x.i64;
}

Dst dst_nanbox_from_pointer(void *p, uint64_t tagmask) {
    Dst ret;
    ret.u64 = (int64_t)p;
#if defined (DST_NANBOX_47) || defined (DST_32)
#else
    ret.u64 &= DST_NANBOX_POINTERBITS;
#endif
    ret.u64 |= tagmask;
    return ret;
}

Dst dst_nanbox_from_cpointer(const void *p, uint64_t tagmask) {
    Dst ret;
    ret.u64 = (int64_t)p;
#if defined (DST_NANBOX_47) || defined (DST_32)
#else
    ret.u64 &= DST_NANBOX_POINTERBITS;
#endif
    ret.u64 |= tagmask;
    return ret;
}

Dst dst_nanbox_from_double(double d) {
    Dst ret;
    ret.real = d;
    /* Normalize NaNs */
    if (d != d)
        ret.u64 = dst_nanbox_tag(DST_REAL);
    return ret;
}

Dst dst_nanbox_from_bits(uint64_t bits) {
    Dst ret;
    ret.u64 = bits;
    return ret;
}

void *dst_nanbox_memalloc_empty(int32_t count) {
    int32_t i;
    void *mem = malloc(count * sizeof(DstKV));
    DstKV *mmem = (DstKV *)mem;
    for (i = 0; i < count; i++) {
        DstKV *kv = mmem + i;
        kv->key = dst_wrap_nil();
        kv->value = dst_wrap_nil();
    }
    return mem;
}

void dst_nanbox_memempty(DstKV *mem, int32_t count) {
    int32_t i;
    for (i = 0; i < count; i++) {
        mem[i].key = dst_wrap_nil();
        mem[i].value = dst_wrap_nil();
    }
}

#else

/* Wrapper functions wrap a data type that is used from C into a
 * dst value, which can then be used in dst internal functions. Use
 * these functions sparingly, as these function will let the programmer
 * leak memory, where as the stack based API ensures that all values can
 * be collected by the garbage collector. */

Dst dst_wrap_nil() {
    Dst y;
    y.type = DST_NIL;
    y.as.u64 = 0;
    return y;
}

Dst dst_wrap_true(void) {
    Dst y;
    y.type = DST_TRUE;
    y.as.u64 = 0;
    return y;
}

Dst dst_wrap_false(void) {
    Dst y;
    y.type = DST_FALSE;
    y.as.u64 = 0;
    return y;
}

Dst dst_wrap_boolean(int x) {
    Dst y;
    y.type = x ? DST_TRUE : DST_FALSE;
    y.as.u64 = 0;
    return y;
}

#define DST_WRAP_DEFINE(NAME, TYPE, DTYPE, UM)\
Dst dst_wrap_##NAME(TYPE x) {\
    Dst y;\
    y.type = DTYPE;\
    y.as.u64 = 0; /* zero other bits in case of 32 bit integer */ \
    y.as.UM = x;\
    return y;\
}

DST_WRAP_DEFINE(real, double, DST_REAL, real)
DST_WRAP_DEFINE(integer, int32_t, DST_INTEGER, integer)
DST_WRAP_DEFINE(string, const uint8_t *, DST_STRING, cpointer)
DST_WRAP_DEFINE(symbol, const uint8_t *, DST_SYMBOL, cpointer)
DST_WRAP_DEFINE(array, DstArray *, DST_ARRAY, pointer)
DST_WRAP_DEFINE(tuple, const Dst *, DST_TUPLE, cpointer)
DST_WRAP_DEFINE(struct, const DstKV *, DST_STRUCT, cpointer)
DST_WRAP_DEFINE(fiber, DstFiber *, DST_FIBER, pointer)
DST_WRAP_DEFINE(buffer, DstBuffer *, DST_BUFFER, pointer)
DST_WRAP_DEFINE(function, DstFunction *, DST_FUNCTION, pointer)
DST_WRAP_DEFINE(cfunction, DstCFunction, DST_CFUNCTION, pointer)
DST_WRAP_DEFINE(table, DstTable *, DST_TABLE, pointer)
DST_WRAP_DEFINE(abstract, void *, DST_ABSTRACT, pointer)

#undef DST_WRAP_DEFINE

#endif

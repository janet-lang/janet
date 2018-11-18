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

#include <janet/janet.h>

#ifdef JANET_NANBOX

void *janet_nanbox_to_pointer(Janet x) {
    /* We need to do this shift to keep the higher bits of the pointer
     * the same as bit 47 as required by the x86 architecture. We may save
     * an instruction if we do x.u64 & JANET_NANBOX_POINTERBITS, but this 0s
     * the high bits, and may make the pointer non-canocial on x86. If we switch
     * to 47 bit pointers (which is what userspace uses on Windows, we can use
     * the single mask rather than two shifts. */
#if defined (JANET_NANBOX_47) || defined (JANET_32)
    x.i64 &= JANET_NANBOX_POINTERBITS;
#else
    x.i64 = (x.i64 << 16) >> 16;
#endif

    return x.pointer;
}

Janet janet_nanbox_from_pointer(void *p, uint64_t tagmask) {
    Janet ret;
    ret.u64 = (int64_t)p;
#if defined (JANET_NANBOX_47) || defined (JANET_32)
#else
    ret.u64 &= JANET_NANBOX_POINTERBITS;
#endif
    ret.u64 |= tagmask;
    return ret;
}

Janet janet_nanbox_from_cpointer(const void *p, uint64_t tagmask) {
    Janet ret;
    ret.u64 = (int64_t)p;
#if defined (JANET_NANBOX_47) || defined (JANET_32)
#else
    ret.u64 &= JANET_NANBOX_POINTERBITS;
#endif
    ret.u64 |= tagmask;
    return ret;
}

Janet janet_nanbox_from_double(double d) {
    Janet ret;
    ret.real = d;
    /* Normalize NaNs */
    if (d != d)
        ret.u64 = janet_nanbox_tag(JANET_REAL);
    return ret;
}

Janet janet_nanbox_from_bits(uint64_t bits) {
    Janet ret;
    ret.u64 = bits;
    return ret;
}

void *janet_nanbox_memalloc_empty(int32_t count) {
    int32_t i;
    void *mem = malloc(count * sizeof(JanetKV));
    JanetKV *mmem = (JanetKV *)mem;
    for (i = 0; i < count; i++) {
        JanetKV *kv = mmem + i;
        kv->key = janet_wrap_nil();
        kv->value = janet_wrap_nil();
    }
    return mem;
}

void janet_nanbox_memempty(JanetKV *mem, int32_t count) {
    int32_t i;
    for (i = 0; i < count; i++) {
        mem[i].key = janet_wrap_nil();
        mem[i].value = janet_wrap_nil();
    }
}

#else

/* Wrapper functions wrap a data type that is used from C into a
 * janet value, which can then be used in janet internal functions. Use
 * these functions sparingly, as these function will let the programmer
 * leak memory, where as the stack based API ensures that all values can
 * be collected by the garbage collector. */

Janet janet_wrap_nil() {
    Janet y;
    y.type = JANET_NIL;
    y.as.u64 = 0;
    return y;
}

Janet janet_wrap_true(void) {
    Janet y;
    y.type = JANET_TRUE;
    y.as.u64 = 0;
    return y;
}

Janet janet_wrap_false(void) {
    Janet y;
    y.type = JANET_FALSE;
    y.as.u64 = 0;
    return y;
}

Janet janet_wrap_boolean(int x) {
    Janet y;
    y.type = x ? JANET_TRUE : JANET_FALSE;
    y.as.u64 = 0;
    return y;
}

#define JANET_WRAP_DEFINE(NAME, TYPE, DTYPE, UM)\
Janet janet_wrap_##NAME(TYPE x) {\
    Janet y;\
    y.type = DTYPE;\
    y.as.u64 = 0; /* zero other bits in case of 32 bit integer */ \
    y.as.UM = x;\
    return y;\
}

JANET_WRAP_DEFINE(real, double, JANET_REAL, real)
JANET_WRAP_DEFINE(integer, int32_t, JANET_INTEGER, integer)
JANET_WRAP_DEFINE(string, const uint8_t *, JANET_STRING, cpointer)
JANET_WRAP_DEFINE(symbol, const uint8_t *, JANET_SYMBOL, cpointer)
JANET_WRAP_DEFINE(array, JanetArray *, JANET_ARRAY, pointer)
JANET_WRAP_DEFINE(tuple, const Janet *, JANET_TUPLE, cpointer)
JANET_WRAP_DEFINE(struct, const JanetKV *, JANET_STRUCT, cpointer)
JANET_WRAP_DEFINE(fiber, JanetFiber *, JANET_FIBER, pointer)
JANET_WRAP_DEFINE(buffer, JanetBuffer *, JANET_BUFFER, pointer)
JANET_WRAP_DEFINE(function, JanetFunction *, JANET_FUNCTION, pointer)
JANET_WRAP_DEFINE(cfunction, JanetCFunction, JANET_CFUNCTION, pointer)
JANET_WRAP_DEFINE(table, JanetTable *, JANET_TABLE, pointer)
JANET_WRAP_DEFINE(abstract, void *, JANET_ABSTRACT, pointer)

#undef JANET_WRAP_DEFINE

#endif

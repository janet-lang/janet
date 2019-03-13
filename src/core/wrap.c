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

#ifndef JANET_AMALG
#include <janet.h>
#endif

void *janet_memalloc_empty(int32_t count) {
    int32_t i;
    void *mem = malloc(count * sizeof(JanetKV));
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }
    JanetKV *mmem = (JanetKV *)mem;
    for (i = 0; i < count; i++) {
        JanetKV *kv = mmem + i;
        kv->key = janet_wrap_nil();
        kv->value = janet_wrap_nil();
    }
    return mem;
}

void janet_memempty(JanetKV *mem, int32_t count) {
    int32_t i;
    for (i = 0; i < count; i++) {
        mem[i].key = janet_wrap_nil();
        mem[i].value = janet_wrap_nil();
    }
}

#ifdef JANET_NANBOX_64

void *janet_nanbox_to_pointer(Janet x) {
    x.i64 &= JANET_NANBOX_PAYLOADBITS;
    return x.pointer;
}

Janet janet_nanbox_from_pointer(void *p, uint64_t tagmask) {
    Janet ret;
    ret.pointer = p;
    ret.u64 |= tagmask;
    return ret;
}

Janet janet_nanbox_from_cpointer(const void *p, uint64_t tagmask) {
    Janet ret;
    ret.pointer = (void *)p;
    ret.u64 |= tagmask;
    return ret;
}

Janet janet_nanbox_from_double(double d) {
    Janet ret;
    ret.number = d;
    return ret;
}

Janet janet_nanbox_from_bits(uint64_t bits) {
    Janet ret;
    ret.u64 = bits;
    return ret;
}

#elif defined(JANET_NANBOX_32)

Janet janet_wrap_number(double x) {
    Janet ret;
    ret.number = x;
    ret.tagged.type += JANET_DOUBLE_OFFSET;
    return ret;
}

Janet janet_nanbox32_from_tagi(uint32_t tag, int32_t integer) {
    Janet ret;
    ret.tagged.type = tag;
    ret.tagged.payload.integer = integer;
    return ret;
}

Janet janet_nanbox32_from_tagp(uint32_t tag, void *pointer) {
    Janet ret;
    ret.tagged.type = tag;
    ret.tagged.payload.pointer = pointer;
    return ret;
}

double janet_unwrap_number(Janet x) {
    x.tagged.type -= JANET_DOUBLE_OFFSET;
    return x.number;
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
    y.type = JANET_BOOLEAN;
    y.as.u64 = 1;
    return y;
}

Janet janet_wrap_false(void) {
    Janet y;
    y.type = JANET_BOOLEAN;
    y.as.u64 = 0;
    return y;
}

Janet janet_wrap_boolean(int x) {
    Janet y;
    y.type = JANET_BOOLEAN;
    y.as.u64 = !!x;
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

JANET_WRAP_DEFINE(number, double, JANET_NUMBER, number)
JANET_WRAP_DEFINE(string, const uint8_t *, JANET_STRING, cpointer)
JANET_WRAP_DEFINE(symbol, const uint8_t *, JANET_SYMBOL, cpointer)
JANET_WRAP_DEFINE(keyword, const uint8_t *, JANET_KEYWORD, cpointer)
JANET_WRAP_DEFINE(array, JanetArray *, JANET_ARRAY, pointer)
JANET_WRAP_DEFINE(tuple, const Janet *, JANET_TUPLE, cpointer)
JANET_WRAP_DEFINE(struct, const JanetKV *, JANET_STRUCT, cpointer)
JANET_WRAP_DEFINE(fiber, JanetFiber *, JANET_FIBER, pointer)
JANET_WRAP_DEFINE(buffer, JanetBuffer *, JANET_BUFFER, pointer)
JANET_WRAP_DEFINE(function, JanetFunction *, JANET_FUNCTION, pointer)
JANET_WRAP_DEFINE(cfunction, JanetCFunction, JANET_CFUNCTION, pointer)
JANET_WRAP_DEFINE(table, JanetTable *, JANET_TABLE, pointer)
JANET_WRAP_DEFINE(abstract, void *, JANET_ABSTRACT, pointer)
JANET_WRAP_DEFINE(pointer, void *, JANET_POINTER, pointer)

#undef JANET_WRAP_DEFINE

#endif

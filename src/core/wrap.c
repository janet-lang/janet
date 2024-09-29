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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include <math.h>
#include "util.h"
#include "state.h"
#endif

/* Macro fills */

JanetType(janet_type)(Janet x) {
    return janet_type(x);
}
int (janet_checktype)(Janet x, JanetType type) {
    return janet_checktype(x, type);
}
int (janet_checktypes)(Janet x, int typeflags) {
    return janet_checktypes(x, typeflags);
}
int (janet_truthy)(Janet x) {
    return janet_truthy(x);
}

JanetStruct(janet_unwrap_struct)(Janet x) {
    return janet_unwrap_struct(x);
}
JanetTuple(janet_unwrap_tuple)(Janet x) {
    return janet_unwrap_tuple(x);
}
JanetFiber *(janet_unwrap_fiber)(Janet x) {
    return janet_unwrap_fiber(x);
}
JanetArray *(janet_unwrap_array)(Janet x) {
    return janet_unwrap_array(x);
}
JanetTable *(janet_unwrap_table)(Janet x) {
    return janet_unwrap_table(x);
}
JanetBuffer *(janet_unwrap_buffer)(Janet x) {
    return janet_unwrap_buffer(x);
}
JanetString(janet_unwrap_string)(Janet x) {
    return janet_unwrap_string(x);
}
JanetSymbol(janet_unwrap_symbol)(Janet x) {
    return janet_unwrap_symbol(x);
}
JanetKeyword(janet_unwrap_keyword)(Janet x) {
    return janet_unwrap_keyword(x);
}
JanetAbstract(janet_unwrap_abstract)(Janet x) {
    return janet_unwrap_abstract(x);
}
void *(janet_unwrap_pointer)(Janet x) {
    return janet_unwrap_pointer(x);
}
JanetFunction *(janet_unwrap_function)(Janet x) {
    return janet_unwrap_function(x);
}
JanetCFunction(janet_unwrap_cfunction)(Janet x) {
    return janet_unwrap_cfunction(x);
}
int (janet_unwrap_boolean)(Janet x) {
    return janet_unwrap_boolean(x);
}
int32_t (janet_unwrap_integer)(Janet x) {
    return janet_unwrap_integer(x);
}

#if defined(JANET_NANBOX_32) || defined(JANET_NANBOX_64)
Janet(janet_wrap_nil)(void) {
    return janet_wrap_nil();
}
Janet(janet_wrap_true)(void) {
    return janet_wrap_true();
}
Janet(janet_wrap_false)(void) {
    return janet_wrap_false();
}
Janet(janet_wrap_boolean)(int x) {
    return janet_wrap_boolean(x);
}
Janet(janet_wrap_string)(JanetString x) {
    return janet_wrap_string(x);
}
Janet(janet_wrap_symbol)(JanetSymbol x) {
    return janet_wrap_symbol(x);
}
Janet(janet_wrap_keyword)(JanetKeyword x) {
    return janet_wrap_keyword(x);
}
Janet(janet_wrap_array)(JanetArray *x) {
    return janet_wrap_array(x);
}
Janet(janet_wrap_tuple)(JanetTuple x) {
    return janet_wrap_tuple(x);
}
Janet(janet_wrap_struct)(JanetStruct x) {
    return janet_wrap_struct(x);
}
Janet(janet_wrap_fiber)(JanetFiber *x) {
    return janet_wrap_fiber(x);
}
Janet(janet_wrap_buffer)(JanetBuffer *x) {
    return janet_wrap_buffer(x);
}
Janet(janet_wrap_function)(JanetFunction *x) {
    return janet_wrap_function(x);
}
Janet(janet_wrap_cfunction)(JanetCFunction x) {
    return janet_wrap_cfunction(x);
}
Janet(janet_wrap_table)(JanetTable *x) {
    return janet_wrap_table(x);
}
Janet(janet_wrap_abstract)(JanetAbstract x) {
    return janet_wrap_abstract(x);
}
Janet(janet_wrap_pointer)(void *x) {
    return janet_wrap_pointer(x);
}
Janet(janet_wrap_integer)(int32_t x) {
    return janet_wrap_integer(x);
}
#endif

#ifndef JANET_NANBOX_32
double (janet_unwrap_number)(Janet x) {
    return janet_unwrap_number(x);
}
#endif

#ifdef JANET_NANBOX_64
Janet(janet_wrap_number)(double x) {
    return janet_wrap_number(x);
}
#endif

/*****/

void *janet_memalloc_empty(int32_t count) {
    int32_t i;
    void *mem = janet_malloc((size_t) count * sizeof(JanetKV));
    janet_vm.next_collection += (size_t) count * sizeof(JanetKV);
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

Janet janet_wrap_number_safe(double d) {
    Janet ret;
    ret.number = isnan(d) ? NAN : d;
    return ret;
}

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

Janet janet_wrap_number_safe(double d) {
    double x = isnan(d) ? NAN : d;
    return janet_wrap_number(x);
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

Janet janet_wrap_number_safe(double d) {
    return janet_wrap_number(d);
}

Janet janet_wrap_nil(void) {
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

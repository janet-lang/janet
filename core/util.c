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

#include "internal.h"

/****/
/* Parsing utils */
/****/

/* Get an integer power of 10 */
static double exp10(int power) {
    if (power == 0) return 1;
    if (power > 0) {
        double result = 10;
        int currentPower = 1;
        while (currentPower * 2 <= power) {
            result = result * result;
            currentPower *= 2;
        }
        return result * exp10(power - currentPower);
    } else {
        return 1 / exp10(-power);
    }
}

int dst_read_integer(const uint8_t *string, const uint8_t *end, int64_t *ret) {
    int sign = 1, x = 0;
    int64_t accum = 0;
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        x = *string;
        if (x < '0' || x > '9') return 0;
        x -= '0';
        accum = accum * 10 + x;
        ++string;
    }
    *ret = accum * sign;
    return 1;
}

/* Read a real from a string. Returns if successfuly
 * parsed a real from the enitre input string.
 * If returned 1, output is int ret.*/
int dst_read_real(const uint8_t *string, const uint8_t *end, double *ret, int forceInt) {
    int sign = 1, x = 0;
    double accum = 0, exp = 1, place = 1;
    /* Check the sign */
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        if (*string == '.' && !forceInt) {
            place = 0.1;
        } else if (!forceInt && (*string == 'e' || *string == 'E')) {
            /* Read the exponent */
            ++string;
            if (string >= end) return 0;
            if (!dst_read_real(string, end, &exp, 1))
                return 0;
            exp = exp10(exp);
            break;
        } else {
            x = *string;
            if (x < '0' || x > '9') return 0;
            x -= '0';
            if (place < 1) {
                accum += x * place;
                place *= 0.1;
            } else {
                accum *= 10;
                accum += x;
            }
        }
        ++string;
    }
    *ret = accum * sign * exp;
    return 1;
}

/* Utilities for manipulating different types with the same semantics */

/* Read both tuples and arrays as c pointers + uint32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int dst_seq_view(DstValue seq, const DstValue **data, uint32_t *len) {
    if (seq.type == DST_ARRAY) {
        *data = seq.data.array->data;
        *len = seq.data.array->count;
        return 1;
    } else if (seq.type == DST_TUPLE) {
        *data = seq.data.st;
        *len = dst_tuple_length(seq.data.st);
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + uint32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int dst_chararray_view(DstValue str, const uint8_t **data, uint32_t *len) {
    if (str.type == DST_STRING || str.type == DST_SYMBOL) {
        *data = str.data.string;
        *len = dst_string_length(str.data.string);
        return 1;
    } else if (str.type == DST_BYTEBUFFER) {
        *data = str.data.buffer->data;
        *len = str.data.buffer->count;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int dst_hashtable_view(DstValue tab, const DstValue **data, uint32_t *cap) {
    if (tab.type == DST_TABLE) {
        *data = tab.data.table->data;
        *cap = tab.data.table->capacity;
        return 1;
    } else if (tab.type == DST_STRUCT) {
        *data = tab.data.st;
        *cap = dst_struct_capacity(tab.data.st);
        return 1;
    }
    return 0;
}

DstReal dst_integer_to_real(DstInteger x) {
    return (DstReal) x;
}

DstInteger dst_real_to_integer(DstReal x) {
    return (DstInteger) x;
}

uint32_t dst_startrange(DstInteger raw, uint32_t len) {
    if (raw >= len)
        return -1;
    if (raw < 0)
        return len + raw;
    return raw;
}

uint32_t dst_endrange(DstInteger raw, uint32_t len) {
    if (raw > len)
        return -1;
    if (raw < 0)
        return len + raw + 1;
    return raw;
}

static DstValue cfunction(DstCFunction fn) {
    DstValue n;
    n.type = DST_CFUNCTION;
    n.data.cfunction = fn;
    return n;
}

int dst_callc(Dst *vm, DstCFunction fn, int numargs, ...) {
    int result, i;
    va_list args;
    DstValue *stack;
    va_start(args, numargs);
    stack = dst_thread_beginframe(vm, vm->thread, cfunction(fn), numargs);
    for (i = 0; i < numargs; ++i) {
        stack[i] = va_arg(args, DstValue);
    }
    va_end(args);
    result = fn(vm);
    dst_thread_popframe(vm, vm->thread);
    return result;
}


/* Stack manipulation functions */
int dst_checkerr(Dst *vm) {
    return !!vm->flags;
}

/* Get an argument from the stack */
DstValue dst_arg(Dst *vm, uint32_t index) {
    DstValue *stack = dst_thread_stack(vm->thread);
    uint32_t frameSize = dst_frame_size(stack);
    if (frameSize <= index) {
        DstValue ret;
        ret.type = DST_NIL;
        return ret;
    }
    return stack[index];
}

/* Put a value on the stack */
void dst_set_arg(Dst* vm, uint32_t index, DstValue x) {
    DstValue *stack = dst_thread_stack(vm->thread);
    uint32_t frameSize = dst_frame_size(stack);
    if (frameSize <= index) return;
    stack[index] = x;
}

/* Get the size of the VMStack */
uint32_t dst_args(Dst *vm) {
    DstValue *stack = dst_thread_stack(vm->thread);
    return dst_frame_size(stack);
}

void dst_addsize(Dst *vm, uint32_t n) {
    dst_thread_pushnil(vm, vm->thread, n);
}

void dst_setsize(Dst *vm, uint32_t n) {
    DstValue *stack = dst_thread_stack(vm->thread);
    uint32_t frameSize = dst_frame_size(stack);
    if (frameSize < n) {
        dst_thread_ensure_extra(vm, vm->thread, n - frameSize);
    }
    dst_frame_size(stack) = n;
}

void dst_swap(Dst *vm, uint32_t x, uint32_t y) {
    DstValue oldx = dst_arg(vm, x);
    DstValue oldy = dst_arg(vm, y);
    dst_set_arg(vm, x, oldy);
    dst_set_arg(vm, y, oldx);
}

void dst_move(Dst *vm, uint32_t dest, uint32_t src) {
    dst_set_arg(vm, dest, dst_arg(vm, src));
}

void dst_nil(Dst *vm, uint32_t dest) {
    DstValue n;
    n.type = DST_NIL;
    dst_set_arg(vm, dest, n);
}

void dst_true(Dst *vm, uint32_t dest) {
    dst_set_boolean(vm, dest, 1);
}

void dst_false(Dst *vm, uint32_t dest) {
    dst_set_boolean(vm, dest, 0);
}

/* Boolean Functions */
void dst_set_boolean(Dst *vm, uint32_t dest, int val) {
    DstValue n;
    n.type = DST_BOOLEAN;
    n.data.boolean = val;
    dst_set_arg(vm, dest, n);
}

int dst_get_boolean(Dst *vm, uint32_t b) {
    DstValue x = dst_arg(vm, b);
    if (x.type != DST_BOOLEAN) {
        return 0;
    }
    return x.data.boolean;
}

/* Integer functions */
void dst_set_integer(Dst *vm, uint32_t dest, int64_t val) {
    DstValue n;
    n.type = DST_INTEGER;
    n.data.integer = val;
    dst_set_arg(vm, dest, n);
}

int64_t dst_get_integer(Dst *vm, uint32_t i) {
    DstValue x = dst_arg(vm, i);
    if (x.type != DST_INTEGER) {
        return 0;
    }
    return x.data.integer;
}

/* Real functions */
void dst_set_real(Dst *vm, uint32_t dest, double val) {
    DstValue n;
    n.type = DST_REAL;
    n.data.real = val;
    dst_set_arg(vm, dest, n);
}

double dst_get_real(Dst *vm, uint32_t r) {
    DstValue x = dst_arg(vm, r);
    if (x.type != DST_REAL) {
        return 0.0;
    }
    return x.data.real;
}

/* CFunction functions */
void dst_set_cfunction(Dst *vm, uint32_t dest, DstCFunction cfn) {
    DstValue n;
    n.type = DST_CFUNCTION;
    n.data.cfunction = cfn;
    dst_set_arg(vm, dest, n);
}

DstCFunction dst_get_cfunction(Dst *vm, uint32_t cfn) {
    DstValue x = dst_arg(vm, cfn);
    if (x.type != DST_CFUNCTION) {
        return NULL;
    }
    return x.data.cfunction;
}

void dst_return(Dst *vm, uint32_t index) {
    vm->ret = dst_arg(vm, index);
}

void dst_throw(Dst *vm, uint32_t index) {
    vm->flags = 1;
    vm->ret = dst_arg(vm, index);
}

void dst_cerr(Dst *vm, const char *message) {
    vm->flags = 1;
    vm->ret = dst_string_cv(vm, message);
}

int dst_checktype(Dst *vm, uint32_t n, DstType type) {
    return dst_arg(vm, n).type == type;
}
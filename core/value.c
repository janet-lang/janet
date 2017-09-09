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

/* Boolean truth definition */
int dst_value_truthy(DstValue v) {
    return v.type != DST_NIL && !(v.type == DST_BOOLEAN && !v.data.boolean);
}

/* Check if two values are equal. This is strict equality with no conversion. */
int dst_value_equals(DstValue x, DstValue y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
        case DST_NIL:
            result = 1;
            break;
        case DST_BOOLEAN:
            result = (x.data.boolean == y.data.boolean);
            break;
        case DST_REAL:
            result = (x.data.real == y.data.real);
            break;
        case DST_INTEGER:
            result = (x.data.integer == y.data.integer);
            break;
        default:
            /* compare pointers */
            result = (x.data.pointer == y.data.pointer);
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t dst_value_hash(DstValue x) {
    uint32_t hash = 0;
    switch (x.type) {
    case DST_NIL:
        hash = 0;
        break;
    case DST_BOOLEAN:
        hash = x.data.boolean;
        break;
    case DST_STRING:
    case DST_SYMBOL:
        hash = dst_string_hash(x.data.string);
        break;
    case DST_TUPLE:
        hash = dst_tuple_hash(x.data.tuple);
        break;
    case DST_STRUCT:
        hash = dst_struct_hash(x.data.st);
        break;
    default:
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            hash = x.data.dwords[0] ^ x.data.dwords[1];
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (uint32_t) x.data.pointer;
        }
        break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int dst_value_compare(DstValue x, DstValue y) {
    if (x.type == y.type) {
        switch (x.type) {
            case DST_NIL:
                return 0;
            case DST_BOOLEAN:
                if (x.data.boolean == y.data.boolean) {
                    return 0;
                } else {
                    return x.data.boolean ? 1 : -1;
                }
            case DST_REAL:
                if (x.data.real == y.data.real) {
                    return 0;
                } else {
                    return x.data.real > y.data.real ? 1 : -1;
                }
            case DST_INTEGER:
                if (x.data.integer == y.data.integer) {
                    return 0;
                } else {
                    return x.data.integer > y.data.integer ? 1 : -1;
                }
            case DST_STRING:
                return dst_string_compare(x.data.string, y.data.string);
                /* Lower indices are most significant */
            case DST_TUPLE:
                {
                    uint32_t i;
                    uint32_t xlen = dst_tuple_length(x.data.tuple);
                    uint32_t ylen = dst_tuple_length(y.data.tuple);
                    uint32_t count = xlen < ylen ? xlen : ylen;
                    for (i = 0; i < count; ++i) {
                        int comp = dst_value_compare(x.data.tuple[i], y.data.tuple[i]);
                        if (comp != 0) return comp;
                    }
                    if (xlen < ylen)
                        return -1;
                    else if (xlen > ylen)
                        return 1;
                    return 0;
                }
                break;
            default:
                if (x.data.string == y.data.string) {
                    return 0;
                } else {
                    return x.data.string > y.data.string ? 1 : -1;
                }
        }
    } else if (x.type < y.type) {
        return -1;
    }
    return 1;
}

int dst_truthy(Dst *vm, uint32_t x) {
    return dst_value_truthy(dst_arg(vm, x));
}
uint32_t dst_hash(Dst *vm, uint32_t x) {
    return dst_value_hash(dst_arg(vm, x));
}
int dst_compare(Dst *vm, uint32_t x, uint32_t y) {
    return dst_value_compare(dst_arg(vm, x), dst_arg(vm, y));
}
int dst_equals(Dst *vm, uint32_t x, uint32_t y) {
    return dst_value_equals(dst_arg(vm, x), dst_arg(vm, y));
}

/* Get the length of an object. Returns errors for invalid types */
uint32_t dst_length(Dst *vm, uint32_t n) {
    DstValue x = dst_arg(vm, n);
    uint32_t length;
    switch (x.type) {
        default:
            vm->ret = dst_string_cv(vm, "cannot get length");
            vm->flags = 1;
            return 0;
        case DST_STRING:
            length = dst_string_length(x.data.string);
            break;
        case DST_ARRAY:
            length = x.data.array->count;
            break;
        case DST_BYTEBUFFER:
            length = x.data.buffer->count;
            break;
        case DST_TUPLE:
            length = dst_tuple_length(x.data.tuple);
            break;
        case DST_STRUCT:
            length = dst_struct_length(x.data.st);
            break;
        case DST_TABLE:
            length = x.data.table->count;
            break;
    }
    return length;
}

/* Get the capacity of an object. Returns errors for invalid types */
uint32_t dst_capacity(Dst *vm, uint32_t n) {
    DstValue x = dst_arg(vm, n);
    uint32_t cap;
    switch (x.type) {
        default:
            vm->ret = dst_string_cv(vm, "cannot get capacity");
            vm->flags = 1;
            return 0;
        case DST_STRING:
            cap = dst_string_length(x.data.string);
            break;
        case DST_ARRAY:
            cap = x.data.array->capacity;
            break;
        case DST_BYTEBUFFER:
            cap = x.data.buffer->capacity;
            break;
        case DST_TUPLE:
            cap = dst_tuple_length(x.data.tuple);
            break;
        case DST_STRUCT:
            cap = dst_struct_length(x.data.st);
            break;
        case DST_TABLE:
            cap = x.data.table->capacity;
            break;
    }
    return cap;
}
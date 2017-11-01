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

/*
 * Define a number of functions that can be used internally on ANY DstValue.
 */

/* Boolean truth definition */
int dst_truthy(DstValue v) {
    return v.type != DST_NIL && !(v.type == DST_BOOLEAN && !v.as.boolean);
}

/* Check if two values are equal. This is strict equality with no conversion. */
int dst_equals(DstValue x, DstValue y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
        case DST_NIL:
            result = 1;
            break;
        case DST_BOOLEAN:
            result = (x.as.boolean == y.as.boolean);
            break;
        case DST_REAL:
            result = (x.as.real == y.as.real);
            break;
        case DST_INTEGER:
            result = (x.as.integer == y.as.integer);
            break;
        default:
            /* compare pointers */
            result = (x.as.pointer == y.as.pointer);
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t dst_hash(DstValue x) {
    uint32_t hash = 0;
    switch (x.type) {
    case DST_NIL:
        hash = 0;
        break;
    case DST_BOOLEAN:
        hash = x.as.boolean;
        break;
    case DST_STRING:
    case DST_SYMBOL:
        hash = dst_string_hash(x.as.string);
        break;
    case DST_TUPLE:
        hash = dst_tuple_hash(x.as.tuple);
        break;
    case DST_STRUCT:
        hash = dst_struct_hash(x.as.st);
        break;
    default:
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            hash = x.as.dwords[0] ^ x.as.dwords[1];
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (uint32_t) x.as.pointer;
        }
        break;
    }
    return hash;
}

/* Computes hash of an array of values */
uint32_t dst_calchash_array(const DstValue *array, uint32_t len) {
    const DstValue *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + dst_value_hash(*array++);
    return hash;
}

/* Compare two strings */
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = dst_string_length(lhs);
    uint32_t ylen = dst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int dst_compare(DstValue x, DstValue y) {
    if (x.type == y.type) {
        switch (x.type) {
            case DST_NIL:
                return 0;
            case DST_BOOLEAN:
                if (x.as.boolean == y.as.boolean) {
                    return 0;
                } else {
                    return x.as.boolean ? 1 : -1;
                }
            case DST_REAL:
                if (x.as.real == y.as.real) {
                    return 0;
                } else {
                    return x.as.real > y.as.real ? 1 : -1;
                }
            case DST_INTEGER:
                if (x.as.integer == y.as.integer) {
                    return 0;
                } else {
                    return x.as.integer > y.as.integer ? 1 : -1;
                }
            case DST_STRING:
                return dst_string_compare(x.as.string, y.as.string);
                /* Lower indices are most significant */
            case DST_TUPLE:
                {
                    uint32_t i;
                    uint32_t xlen = dst_tuple_length(x.as.tuple);
                    uint32_t ylen = dst_tuple_length(y.as.tuple);
                    uint32_t count = xlen < ylen ? xlen : ylen;
                    for (i = 0; i < count; ++i) {
                        int comp = dst_value_compare(x.as.tuple[i], y.as.tuple[i]);
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
                if (x.as.string == y.as.string) {
                    return 0;
                } else {
                    return x.as.string > y.as.string ? 1 : -1;
                }
        }
    } else if (x.type < y.type) {
        return -1;
    }
    return 1;
}

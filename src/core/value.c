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

/*
 * Define a number of functions that can be used internally on ANY Dst.
 */

/* Check if two values are equal. This is strict equality with no conversion. */
int dst_equals(Dst x, Dst y) {
    int result = 0;
    if (dst_type(x) != dst_type(y)) {
        result = 0;
    } else {
        switch (dst_type(x)) {
        case DST_NIL:
        case DST_TRUE:
        case DST_FALSE:
            result = 1;
            break;
        case DST_REAL:
            result = (dst_unwrap_real(x) == dst_unwrap_real(y));
            break;
        case DST_INTEGER:
            result = (dst_unwrap_integer(x) == dst_unwrap_integer(y));
            break;
        case DST_STRING:
            result = dst_string_equal(dst_unwrap_string(x), dst_unwrap_string(y));
            break;
        case DST_TUPLE:
            result = dst_tuple_equal(dst_unwrap_tuple(x), dst_unwrap_tuple(y));
            break;
        case DST_STRUCT:
            result = dst_struct_equal(dst_unwrap_struct(x), dst_unwrap_struct(y));
            break;
        default:
            /* compare pointers */
            result = (dst_unwrap_pointer(x) == dst_unwrap_pointer(y));
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
int32_t dst_hash(Dst x) {
    int32_t hash = 0;
    switch (dst_type(x)) {
    case DST_NIL:
        hash = 0;
        break;
    case DST_FALSE:
        hash = 1;
        break;
    case DST_TRUE:
        hash = 2;
        break;
    case DST_STRING:
    case DST_SYMBOL:
        hash = dst_string_hash(dst_unwrap_string(x));
        break;
    case DST_TUPLE:
        hash = dst_tuple_hash(dst_unwrap_tuple(x));
        break;
    case DST_STRUCT:
        hash = dst_struct_hash(dst_unwrap_struct(x));
        break;
    case DST_INTEGER:
        hash = dst_unwrap_integer(x);
        break;
    default:
        /* TODO - test performance with different hash functions */
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            uint64_t i = dst_u64(x);
            hash = (int32_t)(i & 0xFFFFFFFF);
            /* Get a bit more entropy by shifting the low bits out */
            hash >>= 3;
            hash ^= (int32_t) (i >> 32);
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (int32_t) ((char *)dst_unwrap_pointer(x) - (char *)0);
            hash >>= 2;
        }
        break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int dst_compare(Dst x, Dst y) {
    if (dst_type(x) == dst_type(y)) {
        switch (dst_type(x)) {
            case DST_NIL:
            case DST_FALSE:
            case DST_TRUE:
                return 0;
            case DST_REAL:
                /* Check for nans to ensure total order */
                if (dst_unwrap_real(x) != dst_unwrap_real(x))
                    return dst_unwrap_real(y) != dst_unwrap_real(y)
                        ? 0
                        : -1;
                if (dst_unwrap_real(y) != dst_unwrap_real(y))
                    return 1;

                if (dst_unwrap_real(x) == dst_unwrap_real(y)) {
                    return 0;
                } else {
                    return dst_unwrap_real(x) > dst_unwrap_real(y) ? 1 : -1;
                }
            case DST_INTEGER:
                if (dst_unwrap_integer(x) == dst_unwrap_integer(y)) {
                    return 0;
                } else {
                    return dst_unwrap_integer(x) > dst_unwrap_integer(y) ? 1 : -1;
                }
            case DST_STRING:
            case DST_SYMBOL:
                return dst_string_compare(dst_unwrap_string(x), dst_unwrap_string(y));
            case DST_TUPLE:
                return dst_tuple_compare(dst_unwrap_tuple(x), dst_unwrap_tuple(y));
            case DST_STRUCT:
                return dst_struct_compare(dst_unwrap_struct(x), dst_unwrap_struct(y));
            default:
                if (dst_unwrap_string(x) == dst_unwrap_string(y)) {
                    return 0;
                } else {
                    return dst_unwrap_string(x) > dst_unwrap_string(y) ? 1 : -1;
                }
        }
    }
    return (dst_type(x) < dst_type(y)) ? -1 : 1;
}

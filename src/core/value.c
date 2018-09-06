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

/*
 * Define a number of functions that can be used internally on ANY Janet.
 */

/* Check if two values are equal. This is strict equality with no conversion. */
int janet_equals(Janet x, Janet y) {
    int result = 0;
    if (janet_type(x) != janet_type(y)) {
        result = 0;
    } else {
        switch (janet_type(x)) {
        case JANET_NIL:
        case JANET_TRUE:
        case JANET_FALSE:
            result = 1;
            break;
        case JANET_REAL:
            result = (janet_unwrap_real(x) == janet_unwrap_real(y));
            break;
        case JANET_INTEGER:
            result = (janet_unwrap_integer(x) == janet_unwrap_integer(y));
            break;
        case JANET_STRING:
            result = janet_string_equal(janet_unwrap_string(x), janet_unwrap_string(y));
            break;
        case JANET_TUPLE:
            result = janet_tuple_equal(janet_unwrap_tuple(x), janet_unwrap_tuple(y));
            break;
        case JANET_STRUCT:
            result = janet_struct_equal(janet_unwrap_struct(x), janet_unwrap_struct(y));
            break;
        default:
            /* compare pointers */
            result = (janet_unwrap_pointer(x) == janet_unwrap_pointer(y));
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
int32_t janet_hash(Janet x) {
    int32_t hash = 0;
    switch (janet_type(x)) {
    case JANET_NIL:
        hash = 0;
        break;
    case JANET_FALSE:
        hash = 1;
        break;
    case JANET_TRUE:
        hash = 2;
        break;
    case JANET_STRING:
    case JANET_SYMBOL:
        hash = janet_string_hash(janet_unwrap_string(x));
        break;
    case JANET_TUPLE:
        hash = janet_tuple_hash(janet_unwrap_tuple(x));
        break;
    case JANET_STRUCT:
        hash = janet_struct_hash(janet_unwrap_struct(x));
        break;
    case JANET_INTEGER:
        hash = janet_unwrap_integer(x);
        break;
    default:
        /* TODO - test performance with different hash functions */
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            uint64_t i = janet_u64(x);
            hash = (int32_t)(i & 0xFFFFFFFF);
            /* Get a bit more entropy by shifting the low bits out */
            hash >>= 3;
            hash ^= (int32_t) (i >> 32);
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (int32_t) ((char *)janet_unwrap_pointer(x) - (char *)0);
            hash >>= 2;
        }
        break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int janet_compare(Janet x, Janet y) {
    if (janet_type(x) == janet_type(y)) {
        switch (janet_type(x)) {
            case JANET_NIL:
            case JANET_FALSE:
            case JANET_TRUE:
                return 0;
            case JANET_REAL:
                /* Check for nans to ensure total order */
                if (janet_unwrap_real(x) != janet_unwrap_real(x))
                    return janet_unwrap_real(y) != janet_unwrap_real(y)
                        ? 0
                        : -1;
                if (janet_unwrap_real(y) != janet_unwrap_real(y))
                    return 1;

                if (janet_unwrap_real(x) == janet_unwrap_real(y)) {
                    return 0;
                } else {
                    return janet_unwrap_real(x) > janet_unwrap_real(y) ? 1 : -1;
                }
            case JANET_INTEGER:
                if (janet_unwrap_integer(x) == janet_unwrap_integer(y)) {
                    return 0;
                } else {
                    return janet_unwrap_integer(x) > janet_unwrap_integer(y) ? 1 : -1;
                }
            case JANET_STRING:
            case JANET_SYMBOL:
                return janet_string_compare(janet_unwrap_string(x), janet_unwrap_string(y));
            case JANET_TUPLE:
                return janet_tuple_compare(janet_unwrap_tuple(x), janet_unwrap_tuple(y));
            case JANET_STRUCT:
                return janet_struct_compare(janet_unwrap_struct(x), janet_unwrap_struct(y));
            default:
                if (janet_unwrap_string(x) == janet_unwrap_string(y)) {
                    return 0;
                } else {
                    return janet_unwrap_string(x) > janet_unwrap_string(y) ? 1 : -1;
                }
        }
    }
    return (janet_type(x) < janet_type(y)) ? -1 : 1;
}

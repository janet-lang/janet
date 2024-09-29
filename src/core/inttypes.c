/*
* Copyright (c) 2024 Calvin Rose & contributors
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
#include "util.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>

/* Conditional compilation */
#ifdef JANET_INT_TYPES

#define MAX_INT_IN_DBL 9007199254740992ULL /* 2^53 */

static int it_s64_get(void *p, Janet key, Janet *out);
static int it_u64_get(void *p, Janet key, Janet *out);
static Janet janet_int64_next(void *p, Janet key);
static Janet janet_uint64_next(void *p, Janet key);

static int32_t janet_int64_hash(void *p1, size_t size) {
    (void) size;
    int32_t *words = p1;
    return words[0] ^ words[1];
}

static int janet_int64_compare(void *p1, void *p2) {
    int64_t x = *((int64_t *)p1);
    int64_t y = *((int64_t *)p2);
    return x == y ? 0 : x < y ? -1 : 1;
}

static int janet_uint64_compare(void *p1, void *p2) {
    uint64_t x = *((uint64_t *)p1);
    uint64_t y = *((uint64_t *)p2);
    return x == y ? 0 : x < y ? -1 : 1;
}

static void int64_marshal(void *p, JanetMarshalContext *ctx) {
    janet_marshal_abstract(ctx, p);
    janet_marshal_int64(ctx, *((int64_t *)p));
}

static void *int64_unmarshal(JanetMarshalContext *ctx) {
    int64_t *p = janet_unmarshal_abstract(ctx, sizeof(int64_t));
    p[0] = janet_unmarshal_int64(ctx);
    return p;
}

static void it_s64_tostring(void *p, JanetBuffer *buffer) {
    char str[32];
    snprintf(str, sizeof(str), "%" PRId64, *((int64_t *)p));
    janet_buffer_push_cstring(buffer, str);
}

static void it_u64_tostring(void *p, JanetBuffer *buffer) {
    char str[32];
    snprintf(str, sizeof(str), "%" PRIu64, *((uint64_t *)p));
    janet_buffer_push_cstring(buffer, str);
}

const JanetAbstractType janet_s64_type = {
    "core/s64",
    NULL,
    NULL,
    it_s64_get,
    NULL,
    int64_marshal,
    int64_unmarshal,
    it_s64_tostring,
    janet_int64_compare,
    janet_int64_hash,
    janet_int64_next,
    JANET_ATEND_NEXT
};

const JanetAbstractType janet_u64_type = {
    "core/u64",
    NULL,
    NULL,
    it_u64_get,
    NULL,
    int64_marshal,
    int64_unmarshal,
    it_u64_tostring,
    janet_uint64_compare,
    janet_int64_hash,
    janet_uint64_next,
    JANET_ATEND_NEXT
};

int64_t janet_unwrap_s64(Janet x) {
    switch (janet_type(x)) {
        default:
            break;
        case JANET_NUMBER : {
            double d = janet_unwrap_number(x);
            if (!janet_checkint64range(d)) break;
            return (int64_t) d;
        }
        case JANET_STRING: {
            int64_t value;
            const uint8_t *str = janet_unwrap_string(x);
            if (janet_scan_int64(str, janet_string_length(str), &value))
                return value;
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if (janet_abstract_type(abst) == &janet_s64_type ||
                    (janet_abstract_type(abst) == &janet_u64_type))
                return *(int64_t *)abst;
            break;
        }
    }
    janet_panicf("can not convert %t %q to 64 bit signed integer", x, x);
    return 0;
}

uint64_t janet_unwrap_u64(Janet x) {
    switch (janet_type(x)) {
        default:
            break;
        case JANET_NUMBER : {
            double d = janet_unwrap_number(x);
            if (!janet_checkuint64range(d)) break;
            return (uint64_t) d;
        }
        case JANET_STRING: {
            uint64_t value;
            const uint8_t *str = janet_unwrap_string(x);
            if (janet_scan_uint64(str, janet_string_length(str), &value))
                return value;
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if (janet_abstract_type(abst) == &janet_s64_type ||
                    (janet_abstract_type(abst) == &janet_u64_type))
                return *(uint64_t *)abst;
            break;
        }
    }
    janet_panicf("can not convert %t %q to a 64 bit unsigned integer", x, x);
    return 0;
}

JanetIntType janet_is_int(Janet x) {
    if (!janet_checktype(x, JANET_ABSTRACT)) return JANET_INT_NONE;
    const JanetAbstractType *at = janet_abstract_type(janet_unwrap_abstract(x));
    return (at == &janet_s64_type) ? JANET_INT_S64 :
           ((at == &janet_u64_type) ? JANET_INT_U64 :
            JANET_INT_NONE);
}

Janet janet_wrap_s64(int64_t x) {
    int64_t *box = janet_abstract(&janet_s64_type, sizeof(int64_t));
    *box = (int64_t)x;
    return janet_wrap_abstract(box);
}

Janet janet_wrap_u64(uint64_t x) {
    uint64_t *box = janet_abstract(&janet_u64_type, sizeof(uint64_t));
    *box = (uint64_t)x;
    return janet_wrap_abstract(box);
}

JANET_CORE_FN(cfun_it_s64_new,
              "(int/s64 value)",
              "Create a boxed signed 64 bit integer from a string value.") {
    janet_fixarity(argc, 1);
    return janet_wrap_s64(janet_unwrap_s64(argv[0]));
}

JANET_CORE_FN(cfun_it_u64_new,
              "(int/u64 value)",
              "Create a boxed unsigned 64 bit integer from a string value.") {
    janet_fixarity(argc, 1);
    return janet_wrap_u64(janet_unwrap_u64(argv[0]));
}

JANET_CORE_FN(cfun_to_number,
              "(int/to-number value)",
              "Convert an int/u64 or int/s64 to a number. Fails if the number is out of range for an int32.") {
    janet_fixarity(argc, 1);
    if (janet_type(argv[0]) == JANET_ABSTRACT) {
        void *abst = janet_unwrap_abstract(argv[0]);

        if (janet_abstract_type(abst) == &janet_s64_type) {
            int64_t value = *((int64_t *)abst);
            if (value > JANET_INTMAX_INT64) {
                janet_panicf("cannot convert %q to a number, must be in the range [%q, %q]", argv[0], janet_wrap_number(JANET_INTMIN_DOUBLE), janet_wrap_number(JANET_INTMAX_DOUBLE));
            }
            if (value < -JANET_INTMAX_INT64) {
                janet_panicf("cannot convert %q to a number, must be in the range [%q, %q]", argv[0], janet_wrap_number(JANET_INTMIN_DOUBLE), janet_wrap_number(JANET_INTMAX_DOUBLE));
            }
            return janet_wrap_number((double)value);
        }

        if (janet_abstract_type(abst) == &janet_u64_type) {
            uint64_t value = *((uint64_t *)abst);
            if (value > JANET_INTMAX_INT64) {
                janet_panicf("cannot convert %q to a number, must be in the range [%q, %q]", argv[0], janet_wrap_number(JANET_INTMIN_DOUBLE), janet_wrap_number(JANET_INTMAX_DOUBLE));
            }

            return janet_wrap_number((double)value);
        }
    }

    janet_panicf("expected int/u64 or int/s64, got %q", argv[0]);
}

JANET_CORE_FN(cfun_to_bytes,
              "(int/to-bytes value &opt endianness buffer)",
              "Write the bytes of an `int/s64` or `int/u64` into a buffer.\n"
              "The `buffer` parameter specifies an existing buffer to write to, if unset a new buffer will be created.\n"
              "Returns the modified buffer.\n"
              "The `endianness` parameter indicates the byte order:\n"
              "- `nil` (unset): system byte order\n"
              "- `:le`: little-endian, least significant byte first\n"
              "- `:be`: big-endian, most significant byte first\n") {
    janet_arity(argc, 1, 3);
    if (janet_is_int(argv[0]) == JANET_INT_NONE) {
        janet_panicf("int/to-bytes: expected an int/s64 or int/u64, got %q", argv[0]);
    }

    int reverse = 0;
    if (argc > 1 && !janet_checktype(argv[1], JANET_NIL)) {
        JanetKeyword endianness_kw = janet_getkeyword(argv, 1);
        if (!janet_cstrcmp(endianness_kw, "le")) {
#if JANET_BIG_ENDIAN
            reverse = 1;
#endif
        } else if (!janet_cstrcmp(endianness_kw, "be")) {
#if JANET_LITTLE_ENDIAN
            reverse = 1;
#endif
        } else {
            janet_panicf("int/to-bytes: expected endianness :le, :be or nil, got %v", argv[1]);
        }
    }

    JanetBuffer *buffer = NULL;
    if (argc > 2 && !janet_checktype(argv[2], JANET_NIL)) {
        if (!janet_checktype(argv[2], JANET_BUFFER)) {
            janet_panicf("int/to-bytes: expected buffer or nil, got %q", argv[2]);
        }

        buffer = janet_unwrap_buffer(argv[2]);
        janet_buffer_extra(buffer, 8);
    } else {
        buffer = janet_buffer(8);
    }

    uint8_t *bytes = janet_unwrap_abstract(argv[0]);
    if (reverse) {
        for (int i = 0; i < 8; ++i) {
            buffer->data[buffer->count + 7 - i] = bytes[i];
        }
    } else {
        memcpy(buffer->data + buffer->count, bytes, 8);
    }
    buffer->count += 8;

    return janet_wrap_buffer(buffer);
}

/*
 * Code to support polymorphic comparison.
 * int/u64 and int/s64 support a "compare" method that allows
 * comparison to each other, and to Janet numbers, using the
 * "compare" "compare<" ... functions.
 * In the following code explicit casts are sometimes used to help
 * make it clear when int/float conversions are happening.
 */
static int compare_double_double(double x, double y) {
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static int compare_int64_double(int64_t x, double y) {
    if (isnan(y)) {
        return 0;
    } else if ((y > JANET_INTMIN_DOUBLE) && (y < JANET_INTMAX_DOUBLE)) {
        double dx = (double) x;
        return compare_double_double(dx, y);
    } else if (y > ((double) INT64_MAX)) {
        return -1;
    } else if (y < ((double) INT64_MIN)) {
        return 1;
    } else {
        int64_t yi = (int64_t) y;
        return (x < yi) ? -1 : ((x > yi) ? 1 : 0);
    }
}

static int compare_uint64_double(uint64_t x, double y) {
    if (isnan(y)) {
        return 0;
    } else if (y < 0) {
        return 1;
    } else if ((y >= 0) && (y < JANET_INTMAX_DOUBLE)) {
        double dx = (double) x;
        return compare_double_double(dx, y);
    } else if (y > ((double) UINT64_MAX)) {
        return -1;
    } else {
        uint64_t yi = (uint64_t) y;
        return (x < yi) ? -1 : ((x > yi) ? 1 : 0);
    }
}

static Janet cfun_it_s64_compare(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    if (janet_is_int(argv[0]) != JANET_INT_S64) {
        janet_panic("compare method requires int/s64 as first argument");
    }
    int64_t x = janet_unwrap_s64(argv[0]);
    switch (janet_type(argv[1])) {
        default:
            break;
        case JANET_NUMBER : {
            double y = janet_unwrap_number(argv[1]);
            return janet_wrap_number(compare_int64_double(x, y));
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(argv[1]);
            if (janet_abstract_type(abst) == &janet_s64_type) {
                int64_t y = *(int64_t *)abst;
                return janet_wrap_number((x < y) ? -1 : (x > y ? 1 : 0));
            } else if (janet_abstract_type(abst) == &janet_u64_type) {
                uint64_t y = *(uint64_t *)abst;
                if (x < 0) {
                    return janet_wrap_number(-1);
                } else if (y > INT64_MAX) {
                    return janet_wrap_number(-1);
                } else {
                    int64_t y2 = (int64_t) y;
                    return janet_wrap_number((x < y2) ? -1 : (x > y2 ? 1 : 0));
                }
            }
            break;
        }
    }
    return janet_wrap_nil();
}

static Janet cfun_it_u64_compare(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    if (janet_is_int(argv[0]) != JANET_INT_U64) {
        janet_panic("compare method requires int/u64 as first argument");
    }
    uint64_t x = janet_unwrap_u64(argv[0]);
    switch (janet_type(argv[1])) {
        default:
            break;
        case JANET_NUMBER : {
            double y = janet_unwrap_number(argv[1]);
            return janet_wrap_number(compare_uint64_double(x, y));
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(argv[1]);
            if (janet_abstract_type(abst) == &janet_u64_type) {
                uint64_t y = *(uint64_t *)abst;
                return janet_wrap_number((x < y) ? -1 : (x > y ? 1 : 0));
            } else if (janet_abstract_type(abst) == &janet_s64_type) {
                int64_t y = *(int64_t *)abst;
                if (y < 0) {
                    return janet_wrap_number(1);
                } else if (x > INT64_MAX) {
                    return janet_wrap_number(1);
                } else {
                    int64_t x2 = (int64_t) x;
                    return janet_wrap_number((x2 < y) ? -1 : (x2 > y ? 1 : 0));
                }
            }
            break;
        }
    }
    return janet_wrap_nil();
}

/*
 * In C, signed arithmetic overflow is undefined behvior
 * but unsigned arithmetic overflow is twos complement
 *
 * Reference:
 * https://en.cppreference.com/w/cpp/language/ub
 * http://blog.llvm.org/2011/05/what-every-c-programmer-should-know.html
 *
 * This means OPMETHOD & OPMETHODINVERT must always use
 * unsigned arithmetic internally, regardless of the true type.
 * This will not affect the end result (property of twos complement).
 */
#define OPMETHOD(T, type, name, oper) \
static Janet cfun_it_##type##_##name(int32_t argc, Janet *argv) { \
    janet_arity(argc, 2, -1); \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[0]); \
    for (int32_t i = 1; i < argc; i++) \
        /* This avoids undefined behavior. See above for why. */ \
        *box = (T) ((uint64_t) (*box)) oper ((uint64_t) janet_unwrap_##type(argv[i])); \
    return janet_wrap_abstract(box); \
} \

#define OPMETHODINVERT(T, type, name, oper) \
static Janet cfun_it_##type##_##name##i(int32_t argc, Janet *argv) { \
    janet_fixarity(argc, 2); \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[1]); \
    /* This avoids undefined behavior. See above for why. */ \
    *box = (T) ((uint64_t) *box) oper ((uint64_t) janet_unwrap_##type(argv[0])); \
    return janet_wrap_abstract(box); \
} \

#define UNARYMETHOD(T, type, name, oper) \
static Janet cfun_it_##type##_##name(int32_t argc, Janet *argv) { \
    janet_fixarity(argc, 1); \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = oper(janet_unwrap_##type(argv[0])); \
    return janet_wrap_abstract(box); \
} \

#define DIVZERO(name) DIVZERO_##name
#define DIVZERO_div janet_panic("division by zero")
#define DIVZERO_rem janet_panic("division by zero")
#define DIVZERO_mod return janet_wrap_abstract(box)

#define DIVMETHOD(T, type, name, oper) \
static Janet cfun_it_##type##_##name(int32_t argc, Janet *argv) { \
    janet_arity(argc, 2, -1);                       \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[0]); \
    for (int32_t i = 1; i < argc; i++) { \
      T value = janet_unwrap_##type(argv[i]); \
      if (value == 0) DIVZERO(name); \
      *box oper##= value; \
    } \
    return janet_wrap_abstract(box); \
} \

#define DIVMETHODINVERT(T, type, name, oper) \
static Janet cfun_it_##type##_##name##i(int32_t argc, Janet *argv) { \
    janet_fixarity(argc, 2);                       \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[1]); \
    T value = janet_unwrap_##type(argv[0]); \
    if (value == 0) DIVZERO(name); \
    *box oper##= value; \
    return janet_wrap_abstract(box); \
} \

#define DIVMETHOD_SIGNED(T, type, name, oper) \
static Janet cfun_it_##type##_##name(int32_t argc, Janet *argv) { \
    janet_arity(argc, 2, -1);                       \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[0]); \
    for (int32_t i = 1; i < argc; i++) { \
      T value = janet_unwrap_##type(argv[i]); \
      if (value == 0) DIVZERO(name); \
      if ((value == -1) && (*box == INT64_MIN)) janet_panic("INT64_MIN divided by -1"); \
      *box oper##= value; \
    } \
    return janet_wrap_abstract(box); \
} \

#define DIVMETHODINVERT_SIGNED(T, type, name, oper) \
static Janet cfun_it_##type##_##name##i(int32_t argc, Janet *argv) { \
    janet_fixarity(argc, 2);                       \
    T *box = janet_abstract(&janet_##type##_type, sizeof(T)); \
    *box = janet_unwrap_##type(argv[1]); \
    T value = janet_unwrap_##type(argv[0]); \
    if (value == 0) DIVZERO(name); \
    if ((value == -1) && (*box == INT64_MIN)) janet_panic("INT64_MIN divided by -1"); \
    *box oper##= value; \
    return janet_wrap_abstract(box); \
} \

static Janet cfun_it_s64_divf(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    int64_t *box = janet_abstract(&janet_s64_type, sizeof(int64_t));
    int64_t op1 = janet_unwrap_s64(argv[0]);
    int64_t op2 = janet_unwrap_s64(argv[1]);
    if (op2 == 0) janet_panic("division by zero");
    int64_t x = op1 / op2;
    *box = x - (((op1 ^ op2) < 0) && (x * op2 != op1));
    return janet_wrap_abstract(box);
}

static Janet cfun_it_s64_divfi(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    int64_t *box = janet_abstract(&janet_s64_type, sizeof(int64_t));
    int64_t op2 = janet_unwrap_s64(argv[0]);
    int64_t op1 = janet_unwrap_s64(argv[1]);
    if (op2 == 0) janet_panic("division by zero");
    int64_t x = op1 / op2;
    *box = x - (((op1 ^ op2) < 0) && (x * op2 != op1));
    return janet_wrap_abstract(box);
}

static Janet cfun_it_s64_mod(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    int64_t *box = janet_abstract(&janet_s64_type, sizeof(int64_t));
    int64_t op1 = janet_unwrap_s64(argv[0]);
    int64_t op2 = janet_unwrap_s64(argv[1]);
    if (op2 == 0) {
        *box = op1;
    } else {
        int64_t x = op1 % op2;
        *box = (((op1 ^ op2) < 0) && (x != 0)) ? x + op2 : x;
    }
    return janet_wrap_abstract(box);
}

static Janet cfun_it_s64_modi(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    int64_t *box = janet_abstract(&janet_s64_type, sizeof(int64_t));
    int64_t op2 = janet_unwrap_s64(argv[0]);
    int64_t op1 = janet_unwrap_s64(argv[1]);
    if (op2 == 0) {
        *box = op1;
    } else {
        int64_t x = op1 % op2;
        *box = (((op1 ^ op2) < 0) && (x != 0)) ? x + op2 : x;
    }
    return janet_wrap_abstract(box);
}

OPMETHOD(int64_t, s64, add, +)
OPMETHOD(int64_t, s64, sub, -)
OPMETHODINVERT(int64_t, s64, sub, -)
OPMETHOD(int64_t, s64, mul, *)
DIVMETHOD_SIGNED(int64_t, s64, div, /)
DIVMETHOD_SIGNED(int64_t, s64, rem, %)
DIVMETHODINVERT_SIGNED(int64_t, s64, div, /)
DIVMETHODINVERT_SIGNED(int64_t, s64, rem, %)
OPMETHOD(int64_t, s64, and, &)
OPMETHOD(int64_t, s64, or, |)
OPMETHOD(int64_t, s64, xor, ^)
UNARYMETHOD(int64_t, s64, not, ~)
OPMETHOD(int64_t, s64, lshift, <<)
OPMETHOD(int64_t, s64, rshift, >>)
OPMETHOD(uint64_t, u64, add, +)
OPMETHOD(uint64_t, u64, sub, -)
OPMETHODINVERT(uint64_t, u64, sub, -)
OPMETHOD(uint64_t, u64, mul, *)
DIVMETHOD(uint64_t, u64, div, /)
DIVMETHOD(uint64_t, u64, rem, %)
DIVMETHOD(uint64_t, u64, mod, %)
DIVMETHODINVERT(uint64_t, u64, div, /)
DIVMETHODINVERT(uint64_t, u64, rem, %)
DIVMETHODINVERT(uint64_t, u64, mod, %)
OPMETHOD(uint64_t, u64, and, &)
OPMETHOD(uint64_t, u64, or, |)
OPMETHOD(uint64_t, u64, xor, ^)
UNARYMETHOD(uint64_t, u64, not, ~)
OPMETHOD(uint64_t, u64, lshift, <<)
OPMETHOD(uint64_t, u64, rshift, >>)

#undef OPMETHOD
#undef DIVMETHOD
#undef DIVMETHOD_SIGNED
#undef COMPMETHOD

static JanetMethod it_s64_methods[] = {
    {"+", cfun_it_s64_add},
    {"r+", cfun_it_s64_add},
    {"-", cfun_it_s64_sub},
    {"r-", cfun_it_s64_subi},
    {"*", cfun_it_s64_mul},
    {"r*", cfun_it_s64_mul},
    {"/", cfun_it_s64_div},
    {"r/", cfun_it_s64_divi},
    {"div", cfun_it_s64_divf},
    {"rdiv", cfun_it_s64_divfi},
    {"mod", cfun_it_s64_mod},
    {"rmod", cfun_it_s64_modi},
    {"%", cfun_it_s64_rem},
    {"r%", cfun_it_s64_remi},
    {"&", cfun_it_s64_and},
    {"r&", cfun_it_s64_and},
    {"|", cfun_it_s64_or},
    {"r|", cfun_it_s64_or},
    {"^", cfun_it_s64_xor},
    {"r^", cfun_it_s64_xor},
    {"~", cfun_it_s64_not},
    {"<<", cfun_it_s64_lshift},
    {">>", cfun_it_s64_rshift},
    {"compare", cfun_it_s64_compare},
    {NULL, NULL}
};

static JanetMethod it_u64_methods[] = {
    {"+", cfun_it_u64_add},
    {"r+", cfun_it_u64_add},
    {"-", cfun_it_u64_sub},
    {"r-", cfun_it_u64_subi},
    {"*", cfun_it_u64_mul},
    {"r*", cfun_it_u64_mul},
    {"/", cfun_it_u64_div},
    {"r/", cfun_it_u64_divi},
    {"div", cfun_it_u64_div},
    {"rdiv", cfun_it_u64_divi},
    {"mod", cfun_it_u64_mod},
    {"rmod", cfun_it_u64_modi},
    {"%", cfun_it_u64_rem},
    {"r%", cfun_it_u64_remi},
    {"&", cfun_it_u64_and},
    {"r&", cfun_it_u64_and},
    {"|", cfun_it_u64_or},
    {"r|", cfun_it_u64_or},
    {"^", cfun_it_u64_xor},
    {"r^", cfun_it_u64_xor},
    {"~", cfun_it_u64_not},
    {"<<", cfun_it_u64_lshift},
    {">>", cfun_it_u64_rshift},
    {"compare", cfun_it_u64_compare},
    {NULL, NULL}
};

static Janet janet_int64_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(it_s64_methods, key);
}

static Janet janet_uint64_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(it_u64_methods, key);
}

static int it_s64_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        return 0;
    return janet_getmethod(janet_unwrap_keyword(key), it_s64_methods, out);
}

static int it_u64_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        return 0;
    return janet_getmethod(janet_unwrap_keyword(key), it_u64_methods, out);
}

/* Module entry point */
void janet_lib_inttypes(JanetTable *env) {
    JanetRegExt it_cfuns[] = {
        JANET_CORE_REG("int/s64", cfun_it_s64_new),
        JANET_CORE_REG("int/u64", cfun_it_u64_new),
        JANET_CORE_REG("int/to-number", cfun_to_number),
        JANET_CORE_REG("int/to-bytes", cfun_to_bytes),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, it_cfuns);
    janet_register_abstract_type(&janet_s64_type);
    janet_register_abstract_type(&janet_u64_type);
}

#endif

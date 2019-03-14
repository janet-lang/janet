/*
* Copyright (c) 2019 Calvin Rose & contributors
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

/* Compiler feature test macros for things */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <limits.h>

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

#define MAX_INT_IN_DBL 9007199254740992UL /*2^53*/

typedef int64_t bi_int64;
typedef uint64_t bi_uint64;


static Janet int64_get(void *p, Janet key);
static Janet uint64_get(void *p, Janet key);

static void int64_marshal(void *p, JanetMarshalContext *ctx) {
    bi_int64 *box = (bi_int64 *)p;
    janet_marshal_size(ctx, (size_t)(*box));
}

static void uint64_marshal(void *p, JanetMarshalContext *ctx) {
    bi_uint64 *box = (bi_uint64 *)p;
    janet_marshal_size(ctx, (size_t)(*box));
}

static void int64_unmarshal(void *p, JanetMarshalContext *ctx) {
    bi_int64 *box = (bi_int64 *)p;
    janet_unmarshal_size(ctx, (size_t *)box);
}

static void uint64_unmarshal(void *p, JanetMarshalContext *ctx) {
    bi_uint64 *box = (bi_uint64 *)p;
    janet_unmarshal_size(ctx, (size_t *)box);
}


static const JanetAbstractType bi_int64_type = {
    "core/int64",
    NULL,
    NULL,
    int64_get,
    NULL,
    int64_marshal,
    int64_unmarshal
};

static const JanetAbstractType bi_uint64_type = {
    "core/uint64",
    NULL,
    NULL,
    uint64_get,
    NULL,
    uint64_marshal,
    uint64_unmarshal
};

static int parse_int64(const char *str, bi_int64 *box) {
    char *endptr;
    int base = (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) ? 16 : 10;
    errno = 0;
    *box = (bi_int64)strtoll(str, &endptr, base);
    if ((errno == ERANGE && (*box == LLONG_MAX || *box == LLONG_MIN)) ||
            (errno != 0 && *box == 0) ||
            (endptr == str)) return 0;
    return 1;
}

static int parse_uint64(const char *str, bi_uint64 *box) {
    char *endptr;
    int base = (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) ? 16 : 10;
    errno = 0;
    *box = (bi_int64)strtoull(str, &endptr, base);
    if ((errno == ERANGE && (*box == ULLONG_MAX)) ||
            (errno != 0 && *box == 0) ||
            (endptr == str)) return 0;
    return 1;
}


static bi_int64 check_bi_int64(Janet x) {
    switch (janet_type(x)) {
        case JANET_NUMBER : {
            double dbl = janet_unwrap_number(x);
            if (fabs(dbl) <=  MAX_INT_IN_DBL)
                return (bi_int64)dbl;
            break;
        }
        case JANET_STRING: {
            bi_int64 value;
            if (parse_int64((const char *)janet_unwrap_string(x), &value))
                return value;
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if ((janet_abstract_type(abst) == &bi_int64_type) || (janet_abstract_type(abst) == &bi_uint64_type))
                return *(bi_int64 *)abst;
            break;
        }
    }
    janet_panic("bad int64 initializer");
    return 0;
}

static bi_uint64 check_bi_uint64(Janet x) {
    switch (janet_type(x)) {
        case JANET_NUMBER : {
            double dbl = janet_unwrap_number(x);
            if ((dbl >= 0) && (dbl <= MAX_INT_IN_DBL))
                return (bi_uint64)dbl;
            break;
        }
        case JANET_STRING: {
            bi_uint64 value;
            if (parse_uint64((const char *)janet_unwrap_string(x), &value))
                return value;
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if (janet_abstract_type(abst) == &bi_uint64_type)
                return *(bi_uint64 *)abst;
            break;
        }
    }
    janet_panic("bad uint64 initializer");
    return 0;
}


static Janet make_bi_int64(Janet x) {
    bi_int64 *box = (bi_int64 *)janet_abstract(&bi_int64_type, sizeof(bi_int64));
    *box = check_bi_int64(x);
    return janet_wrap_abstract(box);
}

static Janet make_bi_uint64(Janet x) {
    bi_uint64 *box = (bi_uint64 *)janet_abstract(&bi_uint64_type, sizeof(bi_uint64));
    *box = check_bi_uint64(x);
    return janet_wrap_abstract(box);
}


JanetBigintType janet_is_bigint(Janet x) {
    if (!janet_checktype(x, JANET_ABSTRACT)) return JANET_BIGINT_TYPE_none;
    const JanetAbstractType *at = janet_abstract_type(janet_unwrap_abstract(x));
    return (at ==  &bi_int64_type) ? JANET_BIGINT_TYPE_int64 : ((at ==  &bi_uint64_type) ? JANET_BIGINT_TYPE_uint64 : JANET_BIGINT_TYPE_none);
}


static Janet cfun_bi_int64_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return make_bi_int64(argv[0]);
}

static Janet cfun_bi_uint64_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return make_bi_uint64(argv[0]);
}

#define OPMETHOD(type,name,oper) \
static Janet cfun_##type##_##name(int32_t argc, Janet *argv) { \
    janet_arity(argc, 2, -1);                       \
    bi_##type *box = (bi_##type *)janet_abstract(&bi_##type##_type, sizeof(bi_##type)); \
    *box = check_bi_##type(argv[0]); \
    for (int i=1;i<argc;i++) \
      *box oper##= check_bi_##type(argv[i]); \
    return janet_wrap_abstract(box); \
}

#define COMPMETHOD(type,name,oper) \
static Janet cfun_##type##_##name(int32_t argc, Janet *argv) { \
    janet_fixarity(argc, 2); \
    bi_##type v1 = check_bi_##type(argv[0]); \
    bi_##type v2 = check_bi_##type(argv[1]); \
    return janet_wrap_boolean(v1 oper v2); \
}



OPMETHOD(int64, add, +)
OPMETHOD(int64, sub, -)
OPMETHOD(int64, mul, *)
OPMETHOD(int64, div, /)

COMPMETHOD(int64, lt, <)
COMPMETHOD(int64, gt, >)
COMPMETHOD(int64, le, <=)
COMPMETHOD(int64, ge, >=)
COMPMETHOD(int64, eq, ==)
COMPMETHOD(int64, ne, !=)

OPMETHOD(uint64, add, +)
OPMETHOD(uint64, sub, -)
OPMETHOD(uint64, mul, *)
OPMETHOD(uint64, div, /)

COMPMETHOD(uint64, lt, <)
COMPMETHOD(uint64, gt, >)
COMPMETHOD(uint64, le, <=)
COMPMETHOD(uint64, ge, >=)
COMPMETHOD(uint64, eq, ==)
COMPMETHOD(uint64, ne, !=)



static JanetMethod int64_methods[] = {
    {"+", cfun_int64_add},
    {"-", cfun_int64_sub},
    {"*", cfun_int64_mul},
    {"/", cfun_int64_div},
    {"<", cfun_int64_lt},
    {">", cfun_int64_gt},
    {"<=", cfun_int64_le},
    {">=", cfun_int64_ge},
    {"==", cfun_int64_eq},
    {"!=", cfun_int64_ne},
    {NULL, NULL}
};

static JanetMethod uint64_methods[] = {
    {"+", cfun_uint64_add},
    {"-", cfun_uint64_sub},
    {"*", cfun_uint64_mul},
    {"/", cfun_uint64_div},
    {"<", cfun_uint64_lt},
    {">", cfun_uint64_gt},
    {"<=", cfun_uint64_le},
    {">=", cfun_uint64_ge},
    {"==", cfun_uint64_eq},
    {"!=", cfun_uint64_ne},
    {NULL, NULL}
};


static Janet int64_get(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        janet_panicf("expected keyword, got %v", key);
    return janet_getmethod(janet_unwrap_keyword(key), int64_methods);
}

static Janet uint64_get(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        janet_panicf("expected keyword, got %v", key);
    return janet_getmethod(janet_unwrap_keyword(key), uint64_methods);
}

static const JanetReg bi_cfuns[] = {
    {
        "bigint/int64", cfun_bi_int64_new,
        JDOC("(bigint/int64 value )\n\n"
             "Create new int64.")
    },
    {
        "bigint/uint64", cfun_bi_uint64_new,
        JDOC("(bigint/uint64 value )\n\n"
             "Create new uint64.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_bigint(JanetTable *env) {
    janet_core_cfuns(env, NULL, bi_cfuns);
    janet_register_abstract_type(&bi_int64_type);
    janet_register_abstract_type(&bi_uint64_type);
}

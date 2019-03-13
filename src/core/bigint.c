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

typedef int64_t bn_int64;
typedef uint64_t bn_uint64;


static const JanetAbstractType bn_int64_type = {
    "core/int64",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static const JanetAbstractType bn_uint64_type = {
    "core/uint64",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static int str_to_int64(const char *str, bn_int64 *box, int base) {
    char *endptr;
    errno = 0;
    *box = (bn_int64)strtoll(str, &endptr, base);
    if ((errno == ERANGE && (*box == LLONG_MAX || *box == LLONG_MIN)) ||
            (errno != 0 && *box == 0) ||
            (endptr == str)) return 0;
    return 1;
}

static int str_to_uint64(const char *str, bn_uint64 *box, int base) {
    char *endptr;
    errno = 0;
    *box = (bn_int64)strtoull(str, &endptr, base);
    if ((errno == ERANGE && (*box == ULLONG_MAX)) ||
            (errno != 0 && *box == 0) ||
            (endptr == str)) return 0;
    return 1;
}



static Janet make_bn_int64(Janet x) {
    bn_int64 *box = (bn_int64 *)janet_abstract(&bn_int64_type, sizeof(bn_int64));
    switch (janet_type(x)) {
        case JANET_NUMBER : {
            double dbl = janet_unwrap_number(x);
            if (dbl == (bn_int64)dbl) {
                *box = (bn_int64)dbl;
                return janet_wrap_abstract(box);
            }
            break;
        }
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            if (str_to_int64((const char *)janet_unwrap_string(x), box, 16))
                return janet_wrap_abstract(box);
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if ((janet_abstract_type(abst) == &bn_int64_type) || (janet_abstract_type(abst) == &bn_uint64_type)) {
                *box = *(bn_int64 *)abst;
                return janet_wrap_abstract(box);
            }
            break;
        }
    }
    janet_panic("bad int64 initializer");
    return janet_wrap_nil();
}

static Janet make_bn_uint64(Janet x) {
    bn_uint64 *box = (bn_uint64 *)janet_abstract(&bn_uint64_type, sizeof(bn_uint64));
    switch (janet_type(x)) {
        case JANET_NUMBER : {
            double dbl = janet_unwrap_number(x);
            if (dbl == (bn_uint64)dbl) {
                *box = (bn_uint64)dbl;
                return janet_wrap_abstract(box);
            }
            break;
        }
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            if (str_to_uint64((const char *)janet_unwrap_string(x), box, 16))
                return janet_wrap_abstract(box);
            break;
        }
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            if (janet_abstract_type(abst) == &bn_uint64_type) {
                *box = *(bn_uint64 *)abst;
                return janet_wrap_abstract(box);
            }
            break;
        }
    }
    janet_panic("bad uint64 initializer");
    return janet_wrap_nil();
}



int janet_is_bigint(Janet x, JanetBigintType type) {
    return janet_checktype(x, JANET_ABSTRACT) &&
           (((type == JANET_BIGINT_TYPE_int64) && (janet_abstract_type(janet_unwrap_abstract(x)) == &bn_int64_type)) ||
            ((type == JANET_BIGINT_TYPE_uint64) && (janet_abstract_type(janet_unwrap_abstract(x)) == &bn_uint64_type)));
}




static Janet cfun_bn_int64_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return make_bn_int64(argv[0]);
}

static Janet cfun_bn_uint64_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return make_bn_uint64(argv[0]);
}


static Janet cfun_bn_pretty(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    char buf[32];
    if (janet_checktype(argv[0], JANET_ABSTRACT)) {
        void *box = janet_unwrap_abstract(argv[0]);
        if (janet_abstract_type(box) == &bn_int64_type) {
            snprintf(buf, 32, "%li", *(bn_int64 *)box);
            return janet_cstringv(buf);
        }
        if (janet_abstract_type(box) == &bn_uint64_type) {
            snprintf(buf, 32, "%lu", *(bn_uint64 *)box);
            return janet_cstringv(buf);
        }
    }
    janet_panicf("expected bigint");
    return janet_wrap_nil();
}


static const JanetReg bn_cfuns[] = {
    {
        "bigint/int64", cfun_bn_int64_new,
        JDOC("(bigint/int64 value )\n\n"
             "Create new int64.")
    },
    {
        "bigint/uint64", cfun_bn_uint64_new,
        JDOC("(bigint/uint64 value )\n\n"
             "Create new uint64.")
    },
    {
        "bigint/pretty", cfun_bn_pretty,
        JDOC("(bigint/pretty bigint )\n\n"
             "return bigint as string")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_bigint(JanetTable *env) {
    janet_core_cfuns(env, NULL, bn_cfuns);
    janet_register_abstract_type(&bn_int64_type);
    janet_register_abstract_type(&bn_uint64_type);
}

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

#include <math.h>

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

static Janet janet_rng_get(void *p, Janet key);

static void janet_rng_marshal(void *p, JanetMarshalContext *ctx) {
    JanetRNG *rng = (JanetRNG *)p;
    janet_marshal_int(ctx, (int32_t) rng->a);
    janet_marshal_int(ctx, (int32_t) rng->b);
    janet_marshal_int(ctx, (int32_t) rng->c);
    janet_marshal_int(ctx, (int32_t) rng->d);
    janet_marshal_int(ctx, (int32_t) rng->counter);
}

static void janet_rng_unmarshal(void *p, JanetMarshalContext *ctx) {
    JanetRNG *rng = (JanetRNG *)p;
    rng->a = (uint32_t) janet_unmarshal_int(ctx);
    rng->b = (uint32_t) janet_unmarshal_int(ctx);
    rng->c = (uint32_t) janet_unmarshal_int(ctx);
    rng->d = (uint32_t) janet_unmarshal_int(ctx);
    rng->counter = (uint32_t) janet_unmarshal_int(ctx);
}

static JanetAbstractType JanetRNG_type = {
    "core/rng",
    NULL,
    NULL,
    janet_rng_get,
    NULL,
    janet_rng_marshal,
    janet_rng_unmarshal,
    NULL
};

static JANET_THREAD_LOCAL JanetRNG janet_vm_rng = {0};

JanetRNG *janet_default_rng(void) {
    return &janet_vm_rng;
}

void janet_rng_seed(JanetRNG *rng, uint32_t seed) {
    rng->a = seed + 123573u;
    rng->b = (seed + 43234283u) % 12391233u;
    rng->c = 0x17af0931u;
    rng->d = 0xFFFaaFFFu;
    rng->counter = 0u;
}

uint32_t janet_rng_u32(JanetRNG *rng) {
    /* Algorithm "xorwow" from p. 5 of Marsaglia, "Xorshift RNGs" */
    uint32_t t = rng->d;
    uint32_t const s = rng->a;
    rng->d = rng->c;
    rng->c = rng->b;
    rng->b = s;
    t ^= t >> 2;
    t ^= t << 1;
    t ^= s ^ (s << 4);
    rng->a = t;
    rng->counter += 362437;
    return t + rng->counter;
}

double janet_rng_double(JanetRNG *rng) {
    uint32_t hi = janet_rng_u32(rng);
    uint32_t lo = janet_rng_u32(rng);
    uint64_t big = (uint64_t)(lo) | (((uint64_t) hi) << 32);
    return ldexp((big >> (64 - 52)), -52);
}

static Janet cfun_rng_make(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    uint32_t seed = (uint32_t)(argc == 1 ? janet_getinteger(argv, 0) : 0);
    JanetRNG *rng = janet_abstract(&JanetRNG_type, sizeof(JanetRNG));
    janet_rng_seed(rng, seed);
    return janet_wrap_abstract(rng);
}

static Janet cfun_rng_uniform(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetRNG *rng = janet_getabstract(argv, 0, &JanetRNG_type);
    return janet_wrap_number(janet_rng_double(rng));
}

static Janet cfun_rng_int(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetRNG *rng = janet_getabstract(argv, 0, &JanetRNG_type);
    if (argc == 1) {
        uint32_t word = janet_rng_u32(rng) >> 1;
        return janet_wrap_integer(word);
    } else {
        int32_t max = janet_optnat(argv, argc, 1, INT32_MAX);
        if (max == 0) return janet_wrap_number(0.0);
        uint32_t modulo = (uint32_t) max;
        uint32_t maxgen = INT32_MAX;
        uint32_t maxword = maxgen - (maxgen % modulo);
        uint32_t word;
        do {
            word = janet_rng_u32(rng) >> 1;
        } while (word > maxword);
        return janet_wrap_integer(word % modulo);
    }
}

static const JanetMethod rng_methods[] = {
    {"uniform", cfun_rng_uniform},
    {"int", cfun_rng_int},
    {NULL, NULL}
};

static Janet janet_rng_get(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) janet_panicf("expected keyword method");
    return janet_getmethod(janet_unwrap_keyword(key), rng_methods);
}

/* Get a random number */
static Janet janet_rand(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_number(janet_rng_double(&janet_vm_rng));
}

/* Seed the random number generator */
static Janet janet_srand(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t x = janet_getinteger(argv, 0);
    janet_rng_seed(&janet_vm_rng, (uint32_t) x);
    return janet_wrap_nil();
}

static Janet janet_remainder(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    double x = janet_getnumber(argv, 0);
    double y = janet_getnumber(argv, 1);
    return janet_wrap_number(fmod(x, y));
}

#define JANET_DEFINE_MATHOP(name, fop)\
static Janet janet_##name(int32_t argc, Janet *argv) {\
    janet_fixarity(argc, 1); \
    double x = janet_getnumber(argv, 0); \
    return janet_wrap_number(fop(x)); \
}

JANET_DEFINE_MATHOP(acos, acos)
JANET_DEFINE_MATHOP(asin, asin)
JANET_DEFINE_MATHOP(atan, atan)
JANET_DEFINE_MATHOP(cos, cos)
JANET_DEFINE_MATHOP(cosh, cosh)
JANET_DEFINE_MATHOP(sin, sin)
JANET_DEFINE_MATHOP(sinh, sinh)
JANET_DEFINE_MATHOP(tan, tan)
JANET_DEFINE_MATHOP(tanh, tanh)
JANET_DEFINE_MATHOP(exp, exp)
JANET_DEFINE_MATHOP(log, log)
JANET_DEFINE_MATHOP(log10, log10)
JANET_DEFINE_MATHOP(sqrt, sqrt)
JANET_DEFINE_MATHOP(ceil, ceil)
JANET_DEFINE_MATHOP(fabs, fabs)
JANET_DEFINE_MATHOP(floor, floor)

#define JANET_DEFINE_MATH2OP(name, fop)\
static Janet janet_##name(int32_t argc, Janet *argv) {\
    janet_fixarity(argc, 2); \
    double lhs = janet_getnumber(argv, 0); \
    double rhs = janet_getnumber(argv, 1); \
    return janet_wrap_number(fop(lhs, rhs)); \
}\

JANET_DEFINE_MATH2OP(atan2, atan2)
JANET_DEFINE_MATH2OP(pow, pow)

static Janet janet_not(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return janet_wrap_boolean(!janet_truthy(argv[0]));
}

static const JanetReg math_cfuns[] = {
    {
        "%", janet_remainder,
        JDOC("(% dividend divisor)\n\n"
             "Returns the remainder of dividend / divisor.")
    },
    {
        "not", janet_not,
        JDOC("(not x)\n\nReturns the boolean inverse of x.")
    },
    {
        "math/random", janet_rand,
        JDOC("(math/random)\n\n"
             "Returns a uniformly distributed random number between 0 and 1.")
    },
    {
        "math/seedrandom", janet_srand,
        JDOC("(math/seedrandom seed)\n\n"
             "Set the seed for the random number generator. 'seed' should be "
             "an integer.")
    },
    {
        "math/cos", janet_cos,
        JDOC("(math/cos x)\n\n"
             "Returns the cosine of x.")
    },
    {
        "math/sin", janet_sin,
        JDOC("(math/sin x)\n\n"
             "Returns the sine of x.")
    },
    {
        "math/tan", janet_tan,
        JDOC("(math/tan x)\n\n"
             "Returns the tangent of x.")
    },
    {
        "math/acos", janet_acos,
        JDOC("(math/acos x)\n\n"
             "Returns the arccosine of x.")
    },
    {
        "math/asin", janet_asin,
        JDOC("(math/asin x)\n\n"
             "Returns the arcsine of x.")
    },
    {
        "math/atan", janet_atan,
        JDOC("(math/atan x)\n\n"
             "Returns the arctangent of x.")
    },
    {
        "math/exp", janet_exp,
        JDOC("(math/exp x)\n\n"
             "Returns e to the power of x.")
    },
    {
        "math/log", janet_log,
        JDOC("(math/log x)\n\n"
             "Returns log base natural number of x.")
    },
    {
        "math/log10", janet_log10,
        JDOC("(math/log10 x)\n\n"
             "Returns log base 10 of x.")
    },
    {
        "math/sqrt", janet_sqrt,
        JDOC("(math/sqrt x)\n\n"
             "Returns the square root of x.")
    },
    {
        "math/floor", janet_floor,
        JDOC("(math/floor x)\n\n"
             "Returns the largest integer value number that is not greater than x.")
    },
    {
        "math/ceil", janet_ceil,
        JDOC("(math/ceil x)\n\n"
             "Returns the smallest integer value number that is not less than x.")
    },
    {
        "math/pow", janet_pow,
        JDOC("(math/pow a x)\n\n"
             "Return a to the power of x.")
    },
    {
        "math/abs", janet_fabs,
        JDOC("(math/abs x)\n\n"
             "Return the absolute value of x.")
    },
    {
        "math/sinh", janet_sinh,
        JDOC("(math/sinh x)\n\n"
             "Return the hyperbolic sine of x.")
    },
    {
        "math/cosh", janet_cosh,
        JDOC("(math/cosh x)\n\n"
             "Return the hyperbolic cosine of x.")
    },
    {
        "math/tanh", janet_tanh,
        JDOC("(math/tanh x)\n\n"
             "Return the hyperbolic tangent of x.")
    },
    {
        "math/atan2", janet_atan2,
        JDOC("(math/atan2 y x)\n\n"
             "Return the arctangent of y/x. Works even when x is 0.")
    },
    {
        "math/rng", cfun_rng_make,
        JDOC("(math/rng &opt seed)\n\n"
             "Creates a Psuedo-Random number generator, with an optional seed. "
             "The seed should be an unsigned 32 bit integer. "
             "Do not use this for cryptography. Returns a core/rng abstract type.")
    },
    {
        "math/rng-uniform", cfun_rng_uniform,
        JDOC("(math/rng-seed rng seed)\n\n"
             "Extract a random number in the range [0, 1) from the RNG.")
    },
    {
        "math/rng-int", cfun_rng_int,
        JDOC("(math/rng-int rng &opt max)\n\n"
             "Extract a random random integer in the range [0, max] from the RNG. If "
             "no max is given, the default is 2^31 - 1.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_math(JanetTable *env) {
    janet_core_cfuns(env, NULL, math_cfuns);
#ifdef JANET_BOOTSTRAP
    janet_def(env, "math/pi", janet_wrap_number(3.1415926535897931),
              JDOC("The value pi."));
    janet_def(env, "math/e", janet_wrap_number(2.7182818284590451),
              JDOC("The base of the natural log."));
    janet_def(env, "math/inf", janet_wrap_number(INFINITY),
              JDOC("The number representing positive infinity"));
#endif
}

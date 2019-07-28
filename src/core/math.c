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

/* Get a random number */
static Janet janet_rand(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    double r = (rand() % RAND_MAX) / ((double) RAND_MAX);
    return janet_wrap_number(r);
}

/* Seed the random number generator */
static Janet janet_srand(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t x = janet_getinteger(argv, 0);
    srand((unsigned) x);
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
             "Set the seed for the random number generator. 'seed' should be an "
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

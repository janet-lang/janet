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
#include <math.h>

/* Get a random number */
int janet_rand(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    double r = (rand() % RAND_MAX) / ((double) RAND_MAX);
    JANET_RETURN_REAL(args, r);
}

/* Seed the random number generator */
int janet_srand(JanetArgs args) {
    int32_t x = 0;
    JANET_FIXARITY(args, 1);
    JANET_ARG_INTEGER(x, args, 0);
    srand((unsigned) x);
    return 0;
}

/* Convert a number to an integer */
int janet_int(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    switch (janet_type(args.v[0])) {
        default:
            JANET_THROW(args, "could not convert to integer");
        case JANET_REAL:
            *args.ret = janet_wrap_integer((int32_t) janet_unwrap_real(args.v[0]));
            break;
        case JANET_INTEGER:
            *args.ret = args.v[0];
            break;
    }
    return 0;
}

/* Convert a number to a real number */
int janet_real(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    switch (janet_type(args.v[0])) {
        default:
            JANET_THROW(args, "could not convert to real");
        case JANET_REAL:
            *args.ret = args.v[0];
            break;
        case JANET_INTEGER:
            *args.ret = janet_wrap_real((double) janet_unwrap_integer(args.v[0]));
            break;
    }
    return 0;
}

int janet_remainder(JanetArgs args) {
    JANET_FIXARITY(args, 2);
    if (janet_checktype(args.v[0], JANET_INTEGER) &&
            janet_checktype(args.v[1], JANET_INTEGER)) {
        int32_t x, y;
        x = janet_unwrap_integer(args.v[0]);
        y = janet_unwrap_integer(args.v[1]);
        JANET_RETURN_INTEGER(args, x % y);
    } else {
        double x, y;
        JANET_ARG_NUMBER(x, args, 0);
        JANET_ARG_NUMBER(y, args, 1);
        JANET_RETURN_REAL(args, fmod(x, y));
    }
}

#define JANET_DEFINE_MATHOP(name, fop)\
int janet_##name(JanetArgs args) {\
    double x;\
    JANET_FIXARITY(args, 1);\
    JANET_ARG_NUMBER(x, args, 0);\
    JANET_RETURN_REAL(args, fop(x));\
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
int janet_##name(JanetArgs args) {\
    double lhs, rhs;\
    JANET_FIXARITY(args, 2);\
    JANET_ARG_NUMBER(lhs, args, 0);\
    JANET_ARG_NUMBER(rhs, args, 1);\
    JANET_RETURN_REAL(args, fop(lhs, rhs));\
}\

JANET_DEFINE_MATH2OP(atan2, atan2)
JANET_DEFINE_MATH2OP(pow, pow)

static int janet_not(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    JANET_RETURN_BOOLEAN(args, !janet_truthy(args.v[0]));
}

static const JanetReg cfuns[] = {
    {"%", janet_remainder,
        "(% dividend divisor)\n\n"
        "Returns the remainder of dividend / divisor."
    },
    {"not", janet_not,
        "(not x)\n\nReturns the boolen inverse of x."
    },
    {"int", janet_int,
        "(int x)\n\nCast a number x to an integer."
    },
    {"real", janet_real,
        "(real x)\n\nCast a number x to a real number."
    },
    {"math.random", janet_rand,
        "(math.random)\n\n"
        "Returns a uniformly distrbuted random real number between 0 and 1."
    },
    {"math.seedrandom", janet_srand,
        "(math.seedrandom seed)\n\n"
        "Set the seed for the random number generator. 'seed' should be an "
        "an integer."
    },
    {"math.cos", janet_cos,
        "(math.cos x)\n\n"
        "Returns the cosine of x."
    },
    {"math.sin", janet_sin,
        "(math.sin x)\n\n"
        "Returns the sine of x."
    },
    {"math.tan", janet_tan,
        "(math.tan x)\n\n"
        "Returns the tangent of x."
    },
    {"math.acos", janet_acos,
        "(math.acos x)\n\n"
        "Returns the arccosine of x."
    },
    {"math.asin", janet_asin,
        "(math.asin x)\n\n"
        "Returns the arcsine of x."
    },
    {"math.atan", janet_atan,
        "(math.atan x)\n\n"
        "Returns the arctangent of x."
    },
    {"math.exp", janet_exp,
        "(math.exp x)\n\n"
        "Returns e to the power of x."
    },
    {"math.log", janet_log,
        "(math.log x)\n\n"
        "Returns log base 2 of x."
    },
    {"math.log10", janet_log10,
        "(math.log10 x)\n\n"
        "Returns log base 10 of x."
    },
    {"math.sqrt", janet_sqrt,
        "(math.sqrt x)\n\n"
        "Returns the square root of x."
    },
    {"math.floor", janet_floor,
        "(math.floor x)\n\n"
        "Returns the largest integer value real number that is not greater than x."
    },
    {"math.ceil", janet_ceil,
        "(math.ceil x)\n\n"
        "Returns the smallest integer value real number that is not less than x."
    },
    {"math.pow", janet_pow,
        "(math.pow a x)\n\n"
        "Return a to the power of x."
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
int janet_lib_math(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);

    janet_def(env, "math.pi", janet_wrap_real(3.1415926535897931),
            "The value pi.");
    janet_def(env, "math.e", janet_wrap_real(2.7182818284590451),
            "The base of the natural log.");
    janet_def(env, "math.inf", janet_wrap_real(INFINITY),
            "The real number representing positive infinity");
    return 0;
}

/*
* Copyright (c) 2024 Calvin Rose
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
#include "state.h"
#include "util.h"
#endif

#include <math.h>

static int janet_rng_get(void *p, Janet key, Janet *out);
static Janet janet_rng_next(void *p, Janet key);

static void janet_rng_marshal(void *p, JanetMarshalContext *ctx) {
    JanetRNG *rng = (JanetRNG *)p;
    janet_marshal_abstract(ctx, p);
    janet_marshal_int(ctx, (int32_t) rng->a);
    janet_marshal_int(ctx, (int32_t) rng->b);
    janet_marshal_int(ctx, (int32_t) rng->c);
    janet_marshal_int(ctx, (int32_t) rng->d);
    janet_marshal_int(ctx, (int32_t) rng->counter);
}

static void *janet_rng_unmarshal(JanetMarshalContext *ctx) {
    JanetRNG *rng = janet_unmarshal_abstract(ctx, sizeof(JanetRNG));
    rng->a = (uint32_t) janet_unmarshal_int(ctx);
    rng->b = (uint32_t) janet_unmarshal_int(ctx);
    rng->c = (uint32_t) janet_unmarshal_int(ctx);
    rng->d = (uint32_t) janet_unmarshal_int(ctx);
    rng->counter = (uint32_t) janet_unmarshal_int(ctx);
    return rng;
}

const JanetAbstractType janet_rng_type = {
    "core/rng",
    NULL,
    NULL,
    janet_rng_get,
    NULL,
    janet_rng_marshal,
    janet_rng_unmarshal,
    NULL, /* tostring */
    NULL, /* compare */
    NULL, /* hash */
    janet_rng_next,
    JANET_ATEND_NEXT
};

JanetRNG *janet_default_rng(void) {
    return &janet_vm.rng;
}

void janet_rng_seed(JanetRNG *rng, uint32_t seed) {
    rng->a = seed;
    rng->b = 0x97654321u;
    rng->c = 123871873u;
    rng->d = 0xf23f56c8u;
    rng->counter = 0u;
    /* First several numbers aren't that random. */
    for (int i = 0; i < 16; i++) janet_rng_u32(rng);
}

void janet_rng_longseed(JanetRNG *rng, const uint8_t *bytes, int32_t len) {
    uint8_t state[16] = {0};
    for (int32_t i = 0; i < len; i++)
        state[i & 0xF] ^= bytes[i];
    rng->a = state[0] + ((uint32_t) state[1] << 8) + ((uint32_t) state[2] << 16) + ((uint32_t) state[3] << 24);
    rng->b = state[4] + ((uint32_t) state[5] << 8) + ((uint32_t) state[6] << 16) + ((uint32_t) state[7] << 24);
    rng->c = state[8] + ((uint32_t) state[9] << 8) + ((uint32_t) state[10] << 16) + ((uint32_t) state[11] << 24);
    rng->d = state[12] + ((uint32_t) state[13] << 8) + ((uint32_t) state[14] << 16) + ((uint32_t) state[15] << 24);
    rng->counter = 0u;
    /* a, b, c, d can't all be 0 */
    if (rng->a == 0) rng->a = 1u;
    for (int i = 0; i < 16; i++) janet_rng_u32(rng);
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
    return ldexp((double)(big >> (64 - 52)), -52);
}

JANET_CORE_FN(cfun_rng_make,
              "(math/rng &opt seed)",
              "Creates a Pseudo-Random number generator, with an optional seed. "
              "The seed should be an unsigned 32 bit integer or a buffer. "
              "Do not use this for cryptography. Returns a core/rng abstract type."
             ) {
    janet_arity(argc, 0, 1);
    JanetRNG *rng = janet_abstract(&janet_rng_type, sizeof(JanetRNG));
    if (argc == 1) {
        if (janet_checkint(argv[0])) {
            uint32_t seed = (uint32_t)(janet_getinteger(argv, 0));
            janet_rng_seed(rng, seed);
        } else {
            JanetByteView bytes = janet_getbytes(argv, 0);
            janet_rng_longseed(rng, bytes.bytes, bytes.len);
        }
    } else {
        janet_rng_seed(rng, 0);
    }
    return janet_wrap_abstract(rng);
}

JANET_CORE_FN(cfun_rng_uniform,
              "(math/rng-uniform rng)",
              "Extract a random number in the range [0, 1) from the RNG."
             ) {
    janet_fixarity(argc, 1);
    JanetRNG *rng = janet_getabstract(argv, 0, &janet_rng_type);
    return janet_wrap_number(janet_rng_double(rng));
}

JANET_CORE_FN(cfun_rng_int,
              "(math/rng-int rng &opt max)",
              "Extract a random integer in the range [0, max) for max > 0 from the RNG.  "
              "If max is 0, return 0.  If no max is given, the default is 2^31 - 1."
             ) {
    janet_arity(argc, 1, 2);
    JanetRNG *rng = janet_getabstract(argv, 0, &janet_rng_type);
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

static void rng_get_4bytes(JanetRNG *rng, uint8_t *buf) {
    uint32_t word = janet_rng_u32(rng);
    buf[0] = word & 0xFF;
    buf[1] = (word >> 8) & 0xFF;
    buf[2] = (word >> 16) & 0xFF;
    buf[3] = (word >> 24) & 0xFF;
}

JANET_CORE_FN(cfun_rng_buffer,
              "(math/rng-buffer rng n &opt buf)",
              "Get n random bytes and put them in a buffer. Creates a new buffer if no buffer is "
              "provided, otherwise appends to the given buffer. Returns the buffer."
             ) {
    janet_arity(argc, 2, 3);
    JanetRNG *rng = janet_getabstract(argv, 0, &janet_rng_type);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, n);

    /* Split into first part (that is divisible by 4), and rest */
    int32_t first_part = n & ~3;
    int32_t second_part = n - first_part;

    /* Get first part in chunks of 4 bytes */
    janet_buffer_extra(buffer, n);
    uint8_t *buf = buffer->data + buffer->count;
    for (int32_t i = 0; i < first_part; i += 4) rng_get_4bytes(rng, buf + i);
    buffer->count += first_part;

    /* Get remaining 0 - 3 bytes */
    if (second_part) {
        uint8_t wordbuf[4] = {0};
        rng_get_4bytes(rng, wordbuf);
        janet_buffer_push_bytes(buffer, wordbuf, second_part);
    }

    return janet_wrap_buffer(buffer);
}

static const JanetMethod rng_methods[] = {
    {"uniform", cfun_rng_uniform},
    {"int", cfun_rng_int},
    {"buffer", cfun_rng_buffer},
    {NULL, NULL}
};

static int janet_rng_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), rng_methods, out);
}

static Janet janet_rng_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(rng_methods, key);
}

/* Get a random number */
JANET_CORE_FN(janet_rand,
              "(math/random)",
              "Returns a uniformly distributed random number between 0 and 1.") {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_number(janet_rng_double(&janet_vm.rng));
}

/* Seed the random number generator */
JANET_CORE_FN(janet_srand,
              "(math/seedrandom seed)",
              "Set the seed for the random number generator. `seed` should be "
              "an integer or a buffer."
             ) {
    janet_fixarity(argc, 1);
    if (janet_checkint(argv[0])) {
        uint32_t seed = (uint32_t)(janet_getinteger(argv, 0));
        janet_rng_seed(&janet_vm.rng, seed);
    } else {
        JanetByteView bytes = janet_getbytes(argv, 0);
        janet_rng_longseed(&janet_vm.rng, bytes.bytes, bytes.len);
    }
    return janet_wrap_nil();
}

#define JANET_DEFINE_NAMED_MATHOP(janet_name, fop, doc)\
JANET_CORE_FN(janet_##fop, "(math/" janet_name " x)", doc) {\
    janet_fixarity(argc, 1); \
    double x = janet_getnumber(argv, 0); \
    return janet_wrap_number(fop(x)); \
}

#define JANET_DEFINE_MATHOP(fop, doc) JANET_DEFINE_NAMED_MATHOP(#fop, fop, doc)

JANET_DEFINE_MATHOP(acos, "Returns the arccosine of x.")
JANET_DEFINE_MATHOP(asin, "Returns the arcsin of x.")
JANET_DEFINE_MATHOP(atan, "Returns the arctangent of x.")
JANET_DEFINE_MATHOP(cos, "Returns the cosine of x.")
JANET_DEFINE_MATHOP(cosh, "Returns the hyperbolic cosine of x.")
JANET_DEFINE_MATHOP(acosh, "Returns the hyperbolic arccosine of x.")
JANET_DEFINE_MATHOP(sin, "Returns the sine of x.")
JANET_DEFINE_MATHOP(sinh, "Returns the hyperbolic sine of x.")
JANET_DEFINE_MATHOP(asinh, "Returns the hyperbolic arcsine of x.")
JANET_DEFINE_MATHOP(tan, "Returns the tangent of x.")
JANET_DEFINE_MATHOP(tanh, "Returns the hyperbolic tangent of x.")
JANET_DEFINE_MATHOP(atanh, "Returns the hyperbolic arctangent of x.")
JANET_DEFINE_MATHOP(exp, "Returns e to the power of x.")
JANET_DEFINE_MATHOP(exp2, "Returns 2 to the power of x.")
JANET_DEFINE_MATHOP(expm1, "Returns e to the power of x minus 1.")
JANET_DEFINE_MATHOP(log, "Returns the natural logarithm of x.")
JANET_DEFINE_MATHOP(log10, "Returns the log base 10 of x.")
JANET_DEFINE_MATHOP(log2, "Returns the log base 2 of x.")
JANET_DEFINE_MATHOP(sqrt, "Returns the square root of x.")
JANET_DEFINE_MATHOP(cbrt, "Returns the cube root of x.")
JANET_DEFINE_MATHOP(ceil, "Returns the smallest integer value number that is not less than x.")
JANET_DEFINE_MATHOP(floor, "Returns the largest integer value number that is not greater than x.")
JANET_DEFINE_MATHOP(trunc, "Returns the integer between x and 0 nearest to x.")
JANET_DEFINE_MATHOP(round, "Returns the integer nearest to x.")
JANET_DEFINE_MATHOP(log1p, "Returns (log base e of x) + 1 more accurately than (+ (math/log x) 1)")
JANET_DEFINE_MATHOP(erf, "Returns the error function of x.")
JANET_DEFINE_MATHOP(erfc, "Returns the complementary error function of x.")
JANET_DEFINE_NAMED_MATHOP("log-gamma", lgamma, "Returns log-gamma(x).")
JANET_DEFINE_NAMED_MATHOP("abs", fabs, "Return the absolute value of x.")
JANET_DEFINE_NAMED_MATHOP("gamma", tgamma, "Returns gamma(x).")

#define JANET_DEFINE_MATH2OP(name, fop, signature, doc)\
JANET_CORE_FN(janet_##name, signature, doc) {\
    janet_fixarity(argc, 2); \
    double lhs = janet_getnumber(argv, 0); \
    double rhs = janet_getnumber(argv, 1); \
    return janet_wrap_number(fop(lhs, rhs)); \
}

JANET_DEFINE_MATH2OP(atan2, atan2, "(math/atan2 y x)", "Returns the arctangent of y/x. Works even when x is 0.")
JANET_DEFINE_MATH2OP(pow, pow, "(math/pow a x)", "Returns a to the power of x.")
JANET_DEFINE_MATH2OP(hypot, hypot, "(math/hypot a b)", "Returns c from the equation c^2 = a^2 + b^2.")
JANET_DEFINE_MATH2OP(nextafter, nextafter,  "(math/next x y)", "Returns the next representable floating point value after x in the direction of y.")

JANET_CORE_FN(janet_not, "(not x)", "Returns the boolean inverse of x.") {
    janet_fixarity(argc, 1);
    return janet_wrap_boolean(!janet_truthy(argv[0]));
}

static double janet_gcd(double x, double y) {
    if (isnan(x) || isnan(y)) {
#ifdef NAN
        return NAN;
#else
        return 0.0 / 0.0;
#endif
    }
    if (isinf(x) || isinf(y)) return INFINITY;
    while (y != 0) {
        double temp = y;
        y = fmod(x, y);
        x = temp;
    }
    return x;
}

static double janet_lcm(double x, double y) {
    return (x / janet_gcd(x, y)) * y;
}

JANET_CORE_FN(janet_cfun_gcd, "(math/gcd x y)",
              "Returns the greatest common divisor between x and y.") {
    janet_fixarity(argc, 2);
    double x = janet_getnumber(argv, 0);
    double y = janet_getnumber(argv, 1);
    return janet_wrap_number(janet_gcd(x, y));
}

JANET_CORE_FN(janet_cfun_lcm, "(math/lcm x y)",
              "Returns the least common multiple of x and y.") {
    janet_fixarity(argc, 2);
    double x = janet_getnumber(argv, 0);
    double y = janet_getnumber(argv, 1);
    return janet_wrap_number(janet_lcm(x, y));
}

JANET_CORE_FN(janet_cfun_frexp, "(math/frexp x)",
              "Returns a tuple of (mantissa, exponent) from number.") {
    janet_fixarity(argc, 1);
    double x = janet_getnumber(argv, 0);
    int exp;
    x = frexp(x, &exp);
    Janet *result = janet_tuple_begin(2);
    result[0] = janet_wrap_number(x);
    result[1] = janet_wrap_number((double) exp);
    return janet_wrap_tuple(janet_tuple_end(result));
}

JANET_CORE_FN(janet_cfun_ldexp, "(math/ldexp m e)",
              "Creates a new number from a mantissa and an exponent.") {
    janet_fixarity(argc, 2);
    double x = janet_getnumber(argv, 0);
    int32_t y = janet_getinteger(argv, 1);
    return janet_wrap_number(ldexp(x, y));
}

/* Module entry point */
void janet_lib_math(JanetTable *env) {
    JanetRegExt math_cfuns[] = {
        JANET_CORE_REG("not", janet_not),
        JANET_CORE_REG("math/random", janet_rand),
        JANET_CORE_REG("math/seedrandom", janet_srand),
        JANET_CORE_REG("math/cos", janet_cos),
        JANET_CORE_REG("math/sin", janet_sin),
        JANET_CORE_REG("math/tan", janet_tan),
        JANET_CORE_REG("math/acos", janet_acos),
        JANET_CORE_REG("math/asin", janet_asin),
        JANET_CORE_REG("math/atan", janet_atan),
        JANET_CORE_REG("math/exp", janet_exp),
        JANET_CORE_REG("math/log", janet_log),
        JANET_CORE_REG("math/log10", janet_log10),
        JANET_CORE_REG("math/log2", janet_log2),
        JANET_CORE_REG("math/sqrt", janet_sqrt),
        JANET_CORE_REG("math/cbrt", janet_cbrt),
        JANET_CORE_REG("math/floor", janet_floor),
        JANET_CORE_REG("math/ceil", janet_ceil),
        JANET_CORE_REG("math/pow", janet_pow),
        JANET_CORE_REG("math/abs", janet_fabs),
        JANET_CORE_REG("math/sinh", janet_sinh),
        JANET_CORE_REG("math/cosh", janet_cosh),
        JANET_CORE_REG("math/tanh", janet_tanh),
        JANET_CORE_REG("math/atanh", janet_atanh),
        JANET_CORE_REG("math/asinh", janet_asinh),
        JANET_CORE_REG("math/acosh", janet_acosh),
        JANET_CORE_REG("math/atan2", janet_atan2),
        JANET_CORE_REG("math/rng", cfun_rng_make),
        JANET_CORE_REG("math/rng-uniform", cfun_rng_uniform),
        JANET_CORE_REG("math/rng-int", cfun_rng_int),
        JANET_CORE_REG("math/rng-buffer", cfun_rng_buffer),
        JANET_CORE_REG("math/hypot", janet_hypot),
        JANET_CORE_REG("math/exp2", janet_exp2),
        JANET_CORE_REG("math/log1p", janet_log1p),
        JANET_CORE_REG("math/gamma", janet_tgamma),
        JANET_CORE_REG("math/log-gamma", janet_lgamma),
        JANET_CORE_REG("math/erfc", janet_erfc),
        JANET_CORE_REG("math/erf", janet_erf),
        JANET_CORE_REG("math/expm1", janet_expm1),
        JANET_CORE_REG("math/trunc", janet_trunc),
        JANET_CORE_REG("math/round", janet_round),
        JANET_CORE_REG("math/next", janet_nextafter),
        JANET_CORE_REG("math/gcd", janet_cfun_gcd),
        JANET_CORE_REG("math/lcm", janet_cfun_lcm),
        JANET_CORE_REG("math/frexp", janet_cfun_frexp),
        JANET_CORE_REG("math/ldexp", janet_cfun_ldexp),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, math_cfuns);
    janet_register_abstract_type(&janet_rng_type);
#ifdef JANET_BOOTSTRAP
    JANET_CORE_DEF(env, "math/pi", janet_wrap_number(3.1415926535897931),
                   "The value pi.");
    JANET_CORE_DEF(env, "math/e", janet_wrap_number(2.7182818284590451),
                   "The base of the natural log.");
    JANET_CORE_DEF(env, "math/inf", janet_wrap_number(INFINITY),
                   "The number representing positive infinity");
    JANET_CORE_DEF(env, "math/-inf", janet_wrap_number(-INFINITY),
                   "The number representing negative infinity");
    JANET_CORE_DEF(env, "math/int32-min", janet_wrap_number(INT32_MIN),
                   "The minimum contiguous integer representable by a 32 bit signed integer");
    JANET_CORE_DEF(env, "math/int32-max", janet_wrap_number(INT32_MAX),
                   "The maximum contiguous integer representable by a 32 bit signed integer");
    JANET_CORE_DEF(env, "math/int-min", janet_wrap_number(JANET_INTMIN_DOUBLE),
                   "The minimum contiguous integer representable by a double (2^53)");
    JANET_CORE_DEF(env, "math/int-max", janet_wrap_number(JANET_INTMAX_DOUBLE),
                   "The maximum contiguous integer representable by a double (-(2^53))");
#ifdef NAN
    JANET_CORE_DEF(env, "math/nan", janet_wrap_number(NAN), "Not a number (IEEE-754 NaN)");
#else
    JANET_CORE_DEF(env, "math/nan", janet_wrap_number(0.0 / 0.0), "Not a number (IEEE-754 NaN)");
#endif
#endif
}

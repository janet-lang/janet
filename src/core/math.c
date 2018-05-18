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
#include <dst/dstcorelib.h>
#include <math.h>

/* Get a random number */
int dst_rand(DstArgs args) {
    DST_FIXARITY(args, 0);
    double r = (rand() % RAND_MAX) / ((double) RAND_MAX);
    DST_RETURN_REAL(args, r);
}

/* Seed the random number generator */
int dst_srand(DstArgs args) {
    int32_t x = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(x, args, 0);
    srand((unsigned) x);
    return 0;
}

/* Convert a number to an integer */
int dst_int(DstArgs args) {
    DST_FIXARITY(args, 1);
    switch (dst_type(args.v[0])) {
        default:
            DST_THROW(args, "could not convert to integer");
        case DST_REAL:
            *args.ret = dst_wrap_integer((int32_t) dst_unwrap_real(args.v[0]));
            break;
        case DST_INTEGER:
            *args.ret = args.v[0];
            break;
    }
    return 0;
}

/* Convert a number to a real number */
int dst_real(DstArgs args) {
    DST_FIXARITY(args, 1);
    switch (dst_type(args.v[0])) {
        default:
            DST_THROW(args, "could not convert to real");
        case DST_REAL:
            *args.ret = args.v[0];
            break;
        case DST_INTEGER:
            *args.ret = dst_wrap_real((double) dst_unwrap_integer(args.v[0]));
            break;
    }
    return 0;
}

#define ADD(x, y) ((x) + (y))
#define SUB(x, y) ((x) - (y))
#define MUL(x, y) ((x) * (y))
#define MOD(x, y) ((x) % (y))
#define DIV(x, y) ((x) / (y))

#define DST_DEFINE_BINOP(name, op, rop, onerr)\
Dst dst_op_##name(Dst lhs, Dst rhs) {\
    if (!(dst_checktype(lhs, DST_INTEGER) || dst_checktype(lhs, DST_REAL))) onerr;\
    if (!(dst_checktype(rhs, DST_INTEGER) || dst_checktype(rhs, DST_REAL))) onerr;\
    return dst_checktype(lhs, DST_INTEGER)\
        ? (dst_checktype(rhs, DST_INTEGER)\
            ? dst_wrap_integer(op(dst_unwrap_integer(lhs), dst_unwrap_integer(rhs)))\
            : dst_wrap_real(rop((double)dst_unwrap_integer(lhs), dst_unwrap_real(rhs))))\
        : (dst_checktype(rhs, DST_INTEGER)\
            ? dst_wrap_real(rop(dst_unwrap_real(lhs), (double)dst_unwrap_integer(rhs)))\
            : dst_wrap_real(rop(dst_unwrap_real(lhs), dst_unwrap_real(rhs))));\
}

DST_DEFINE_BINOP(add, ADD, ADD, return dst_wrap_nil())
DST_DEFINE_BINOP(subtract, SUB, SUB, return dst_wrap_nil())
DST_DEFINE_BINOP(multiply, MUL, MUL, return dst_wrap_nil())

#define DST_DEFINE_DIVIDER_OP(name, op, rop)\
Dst dst_op_##name(Dst lhs, Dst rhs) {\
    if (!(dst_checktype(lhs, DST_INTEGER) || dst_checktype(lhs, DST_REAL))) return dst_wrap_nil();\
    if (!(dst_checktype(rhs, DST_INTEGER) || dst_checktype(rhs, DST_REAL))) return dst_wrap_nil();\
    return dst_checktype(lhs, DST_INTEGER)\
        ? (dst_checktype(rhs, DST_INTEGER)\
            ? (dst_unwrap_integer(rhs) == 0 || ((dst_unwrap_integer(lhs) == INT32_MIN) && (dst_unwrap_integer(rhs) == -1)))\
                ? dst_wrap_nil()\
                : dst_wrap_integer(op(dst_unwrap_integer(lhs), dst_unwrap_integer(rhs)))\
            : dst_wrap_real(rop((double)dst_unwrap_integer(lhs), dst_unwrap_real(rhs))))\
        : (dst_checktype(rhs, DST_INTEGER)\
            ? dst_wrap_real(rop(dst_unwrap_real(lhs), (double)dst_unwrap_integer(rhs)))\
            : dst_wrap_real(rop(dst_unwrap_real(lhs), dst_unwrap_real(rhs))));\
}

DST_DEFINE_DIVIDER_OP(divide, DIV, DIV)
DST_DEFINE_DIVIDER_OP(modulo, MOD, fmod)

#define DST_DEFINE_REDUCER(name, fop, start)\
int dst_##name(DstArgs args) {\
    int32_t i;\
    Dst accum = dst_wrap_integer(start);\
    for (i = 0; i < args.n; i++) {\
        accum = fop(accum, args.v[i]);\
    }\
    if (dst_checktype(accum, DST_NIL)) {\
        *args.ret = dst_cstringv("expected number");\
        return 1;\
    }\
    *args.ret = accum;\
    return 0;\
}

DST_DEFINE_REDUCER(add, dst_op_add, 0)
DST_DEFINE_REDUCER(multiply, dst_op_multiply, 1)

#define DST_DEFINE_DIVIDER(name, unarystart)\
int dst_##name(DstArgs args) {\
    int32_t i;\
    Dst accum;\
    if (args.n < 1) {\
        *args.ret = dst_cstringv("expected at least one argument");\
        return 1;\
    } else if (args.n == 1) {\
        accum = unarystart;\
        i = 0;\
    } else {\
        accum = args.v[0];\
        i = 1;\
    }\
    for (; i < args.n; i++) {\
        accum = dst_op_##name(accum, args.v[i]);\
    }\
    if (dst_checktype(accum, DST_NIL)) {\
        *args.ret = dst_cstringv("expected number or division error");\
        return 1;\
    }\
    *args.ret = accum;\
    return 0;\
}

DST_DEFINE_DIVIDER(divide, dst_wrap_real(1))
DST_DEFINE_DIVIDER(modulo, dst_wrap_real(1))
DST_DEFINE_DIVIDER(subtract, dst_wrap_integer(0))

#undef ADD
#undef SUB
#undef MUL
#undef MOD
#undef DST_DEFINE_BINOP

int dst_bnot(DstArgs args) {
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    if (!dst_checktype(args.v[0], DST_INTEGER)) {
        *args.ret = dst_cstringv("expected integer");
        return 1;
    }
    *args.ret = dst_wrap_integer(~dst_unwrap_integer(args.v[0]));
    return 0;
}

#define DST_DEFINE_BITOP(name, op, start)\
int dst_##name(DstArgs args) {\
    int32_t i;\
    int32_t accum = start;\
    for (i = 0; i < args.n; i++) {\
        Dst arg = args.v[i];\
        if (!dst_checktype(arg, DST_INTEGER)) {\
            *args.ret = dst_cstringv("expected integer");\
            return -1;\
        }\
        accum op dst_unwrap_integer(arg);\
    }\
    *args.ret = dst_wrap_integer(accum);\
    return 0;\
}

DST_DEFINE_BITOP(band, &=, -1)
DST_DEFINE_BITOP(bor, |=, 0)
DST_DEFINE_BITOP(bxor, ^=, 0)

int dst_lshift(DstArgs args) {
    int32_t lhs, rhs;
    DST_FIXARITY(args, 2);
    DST_ARG_INTEGER(lhs, args, 0);
    DST_ARG_INTEGER(rhs, args, 1);
    DST_RETURN_INTEGER(args, lhs >> rhs);
}

int dst_rshift(DstArgs args) {
    int32_t lhs, rhs;
    DST_FIXARITY(args, 2);
    DST_ARG_INTEGER(lhs, args, 0);
    DST_ARG_INTEGER(rhs, args, 1);
    DST_RETURN_INTEGER(args, lhs << rhs);
}

int dst_lshiftu(DstArgs args) {
    int32_t lhs, rhs;
    DST_FIXARITY(args, 2);
    DST_ARG_INTEGER(lhs, args, 0);
    DST_ARG_INTEGER(rhs, args, 1);
    DST_RETURN_INTEGER(args, (int32_t)((uint32_t)lhs << rhs));
}

#define DST_DEFINE_MATHOP(name, fop)\
int dst_##name(DstArgs args) {\
    double x;\
    DST_FIXARITY(args, 1);\
    DST_ARG_NUMBER(x, args, 0);\
    DST_RETURN_REAL(args, fop(x));\
}

DST_DEFINE_MATHOP(acos, acos)
DST_DEFINE_MATHOP(asin, asin)
DST_DEFINE_MATHOP(atan, atan)
DST_DEFINE_MATHOP(cos, cos)
DST_DEFINE_MATHOP(cosh, cosh)
DST_DEFINE_MATHOP(sin, sin)
DST_DEFINE_MATHOP(sinh, sinh)
DST_DEFINE_MATHOP(tan, tan)
DST_DEFINE_MATHOP(tanh, tanh)
DST_DEFINE_MATHOP(exp, exp)
DST_DEFINE_MATHOP(log, log)
DST_DEFINE_MATHOP(log10, log10)
DST_DEFINE_MATHOP(sqrt, sqrt)
DST_DEFINE_MATHOP(ceil, ceil)
DST_DEFINE_MATHOP(fabs, fabs)
DST_DEFINE_MATHOP(floor, floor)

#define DST_DEFINE_MATH2OP(name, fop)\
int dst_##name(DstArgs args) {\
    double lhs, rhs;\
    DST_FIXARITY(args, 2);\
    DST_ARG_NUMBER(lhs, args, 0);\
    DST_ARG_NUMBER(rhs, args, 1);\
    DST_RETURN_REAL(args, fop(lhs, rhs));\
}\

DST_DEFINE_MATH2OP(atan2, atan2)
DST_DEFINE_MATH2OP(pow, pow)
DST_DEFINE_MATH2OP(fmod, fmod)

int dst_modf(DstArgs args) {
    double x, intpart;
    Dst *tup;
    DST_FIXARITY(args, 2);
    DST_ARG_NUMBER(x, args, 0);
    tup = dst_tuple_begin(2);
    tup[0] = dst_wrap_real(modf(x, &intpart));
    tup[1] = dst_wrap_real(intpart);
    DST_RETURN_TUPLE(args, dst_tuple_end(tup));
}

/* Comparison */
#define DST_DEFINE_COMPARATOR(name, pred)\
static int dst_##name(DstArgs args) {\
    int32_t i;\
    for (i = 0; i < args.n - 1; i++) {\
        if (dst_compare(args.v[i], args.v[i+1]) pred) {\
            DST_RETURN_FALSE(args);\
        }\
    }\
    DST_RETURN_TRUE(args);\
}

DST_DEFINE_COMPARATOR(ascending, >= 0)
DST_DEFINE_COMPARATOR(descending, <= 0)
DST_DEFINE_COMPARATOR(notdescending, > 0)
DST_DEFINE_COMPARATOR(notascending, < 0)

/* Boolean logic */
static int dst_strict_equal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (!dst_equals(args.v[i], args.v[i+1])) {
            DST_RETURN(args, dst_wrap_false());
        }
    }
    DST_RETURN(args, dst_wrap_true());
}

static int dst_strict_notequal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (dst_equals(args.v[i], args.v[i+1])) {
            DST_RETURN(args, dst_wrap_false());
        }
    }
    DST_RETURN(args, dst_wrap_true());
}

static int dst_not(DstArgs args) {
    DST_FIXARITY(args, 1);
    DST_RETURN_BOOLEAN(args, !dst_truthy(args.v[0]));
}

#define DEF_NUMERIC_COMP(name, op) \
int dst_numeric_##name(DstArgs args) { \
    int32_t i; \
    for (i = 1; i < args.n; i++) { \
        double x = 0, y = 0; \
        DST_ARG_NUMBER(x, args, i-1);\
        DST_ARG_NUMBER(y, args, i);\
        if (!(x op y)) { \
            DST_RETURN(args, dst_wrap_false()); \
        } \
    } \
    DST_RETURN(args, dst_wrap_true()); \
}

DEF_NUMERIC_COMP(gt, >)
DEF_NUMERIC_COMP(lt, <)
DEF_NUMERIC_COMP(lte, <=)
DEF_NUMERIC_COMP(gte, >=)
DEF_NUMERIC_COMP(eq, ==)
DEF_NUMERIC_COMP(neq, !=)

static const DstReg cfuns[] = {
    {"random", dst_rand},
    {"seedrandom", dst_srand},
    {"int", dst_int},
    {"real", dst_real},
    {"+", dst_add},
    {"-", dst_subtract},
    {"*", dst_multiply},
    {"/", dst_divide},
    {"%", dst_modulo},
    {"=", dst_strict_equal},
    {"not=", dst_strict_notequal},
    {"order<", dst_ascending},
    {"order>", dst_descending},
    {"order<=", dst_notdescending},
    {"order>=", dst_notascending},
    {"==", dst_numeric_eq},
    {"not==", dst_numeric_neq},
    {"<", dst_numeric_lt},
    {">", dst_numeric_gt},
    {"<=", dst_numeric_lte},
    {">=", dst_numeric_gte},
    {"|", dst_bor},
    {"&", dst_band},
    {"^", dst_bxor},
    {"~", dst_bnot},
    {">>", dst_lshift},
    {"<<", dst_rshift},
    {">>>", dst_lshiftu},
    {"not", dst_not},
    {"cos", dst_cos},
    {"sin", dst_sin},
    {"tan", dst_tan},
    {"acos", dst_acos},
    {"asin", dst_asin},
    {"atan", dst_atan},
    {"exp", dst_exp},
    {"log", dst_log},
    {"log10", dst_log10},
    {"sqrt", dst_sqrt},
    {"floor", dst_floor},
    {"ceil", dst_ceil},
    {"pow", dst_pow},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_math(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);

    dst_env_def(env, "pi", dst_wrap_real(3.1415926535897931));
    dst_env_def(env, "e", dst_wrap_real(2.7182818284590451));
    dst_env_def(env, "inf", dst_wrap_real(INFINITY));
    return 0;
}

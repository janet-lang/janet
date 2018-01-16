/*
* Copyright (c) 2017 Calvin Rose
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
#include <math.h>

/* Convert a number to an integer */
int dst_int(DstArgs args) {
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    switch (dst_type(args.v[0])) {
        default:
            *args.ret = dst_cstringv("could not convert to integer");
            return 1;
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
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    switch (dst_type(args.v[0])) {
        default:
            *args.ret = dst_cstringv("could not convert to real");
            return 1;
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
    if (args.n != 2 || !dst_checktype(args.v[0], DST_INTEGER) || !dst_checktype(args.v[1], DST_INTEGER)) {
        *args.ret = dst_cstringv("expected 2 integers"); 
        return 1;
    }
    *args.ret = dst_wrap_integer(dst_unwrap_integer(args.v[0]) >> dst_unwrap_integer(args.v[1]));
    return 0;
}

int dst_rshift(DstArgs args) {
    if (args.n != 2 || !dst_checktype(args.v[0], DST_INTEGER) || !dst_checktype(args.v[1], DST_INTEGER)) {
        *args.ret = dst_cstringv("expected 2 integers"); 
        return 1;
    }
    *args.ret = dst_wrap_integer(dst_unwrap_integer(args.v[0]) << dst_unwrap_integer(args.v[1]));
    return 0;
}

int dst_lshiftu(DstArgs args) {
    if (args.n != 2 || !dst_checktype(args.v[0], DST_INTEGER) || !dst_checktype(args.v[1], DST_INTEGER)) {
        *args.ret = dst_cstringv("expected 2 integers"); 
        return 1;
    }
    *args.ret = dst_wrap_integer((int32_t)((uint32_t)dst_unwrap_integer(args.v[0]) >> dst_unwrap_integer(args.v[1])));
    return 0;
}

#define DST_DEFINE_MATHOP(name, fop)\
int dst_##name(DstArgs args) {\
    if (args.n != 1) {\
        *args.ret = dst_cstringv("expected 1 argument");\
        return 1;\
    }\
    if (dst_checktype(args.v[0], DST_INTEGER)) {\
        args.v[0] = dst_wrap_real(dst_unwrap_integer(args.v[0]));\
    }\
    if (!dst_checktype(args.v[0], DST_REAL)) {\
        *args.ret = dst_cstringv("expected number");\
        return 1;\
    }\
    *args.ret = dst_wrap_real(fop(dst_unwrap_real(args.v[0])));\
    return 0;\
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
    if (args.n != 2) {\
        *args.ret = dst_cstringv("expected 2 arguments");\
        return 1;\
    }\
    if (dst_checktype(args.v[0], DST_INTEGER))\
        args.v[0] = dst_wrap_real(dst_unwrap_integer(args.v[0]));\
    if (dst_checktype(args.v[1], DST_INTEGER))\
        args.v[1] = dst_wrap_real(dst_unwrap_integer(args.v[1]));\
    if (!dst_checktype(args.v[0], DST_REAL) || !dst_checktype(args.v[1], DST_REAL)) {\
        *args.ret = dst_cstringv("expected real");\
        return 1;\
    }\
    *args.ret =\
        dst_wrap_real(fop(dst_unwrap_real(args.v[0]), dst_unwrap_real(args.v[1])));\
    return 0;\
}\

DST_DEFINE_MATH2OP(atan2, atan2)
DST_DEFINE_MATH2OP(pow, pow)
DST_DEFINE_MATH2OP(fmod, fmod)

int dst_modf(DstArgs args) {
    double intpart;
    Dst *tup;
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    if (dst_checktype(args.v[0], DST_INTEGER))
        args.v[0] = dst_wrap_real(dst_unwrap_integer(args.v[0]));
    if (!dst_checktype(args.v[0], DST_REAL)) {
        *args.ret = dst_cstringv("expected real");
        return 1;
    }
    tup = dst_tuple_begin(2);
    tup[0] = dst_wrap_real(modf(dst_unwrap_real(args.v[0]), &intpart));
    tup[1] = dst_wrap_real(intpart);
    *args.ret = dst_wrap_tuple(dst_tuple_end(tup));
    return 0;
}

/* Comparison */
#define DST_DEFINE_COMPARATOR(name, pred)\
static int dst_math_##name(DstArgs args) {\
    int32_t i;\
    for (i = 0; i < args.n - 1; i++) {\
        if (dst_compare(args.v[i], args.v[i+1]) pred) {\
            *args.ret = dst_wrap_false();\
            return 0;\
        }\
    }\
    *args.ret = dst_wrap_true();\
    return 0;\
}

DST_DEFINE_COMPARATOR(ascending, >= 0)
DST_DEFINE_COMPARATOR(descending, <= 0)
DST_DEFINE_COMPARATOR(notdescending, > 0)
DST_DEFINE_COMPARATOR(notascending, < 0)

/* Boolean logic */
static int dst_math_equal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (!dst_equals(args.v[i], args.v[i+1])) {
            *args.ret = dst_wrap_false();
            return 0;
        }
    }
    *args.ret = dst_wrap_true();
    return 0;
}

static int dst_math_notequal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (dst_equals(args.v[i], args.v[i+1])) {
            *args.ret = dst_wrap_false();
            return 0;
        }
    }
    *args.ret = dst_wrap_true();
    return 0;
}

static int dst_math_not(DstArgs args) {
    *args.ret = dst_wrap_boolean(args.n == 0 || !dst_truthy(args.v[0]));
    return 0;
}

/* CMath entry point */

/* Define the entry point of the library */
#ifdef DST_LIB
#define dst_math_init _dst_init
#endif

/* Module entry point */
int dst_math_init(DstArgs args) {
    DstTable *module = dst_get_module(args);
    dst_module_def(module, "int", dst_wrap_cfunction(dst_int));
    dst_module_def(module, "real", dst_wrap_cfunction(dst_real));
    dst_module_def(module, "+", dst_wrap_cfunction(dst_add));
    dst_module_def(module, "-", dst_wrap_cfunction(dst_subtract));
    dst_module_def(module, "*", dst_wrap_cfunction(dst_multiply));
    dst_module_def(module, "/", dst_wrap_cfunction(dst_divide));
    dst_module_def(module, "%", dst_wrap_cfunction(dst_modulo));
    dst_module_def(module, "=", dst_wrap_cfunction(dst_math_equal));
    dst_module_def(module, "not=", dst_wrap_cfunction(dst_math_notequal));
    dst_module_def(module, "<", dst_wrap_cfunction(dst_math_ascending));
    dst_module_def(module, ">", dst_wrap_cfunction(dst_math_descending));
    dst_module_def(module, "<=", dst_wrap_cfunction(dst_math_notdescending));
    dst_module_def(module, ">=", dst_wrap_cfunction(dst_math_notascending));
    dst_module_def(module, "|", dst_wrap_cfunction(dst_bor));
    dst_module_def(module, "&", dst_wrap_cfunction(dst_band));
    dst_module_def(module, "^", dst_wrap_cfunction(dst_bxor));
    dst_module_def(module, "~", dst_wrap_cfunction(dst_bnot));
    dst_module_def(module, ">>", dst_wrap_cfunction(dst_lshift));
    dst_module_def(module, "<<", dst_wrap_cfunction(dst_rshift));
    dst_module_def(module, ">>>", dst_wrap_cfunction(dst_lshiftu));
    dst_module_def(module, "not", dst_wrap_cfunction(dst_math_not));
    dst_module_def(module, "cos", dst_wrap_cfunction(dst_cos));
    dst_module_def(module, "sin", dst_wrap_cfunction(dst_sin));
    dst_module_def(module, "tan", dst_wrap_cfunction(dst_tan));
    dst_module_def(module, "acos", dst_wrap_cfunction(dst_acos));
    dst_module_def(module, "asin", dst_wrap_cfunction(dst_asin));
    dst_module_def(module, "atan", dst_wrap_cfunction(dst_atan));
    dst_module_def(module, "exp", dst_wrap_cfunction(dst_exp));
    dst_module_def(module, "log", dst_wrap_cfunction(dst_log));
    dst_module_def(module, "log10", dst_wrap_cfunction(dst_log10));
    dst_module_def(module, "sqrt", dst_wrap_cfunction(dst_sqrt));
    dst_module_def(module, "floor", dst_wrap_cfunction(dst_floor));
    dst_module_def(module, "ceil", dst_wrap_cfunction(dst_ceil));
    dst_module_def(module, "pow", dst_wrap_cfunction(dst_pow));
    dst_module_def(module, "pi", dst_wrap_real(M_PI));
    dst_module_def(module, "\xCF\x80", dst_wrap_real(M_PI));
    dst_module_def(module, "e", dst_wrap_real(M_E));
    return 0;
}

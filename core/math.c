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
int dst_int(int32_t argn, DstValue *argv, DstValue *ret) {
    if (argn != 1) {
        *ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    switch (dst_type(argv[0])) {
        default:
            *ret = dst_cstringv("could not convert to integer");
            return 1;
        case DST_REAL:
            *ret = dst_wrap_integer((int32_t) dst_unwrap_real(argv[0]));
            break;
        case DST_INTEGER:
            *ret = argv[0];
            break;
    }
    return 0;
}

/* Convert a number to a real number */
int dst_real(int32_t argn, DstValue *argv, DstValue *ret) {
    if (argn != 1) {
        *ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    switch (dst_type(argv[0])) {
        default:
            *ret = dst_cstringv("could not convert to real");
            return 1;
        case DST_REAL:
            *ret = argv[0];
            break;
        case DST_INTEGER:
            *ret = dst_wrap_real((double) dst_unwrap_integer(argv[0]));
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
DstValue dst_op_##name(DstValue lhs, DstValue rhs) {\
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
DstValue dst_op_##name(DstValue lhs, DstValue rhs) {\
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
int dst_##name(int32_t argn, DstValue *argv, DstValue *ret) {\
    int32_t i;\
    DstValue accum = dst_wrap_integer(start);\
    for (i = 0; i < argn; i++) {\
        accum = fop(accum, argv[i]);\
    }\
    if (dst_checktype(accum, DST_NIL)) {\
        *ret = dst_cstringv("expected number");\
        return 1;\
    }\
    *ret = accum;\
    return 0;\
}

DST_DEFINE_REDUCER(add, dst_op_add, 0)
DST_DEFINE_REDUCER(subtract, dst_op_subtract, 0)
DST_DEFINE_REDUCER(multiply, dst_op_multiply, 1)

#define DST_DEFINE_DIVIDER(name)\
int dst_##name(int32_t argn, DstValue *argv, DstValue *ret) {\
    int32_t i;\
    DstValue accum;\
    if (argn < 1) {\
        *ret = dst_cstringv("expected at least one argument");\
        return 1;\
    } else if (argn == 1) {\
        accum = dst_wrap_real(1);\
        i = 0;\
    } else {\
        accum = argv[0];\
        i = 1;\
    }\
    for (; i < argn; i++) {\
        accum = dst_op_##name(accum, argv[i]);\
    }\
    if (dst_checktype(accum, DST_NIL)) {\
        *ret = dst_cstringv("expected number or division error");\
        return 1;\
    }\
    *ret = accum;\
    return 0;\
}

DST_DEFINE_DIVIDER(divide)
DST_DEFINE_DIVIDER(modulo)

#undef ADD
#undef SUB
#undef MUL
#undef MOD
#undef DST_DEFINE_BINOP

int dst_bnot(int32_t argn, DstValue *argv, DstValue *ret) {
    if (argn != 1) {
        *ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    if (!dst_checktype(argv[0], DST_INTEGER)) {
        *ret = dst_cstringv("expected integer");
        return 1;
    }
    *ret = dst_wrap_integer(~dst_unwrap_integer(argv[0]));
    return 0;
}

#define DST_DEFINE_BITOP(name, op, start)\
int dst_##name(int32_t argn, DstValue *argv, DstValue *ret) {\
    int32_t i;\
    int32_t accum = start;\
    for (i = 0; i < argn; i++) {\
        DstValue arg = argv[i];\
        if (!dst_checktype(arg, DST_INTEGER)) {\
            *ret = dst_cstringv("expected integer");\
            return -1;\
        }\
        accum op dst_unwrap_integer(arg);\
    }\
    *ret = dst_wrap_integer(accum);\
    return 0;\
}

DST_DEFINE_BITOP(band, &=, -1)
DST_DEFINE_BITOP(bor, |=, 0)
DST_DEFINE_BITOP(bxor, ^=, 0)

#define DST_DEFINE_MATHOP(name, fop)\
int dst_##name(int32_t argn, DstValue *argv, DstValue *ret) {\
    if (argn != 1) {\
        *ret = dst_cstringv("expected 1 argument");\
        return 1;\
    }\
    if (dst_checktype(argv[0], DST_INTEGER)) {\
        argv[0] = dst_wrap_real(dst_unwrap_integer(argv[0]));\
    }\
    if (!dst_checktype(argv[0], DST_REAL)) {\
        *ret = dst_cstringv("expected number");\
        return 1;\
    }\
    *ret = dst_wrap_real(fop(dst_unwrap_real(argv[0])));\
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
int dst_##name(int32_t argn, DstValue *argv, DstValue *ret) {\
    if (argn != 2) {\
        *ret = dst_cstringv("expected 2 arguments");\
        return 1;\
    }\
    if (dst_checktype(argv[0], DST_INTEGER))\
        argv[0] = dst_wrap_real(dst_unwrap_integer(argv[0]));\
    if (dst_checktype(argv[1], DST_INTEGER))\
        argv[1] = dst_wrap_real(dst_unwrap_integer(argv[1]));\
    if (!dst_checktype(argv[0], DST_REAL) || !dst_checktype(argv[1], DST_REAL)) {\
        *ret = dst_cstringv("expected real");\
        return 1;\
    }\
    *ret =\
        dst_wrap_real(fop(dst_unwrap_real(argv[0]), dst_unwrap_real(argv[1])));\
    return 0;\
}\

DST_DEFINE_MATH2OP(atan2, atan2)
DST_DEFINE_MATH2OP(pow, pow)
DST_DEFINE_MATH2OP(fmod, fmod)

int dst_modf(int32_t argn, DstValue *argv, DstValue *ret) {
    double intpart;
    DstValue *tup;
    if (argn != 1) {
        *ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    if (dst_checktype(argv[0], DST_INTEGER))
        argv[0] = dst_wrap_real(dst_unwrap_integer(argv[0]));
    if (!dst_checktype(argv[0], DST_REAL)) {
        *ret = dst_cstringv("expected real");
        return 1;
    }
    tup = dst_tuple_begin(2);
    tup[0] = dst_wrap_real(modf(dst_unwrap_real(argv[0]), &intpart));
    tup[1] = dst_wrap_real(intpart);
    *ret = dst_wrap_tuple(dst_tuple_end(tup));
    return 0;
}

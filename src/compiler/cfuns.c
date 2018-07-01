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
#include <dst/dstcorelib.h>
#include "compile.h"
#define DST_V_NODEF_GROW
#include <headerlibs/vector.h>
#undef DST_V_NODEF_GROW
#include "emit.h"

static int fixarity0(DstFopts opts, DstSlot *args) {
    (void) opts;
    return dst_v_count(args) == 0;
}
static int fixarity1(DstFopts opts, DstSlot *args) {
    (void) opts;
    return dst_v_count(args) == 1;
}
static int fixarity2(DstFopts opts, DstSlot *args) {
    (void) opts;
    return dst_v_count(args) == 2;
}

/* Emit a series of instructions instead of a function call to a math op */
static DstSlot opreduce(
        DstFopts opts,
        DstSlot *args,
        int op,
        Dst zeroArity,
        DstSlot (*unary)(DstFopts opts, DstSlot s)) {
    DstCompiler *c = opts.compiler;
    int32_t i, len;
    len = dst_v_count(args);
    DstSlot t;
    if (len == 0) {
        return dstc_cslot(zeroArity);
    } else if (len == 1) {
        if (unary)
            return unary(opts, args[0]);
        return args[0];
    }
    t = dstc_gettarget(opts);
    /* Compile initial two arguments */
    int32_t lhs = dstc_regnear(c, args[0], DSTC_REGTEMP_0);
    int32_t rhs = dstc_regnear(c, args[1], DSTC_REGTEMP_1);
    dstc_emit(c, op | (t.index << 8) | (lhs << 16) | (rhs << 24));
    dstc_free_reg(c, args[0], lhs);
    dstc_free_reg(c, args[1], rhs);
    /* Don't release t */
    /* Compile the rest of the arguments */
    for (i = 2; i < len; i++) {
        rhs = dstc_regnear(c, args[i], DSTC_REGTEMP_0);
        dstc_emit(c, op | (t.index << 8) | (t.index << 16) | (rhs << 24));
        dstc_free_reg(c, args[i], rhs);
    }
    return t;
}

/* Generic hanldling for $A = B op $C */
static DstSlot genericSSS(DstFopts opts, int op, Dst leftval, DstSlot s) {
    DstSlot target = dstc_gettarget(opts);
    DstSlot zero = dstc_cslot(leftval);
    int32_t lhs = dstc_regnear(opts.compiler, zero, DSTC_REGTEMP_0);
    int32_t rhs = dstc_regnear(opts.compiler, s, DSTC_REGTEMP_1);
    dstc_emit(opts.compiler, op |
            (target.index << 8) | 
            (lhs << 16) |
            (rhs << 24));
    dstc_free_reg(opts.compiler, zero, lhs);
    dstc_free_reg(opts.compiler, s, rhs);
    return target;
}

/* Generic hanldling for $A = op $B */
static DstSlot genericSS(DstFopts opts, int op, DstSlot s) {
    DstSlot target = dstc_gettarget(opts);
    int32_t rhs = dstc_regfar(opts.compiler, s, DSTC_REGTEMP_0);
    dstc_emit(opts.compiler, op |
            (target.index << 8) | 
            (rhs << 16));
    dstc_free_reg(opts.compiler, s, rhs);
    return target;
}

/* Generic hanldling for $A = $B op I */
static DstSlot genericSSI(DstFopts opts, int op, DstSlot s, int32_t imm) {
    DstSlot target = dstc_gettarget(opts);
    int32_t rhs = dstc_regnear(opts.compiler, s, DSTC_REGTEMP_0);
    dstc_emit(opts.compiler, op |
            (target.index << 8) | 
            (rhs << 16) |
            (imm << 24));
    dstc_free_reg(opts.compiler, s, rhs);
    return target;
}

static DstSlot add(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_ADD, dst_wrap_integer(0), NULL);
}

static DstSlot mul(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_MULTIPLY, dst_wrap_integer(1), NULL);
}

static DstSlot subUnary(DstFopts opts, DstSlot onearg) {
    return genericSSS(opts, DOP_SUBTRACT, dst_wrap_integer(0), onearg);
}
static DstSlot sub(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_SUBTRACT, dst_wrap_integer(0), subUnary);
}

static DstSlot divUnary(DstFopts opts, DstSlot onearg) {
    return genericSSS(opts, DOP_DIVIDE, dst_wrap_integer(1), onearg);
}
static DstSlot divide(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_DIVIDE, dst_wrap_integer(1), divUnary);
}

static const DstCFunOptimizer coptimizers[] = {
    {dst_add, NULL, add},
    {dst_subtract, NULL, sub},
    {dst_multiply, NULL, mul},
    {dst_divide, NULL, divide},
};

/* Get a cfunction optimizer. Return NULL if none exists.  */
const DstCFunOptimizer *dstc_cfunopt(DstCFunction cfun) {
    size_t i;
    size_t n = sizeof(coptimizers)/sizeof(DstCFunOptimizer);
    for (i = 0; i < n; i++)
        if (coptimizers[i].cfun == cfun)
            return coptimizers + i;
    return NULL;
}

/* Normal function optimizers */

/* Get, put, etc. */
static DstSlot do_error(DstFopts opts, DstSlot *args) {
    dstc_emit_s(opts.compiler, DOP_ERROR, args[0]);
    return dstc_cslot(dst_wrap_nil());
}
static DstSlot do_debug(DstFopts opts, DstSlot *args) {
    (void)args;
    dstc_emit(opts.compiler, DOP_SIGNAL | (2 << 24));
    return dstc_cslot(dst_wrap_nil());
}
static DstSlot do_get(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_GET, dst_wrap_nil(), NULL);
}
static DstSlot do_put(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_PUT, dst_wrap_nil(), NULL);
}
static DstSlot do_length(DstFopts opts, DstSlot *args) {
    return genericSS(opts, DOP_LENGTH, args[0]);
}
static DstSlot do_yield(DstFopts opts, DstSlot *args) {
    return genericSSI(opts, DOP_SIGNAL, args[0], 3);
}
static DstSlot do_resume(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_RESUME, dst_wrap_nil(), NULL);
}
static DstSlot do_apply1(DstFopts opts, DstSlot *args) {
    /* Push phase */
    int32_t array_reg = dstc_regfar(opts.compiler, args[1], DSTC_REGTEMP_1);
    dstc_emit(opts.compiler, DOP_PUSH_ARRAY | (array_reg << 8));
    dstc_free_reg(opts.compiler, args[1], array_reg);
    /* Call phase */
    int32_t fun_reg = dstc_regnear(opts.compiler, args[0], DSTC_REGTEMP_0);
    DstSlot target;
    if (opts.flags & DST_FOPTS_TAIL) {
        dstc_emit(opts.compiler, DOP_TAILCALL | (fun_reg << 8));
        target = dstc_cslot(dst_wrap_nil());
        target.flags |= DST_SLOT_RETURNED;
    } else {
        target = dstc_gettarget(opts);
        dstc_emit(opts.compiler, DOP_CALL |
                (target.index << 8) | 
                (fun_reg << 16));
    }
    dstc_free_reg(opts.compiler, args[0], fun_reg);
    return target;
}

/* Arranged by tag */
static const DstFunOptimizer optimizers[] = {
    {NULL, NULL},
    {fixarity0, do_debug},
    {fixarity1, do_error},
    {fixarity2, do_apply1},
    {fixarity1, do_yield},
    {fixarity2, do_resume},
    {fixarity2, do_get},
    {fixarity2, do_put},
    {fixarity1, do_length}
};

const DstFunOptimizer *dstc_funopt(uint32_t flags) {
    uint32_t tag = flags & DST_FUNCDEF_FLAG_TAG;
    if (tag == 0 || tag > 8) return NULL;
    return optimizers + tag;
}


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
#include "compile.h"
#include "emit.h"
#include "vector.h"

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

/* Generic hanldling for $A = op $B */
static DstSlot genericSS(DstFopts opts, int op, DstSlot s) {
    DstSlot target = dstc_gettarget(opts);
    dstc_emit_ss(opts.compiler, op, target, s, 1);
    return target;
}

/* Generic hanldling for $A = $B op I */
static DstSlot genericSSI(DstFopts opts, int op, DstSlot s, int32_t imm) {
    DstSlot target = dstc_gettarget(opts);
    dstc_emit_ssi(opts.compiler, op, target, s, imm, 1);
    return target;
}

/* Emit a series of instructions instead of a function call to a math op */
static DstSlot opreduce(
        DstFopts opts,
        DstSlot *args,
        int op,
        Dst nullary) {
    DstCompiler *c = opts.compiler;
    int32_t i, len;
    len = dst_v_count(args);
    DstSlot t;
    if (len == 0) {
        return dstc_cslot(nullary);
    } else if (len == 1) {
        t = dstc_gettarget(opts);
        dstc_emit_sss(c, op, t, dstc_cslot(nullary), args[0], 1);
        return t;
    }
    t = dstc_gettarget(opts);
    dstc_emit_sss(c, op, t, args[0], args[1], 1);
    for (i = 2; i < len; i++)
        dstc_emit_sss(c, op, t, t, args[i], 1);
    return t;
}

/* Function optimizers */

static DstSlot do_error(DstFopts opts, DstSlot *args) {
    dstc_emit_s(opts.compiler, DOP_ERROR, args[0], 0);
    return dstc_cslot(dst_wrap_nil());
}
static DstSlot do_debug(DstFopts opts, DstSlot *args) {
    (void)args;
    dstc_emit(opts.compiler, DOP_SIGNAL | (2 << 24));
    return dstc_cslot(dst_wrap_nil());
}
static DstSlot do_get(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_GET, dst_wrap_nil());
}
static DstSlot do_put(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_PUT, dst_wrap_nil());
}
static DstSlot do_length(DstFopts opts, DstSlot *args) {
    return genericSS(opts, DOP_LENGTH, args[0]);
}
static DstSlot do_yield(DstFopts opts, DstSlot *args) {
    return genericSSI(opts, DOP_SIGNAL, args[0], 3);
}
static DstSlot do_resume(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_RESUME, dst_wrap_nil());
}
static DstSlot do_apply1(DstFopts opts, DstSlot *args) {
    /* Push phase */
    dstc_emit_s(opts.compiler, DOP_PUSH_ARRAY, args[1], 0);
    /* Call phase */
    DstSlot target;
    if (opts.flags & DST_FOPTS_TAIL) {
        dstc_emit_s(opts.compiler, DOP_TAILCALL, args[0], 0);
        target = dstc_cslot(dst_wrap_nil());
        target.flags |= DST_SLOT_RETURNED;
    } else {
        target = dstc_gettarget(opts);
        dstc_emit_ss(opts.compiler, DOP_CALL, target, args[0], 1);
    }
    return target;
}

/* Varidadic operatros specialization */

static DstSlot do_add(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_ADD, dst_wrap_integer(0));
}
static DstSlot do_sub(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_SUBTRACT, dst_wrap_integer(0));
}
static DstSlot do_mul(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_MULTIPLY, dst_wrap_integer(1));
}
static DstSlot do_div(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_DIVIDE, dst_wrap_integer(1));
}
static DstSlot do_band(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_BAND, dst_wrap_integer(-1));
}
static DstSlot do_bor(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_BOR, dst_wrap_integer(0));
}
static DstSlot do_bxor(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_BXOR, dst_wrap_integer(0));
}
static DstSlot do_lshift(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_SHIFT_LEFT, dst_wrap_integer(1));
}
static DstSlot do_rshift(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_SHIFT_RIGHT, dst_wrap_integer(1));
}
static DstSlot do_rshiftu(DstFopts opts, DstSlot *args) {
    return opreduce(opts, args, DOP_SHIFT_RIGHT, dst_wrap_integer(1));
}
static DstSlot do_bnot(DstFopts opts, DstSlot *args) {
    return genericSS(opts, DOP_BNOT, args[0]);
}

/* Arranged by tag */
static const DstFunOptimizer optimizers[] = {
    {fixarity0, do_debug},
    {fixarity1, do_error},
    {fixarity2, do_apply1},
    {fixarity1, do_yield},
    {fixarity2, do_resume},
    {fixarity2, do_get},
    {fixarity2, do_put},
    {fixarity1, do_length},
    {NULL, do_add},
    {NULL, do_sub},
    {NULL, do_mul},
    {NULL, do_div},
    {NULL, do_band},
    {NULL, do_bor},
    {NULL, do_bxor},
    {NULL, do_lshift},
    {NULL, do_rshift},
    {NULL, do_rshiftu},
    {fixarity1, do_bnot}
};

const DstFunOptimizer *dstc_funopt(uint32_t flags) {
    uint32_t tag = flags & DST_FUNCDEF_FLAG_TAG;
    if (tag == 0 || tag >=
            ((sizeof(optimizers)/sizeof(uint32_t) - 1)))
        return NULL;
    return optimizers + tag - 1;
}


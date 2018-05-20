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
#include <headerlibs/vector.h>
#undef DST_V_NODEF_GROW

/* This logic needs to be expanded for more types */

/* Check if a function received only numbers */
static int numbers(DstFopts opts, DstAst *ast, DstSM *args) {
   int32_t i;
   int32_t len = dst_v_count(args);
   (void) opts;
   (void) ast;
   for (i = 0; i < len; i++) {
       DstSlot s = args[i].slot;
       if (s.flags & DST_SLOT_CONSTANT) {
           Dst c = s.constant;
           if (!dst_checktype(c, DST_INTEGER) &&
                !dst_checktype(c, DST_REAL)) {
               /*dstc_cerror(opts.compiler, args[i].map, "expected number");*/
               return 0;
           }
       }
   }
   return 1;
}

/* Fold constants in a DstSM */
static DstSM *foldc(DstSM *sms, DstAst *ast, Dst (*fn)(Dst lhs, Dst rhs)) {
    int32_t ccount;
    int32_t i;
    DstSM *ret = NULL;
    DstSM sm;
    Dst current;
    for (ccount = 0; ccount < dst_v_count(sms); ccount++) {
        if (sms[ccount].slot.flags & DST_SLOT_CONSTANT) continue;
        break;
    }
    if (ccount < 2) return sms;
    current = fn(sms[0].slot.constant, sms[1].slot.constant);
    for (i = 2; i < ccount; i++) {
        Dst nextarg = sms[i].slot.constant;
        current = fn(current, nextarg);
    }
    sm.slot = dstc_cslot(current);
    sm.map = ast;
    dst_v_push(ret, sm);
    for (; i < dst_v_count(sms); i++) {
        dst_v_push(ret, sms[i]);
    }
    return ret;
}

/* Emit a series of instructions instead of a function call to a math op */
static DstSlot opreduce(DstFopts opts, DstAst *ast, DstSM *args, int op) {
    DstCompiler *c = opts.compiler;
    int32_t i, len;
    int32_t op1, op2;
    len = dst_v_count(args);
    DstSlot t;
    if (len == 0) {
        return dstc_cslot(dst_wrap_integer(0));
    } else if (len == 1) {
        return args[0].slot;
    }
    t = dstc_gettarget(opts);
    /* Compile initial two arguments */
    op1 = dstc_preread(c, args[0].map, 0xFF, 1, args[0].slot);
    op2 = dstc_preread(c, args[1].map, 0xFF, 2, args[1].slot);
    dstc_emit(c, ast, (t.index << 8) | (op1 << 16) | (op2 << 24) | op);
    dstc_postread(c, args[0].slot, op1);
    dstc_postread(c, args[1].slot, op2);
    for (i = 2; i < len; i++) {
        op1 = dstc_preread(c, args[i].map, 0xFF, 1, args[i].slot);
        dstc_emit(c, ast, (t.index << 8) | (t.index << 16) | (op1 << 24) | op);
        dstc_postread(c, args[i].slot, op1);
    }
    return t;
}

static DstSlot add(DstFopts opts, DstAst *ast, DstSM *args) {
    DstSM *newargs = foldc(args, ast, dst_op_add);
    DstSlot ret = opreduce(opts, ast, newargs, DOP_ADD);
    if (newargs != args) dstc_freeslots(opts.compiler, newargs);
    return ret;
}

static DstSlot sub(DstFopts opts, DstAst *ast, DstSM *args) {
    DstSM *newargs;
    if (dst_v_count(args) == 1) {
        newargs = NULL;
        dst_v_push(newargs, args[0]);
        dst_v_push(newargs, args[0]);
        newargs[0].slot = dstc_cslot(dst_wrap_integer(0));
        newargs[0].map = ast;
    } else {
        newargs = foldc(args, ast, dst_op_subtract);
    }
    DstSlot ret = opreduce(opts, ast, newargs, DOP_SUBTRACT);
    if (newargs != args) dstc_freeslots(opts.compiler, newargs);
    return ret;
}

/* Keep in lexographic order */
static const DstCFunOptimizer optimizers[] = {
    {dst_add, numbers, add},
    {dst_subtract, numbers, sub}
};

/* Get a cfunction optimizer. Return NULL if none exists.  */
const DstCFunOptimizer *dstc_cfunopt(DstCFunction cfun) {
    size_t i;
    size_t n = sizeof(optimizers)/sizeof(DstCFunOptimizer);
    for (i = 0; i < n; i++)
        if (optimizers[i].cfun == cfun)
            return optimizers + i;
    return NULL;
}


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
#include "emit.h"

/* This logic needs to be expanded for more types */

/* Check if a function received only numbers */
static int numbers(DstFopts opts, DstSlot *args) {
   int32_t i;
   int32_t len = dst_v_count(args);
   (void) opts;
   for (i = 0; i < len; i++) {
       DstSlot s = args[i];
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

/* Fold constants in a DstSlot [] */
static DstSlot *foldc(DstSlot *slots, Dst (*fn)(Dst lhs, Dst rhs)) {
    int32_t ccount;
    int32_t i;
    DstSlot *ret = NULL;
    DstSlot s;
    Dst current;
    for (ccount = 0; ccount < dst_v_count(slots); ccount++) {
        if (slots[ccount].flags & DST_SLOT_CONSTANT) continue;
        break;
    }
    if (ccount < 2) return slots;
    current = fn(slots[0].constant, slots[1].constant);
    for (i = 2; i < ccount; i++) {
        Dst nextarg = slots[i].constant;
        current = fn(current, nextarg);
    }
    s = dstc_cslot(current);
    dst_v_push(ret, s);
    for (; i < dst_v_count(slots); i++) {
        dst_v_push(ret, slots[i]);
    }
    return ret;
}

/* Emit a series of instructions instead of a function call to a math op */
static DstSlot opreduce(DstFopts opts, DstSlot *args, int op) {
    DstCompiler *c = opts.compiler;
    int32_t i, len;
    len = dst_v_count(args);
    DstSlot t;
    if (len == 0) {
        return dstc_cslot(dst_wrap_integer(0));
    } else if (len == 1) {
        return args[0];
    }
    t = dstc_gettarget(opts);
    /* Compile initial two arguments */
    dstc_emit_sss(c, op, t, args[0], args[1]);
    for (i = 2; i < len; i++) {
        dstc_emit_sss(c, op, t, t, args[i]);
    }
    return t;
}

static DstSlot add(DstFopts opts, DstSlot *args) {
    DstSlot *newargs = foldc(args, dst_op_add);
    DstSlot ret = opreduce(opts, newargs, DOP_ADD);
    if (newargs != args) dstc_freeslots(opts.compiler, newargs);
    return ret;
}

static DstSlot sub(DstFopts opts, DstSlot *args) {
    DstSlot *newargs;
    if (dst_v_count(args) == 1) {
        newargs = NULL;
        dst_v_push(newargs, args[0]);
        dst_v_push(newargs, args[0]);
        newargs[0] = dstc_cslot(dst_wrap_integer(0));
    } else {
        newargs = foldc(args, dst_op_subtract);
    }
    DstSlot ret = opreduce(opts, newargs, DOP_SUBTRACT);
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


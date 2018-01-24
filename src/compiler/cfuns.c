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
#include <dst/dststl.h>
#include "compile.h"
#define DST_V_NODEF_GROW
#include <headerlibs/vector.h>
#undef DST_V_NODEF_GROW

/* This logic needs to be expanded for more types */

/* Check if a function recieved only numbers */
static int numbers(DstFopts opts, DstSM *args) {
   int32_t i;
   int32_t len = dst_v_count(args);
   (void) opts;
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

static int can_add(DstFopts opts, DstAst *ast, DstSM *args) {
    (void) ast;
    return numbers(opts, args);
}

static DstSlot add(DstFopts opts, DstAst *ast, DstSM *args) {
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
    dstc_emit(c, ast, (t.index << 8) | (op1 << 16) | (op2 << 24) | DOP_ADD);
    dstc_postread(c, args[0].slot, op1);
    dstc_postread(c, args[1].slot, op2);
    for (i = 2; i < len; i++) {
        op1 = dstc_preread(c, args[i].map, 0xFF, 1, args[i].slot);
        dstc_emit(c, ast, (t.index << 8) | (t.index << 16) | (op1 << 24) | DOP_ADD);
        dstc_postread(c, args[i].slot, op1);
    }
    return t;
}

/* Keep in lexographic order */
static const DstCFunOptimizer optimizers[] = {
    {dst_add, can_add, add}
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


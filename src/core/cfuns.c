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
#include "compile.h"
#include "emit.h"
#include "vector.h"
#endif

static int arity1or2(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    int32_t arity = janet_v_count(args);
    return arity == 1 || arity == 2;
}
static int arity2or3(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    int32_t arity = janet_v_count(args);
    return arity == 2 || arity == 3;
}
static int fixarity1(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    return janet_v_count(args) == 1;
}
static int maxarity1(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    return janet_v_count(args) <= 1;
}
static int minarity2(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    return janet_v_count(args) >= 2;
}
static int fixarity2(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    return janet_v_count(args) == 2;
}
static int fixarity3(JanetFopts opts, JanetSlot *args) {
    (void) opts;
    return janet_v_count(args) == 3;
}

/* Generic handling for $A = op $B */
static JanetSlot genericSS(JanetFopts opts, int op, JanetSlot s) {
    JanetSlot target = janetc_gettarget(opts);
    janetc_emit_ss(opts.compiler, op, target, s, 1);
    return target;
}

/* Generic handling for $A = $B op I */
static JanetSlot genericSSI(JanetFopts opts, int op, JanetSlot s, int32_t imm) {
    JanetSlot target = janetc_gettarget(opts);
    janetc_emit_ssi(opts.compiler, op, target, s, imm, 1);
    return target;
}

/* Emit an insruction that implements a form by itself. */
static JanetSlot opfunction(
    JanetFopts opts,
    JanetSlot *args,
    int op,
    Janet defaultArg2) {
    JanetCompiler *c = opts.compiler;
    int32_t len;
    len = janet_v_count(args);
    JanetSlot t;
    if (len == 1) {
        t = janetc_gettarget(opts);
        janetc_emit_sss(c, op, t, args[0], janetc_cslot(defaultArg2), 1);
        return t;
    } else {
        /* len == 2 */
        t = janetc_gettarget(opts);
        janetc_emit_sss(c, op, t, args[0], args[1], 1);
    }
    return t;
}

/* Check if a value can be coerced to an immediate value */
static int can_be_imm(Janet x, int8_t *out) {
    if (!janet_checkint(x)) return 0;
    int32_t integer = janet_unwrap_integer(x);
    if (integer > INT8_MAX || integer < INT8_MIN) return 0;
    *out = (int8_t) integer;
    return 1;
}

/* Check if a slot can be coerced to an immediate value */
static int can_slot_be_imm(JanetSlot s, int8_t *out) {
    if (!(s.flags & JANET_SLOT_CONSTANT)) return 0;
    return can_be_imm(s.constant, out);
}

/* Emit a series of instructions instead of a function call to a math op */
static JanetSlot opreduce(
    JanetFopts opts,
    JanetSlot *args,
    int op,
    int opim,
    Janet nullary,
    Janet unary) {
    JanetCompiler *c = opts.compiler;
    int32_t i, len;
    int8_t imm = 0;
    len = janet_v_count(args);
    JanetSlot t;
    if (len == 0) {
        return janetc_cslot(nullary);
    } else if (len == 1) {
        t = janetc_gettarget(opts);
        /* Special case subtract to be times -1 */
        if (op == JOP_SUBTRACT) {
            janetc_emit_ssi(c, JOP_MULTIPLY_IMMEDIATE, t, args[0], -1, 1);
        } else {
            janetc_emit_sss(c, op, t, janetc_cslot(unary), args[0], 1);
        }
        return t;
    }
    t = janetc_gettarget(opts);
    if (opim && can_slot_be_imm(args[1], &imm)) {
        janetc_emit_ssi(c, opim, t, args[0], imm, 1);
    } else {
        janetc_emit_sss(c, op, t, args[0], args[1], 1);
    }
    for (i = 2; i < len; i++) {
        if (opim && can_slot_be_imm(args[i], &imm)) {
            janetc_emit_ssi(c, opim, t, t, imm, 1);
        } else {
            janetc_emit_sss(c, op, t, t, args[i], 1);
        }
    }
    return t;
}

/* Function optimizers */

static JanetSlot do_propagate(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_PROPAGATE, 0, janet_wrap_nil(), janet_wrap_nil());
}
static JanetSlot do_error(JanetFopts opts, JanetSlot *args) {
    janetc_emit_s(opts.compiler, JOP_ERROR, args[0], 0);
    return janetc_cslot(janet_wrap_nil());
}
static JanetSlot do_debug(JanetFopts opts, JanetSlot *args) {
    (void)args;
    int32_t len = janet_v_count(args);
    JanetSlot t = janetc_gettarget(opts);
    janetc_emit_ssu(opts.compiler, JOP_SIGNAL, t,
                    (len == 1) ? args[0] : janetc_cslot(janet_wrap_nil()),
                    JANET_SIGNAL_DEBUG,
                    1);
    return t;
}
static JanetSlot do_in(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_IN, 0, janet_wrap_nil(), janet_wrap_nil());
}
static JanetSlot do_get(JanetFopts opts, JanetSlot *args) {
    if (janet_v_count(args) == 3) {
        JanetCompiler *c = opts.compiler;
        JanetSlot t = janetc_gettarget(opts);
        int target_is_default = janetc_sequal(t, args[2]);
        JanetSlot dflt_slot = args[2];
        if (target_is_default) {
            dflt_slot = janetc_farslot(c);
            janetc_copy(c, dflt_slot, t);
        }
        janetc_emit_sss(c, JOP_GET, t, args[0], args[1], 1);
        int32_t label = janetc_emit_si(c, JOP_JUMP_IF_NOT_NIL, t, 0, 0);
        janetc_copy(c, t, dflt_slot);
        if (target_is_default) janetc_freeslot(c, dflt_slot);
        int32_t current = janet_v_count(c->buffer);
        c->buffer[label] |= (current - label) << 16;
        return t;
    } else {
        return opreduce(opts, args, JOP_GET, 0, janet_wrap_nil(), janet_wrap_nil());
    }
}
static JanetSlot do_next(JanetFopts opts, JanetSlot *args) {
    return opfunction(opts, args, JOP_NEXT, janet_wrap_nil());
}
static JanetSlot do_cmp(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_COMPARE, 0, janet_wrap_nil(), janet_wrap_nil());
}
static JanetSlot do_put(JanetFopts opts, JanetSlot *args) {
    if (opts.flags & JANET_FOPTS_DROP) {
        janetc_emit_sss(opts.compiler, JOP_PUT, args[0], args[1], args[2], 0);
        return janetc_cslot(janet_wrap_nil());
    } else {
        JanetSlot t = janetc_gettarget(opts);
        janetc_copy(opts.compiler, t, args[0]);
        janetc_emit_sss(opts.compiler, JOP_PUT, t, args[1], args[2], 0);
        return t;
    }
}
static JanetSlot do_length(JanetFopts opts, JanetSlot *args) {
    return genericSS(opts, JOP_LENGTH, args[0]);
}
static JanetSlot do_yield(JanetFopts opts, JanetSlot *args) {
    if (janet_v_count(args) == 0) {
        return genericSSI(opts, JOP_SIGNAL, janetc_cslot(janet_wrap_nil()), 3);
    } else {
        return genericSSI(opts, JOP_SIGNAL, args[0], 3);
    }
}
static JanetSlot do_resume(JanetFopts opts, JanetSlot *args) {
    return opfunction(opts, args, JOP_RESUME, janet_wrap_nil());
}
static JanetSlot do_cancel(JanetFopts opts, JanetSlot *args) {
    return opfunction(opts, args, JOP_CANCEL, janet_wrap_nil());
}
static JanetSlot do_apply(JanetFopts opts, JanetSlot *args) {
    /* Push phase */
    JanetCompiler *c = opts.compiler;
    int32_t i;
    for (i = 1; i < janet_v_count(args) - 3; i += 3)
        janetc_emit_sss(c, JOP_PUSH_3, args[i], args[i + 1], args[i + 2], 0);
    if (i == janet_v_count(args) - 3)
        janetc_emit_ss(c, JOP_PUSH_2, args[i], args[i + 1], 0);
    else if (i == janet_v_count(args) - 2)
        janetc_emit_s(c, JOP_PUSH, args[i], 0);
    /* Push array phase */
    janetc_emit_s(c, JOP_PUSH_ARRAY, janet_v_last(args), 0);
    /* Call phase */
    JanetSlot target;
    if (opts.flags & JANET_FOPTS_TAIL) {
        janetc_emit_s(c, JOP_TAILCALL, args[0], 0);
        target = janetc_cslot(janet_wrap_nil());
        target.flags |= JANET_SLOT_RETURNED;
    } else {
        target = janetc_gettarget(opts);
        janetc_emit_ss(c, JOP_CALL, target, args[0], 1);
    }
    return target;
}

/* Variadic operators specialization */

static JanetSlot do_add(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_ADD, JOP_ADD_IMMEDIATE, janet_wrap_integer(0), janet_wrap_integer(0));
}
static JanetSlot do_sub(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_SUBTRACT, JOP_SUBTRACT_IMMEDIATE, janet_wrap_integer(0), janet_wrap_integer(0));
}
static JanetSlot do_mul(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_MULTIPLY, JOP_MULTIPLY_IMMEDIATE, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_div(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_DIVIDE, JOP_DIVIDE_IMMEDIATE, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_divf(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_DIVIDE_FLOOR, 0, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_modulo(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_MODULO, 0, janet_wrap_integer(0), janet_wrap_integer(1));
}
static JanetSlot do_remainder(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_REMAINDER, 0, janet_wrap_integer(0), janet_wrap_integer(1));
}
static JanetSlot do_band(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_BAND, 0, janet_wrap_integer(-1), janet_wrap_integer(-1));
}
static JanetSlot do_bor(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_BOR, 0, janet_wrap_integer(0), janet_wrap_integer(0));
}
static JanetSlot do_bxor(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_BXOR, 0, janet_wrap_integer(0), janet_wrap_integer(0));
}
static JanetSlot do_lshift(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_SHIFT_LEFT, JOP_SHIFT_LEFT_IMMEDIATE, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_rshift(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_SHIFT_RIGHT, JOP_SHIFT_RIGHT_IMMEDIATE, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_rshiftu(JanetFopts opts, JanetSlot *args) {
    return opreduce(opts, args, JOP_SHIFT_RIGHT_UNSIGNED, JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE, janet_wrap_integer(1), janet_wrap_integer(1));
}
static JanetSlot do_bnot(JanetFopts opts, JanetSlot *args) {
    return genericSS(opts, JOP_BNOT, args[0]);
}

/* Specialization for comparators */
static JanetSlot compreduce(
    JanetFopts opts,
    JanetSlot *args,
    int op,
    int opim,
    int invert) {
    JanetCompiler *c = opts.compiler;
    int32_t i, len;
    int8_t imm = 0;
    len = janet_v_count(args);
    int32_t *labels = NULL;
    JanetSlot t;
    if (len < 2) {
        return invert
               ? janetc_cslot(janet_wrap_false())
               : janetc_cslot(janet_wrap_true());
    }
    t = janetc_gettarget(opts);
    for (i = 1; i < len; i++) {
        if (opim && can_slot_be_imm(args[i], &imm)) {
            janetc_emit_ssi(c, opim, t, args[i - 1], imm, 1);
        } else {
            janetc_emit_sss(c, op, t, args[i - 1], args[i], 1);
        }
        if (i != (len - 1)) {
            int32_t label = janetc_emit_si(c, invert ? JOP_JUMP_IF : JOP_JUMP_IF_NOT, t, 0, 1);
            janet_v_push(labels, label);
        }
    }
    int32_t end = janet_v_count(c->buffer);
    for (i = 0; i < janet_v_count(labels); i++) {
        int32_t label = labels[i];
        c->buffer[label] |= ((end - label) << 16);
    }
    janet_v_free(labels);
    return t;
}

static JanetSlot do_gt(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_GREATER_THAN, JOP_GREATER_THAN_IMMEDIATE, 0);
}
static JanetSlot do_lt(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_LESS_THAN, JOP_LESS_THAN_IMMEDIATE, 0);
}
static JanetSlot do_gte(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_GREATER_THAN_EQUAL, 0, 0);
}
static JanetSlot do_lte(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_LESS_THAN_EQUAL, 0, 0);
}
static JanetSlot do_eq(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_EQUALS, JOP_EQUALS_IMMEDIATE, 0);
}
static JanetSlot do_neq(JanetFopts opts, JanetSlot *args) {
    return compreduce(opts, args, JOP_NOT_EQUALS, JOP_NOT_EQUALS_IMMEDIATE, 1);
}

/* Arranged by tag */
static const JanetFunOptimizer optimizers[] = {
    {maxarity1, do_debug},
    {fixarity1, do_error},
    {minarity2, do_apply},
    {maxarity1, do_yield},
    {arity1or2, do_resume},
    {fixarity2, do_in},
    {fixarity3, do_put},
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
    {fixarity1, do_bnot},
    {NULL, do_gt},
    {NULL, do_lt},
    {NULL, do_gte},
    {NULL, do_lte},
    {NULL, do_eq},
    {NULL, do_neq},
    {fixarity2, do_propagate},
    {arity2or3, do_get},
    {arity1or2, do_next},
    {NULL, do_modulo},
    {NULL, do_remainder},
    {fixarity2, do_cmp},
    {fixarity2, do_cancel},
    {NULL, do_divf}
};

const JanetFunOptimizer *janetc_funopt(uint32_t flags) {
    uint32_t tag = flags & JANET_FUNCDEF_FLAG_TAG;
    if (tag == 0)
        return NULL;
    uint32_t index = tag - 1;
    if (index >= (sizeof(optimizers) / sizeof(optimizers[0])))
        return NULL;
    return optimizers + index;
}


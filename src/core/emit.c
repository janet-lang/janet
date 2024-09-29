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
#include "emit.h"
#include "vector.h"
#include "regalloc.h"
#include "util.h"
#endif

/* Get a register */
int32_t janetc_allocfar(JanetCompiler *c) {
    int32_t reg = janetc_regalloc_1(&c->scope->ra);
    if (reg > 0xFFFF) {
        janetc_cerror(c, "ran out of internal registers");
    }
    return reg;
}

/* Get a register less than 256 for temporary use. */
int32_t janetc_allocnear(JanetCompiler *c, JanetcRegisterTemp tag) {
    return janetc_regalloc_temp(&c->scope->ra, tag);
}

/* Emit a raw instruction with source mapping. */
void janetc_emit(JanetCompiler *c, uint32_t instr) {
    janet_v_push(c->buffer, instr);
    janet_v_push(c->mapbuffer, c->current_mapping);
}

/* Add a constant to the current scope. Return the index of the constant. */
static int32_t janetc_const(JanetCompiler *c, Janet x) {
    JanetScope *scope = c->scope;
    int32_t i, len;
    /* Get the topmost function scope */
    while (scope) {
        if (scope->flags & JANET_SCOPE_FUNCTION)
            break;
        scope = scope->parent;
    }
    /* Check if already added */
    len = janet_v_count(scope->consts);
    for (i = 0; i < len; i++) {
        if (janet_equals(x, scope->consts[i]))
            return i;
    }
    /* Ensure not too many constants. */
    if (len >= 0xFFFF) {
        janetc_cerror(c, "too many constants");
        return 0;
    }
    janet_v_push(scope->consts, x);
    return len;
}

/* Load a constant into a local register */
static void janetc_loadconst(JanetCompiler *c, Janet k, int32_t reg) {
    switch (janet_type(k)) {
        case JANET_NIL:
            janetc_emit(c, (reg << 8) | JOP_LOAD_NIL);
            break;
        case JANET_BOOLEAN:
            janetc_emit(c, (reg << 8) |
                        (janet_unwrap_boolean(k) ? JOP_LOAD_TRUE : JOP_LOAD_FALSE));
            break;
        case JANET_NUMBER: {
            double dval = janet_unwrap_number(k);
            if (dval < INT16_MIN || dval > INT16_MAX)
                goto do_constant;
            int32_t i = (int32_t) dval;
            if (dval != i)
                goto do_constant;
            uint32_t iu = (uint32_t)i;
            janetc_emit(c,
                        (iu << 16) |
                        (reg << 8) |
                        JOP_LOAD_INTEGER);
            break;
        }
        default:
        do_constant: {
                int32_t cindex = janetc_const(c, k);
                janetc_emit(c,
                            (cindex << 16) |
                            (reg << 8) |
                            JOP_LOAD_CONSTANT);
                break;
            }
    }
}

/* Move a slot to a near register */
static void janetc_movenear(JanetCompiler *c,
                            int32_t dest,
                            JanetSlot src) {
    if (src.flags & (JANET_SLOT_CONSTANT | JANET_SLOT_REF)) {
        janetc_loadconst(c, src.constant, dest);
        /* If we also are a reference, deref the one element array */
        if (src.flags & JANET_SLOT_REF) {
            janetc_emit(c,
                        (dest << 16) |
                        (dest << 8) |
                        JOP_GET_INDEX);
        }
    } else if (src.envindex >= 0) {
        janetc_emit(c,
                    ((uint32_t)(src.index) << 24) |
                    ((uint32_t)(src.envindex) << 16) |
                    ((uint32_t)(dest) << 8) |
                    JOP_LOAD_UPVALUE);
    } else if (src.index != dest) {
        janet_assert(src.index >= 0, "bad slot");
        janetc_emit(c,
                    ((uint32_t)(src.index) << 16) |
                    ((uint32_t)(dest) << 8) |
                    JOP_MOVE_NEAR);
    }
}

/* Move a near register to a Slot. */
static void janetc_moveback(JanetCompiler *c,
                            JanetSlot dest,
                            int32_t src) {
    if (dest.flags & JANET_SLOT_REF) {
        int32_t refreg = janetc_regalloc_temp(&c->scope->ra, JANETC_REGTEMP_5);
        janetc_loadconst(c, dest.constant, refreg);
        janetc_emit(c,
                    (src << 16) |
                    (refreg << 8) |
                    JOP_PUT_INDEX);
        janetc_regalloc_freetemp(&c->scope->ra, refreg, JANETC_REGTEMP_5);
    } else if (dest.envindex >= 0) {
        janetc_emit(c,
                    ((uint32_t)(dest.index) << 24) |
                    ((uint32_t)(dest.envindex) << 16) |
                    ((uint32_t)(src) << 8) |
                    JOP_SET_UPVALUE);
    } else if (dest.index != src) {
        janet_assert(dest.index >= 0, "bad slot");
        janetc_emit(c,
                    ((uint32_t)(dest.index) << 16) |
                    ((uint32_t)(src) << 8) |
                    JOP_MOVE_FAR);
    }
}

/* Call this to release a register after emitting the instruction. */
static void janetc_free_regnear(JanetCompiler *c, JanetSlot s, int32_t reg, JanetcRegisterTemp tag) {
    if (reg != s.index ||
            s.envindex >= 0 ||
            s.flags & (JANET_SLOT_CONSTANT | JANET_SLOT_REF)) {
        /* We need to free the temporary slot */
        janetc_regalloc_freetemp(&c->scope->ra, reg, tag);
    }
}

/* Convert a slot to a two byte register */
static int32_t janetc_regfar(JanetCompiler *c, JanetSlot s, JanetcRegisterTemp tag) {
    /* check if already near register */
    if (s.envindex < 0 && s.index >= 0) {
        return s.index;
    }
    int32_t reg;
    int32_t nearreg = janetc_regalloc_temp(&c->scope->ra, tag);
    janetc_movenear(c, nearreg, s);
    if (nearreg >= 0xF0) {
        reg = janetc_allocfar(c);
        janetc_emit(c, JOP_MOVE_FAR | (nearreg << 8) | (reg << 16));
        janetc_regalloc_freetemp(&c->scope->ra, nearreg, tag);
    } else {
        reg = nearreg;
        janetc_regalloc_freetemp(&c->scope->ra, nearreg, tag);
        janetc_regalloc_touch(&c->scope->ra, reg);
    }
    return reg;
}

/* Convert a slot to a temporary 1 byte register */
static int32_t janetc_regnear(JanetCompiler *c, JanetSlot s, JanetcRegisterTemp tag) {
    /* check if already near register */
    if (s.envindex < 0 && s.index >= 0 && s.index <= 0xFF) {
        return s.index;
    }
    int32_t reg = janetc_regalloc_temp(&c->scope->ra, tag);
    janetc_movenear(c, reg, s);
    return reg;
}

/* Check if two slots are equal */
int janetc_sequal(JanetSlot lhs, JanetSlot rhs) {
    if ((lhs.flags & ~JANET_SLOTTYPE_ANY) == (rhs.flags & ~JANET_SLOTTYPE_ANY) &&
            lhs.index == rhs.index &&
            lhs.envindex == rhs.envindex) {
        if (lhs.flags & (JANET_SLOT_REF | JANET_SLOT_CONSTANT)) {
            return janet_equals(lhs.constant, rhs.constant);
        } else {
            return 1;
        }
    }
    return 0;
}

/* Move values from one slot to another. The destination must
 * be writeable (not a literal). */
void janetc_copy(
    JanetCompiler *c,
    JanetSlot dest,
    JanetSlot src) {
    if (dest.flags & JANET_SLOT_CONSTANT) {
        janetc_cerror(c, "cannot write to constant");
        return;
    }
    if (janetc_sequal(dest, src)) return;
    /* If dest is a near register */
    if (dest.envindex < 0 && dest.index >= 0 && dest.index <= 0xFF) {
        janetc_movenear(c, dest.index, src);
        return;
    }
    /* If src is a near register */
    if (src.envindex < 0 && src.index >= 0 && src.index <= 0xFF) {
        janetc_moveback(c, dest, src.index);
        return;
    }
    /* Process: src -> near -> dest */
    int32_t nearreg = janetc_allocnear(c, JANETC_REGTEMP_3);
    janetc_movenear(c, nearreg, src);
    janetc_moveback(c, dest, nearreg);
    /* Cleanup */
    janetc_regalloc_freetemp(&c->scope->ra, nearreg, JANETC_REGTEMP_3);
}

/* Instruction templated emitters */

static int32_t emit1s(JanetCompiler *c, uint8_t op, JanetSlot s, int32_t rest, int wr) {
    int32_t reg = janetc_regnear(c, s, JANETC_REGTEMP_0);
    int32_t label = janet_v_count(c->buffer);
    janetc_emit(c, op | (reg << 8) | ((uint32_t)rest << 16));
    if (wr)
        janetc_moveback(c, s, reg);
    janetc_free_regnear(c, s, reg, JANETC_REGTEMP_0);
    return label;
}

int32_t janetc_emit_s(JanetCompiler *c, uint8_t op, JanetSlot s, int wr) {
    int32_t reg = janetc_regfar(c, s, JANETC_REGTEMP_0);
    int32_t label = janet_v_count(c->buffer);
    janetc_emit(c, op | (reg << 8));
    if (wr)
        janetc_moveback(c, s, reg);
    janetc_free_regnear(c, s, reg, JANETC_REGTEMP_0);
    return label;
}

int32_t janetc_emit_sl(JanetCompiler *c, uint8_t op, JanetSlot s, int32_t label) {
    int32_t current = janet_v_count(c->buffer) - 1;
    int32_t jump = label - current;
    if (jump < INT16_MIN || jump > INT16_MAX) {
        janetc_cerror(c, "jump is too far");
    }
    return emit1s(c, op, s, jump, 0);
}

int32_t janetc_emit_st(JanetCompiler *c, uint8_t op, JanetSlot s, int32_t tflags) {
    return emit1s(c, op, s, tflags, 0);
}

int32_t janetc_emit_si(JanetCompiler *c, uint8_t op, JanetSlot s, int16_t immediate, int wr) {
    return emit1s(c, op, s, immediate, wr);
}

int32_t janetc_emit_su(JanetCompiler *c, uint8_t op, JanetSlot s, uint16_t immediate, int wr) {
    return emit1s(c, op, s, (int32_t) immediate, wr);
}

static int32_t emit2s(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, int32_t rest, int wr) {
    int32_t reg1 = janetc_regnear(c, s1, JANETC_REGTEMP_0);
    int32_t reg2 = janetc_regnear(c, s2, JANETC_REGTEMP_1);
    int32_t label = janet_v_count(c->buffer);
    janetc_emit(c, op | (reg1 << 8) | (reg2 << 16) | ((uint32_t)rest << 24));
    janetc_free_regnear(c, s2, reg2, JANETC_REGTEMP_1);
    if (wr)
        janetc_moveback(c, s1, reg1);
    janetc_free_regnear(c, s1, reg1, JANETC_REGTEMP_0);
    return label;
}

int32_t janetc_emit_ss(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, int wr) {
    int32_t reg1 = janetc_regnear(c, s1, JANETC_REGTEMP_0);
    int32_t reg2 = janetc_regfar(c, s2, JANETC_REGTEMP_1);
    int32_t label = janet_v_count(c->buffer);
    janetc_emit(c, op | (reg1 << 8) | (reg2 << 16));
    janetc_free_regnear(c, s2, reg2, JANETC_REGTEMP_1);
    if (wr)
        janetc_moveback(c, s1, reg1);
    janetc_free_regnear(c, s1, reg1, JANETC_REGTEMP_0);
    return label;
}

int32_t janetc_emit_ssi(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, int8_t immediate, int wr) {
    return emit2s(c, op, s1, s2, immediate, wr);
}

int32_t janetc_emit_ssu(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, uint8_t immediate, int wr) {
    return emit2s(c, op, s1, s2, (int32_t) immediate, wr);
}

int32_t janetc_emit_sss(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, JanetSlot s3, int wr) {
    int32_t reg1 = janetc_regnear(c, s1, JANETC_REGTEMP_0);
    int32_t reg2 = janetc_regnear(c, s2, JANETC_REGTEMP_1);
    int32_t reg3 = janetc_regnear(c, s3, JANETC_REGTEMP_2);
    int32_t label = janet_v_count(c->buffer);
    janetc_emit(c, op | (reg1 << 8) | (reg2 << 16) | ((uint32_t)reg3 << 24));
    janetc_free_regnear(c, s2, reg2, JANETC_REGTEMP_1);
    janetc_free_regnear(c, s3, reg3, JANETC_REGTEMP_2);
    if (wr)
        janetc_moveback(c, s1, reg1);
    janetc_free_regnear(c, s1, reg1, JANETC_REGTEMP_0);
    return label;
}

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
#include "emit.h"
#include "vector.h"
#include "regalloc.h"

/* Get a register */
int32_t dstc_allocfar(DstCompiler *c) {
    int32_t reg = dstc_regalloc_1(&c->scope->ra);
    if (reg > 0xFFFF) {
        dstc_cerror(c, "ran out of internal registers");
    }
    return reg;
}

/* Get a register less than 256 */
int32_t dstc_allocnear(DstCompiler *c, DstcRegisterTemp tag) {
    return dstc_regalloc_temp(&c->scope->ra, tag);
}

/* Emit a raw instruction with source mapping. */
void dstc_emit(DstCompiler *c, uint32_t instr) {
    dst_v_push(c->buffer, instr);
    dst_v_push(c->mapbuffer, c->current_mapping);
}

/* Add a constant to the current scope. Return the index of the constant. */
static int32_t dstc_const(DstCompiler *c, Dst x) {
    DstScope *scope = c->scope;
    int32_t i, len;
    /* Get the topmost function scope */
    while (scope) {
        if (scope->flags & DST_SCOPE_FUNCTION)
            break;
        scope = scope->parent;
    }
    /* Check if already added */
    len = dst_v_count(scope->consts);
    for (i = 0; i < len; i++) {
        if (dst_equals(x, scope->consts[i]))
            return i;
    }
    /* Ensure not too many constsants. */
    if (len >= 0xFFFF) {
        dstc_cerror(c, "too many constants");
        return 0;
    }
    dst_v_push(scope->consts, x);
    return len;
}

/* Load a constant into a local register */
static void dstc_loadconst(DstCompiler *c, Dst k, int32_t reg) {
    switch (dst_type(k)) {
        case DST_NIL:
            dstc_emit(c, (reg << 8) | DOP_LOAD_NIL);
            break;
        case DST_TRUE:
            dstc_emit(c, (reg << 8) | DOP_LOAD_TRUE);
            break;
        case DST_FALSE:
            dstc_emit(c, (reg << 8) | DOP_LOAD_FALSE);
            break;
        case DST_INTEGER:
            {
                int32_t i = dst_unwrap_integer(k);
                if (i <= INT16_MAX && i >= INT16_MIN) {
                    dstc_emit(c,
                            (i << 16) |
                            (reg << 8) |
                            DOP_LOAD_INTEGER);
                    break;
                }
                goto do_constant;
            }
        default:
        do_constant:
            {
                int32_t cindex = dstc_const(c, k);
                dstc_emit(c,
                        (cindex << 16) |
                        (reg << 8) |
                        DOP_LOAD_CONSTANT);
                break;
            }
    }
}

/* Move a slot to a near register */
static void dstc_movenear(DstCompiler *c,
        int32_t dest,
        DstSlot src) {
    if (src.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        dstc_loadconst(c, src.constant, dest);
        /* If we also are a reference, deref the one element array */
        if (src.flags & DST_SLOT_REF) {
            dstc_emit(c,
                    (dest << 16) |
                    (dest << 8) |
                    DOP_GET_INDEX);
        }
    } else if (src.envindex >= 0) {
        dstc_emit(c,
                ((uint32_t)(src.index) << 24) |
                ((uint32_t)(src.envindex) << 16) |
                ((uint32_t)(dest) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (src.index > 0xFF || src.index != dest) {
        dstc_emit(c,
                ((uint32_t)(src.index) << 16) |
                ((uint32_t)(dest) << 8) |
                    DOP_MOVE_NEAR);
    }
}

/* Move a near register to a Slot. */
static void dstc_moveback(DstCompiler *c,
        DstSlot dest,
        int32_t src) {
    if (dest.flags & DST_SLOT_REF) {
        int32_t refreg = dstc_regalloc_temp(&c->scope->ra, DSTC_REGTEMP_5);
        dstc_loadconst(c, dest.constant, refreg);
        dstc_emit(c,
               (src << 16) |
               (refreg << 8) |
               DOP_PUT_INDEX);
        dstc_regalloc_freetemp(&c->scope->ra, refreg, DSTC_REGTEMP_5);
    } else if (dest.envindex >= 0) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 24) |
                ((uint32_t)(dest.envindex) << 16) |
                ((uint32_t)(src) << 8) |
                DOP_SET_UPVALUE);
    } else if (dest.index != src) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 16) |
                ((uint32_t)(src) << 8) |
                    DOP_MOVE_FAR);
    }
}

/* Call this to release a register after emitting the instruction. */
static void dstc_free_regnear(DstCompiler *c, DstSlot s, int32_t reg, DstcRegisterTemp tag) {
    if (reg != s.index || 
            s.envindex >= 0 || 
            s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        /* We need to free the temporary slot */
        dstc_regalloc_freetemp(&c->scope->ra, reg, tag);
    }
}

/* Convert a slot to a two byte register */
static int32_t dstc_regfar(DstCompiler *c, DstSlot s, DstcRegisterTemp tag) {
    /* check if already near register */
    if (s.envindex < 0 && s.index >= 0) {
        return s.index;
    }
    int32_t reg;
    int32_t nearreg = dstc_regalloc_temp(&c->scope->ra, tag);
    dstc_movenear(c, nearreg, s);
    if (nearreg >= 0xF0) {
        reg = dstc_allocfar(c);
        dstc_emit(c, DOP_MOVE_FAR | (nearreg << 8) | (reg << 16));
        dstc_regalloc_freetemp(&c->scope->ra, nearreg, tag);
    } else {
        reg = nearreg;
        dstc_regalloc_freetemp(&c->scope->ra, nearreg, tag);
        dstc_regalloc_touch(&c->scope->ra, reg);
    }
    return reg;
}

/* Convert a slot to a temporary 1 byte register */
static int32_t dstc_regnear(DstCompiler *c, DstSlot s, DstcRegisterTemp tag) {
    /* check if already near register */
    if (s.envindex < 0 && s.index >= 0 && s.index <= 0xFF) {
        return s.index;
    }
    int32_t reg = dstc_regalloc_temp(&c->scope->ra, tag);
    dstc_movenear(c, reg, s);
    return reg;
}

/* Check if two slots are equal */
static int dstc_sequal(DstSlot lhs, DstSlot rhs) {
    if ((lhs.flags & ~DST_SLOTTYPE_ANY) == (rhs.flags & ~DST_SLOTTYPE_ANY) &&
            lhs.index == rhs.index &&
            lhs.envindex == rhs.envindex) {
        if (lhs.flags & (DST_SLOT_REF | DST_SLOT_CONSTANT)) {
            return dst_equals(lhs.constant, rhs.constant);
        } else {
            return 1;
        }
    }
    return 0;
}

/* Move values from one slot to another. The destination must
 * be writeable (not a literal). */
void dstc_copy(
        DstCompiler *c,
        DstSlot dest,
        DstSlot src) {
    if (dest.flags & DST_SLOT_CONSTANT) {
        dstc_cerror(c, "cannot write to constant");
        return;
    }
    if (dstc_sequal(dest, src)) return;
    /* If dest is a near register */
    if (dest.envindex < 0 && dest.index >= 0 && dest.index <= 0xFF) {
        dstc_movenear(c, dest.index, src);
        return;
    }
    /* If src is a near register */
    if (src.envindex < 0 && src.index >= 0 && src.index <= 0xFF) {
        dstc_moveback(c, dest, src.index);
        return;
    }
    /* Process: src -> near -> dest */
    int32_t near = dstc_allocnear(c, DSTC_REGTEMP_3);
    dstc_movenear(c, near, src);
    dstc_moveback(c, dest, near);
    /* Cleanup */
    dstc_regalloc_freetemp(&c->scope->ra, near, DSTC_REGTEMP_3);

}
/* Instruction templated emitters */

static int32_t emit1s(DstCompiler *c, uint8_t op, DstSlot s, int32_t rest, int wr) {
    int32_t reg = dstc_regnear(c, s, DSTC_REGTEMP_0);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg << 8) | (rest << 16));
    if (wr)
        dstc_moveback(c, s, reg);
    dstc_free_regnear(c, s, reg, DSTC_REGTEMP_0);
    return label;
}

int32_t dstc_emit_s(DstCompiler *c, uint8_t op, DstSlot s, int wr) {
    int32_t reg = dstc_regfar(c, s, DSTC_REGTEMP_0);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg << 8));
    if (wr)
        dstc_moveback(c, s, reg);
    dstc_free_regnear(c, s, reg, DSTC_REGTEMP_0);
    return label;
}

int32_t dstc_emit_sl(DstCompiler *c, uint8_t op, DstSlot s, int32_t label) {
    int32_t current = dst_v_count(c->buffer) - 1;
    int32_t jump = label - current;
    if (jump < INT16_MIN || jump > INT16_MAX) {
        dstc_cerror(c, "jump is too far");
    }
    return emit1s(c, op, s, jump, 0);
}

int32_t dstc_emit_st(DstCompiler *c, uint8_t op, DstSlot s, int32_t tflags) {
    return emit1s(c, op, s, tflags, 0);
}

int32_t dstc_emit_si(DstCompiler *c, uint8_t op, DstSlot s, int16_t immediate, int wr) {
    return emit1s(c, op, s, immediate, wr);
}

int32_t dstc_emit_su(DstCompiler *c, uint8_t op, DstSlot s, uint16_t immediate, int wr) {
    return emit1s(c, op, s, (int32_t) immediate, wr);
}

static int32_t emit2s(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int32_t rest, int wr) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regnear(c, s2, DSTC_REGTEMP_1);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16) | (rest << 24));
    dstc_free_regnear(c, s2, reg2, DSTC_REGTEMP_1);
    if (wr)
        dstc_moveback(c, s1, reg1);
    dstc_free_regnear(c, s1, reg1, DSTC_REGTEMP_0);
    return label;
}

int32_t dstc_emit_ss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int wr) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regfar(c, s2, DSTC_REGTEMP_1);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16));
    dstc_free_regnear(c, s2, reg2, DSTC_REGTEMP_1);
    if (wr)
        dstc_moveback(c, s1, reg1);
    dstc_free_regnear(c, s1, reg1, DSTC_REGTEMP_0);
    return label;
}

int32_t dstc_emit_ssi(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int8_t immediate, int wr) {
    return emit2s(c, op, s1, s2, immediate, wr);
}

int32_t dstc_emit_ssu(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, uint8_t immediate, int wr) {
    return emit2s(c, op, s1, s2, (int32_t) immediate, wr);
}

int32_t dstc_emit_sss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, DstSlot s3, int wr) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regnear(c, s2, DSTC_REGTEMP_1);
    int32_t reg3 = dstc_regnear(c, s3, DSTC_REGTEMP_2);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16) | (reg3 << 24));
    dstc_free_regnear(c, s2, reg2, DSTC_REGTEMP_1);
    dstc_free_regnear(c, s3, reg3, DSTC_REGTEMP_2);
    if (wr)
        dstc_moveback(c, s1, reg1);
    dstc_free_regnear(c, s1, reg1, DSTC_REGTEMP_0);
    return label;
}

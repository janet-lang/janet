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

#include <dst/dstcompile.h>
#include <headerlibs/vector.h>
#include "emit.h"

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

/* Convert a slot to a two byte register */
int32_t dstc_regfar(DstCompiler *c, DstSlot s, DstcRegisterTemp tag) {
    int32_t reg;
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        reg = dstc_allocnear(c, tag);
        dstc_loadconst(c, s.constant, reg);
        /* If we also are a reference, deref the one element array */
        if (s.flags & DST_SLOT_REF) {
            dstc_emit(c,
                    (reg << 16) |
                    (reg << 8) |
                    DOP_GET_INDEX);
        }
    } else if (s.envindex >= 0) {
        reg = dstc_allocnear(c, tag);
        dstc_emit(c,
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(reg) << 8) |
                DOP_LOAD_UPVALUE);
    } else {
        /* We have a normal slot that fits in the required bit width */
        reg = s.index;
    }
    return reg;
}

/* Convert a slot to a temporary 1 byte register */
int32_t dstc_regnear(DstCompiler *c, DstSlot s, DstcRegisterTemp tag) {
    int32_t reg;
    if (s.flags & (DST_SLOT_CONSTANT | DST_SLOT_REF)) {
        reg = dstc_allocnear(c, tag);
        dstc_loadconst(c, s.constant, reg);
        /* If we also are a reference, deref the one element array */
        if (s.flags & DST_SLOT_REF) {
            dstc_emit(c,
                    (reg << 16) |
                    (reg << 8) |
                    DOP_GET_INDEX);
        }
    } else if (s.envindex >= 0) {
        reg = dstc_allocnear(c, tag);
        dstc_emit(c,
                ((uint32_t)(s.index) << 24) |
                ((uint32_t)(s.envindex) << 16) |
                ((uint32_t)(reg) << 8) |
                DOP_LOAD_UPVALUE);
    } else if (s.index > 0xFF) {
        reg = dstc_allocnear(c, tag);
        dstc_emit(c,
                ((uint32_t)(s.index) << 16) |
                ((uint32_t)(reg) << 8) |
                    DOP_MOVE_NEAR);
    } else {
        /* We have a normal slot that fits in the required bit width */
        reg = s.index;
    }
    return reg;
}

/* Call this to release a register after emitting the instruction. */
void dstc_free_reg(DstCompiler *c, DstSlot s, int32_t reg) {
    if (reg != s.index || s.envindex >= 0 || s.flags & DST_SLOT_CONSTANT) {
        /* We need to free the temporary slot */
        dstc_regalloc_free(&c->scope->ra, reg);
    }
}

/* Check if two slots are equal */
static int dstc_sequal(DstSlot lhs, DstSlot rhs) {
    if (lhs.flags == rhs.flags &&
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
    int writeback = 0;
    int32_t destlocal = -1;
    int32_t srclocal = -1;
    int32_t reflocal = -1;

    /* Can't write to constants */
    if (dest.flags & DST_SLOT_CONSTANT) {
        dstc_cerror(c, "cannot write to constant");
        return;
    }

    /* Short circuit if dest and source are equal */
    if (dstc_sequal(dest, src)) return;

    /* Types of slots - src */
    /* constants */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* Types of slots - dest */
    /* upvalues */
    /* refs */
    /* near index */
    /* far index */

    /* If dest is a near index, do some optimization */
    if (dest.envindex < 0 && dest.index >= 0 && dest.index <= 0xFF) {
        if (src.flags & DST_SLOT_CONSTANT) {
            dstc_loadconst(c, src.constant, dest.index);
        } else if (src.flags & DST_SLOT_REF) {
            dstc_loadconst(c, src.constant, dest.index);
            dstc_emit(c,
                    (dest.index << 16) |
                    (dest.index << 8) |
                    DOP_GET_INDEX);
        } else if (src.envindex >= 0) {
            dstc_emit(c,
                    (src.index << 24) |
                    (src.envindex << 16) |
                    (dest.index << 8) |
                    DOP_LOAD_UPVALUE);
        } else {
            dstc_emit(c,
                    (src.index << 16) |
                    (dest.index << 8) |
                    DOP_MOVE_NEAR);
        }
        return;
    }

    /* Process: src -> srclocal -> destlocal -> dest */

    /* src -> srclocal */
    srclocal = dstc_regnear(c, src, DSTC_REGTEMP_0);

    /* Pull down dest (find destlocal) */
    if (dest.flags & DST_SLOT_REF) {
        writeback = 1;
        destlocal = srclocal;
        reflocal = dstc_allocnear(c, DSTC_REGTEMP_1);
        dstc_emit(c,
                (dstc_const(c, dest.constant) << 16) |
                (reflocal << 8) |
                DOP_LOAD_CONSTANT);
    } else if (dest.envindex >= 0) {
        writeback = 2;
        destlocal = srclocal;
    } else if (dest.index > 0xFF) {
        writeback = 3;
        destlocal = srclocal;
    } else {
        destlocal = dest.index;
    }

    /* srclocal -> destlocal */
    if (srclocal != destlocal) {
        dstc_emit(c,
                ((uint32_t)(srclocal) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_NEAR);
    }

    /* destlocal -> dest */
    if (writeback == 1) {
        dstc_emit(c,
                (destlocal << 16) |
                (reflocal << 8) |
                DOP_PUT_INDEX);
    } else if (writeback == 2) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 24) |
                ((uint32_t)(dest.envindex) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_SET_UPVALUE);
    } else if (writeback == 3) {
        dstc_emit(c,
                ((uint32_t)(dest.index) << 16) |
                ((uint32_t)(destlocal) << 8) |
                DOP_MOVE_FAR);
    }

    /* Cleanup */
    if (reflocal >= 0) {
        dstc_regalloc_free(&c->scope->ra, reflocal);
    }
    dstc_free_reg(c, src, srclocal);
}

/* Instruction templated emitters */

static int32_t emit1s(DstCompiler *c, uint8_t op, DstSlot s, int32_t rest) {
    int32_t reg = dstc_regnear(c, s, DSTC_REGTEMP_0);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg << 8) | (rest << 16));
    dstc_free_reg(c, s, reg);
    return label;
}

int32_t dstc_emit_s(DstCompiler *c, uint8_t op, DstSlot s) {
    int32_t reg = dstc_regfar(c, s, DSTC_REGTEMP_0);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg << 8));
    dstc_free_reg(c, s, reg);
    return label;
}

int32_t dstc_emit_sl(DstCompiler *c, uint8_t op, DstSlot s, int32_t label) {
    int32_t current = dst_v_count(c->buffer) - 1;
    int32_t jump = label - current;
    if (jump < INT16_MIN || jump > INT16_MAX) {
        dstc_cerror(c, "jump is too far");
    }
    return emit1s(c, op, s, jump);
}

int32_t dstc_emit_st(DstCompiler *c, uint8_t op, DstSlot s, int32_t tflags) {
    return emit1s(c, op, s, tflags);
}

int32_t dstc_emit_si(DstCompiler *c, uint8_t op, DstSlot s, int16_t immediate) {
    return emit1s(c, op, s, immediate);
}

int32_t dstc_emit_su(DstCompiler *c, uint8_t op, DstSlot s, uint16_t immediate) {
    return emit1s(c, op, s, (int32_t) immediate);
}

static int32_t emit2s(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int32_t rest) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regnear(c, s2, DSTC_REGTEMP_1);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16) | (rest << 24));
    dstc_free_reg(c, s1, reg1);
    dstc_free_reg(c, s2, reg2);
    return label;
}

int32_t dstc_emit_ss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regfar(c, s2, DSTC_REGTEMP_1);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16));
    dstc_free_reg(c, s1, reg1);
    dstc_free_reg(c, s2, reg2);
    return label;
}

int32_t dstc_emit_ssi(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int8_t immediate) {
    return emit2s(c, op, s1, s2, immediate);
}

int32_t dstc_emit_ssu(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, uint8_t immediate) {
    return emit2s(c, op, s1, s2, (int32_t) immediate);
}

int32_t dstc_emit_sss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, DstSlot s3) {
    int32_t reg1 = dstc_regnear(c, s1, DSTC_REGTEMP_0);
    int32_t reg2 = dstc_regnear(c, s2, DSTC_REGTEMP_1);
    int32_t reg3 = dstc_regnear(c, s3, DSTC_REGTEMP_2);
    int32_t label = dst_v_count(c->buffer);
    dstc_emit(c, op | (reg1 << 8) | (reg2 << 16) | (reg3 << 24));
    dstc_free_reg(c, s1, reg1);
    dstc_free_reg(c, s2, reg2);
    dstc_free_reg(c, s3, reg3);
    return label;
}

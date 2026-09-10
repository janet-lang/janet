/*
* Copyright (c) 2026 Calvin Rose
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
#include "state.h"
#include "fiber.h"
#include "gc.h"
#include "symcache.h"
#include "util.h"
#include "vector.h"
#include "compile.h"
#endif

/* The OVM (optimistic virtual machine) is a module to help optimize and compile
 * bytecode. It is a collection tools rather than a single-purpose JIT or AOT compiler.
 *
 * Intended optimization features:
 *
 * - Statically reject some subset of functions for optimization
 * - Optimized code is not cached with marshal and can be reconstructed on demand
 * - Hoist all or most required type checks to function entry
 * - Deoptimization before COMMIT will jump back to normal VM. After commit will hard error (internal error)
 * - More instructions can reference constants directly without loading, including arithmetic, call, and tcall instructions
 * - Arithmetic code will only handle numbers and deoptimize upon encounter other types
 * - Test-branch fusion - compare follow by branch-if will be fused
 * - Common-subexpression-elimination
 * - Constant propogation
 * - Strength reduction
 * - Limited LICM
 * - Inlining
 */

/* Utility functions */

/* Embed bitset into array of uint32_ts */
static uint32_t bs_mask(int32_t index) {
    return ((uint32_t)1) << (index & 0x1F);
}
static uint32_t bs_indx(int32_t index) {
    return index >> 5;
}
static void bs_set_bit(uint32_t *bitset, int32_t index) {
    bitset[bs_indx(index)] |= bs_mask(index);
}
static void bs_clear_bit(uint32_t *bitset, int32_t index) {
    bitset[bs_indx(index)] &= ~(bs_mask(index));
}
/*
static void bs_toggle_bit(uint32_t *bitset, int32_t index) {
    bitset[bs_indx(index)] ^= bs_mask(index);
}
*/
static int bs_read_bit(uint32_t *bitset, int32_t index) {
    return (bitset[bs_indx(index)] & bs_mask(index)) ? 1 : 0;
}
static uint32_t *make_bitset(size_t nbits) {
    size_t bitset_len = (nbits + 31) / 32;
    if (bitset_len == 0) return NULL;
    uint32_t *bitset = janet_malloc(bitset_len * sizeof(uint32_t));
    if (NULL == bitset) {
        JANET_OUT_OF_MEMORY;
    }
    memset(bitset, 0, bitset_len * sizeof(uint32_t));
    return bitset;
}
/*
static void clear_bitset(uint32_t *bitset, size_t nbits) {
    size_t bitset_len = (nbits + 31) / 32;
    if (bitset_len) {
        memset(bitset, 0, bitset_len * sizeof(uint32_t));
    }
}
*/

/* Basic Block extraction
 *
 * From arbitrary bytecode, extract basic-blocks for CFG analysis.
 *
 * Each basic block includes a start (inclusive) index and end (exclusive)
 * index, Up to two successor blocks, and up to two predecessor blocks. In the
 * case where a block has more than these limits, generate empty blocks (start
 * == end) with multiple predecessors and one successor to funnel the flow into
 * the final successor block that contains instructions. Use -1 in the pred and
 * succ arrays to represent no predecessor or successor.
 *
 * A basic block has one entrance and one (normal) exit. The last instruction therefor will always
 * be a branch, jump, or return instruction, except in the degenerate case of an empty block.
 *
 * Basic block with no predecessors where start != 0 is dead code. Instructions not in any
 * basic block are also dead.
 *
 * Conditional errors or resumable instructions (yield) are ignored and considered
 * as normal operators with side-effects in most cases. Some unconditional errors
 * are treated as block terminators, while others may be missed by the analysis.
 *
 * Used for subsequent optimization, coarse-grained DCE, local value numbering,
 * lowering to SSA, etc.
 */

typedef struct {
    int32_t start;
    int32_t end;
    int32_t pred[2];
    int32_t succ[2];
} JanetBB;

/* Find a successor given a branch target */
static int32_t janet_find_bb_with_entry(JanetBB *bbs, int32_t n, int32_t entry_idx) {
    for (int32_t i = 0; i < n; i++) {
        if (bbs[i].start == entry_idx) {
            return i;
        }
    }
    janet_assert(0, "did not find block");
}

/* Add a successor, handling case when the successor already has two predecessors.
 * If it does, add a new degenerate successor to allow another logical predecessor. */
static JanetBB *janet_bb_add_succ(JanetBB *bbs, int32_t index, int32_t succ) {
    /* Check if succ already has two preds */
    if (bbs[succ].pred[0] >= 0 && bbs[succ].pred[1] >= 0) {
        /* Degenerate case: add a new empty basic block */
        int32_t old_succpred = bbs[succ].pred[1];
        int32_t new_index = janet_v_count(bbs);
        JanetBB empty;
        empty.start = empty.end = -1;
        empty.pred[0] = old_succpred;
        empty.pred[1] = -1;
        empty.succ[0] = succ;
        empty.succ[1] = -1;
        /* Rewrite old */
        if (bbs[old_succpred].succ[0] == succ) {
            bbs[old_succpred].succ[0] = new_index;
        } else {
            bbs[old_succpred].succ[1] = new_index;
        }
        janet_v_push(bbs, empty);
        succ = new_index;
    }
    /* We know succ has at most 1 existing pred at this point */
    if (bbs[index].succ[0] == -1) {
        bbs[index].succ[0] = succ;
    } else if (bbs[index].succ[1] == -1) {
        bbs[index].succ[1] = succ;
    } else {
        janet_assert(0, "index has too many successors");
    }
    if (bbs[succ].pred[0] == -1) {
        bbs[succ].pred[0] = index;
    } else if (bbs[succ].pred[1] == -1) {
        bbs[succ].pred[1] = index;
    } else {
        janet_assert(0, "succ has too many predecessors");
    }
    return bbs; /* May have been reallocated */
}

JanetBB *janet_basic_blocks(JanetFuncDef *def) {
    uint32_t *bytecode = def->bytecode;
    int32_t blen = def->bytecode_length;

    /* Mark branch target locs with bitset (leaders) Add a fake leader at the end. */
    uint32_t *bitset = make_bitset(blen + 1);
    bitset[0] = 1; /* Mark first bit as instruction 0 is a leader */
    int did_jump = 0;
    for (int32_t i = 0; i <= blen; i++) {
        int32_t target = -1;
        if (did_jump) bitset[bs_indx(i)] |= bs_mask(i);
        if (i == blen) break;
        switch (bytecode[i] & 0x7F) {
            default:
                did_jump = 0;
                continue;
            case JOP_RETURN:
            case JOP_RETURN_NIL:
            case JOP_ERROR:
            case JOP_TAILCALL:
                did_jump = 1;
                continue;
            case JOP_JUMP:
                target = i + (((int32_t)(bytecode[i])) >> 8);
                break;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT_NIL:
                target = i + (((int32_t)(bytecode[i])) >> 16);
                break;
        }
        bitset[bs_indx(target)] |= bs_mask(target);
        did_jump = 1;
    }

    /* Initially allocate basic blocks */
    JanetBB *bbs = NULL;
    int32_t start = 0;
    for (int32_t i = 0; i <= blen; i++) {
        int is_leader = (bitset[bs_indx(i)] & bs_mask(i));
        if (!is_leader) continue;
        if (i > 0) {
            /* End current BB */
            JanetBB bb;
            bb.start = start;
            bb.end = i;
            bb.pred[0] = bb.pred[1] = bb.succ[0] = bb.succ[1] = -1;
            janet_v_push(bbs, bb);
        }
        start = i;
    }

    /* Set successors and predecessors */
    int32_t origcount = janet_v_count(bbs);
    for (int32_t i = 0; i < origcount; i++) {
        janet_assert(bbs[i].start < bbs[i].end, "no degenerate basic blocks");
        int32_t endi = bbs[i].end;
        uint32_t lasti = bytecode[endi - 1];
        int32_t target = -1;
        switch (lasti & 0x7F) {
            default:
                /* 1 successor is next instruction */
                bbs = janet_bb_add_succ(bbs, i, janet_find_bb_with_entry(bbs, origcount, endi));
                break;
            case JOP_JUMP:
                /* 1 successor */
                target = endi - 1 + (((int32_t)(lasti)) >> 8);
                bbs = janet_bb_add_succ(bbs, i, janet_find_bb_with_entry(bbs, origcount, target));
                break;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT_NIL:
                /* 2 successors */
                target = endi - 1 + (((int32_t)(lasti)) >> 16);
                if (endi == target) {
                    /* Degenerate branch */
                    bbs = janet_bb_add_succ(bbs, i, janet_find_bb_with_entry(bbs, origcount, target));
                } else {
                    bbs = janet_bb_add_succ(bbs, i, janet_find_bb_with_entry(bbs, origcount, endi));
                    bbs = janet_bb_add_succ(bbs, i, janet_find_bb_with_entry(bbs, origcount, target));
                }
                break;
            case JOP_RETURN:
            case JOP_RETURN_NIL:
            case JOP_ERROR:
            case JOP_TAILCALL:
                /* No successors */
                break;
        }
    }

    janet_free(bitset);
    return bbs;
}

/* Remove dead code not in any basic blocks */
static void bb_dead_to_noop(JanetFuncDef *def, JanetBB *bbs) {
    uint32_t *bits = make_bitset(def->bytecode_length);
    JanetBB *end = bbs + janet_v_count(bbs);
    for (JanetBB *bb = bbs; bb < end; bb++) {
        if (bb->start < 0) continue;
        /* Orphan check */
        if ((bb->start != 0) && bb->pred[0] == -1 && bb->pred[1] == -1) continue;
        for (int32_t i = bb->start; i < bb->end; i++) {
            bs_set_bit(bits, i);
        }
    }
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        if (!bs_read_bit(bits, i)) {
            def->bytecode[i] = JOP_NOOP;
        }
    }
    janet_free(bits);
}

/* Simple Jump threading. Traverse basic blocks instead?
 * jump instructions are always last instruction in basic block. */
static void janet_jump_threading(JanetFuncDef *def) {
    int32_t blen = def->bytecode_length;
    uint32_t *code = def->bytecode;
    int32_t *codes = (int32_t *)code;
    int recur = 1;
    while (recur) {
        recur = 0;
        for (int32_t i = 0; i < blen; i++) {
            int32_t target;
            int is_branch = 0;
            switch (code[i] & 0x7F) {
                default:
                    continue;
                case JOP_JUMP:
                    target = i + (codes[i] >> 8);
                    break;
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT_NIL:
                    is_branch = 1;
                    target = i + (codes[i] >> 16);
                    break;
            }
            if (target == i) continue; /* infinite loop */
            if (target == i + 1) {
                code[i] = JOP_NOOP;
                recur = 1;
                continue;
            }
            int32_t original_target = target;
            while ((code[target] & 0x7F) == JOP_NOOP) { /* Skip noops */
                target++;
            }
            if ((code[target] & 0x7F) == JOP_RETURN_NIL && !is_branch) {
                code[i] = JOP_RETURN_NIL;
                recur = 1;
                continue;
            }
            /* update target */
            if ((code[target] & 0x7F) == JOP_JUMP) {
                target += (codes[target] >> 8);
            }
            uint32_t newcode;
            if (is_branch) {
                newcode = (code[i] & 0xFFFF) | (uint32_t)((target - i) << 16);
            } else {
                newcode = (code[i] & 0xFF) | (uint32_t)((target - i) << 8);
            }
            if (newcode != code[i]) recur = 1;
            code[i] = newcode;
        }
    }
}


/* Stack analysis
 *
 * We need this to map arguments to function calls and data structure creation.
 * When the initial stack is 0, we can map all arguments in a basic block to
 * function calls (until JOP_PUSH_ARRAY is called). This suggests that removal
 * of JOP_PUSH_ARRAY whenever possible would be good for analysis and possibly
 * should be a pass. For example, PUSH_ARRAY of a constant gets converted to a
 * sequence of pushes.
 * */

typedef struct {
    int32_t delta; /* how many values this basic block pushes to stack */
    int32_t delta_big; /* 1 if stack delta is very large */
    int32_t reset; /* 1 if delta is relative to 0; stack was reset */
    int32_t initial; /* -1 means unknown */
    int32_t final; /* -1 means unknown */
} StackInfo;

static StackInfo *stack_deltas(JanetFuncDef *def, JanetBB *bbs) {
    uint32_t *bytecode = def->bytecode;
    StackInfo *infos = array_allocate(sizeof(StackInfo), janet_v_count(bbs));
    for (int32_t i = 0; i < janet_v_count(bbs); i++) {
        StackInfo info;
        info.reset = 0;
        info.delta = 0;
        info.delta_big = 0;
        info.initial = -1;
        info.final = -1;
        for (int32_t j = bbs[i].start; j < bbs[i].end; j++) {
            switch (bytecode[j] & 0x7F) {
                default:
                    continue;
                case JOP_PUSH:
                    info.delta++;
                    continue;
                case JOP_PUSH_2:
                    info.delta += 2;
                    continue;
                case JOP_PUSH_3:
                    info.delta += 3;
                    continue;
                case JOP_PUSH_ARRAY:
                    info.delta_big = 1;
                    continue;
                case JOP_MAKE_ARRAY:
                case JOP_MAKE_BUFFER:
                case JOP_MAKE_STRING:
                case JOP_MAKE_STRUCT:
                case JOP_MAKE_TABLE:
                case JOP_MAKE_TUPLE:
                case JOP_MAKE_BRACKET_TUPLE:
                case JOP_CALL:
                    info.reset = 1;
                    info.delta = 0;
                    continue;
            }
        }
        infos[i] = info;
    }
    /* Traverse blocks. If a blocks predecessors have the same final counts, that is the intial
     * count. Otherwise, it is unknown. First block has initial stack of 0. */
    int recur = 1;
    while (recur) {
        recur = 0;
        for (int32_t i = 0; i < janet_v_count(bbs); i++) {
            if (bbs[i].start == 0) {
                infos[i].initial = 0;
            }
            int32_t pred0 = bbs[i].pred[0];
            int32_t pred1 = bbs[i].pred[1];
            int32_t prev_final = (pred0 == -1) ? -1 : infos[pred0].final;
            prev_final = (pred1 == -1) ? -1 : (infos[pred1].final == prev_final) ? prev_final : -1;
            int32_t initial = prev_final;
            int32_t final = initial;
            if (infos[i].reset) final = 0;
            if (infos[i].delta_big) final = -1;
            if (final > -1) {
                final += infos[i].delta;
            }
            if (initial != infos[i].initial) recur = 1;
            if (final != infos[i].final) recur = 1;
            infos[i].initial = initial;
            infos[i].final = final;
        }
    }
    return infos;
}

typedef struct {
    union {
        uint16_t a_types;
        uint16_t d_types;
    };
    union {
        uint16_t b_types;
        uint16_t e_types;
    };
    uint16_t c_types;
    uint16_t flags;
} JanetTypeflowInstruction;

/* Flags for lattice_types instruction */
#define TYPEFLOW_UNUSED 0
#define TYPEFLOW_INPUT_A 1
#define TYPEFLOW_OUTPUT_A 2
#define TYPEFLOW_INPUT_B 4
#define TYPEFLOW_OUTPUT_B 8
#define TYPEFLOW_INPUT_C 16
#define TYPEFLOW_OUTPUT_C 32
#define TYPEFLOW_DID_EXIT 64 /* At least this path exited */
#define TYPEFLOW_MAY_SIGNAL 128
#define TYPEFLOW_DID_PROCEED 256
#define TYPEFLOW_LIVE_CODE 512
#define TYPEFLOW_INPUT_D 1024
#define TYPEFLOW_OUTPUT_D 2048
#define TYPEFLOW_INPUT_E 4096
#define TYPEFLOW_OUTPUT_E 8192
#define TYPEFLOW_INPUT_STACK 16384
#define TYPEFLOW_OUTPUT_STACK 32768

#define TYPEFLOW_ERR (TYPEFLOW_DID_EXIT | TYPEFLOW_MAY_SIGNAL)

/* Generate conservative static typing and flow analysis with the primitive types. Turns
 * the untyped VM bytecode into typed bytecode with annotations.
 *
 * We produce auxiliary data for each bytecode instruction:
 *
 * 1. 16-wide bitset of possible primitive types for parameter A (or D for D instructions)
 * 2. 16-wide bitset of possible primitive types for parameter B (or E for AE instructions)
 * 3. 16-wide bitset of possible primitive types for parameter C
 * 4. For each of 1, 2, and 3, 2 bits for unused/input/output
 *    00 means unused
 *    01 means input
 *    10 means output
 * 5. bit if will exit
 * 6. bit if instruction may error or signal terminally (e.g. error)
 * 7. bit if instruction may signal temporarily (e.g. await or yield)
 * 8. bit for live code
 *
 * This type/flow analysis should enable certain kinds of further
 * optimizations, as well as a faster, more specialized interpreter or JIT
 * compiler to avoid as many checks as possible when interpreting code. Bit-sets
 * of possible primitive types are guaranteed to be a superset of runtime
 * types, but are not exact and may be overly general. Error, signal, and return flags
 * are speculative -- an instruction may have a flag set even if the flow will never be reached
 * at runtime. An unset bit means that the flow must never happen! The intent of unset bits
 * is to enable the implementation to skip an otherwise necessary check.
 *
 * All instructions that are not marked as "live" can be safely eliminated or simply
 * not emitted to the target.
 *
 * Return a newly-allocated array of JanetTypeflowInstruction that contains
 * def->bytecode_length elements. The caller must free the returned pointer with janet_free.
 *
 * May return NULL if we decide the function is too complicated, so users should allow
 * for this optimization not to happen.
 *
 * TODO - this is very slow and uses the naive "dense" analysis. However, Janet functions
 * tend to be small and the overhead of creating and dealing with many extra nodes might
 * not be all that helpful for our ISA. Sparse is probably better though.
 */

JanetTypeflowInstruction *janet_bytecode_lattice_types(JanetFuncDef *def, uint16_t *ret_types) {
    JanetTable *states = janet_table(0); /* PC -> register set */
    int32_t *state_stack = NULL;
    uint16_t rettype = 0;

    /* Setup return buffer */
    JanetTypeflowInstruction *flow = array_allocate(sizeof(JanetTypeflowInstruction), def->bytecode_length);
    memset(flow, 0, sizeof(JanetTypeflowInstruction) * def->bytecode_length);

    /* Setup initial state */
    uint16_t *types = array_allocate(sizeof(uint16_t), def->slotcount);
    for (int32_t i = 0; i < def->slotcount; i++) {
        types[i] = JANET_TFLAG_NIL;
    }
    for (int32_t i = 0; i < def->max_arity && i < def->slotcount; i++) {
        types[i] = (uint16_t) 0xFFFF;
    }
    {
        uint16_t *next_types = array_allocate(sizeof(uint16_t), def->slotcount);
        memcpy(next_types, types, sizeof(uint16_t) * def->slotcount);
        janet_table_put(states, janet_wrap_integer(0), janet_wrap_pointer(next_types));
        janet_v_push(state_stack, 0);
    }

    int32_t num_blocks = def->bytecode_length;
    const size_t MAX_ITERATIONS = ((size_t) def->slotcount * 16 * num_blocks); /* The absolute worst possible case */
    size_t iterations = 0; /* debug counter */
    /* While we have more states to visit, traverse them */
    while (janet_v_count(state_stack)) {
        janet_assert(iterations < MAX_ITERATIONS, "too many iterations. Check the code.");
        iterations += 1;
        int32_t pc = janet_v_last(state_stack);
        janet_v_pop(state_stack);
        uint16_t *work_types = janet_unwrap_pointer(janet_table_get(states, janet_wrap_integer(pc)));
        memcpy(types, work_types, sizeof(uint16_t) * def->slotcount);
        /* Iterate over bytecode */
        while (pc < def->bytecode_length) { /* Just in case prevent overrun */
            uint32_t I = def->bytecode[pc];
            uint32_t Iop = I & 0x7F; /* Discard interrupt bit */
            uint32_t Ia = (I >> 8) & 0xFF;
            uint32_t Ib = (I >> 16) & 0xFF;
            uint32_t Ic = (I >> 24) & 0xFF;
            uint32_t Id = (I >> 8);
            int32_t Ids = ((int32_t) I >> 8);
            uint32_t Ie = (I >> 16);
            uint32_t Ies = ((int32_t) I >> 16);
            flow[pc].flags |= TYPEFLOW_LIVE_CODE;
            switch (Iop) {
                case JOP_NOOP:
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_ERROR:
                    flow[pc].flags |= TYPEFLOW_INPUT_E | TYPEFLOW_ERR;
                    flow[pc].e_types |= types[Ie];
                    break;
                case JOP_RETURN:
                    flow[pc].flags |= TYPEFLOW_INPUT_D | TYPEFLOW_DID_EXIT;
                    flow[pc].d_types |= types[Id];
                    rettype |= types[Id];
                    break;
                case JOP_RETURN_NIL:
                    rettype |= JANET_TFLAG_NIL;
                    flow[pc].flags |= TYPEFLOW_DID_EXIT;
                    break;
                case JOP_TYPECHECK:
                    flow[pc].flags |= TYPEFLOW_INPUT_A;
                    flow[pc].a_types |= types[Ia];
                    if (0 == (types[Ia] & (uint16_t) Ie)) { /* no overlap, never pass */
                        flow[pc].flags |= TYPEFLOW_ERR;
                        break;
                    }
                    if (types[Ia] == (types[Ia] & ((uint16_t) Ie))) {
                        ; /* type mask is super-set of available types, always pass */
                    } else {
                        /* sometimes pass */
                        flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    }
                    /* narrow types of output */
                    types[Ia] = (uint16_t) Ie & types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_ADD:
                case JOP_SUBTRACT:
                case JOP_MULTIPLY:
                    /* Closed over numbers, no runtime errors */
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    int is_numbers = (types[Ib] == types[Ic]) && (types[Ib] == JANET_TFLAG_NUMBER);
                    types[Ia] = is_numbers ? JANET_TFLAG_NUMBER : 0xFFFF;
                    if (!is_numbers) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    {
                        int no_abstract = !(types[Ib] & JANET_TFLAG_ABSTRACT) && !(types[Ic] & JANET_TFLAG_ABSTRACT);
                        if (no_abstract && !is_numbers) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        if (no_abstract) {
                            /* If no abstract types, we can dramatically narrow input typing for subsequent instructions */
                            types[Ib] = JANET_TFLAG_NUMBER;
                            types[Ic] = JANET_TFLAG_NUMBER;
                        }
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_DIVIDE:
                case JOP_DIVIDE_FLOOR:
                case JOP_MODULO:
                case JOP_REMAINDER:
                case JOP_BAND:
                case JOP_BOR:
                case JOP_BXOR:
                case JOP_BNOT:
                case JOP_SHIFT_LEFT:
                case JOP_SHIFT_RIGHT:
                case JOP_SHIFT_RIGHT_UNSIGNED:
                    /* Almost closed over numbers, but certain parameters will cause runtime errors */
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    {
                        int is_numbers = (types[Ib] == types[Ic]) && (types[Ib] == JANET_TFLAG_NUMBER);
                        int no_abstract = !(types[Ib] & JANET_TFLAG_ABSTRACT) && !(types[Ic] & JANET_TFLAG_ABSTRACT);
                        types[Ia] = is_numbers ? JANET_TFLAG_NUMBER : 0xFFFF;
                        if (no_abstract && !is_numbers) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        if (no_abstract) {
                            /* If no abstract types, we can dramatically narrow input typing for subsequent instructions */
                            types[Ib] = JANET_TFLAG_NUMBER;
                            types[Ic] = JANET_TFLAG_NUMBER;
                        }
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_ADD_IMMEDIATE:
                case JOP_SUBTRACT_IMMEDIATE:
                case JOP_MULTIPLY_IMMEDIATE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].b_types |= types[Ib];
                    {
                        int is_numbers = types[Ib] == JANET_TFLAG_NUMBER;
                        int no_abstract = !(types[Ib] & JANET_TFLAG_ABSTRACT);
                        if (no_abstract && !is_numbers) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        types[Ia] = (is_numbers) ? JANET_TFLAG_NUMBER : 0xFFFF;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_DIVIDE_IMMEDIATE:
                case JOP_SHIFT_LEFT_IMMEDIATE:
                case JOP_SHIFT_RIGHT_IMMEDIATE:
                case JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    flow[pc].b_types |= types[Ib];
                    {
                        int is_numbers = types[Ib] == JANET_TFLAG_NUMBER;
                        int no_abstract = !(types[Ib] & JANET_TFLAG_ABSTRACT);
                        if (no_abstract && !is_numbers) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        types[Ia] = (is_numbers) ? JANET_TFLAG_NUMBER : 0xFFFF;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MOVE_FAR:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_E | TYPEFLOW_INPUT_A;
                    flow[pc].a_types |= types[Ia];
                    types[Ie] = types[Ia];
                    flow[pc].e_types |= types[Ie];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MOVE_NEAR:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_E;
                    flow[pc].e_types |= types[Ie];
                    types[Ia] = types[Ie];
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_JUMP:
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    int32_t target = pc + Ids;
                    /* Merge with existing state */
                    int changed = 0;
                    Janet oldwrapper = janet_table_get(states, janet_wrap_integer(target));
                    if (janet_checktype(oldwrapper, JANET_NIL)) {
                        changed = 1;
                        uint16_t *new_types = array_allocate(sizeof(uint16_t), def->slotcount);
                        memcpy(new_types, types, sizeof(uint16_t) * def->slotcount);
                        janet_table_put(states, janet_wrap_integer(target), janet_wrap_pointer(new_types));
                    } else {
                        uint16_t *old_types = janet_unwrap_pointer(oldwrapper);
                        for (int32_t sloti = 0; sloti < def->slotcount; sloti++) {
                            if (old_types[sloti] != types[sloti]) changed = 1;
                            old_types[sloti] |= types[sloti];
                        }
                    }
                    if (changed) janet_v_push(state_stack, target);
                    break;
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT_NIL:
                    /* Traverse both branches. Use type-info for dead code elimination. */
                    flow[pc].flags |= TYPEFLOW_INPUT_A | TYPEFLOW_DID_PROCEED;
                    flow[pc].a_types |= types[Ia];
                    {
                        int changed = 0;
                        int nilcheck = (Iop == JOP_JUMP_IF_NIL) || (Iop == JOP_JUMP_IF_NOT_NIL);
                        int invert = (Iop == JOP_JUMP_IF_NOT) || (Iop == JOP_JUMP_IF_NOT_NIL);
                        uint16_t oldt = types[Ia];
                        uint16_t true_t = oldt & ~JANET_TFLAG_NIL; /* truthy can't be nil */
                        uint16_t false_t = oldt & (nilcheck ? JANET_TFLAG_NIL : (JANET_TFLAG_BOOLEAN | JANET_TFLAG_NIL)); /* falsey is either false or nil */
                        /*janet_assert(true_t | false_t, "branch is both always and never taken");*/
                        uint16_t taken = invert ? false_t : true_t;
                        uint16_t not_taken = invert ? true_t : false_t;
                        uint16_t branches[2] = { taken, not_taken };
                        int32_t targets[2] = { pc + 1, pc + Ies };
                        for (int j = 0; j < 2; j++) {
                            target = targets[j];
                            types[Ia] = branches[j];
                            /* Merge with existing state */
                            Janet oldwrapper = janet_table_get(states, janet_wrap_integer(target));
                            if (janet_checktype(oldwrapper, JANET_NIL)) {
                                changed = 1;
                                uint16_t *new_types = array_allocate(sizeof(uint16_t), def->slotcount);
                                memcpy(new_types, types, sizeof(uint16_t) * def->slotcount);
                                janet_table_put(states, janet_wrap_integer(target), janet_wrap_pointer(new_types));
                            } else {
                                uint16_t *old_types = janet_unwrap_pointer(oldwrapper);
                                for (int32_t sloti = 0; sloti < def->slotcount; sloti++) {
                                    if (old_types[sloti] != types[sloti]) changed = 1;
                                    old_types[sloti] |= types[sloti];
                                }
                            }
                            if (changed) janet_v_push(state_stack, target);
                        }
                        break;
                    }
                case JOP_GREATER_THAN:
                case JOP_LESS_THAN:
                case JOP_EQUALS:
                case JOP_GREATER_THAN_EQUAL:
                case JOP_LESS_THAN_EQUAL:
                case JOP_NOT_EQUALS:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    types[Ia] = JANET_TFLAG_BOOLEAN;
                    {
                        int is_numbers = (types[Ib] == types[Ic]) && (types[Ib] == JANET_TFLAG_NUMBER);
                        /* Maybe we don't need this? If we do need it, we can also narrow types */
                        if (!is_numbers) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_GREATER_THAN_IMMEDIATE:
                case JOP_LESS_THAN_IMMEDIATE:
                case JOP_EQUALS_IMMEDIATE:
                case JOP_NOT_EQUALS_IMMEDIATE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].b_types |= types[Ib];
                    types[Ia] = JANET_TFLAG_BOOLEAN;
                    {
                        int is_numbers = (types[Ib] == JANET_TFLAG_NUMBER);
                        /* Maybe we don't need this? If we do need it, we can also narrow types */
                        if (!is_numbers) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_COMPARE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    types[Ia] = JANET_TFLAG_NUMBER;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_NIL:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D;
                    types[Id] = JANET_TFLAG_NIL;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_TRUE:
                case JOP_LOAD_FALSE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D;
                    types[Id] = JANET_TFLAG_BOOLEAN;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_INTEGER:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A;
                    types[Ia] = JANET_TFLAG_NUMBER;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_CONSTANT:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A;
                    types[Ia] = 1 << janet_type(def->constants[Ie]);
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_UPVALUE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A;
                    types[Ia] = 0xFFFF; /* TODO - be smarter with upvalues */
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_LOAD_SELF:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D;
                    types[Id] = JANET_TFLAG_FUNCTION;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_SET_UPVALUE:
                    flow[pc].flags |= TYPEFLOW_INPUT_A;
                    flow[pc].a_types |= types[Ia];
                    /* TODO - save this information somehow */
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_CLOSURE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A;
                    types[Ia] = JANET_TFLAG_FUNCTION;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                /* We should do some analysis for push instructions as well -
                 * in most cases we should statically calculate the lower and
                 * upper bounds of arity for a function call. For the lower
                 * bound, we should also know which slots are parameters and
                 * record that information at the site of the function call.
                 * This must be conservative - an easy approach would be to
                 * simply stop keeping track after the first N arguments for
                 * the lower bound. For unknown arguments after the lower
                 * bound, there type would be the "any" type. Or we could just
                 * do a separate pass. */
                case JOP_PUSH:
                    flow[pc].flags |= TYPEFLOW_INPUT_D | TYPEFLOW_OUTPUT_STACK;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_PUSH_2:
                    flow[pc].flags |= TYPEFLOW_INPUT_A | TYPEFLOW_INPUT_E | TYPEFLOW_OUTPUT_STACK;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].e_types |= types[Ie];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_PUSH_3:
                    flow[pc].flags |= TYPEFLOW_INPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C | TYPEFLOW_OUTPUT_STACK;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    /* Every time we push a argument, add both slot index and types of slot to the stack. */
                    pc++;
                    continue;
                case JOP_PUSH_ARRAY:
                    flow[pc].flags |= TYPEFLOW_INPUT_D | TYPEFLOW_OUTPUT_STACK;
                    flow[pc].d_types |= types[Id];
                    int always_indexed = (types[Id] & JANET_TFLAG_INDEXED) == JANET_TFLAG_INDEXED;
                    if (!always_indexed) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    if (0 == (types[Id] & JANET_TFLAG_INDEXED)) {
                        flow[pc].flags |= TYPEFLOW_DID_EXIT;
                        break;
                    }
                    types[Id] &= JANET_TFLAG_INDEXED; /* narrowing */
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_CALL:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_E | TYPEFLOW_INPUT_STACK | TYPEFLOW_MAY_SIGNAL;
                    flow[pc].e_types |= types[Ie];
                    {
                        int might_call = types[Ie] & JANET_TFLAG_CALLABLE;
                        if (!might_call) {
                            /* Bad callee type, early exit */
                            flow[pc].flags |= TYPEFLOW_ERR;
                            break;
                        }
                        flow[pc].flags |= TYPEFLOW_MAY_SIGNAL | TYPEFLOW_DID_PROCEED;
                        /* This can be determined recursively. Ignore stack overflows for now. Exit flag
                         * can also be eventually inferred. */
                    }
                    types[Ie] &= JANET_TFLAG_CALLABLE; /* callee narrowing */
                    types[Ia] = 0xFFFF; /* TODO Type infer across functions. Narrow _after_ callee narrowing in case A = E */
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_TAILCALL:
                    flow[pc].flags |= TYPEFLOW_INPUT_D | TYPEFLOW_INPUT_STACK | TYPEFLOW_MAY_SIGNAL;
                    flow[pc].d_types |= types[Id];
                    {
                        int might_call = types[Id] & JANET_TFLAG_CALLABLE;
                        if (!might_call) {
                            /* Bad callee type, early exit */
                            flow[pc].flags |= TYPEFLOW_ERR;
                            break;
                        }
                        flow[pc].flags |= TYPEFLOW_MAY_SIGNAL | TYPEFLOW_DID_EXIT;
                        rettype |= 0xFFFF; /* TODO - get tailcall type */
                    }
                    break;
                case JOP_RESUME:
                case JOP_CANCEL:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    /* TODO - more checking here */
                    types[Ib] &= JANET_TFLAG_FIBER; /* narrowing on fiber type */
                    types[Ia] = 0xFFFF; /* Do _after_ setting b type in case A=B */
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_SIGNAL: {
                    if (Ic <= 4) {
                        /* terminal */
                        flow[pc].flags |= TYPEFLOW_INPUT_B;
                        flow[pc].b_types |= types[Ib];
                        flow[pc].flags |= TYPEFLOW_DID_EXIT;
                        if (Ic == 0) {
                            rettype |= types[Ib];
                        }
                        break;
                    }
                    /* non terminal */
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL | TYPEFLOW_DID_PROCEED;
                    flow[pc].b_types |= types[Ib];
                    types[Ia] = 0xFFFF; /* Maybe do better? */
                    flow[pc].a_types |= types[Ia];
                }
                pc++;
                continue;
                case JOP_PROPAGATE:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL | TYPEFLOW_DID_PROCEED;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    types[Ia] = 0xFFFF; /* Maybe do better? */
                    flow[pc].a_types |= types[Ia];
                    pc++;
                    continue;
                case JOP_IN:
                case JOP_GET:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    {
                        uint16_t aType = 0;
                        int definitely_odd = 0;
                        int maybe_odd = 1;
                        if (types[Ib] & (JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY | JANET_TFLAG_ABSTRACT)) {
                            aType = 0xFFFF; /* arrays, tuples, structs, and tables can contain anything */
                        }
                        if (types[Ib] & JANET_TFLAG_BYTES) {
                            aType |= JANET_TFLAG_NUMBER;
                        }
                        if (types[Ib] & JANET_TFLAG_FIBER) {
                            aType = 0xFFFF; /* We could narrow this in some cases but probably not worth it */
                        }
                        if (types[Ib] == (types[Ib] & (JANET_TFLAG_NIL | JANET_TFLAG_BOOLEAN | JANET_TFLAG_FUNCTION |
                                                       JANET_TFLAG_CFUNCTION | JANET_TFLAG_NUMBER | JANET_TFLAG_POINTER))) {
                            /* B is definitely not a container type (types[Ib] is subset of non-container types) */
                            /* nil or error - non container types */
                            definitely_odd = 1;
                        }
                        if (Iop == JOP_GET) {
                            if (maybe_odd) {
                                aType |= JANET_TFLAG_NIL;
                            }
                            if (types[Ib] & JANET_TFLAG_ABSTRACT) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                        } else {
                            if (maybe_odd) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                            if (definitely_odd) {
                                flow[pc].flags |= TYPEFLOW_DID_EXIT;
                                break;
                            }
                        }
                        types[Ia] = aType;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_PUT:
                    flow[pc].flags |= TYPEFLOW_INPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    /* could refine more - will exit = (types[Ia] not mutable) */
                    /* We can also narrow all inputs to remove combos that will always error for
                     * next instruction. */
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_GET_INDEX:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_PUT_INDEX:
                    flow[pc].flags |= TYPEFLOW_INPUT_A | TYPEFLOW_INPUT_B;
                    flow[pc].a_types |= types[Ia];
                    flow[pc].b_types |= types[Ib];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED | TYPEFLOW_MAY_SIGNAL;
                    pc++;
                    continue;
                case JOP_LENGTH:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_E;
                    flow[pc].e_types |= types[Ie];
                    {
                        if (types[Ie] & JANET_TFLAG_ABSTRACT) flow[pc].flags |= TYPEFLOW_MAY_SIGNAL;
                        /* If type of B is never abstract or a lengthable type, always error. */
                        if (0 == (types[Ie] & (JANET_TFLAG_LENGTHABLE | JANET_TFLAG_ABSTRACT))) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        uint16_t out_type = (types[Ie] & JANET_TFLAG_ABSTRACT) ? 0xFFFF : JANET_TFLAG_NUMBER;
                        types[Ie] &= JANET_TFLAG_LENGTHABLE; /* narrowing */
                        /* If type of B is not always abstract or lengthable type, sometimes error. */
                        /* An abstract type _could_ return a weird type for length. */
                        types[Ia] = out_type;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_ARRAY:
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_ARRAY;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_BUFFER:
                    // TODO - calculate static params
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_BUFFER;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_STRING:
                    // TODO - calculate static params
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_STRING;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_STRUCT:
                    // TODO - calculate static params
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_STRUCT;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_TABLE:
                    // TODO - calculate static params
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_TABLE;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_MAKE_TUPLE:
                case JOP_MAKE_BRACKET_TUPLE:
                    // TODO - calculate static params
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D | TYPEFLOW_INPUT_STACK;
                    types[Id] = JANET_TFLAG_TUPLE;
                    flow[pc].d_types |= types[Id];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
                case JOP_NEXT:
                    // TODO - careful consideration here would likely help good loop optimizations
                    flow[pc].flags |= TYPEFLOW_OUTPUT_A | TYPEFLOW_INPUT_B | TYPEFLOW_INPUT_C;
                    flow[pc].b_types |= types[Ib];
                    flow[pc].c_types |= types[Ic];
                    {
                        uint16_t outt = JANET_TFLAG_NIL;
                        if (0 == (types[Ib] & (JANET_TFLAG_BYTES | JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY | JANET_TFLAG_ABSTRACT | JANET_TFLAG_FIBER))) {
                            flow[pc].flags |= TYPEFLOW_DID_EXIT;
                            break;
                        }
                        if (types[Ib] & (JANET_TFLAG_BYTES | JANET_TFLAG_INDEXED)) {
                            outt |= JANET_TFLAG_NUMBER;
                        }
                        if (types[Ib] & JANET_TFLAG_FIBER) {
                            outt |= JANET_TFLAG_NUMBER;
                        }
                        if (types[Ib] & (JANET_TFLAG_DICTIONARY | JANET_TFLAG_ABSTRACT)) {
                            outt |= 0xFFFF;
                        }
                        /* TODO - narrowing? */
                        types[Ia] = outt;
                    }
                    flow[pc].a_types |= types[Ia];
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
            }
            break;
        }
    }
    // TODO - free all of the type bitsets in the states table */
    janet_free(types);
    janet_v_free(state_stack);
    *ret_types = rettype;
    return flow;
}

/* Remove dead code from lattice analysis */
static void lattice_dead_to_noop(JanetFuncDef *def, JanetTypeflowInstruction *infos) {
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        if (!(infos[i].flags & TYPEFLOW_LIVE_CODE)) {
            def->bytecode[i] = JOP_NOOP;
        }
    }
}

/* Local value numbering
 *
 * Mutate local code such that blocks only use the first
 * available instance of a value.
 *
 * Will not directly try to eliminate any dead code or unused variables, but
 * this should help make such eliminations easier.
 *
 * Example:
 *
 * (ldi 0 10)
 * (ldi 1 20)
 * (add 2 0 1)
 * (ldi 3 10) # slot 3 is the same as slot 0
 * (add 4 3 2)
 *
 * Goal conversion:
 *
 * (ldi 0 10)
 * (ldi 1 20)
 * (add 2 0 1)
 * (ldi 3 10) # or (movn 3 0)
 * (add 4 1 2) # use slot 0 instead slot 3
 *
 */

typedef struct {
    int32_t slot;
    int32_t num;
} NumberedSlot;

typedef struct {
    Janet value;
    int32_t num;
} NumberedValue;

/* Once we are making the replacement, generate
 * a load that is more suitable for
 * setting the target slot.
 *
 * Also consider adding support for new constants by appending
 * to the constant buffer. This would allow us to fully generalize to
 * constant propagation.
 *
 * TODO - some duplication of janetc_loadconst in emit.c */
static uint32_t lvn_bytecode_loadconst(JanetFuncDef *def, Janet k, int32_t reg) {
    switch (janet_type(k)) {
        case JANET_NIL:
            return ((uint32_t) reg << 8) | JOP_LOAD_NIL;
        case JANET_BOOLEAN:
            return ((uint32_t) reg << 8) |
                   (janet_unwrap_boolean(k) ? JOP_LOAD_TRUE : JOP_LOAD_FALSE);
        case JANET_NUMBER: {
            double dval = janet_unwrap_number(k);
            if (dval < INT16_MIN || dval > INT16_MAX)
                goto do_constant;
            int32_t i = (int32_t) dval;
            if (dval != i)
                goto do_constant;
            uint32_t iu = (uint32_t)i;
            return
                ((uint32_t) iu << 16) |
                ((uint32_t) reg << 8) |
                JOP_LOAD_INTEGER;
            break;
        }
        default:
        do_constant: {
                int32_t cindex = def->constants_length;
                for (int32_t i = 0; i < def->constants_length; i++) {
                    if (janet_equals(def->constants[i], k)) {
                        cindex = i;
                        break;
                    }
                }
                janet_assert(cindex < def->constants_length, "adding new constants NYI");
                return
                    ((uint32_t) cindex << 16) |
                    ((uint32_t) reg << 8) |
                    JOP_LOAD_CONSTANT;
                break;
            }
    }
}

#if 0
static uint32_t lvn_bytecode_domove(uint32_t dest, uint32_t src) {
    janet_assert((dest < 256) || (src < 256), "target too large");
    if (dest < 256) {
        return JOP_MOVE_NEAR | ((dest & 0xFF) << 8) | (src << 16);
    } else {
        return JOP_MOVE_FAR | ((src & 0xFF) << 8) | (dest << 16);
    }
}
#endif

/* Find needle with the lowest number */
static int32_t find_slot_for_constant(int32_t n, const NumberedValue *constants, Janet needle) {
    int32_t best = -1;
    int32_t lowest = INT32_MAX;
    for (int32_t i = 0; i < n; i++) {
        if (janet_equals(needle, constants[i].value)) {
            if (lowest > constants[i].num) {
                best = i;
                lowest = constants[i].num;
            }
        }
    }
    return best;
}

/* Value numbering (local for now) */
static void janet_ovm_value_numbering(JanetFuncDef *def, JanetBB bb) {

    if (bb.start >= bb.end) return;

    /* Initial data structures */
    /* Replacements: map slot -> slot. replacements[i] is the earliest (best) slot to use as an operand for slot i. If num=-1, don't replace.
     * Constants: map -> value. constants[i] is an optional constant known to be bound to slot i. */
    int32_t num = 0;
    NumberedSlot *replacements = janet_malloc(sizeof(NumberedSlot) * def->slotcount);
    if (replacements == NULL) {
        JANET_OUT_OF_MEMORY;
    }
    NumberedValue *constants = janet_malloc(sizeof(NumberedValue) * def->slotcount);
    if (constants == NULL) {
        JANET_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < def->slotcount; i++) {
        replacements[i].slot = -1;
        replacements[i].num = -1;
        constants[i].value = janet_wrap_nil();
        constants[i].num = -1;
    }

    for (int32_t i = bb.start; i < bb.end; i++) {
        uint32_t I = def->bytecode[i];
        uint32_t Iop = I & 0x7F;
        uint32_t Ia = (I >> 8) & 0xFF;
        uint32_t Ib = (I >> 16) & 0xFF;
        uint32_t Ic = (I >> 24) & 0xFF;
        uint32_t Id = (I >> 8);
        uint32_t Ie = (I >> 16);
        int32_t Ies = ((int32_t) I >> 16);
        switch (Iop) {
            default:
                continue;
            case JOP_PUSH:
            case JOP_PUSH_2:
            case JOP_PUSH_3: {
                int32_t a = (Iop == JOP_PUSH) ? Id : Ia;
                NumberedSlot arep = replacements[a];
                if (arep.num >= 0) {
                    def->bytecode[i] = def->bytecode[i] & 0xFFFF00FFU;
                    def->bytecode[i] = def->bytecode[i] | (((uint32_t)arep.slot & 0xFF) << 8);
                }
                if (Iop != JOP_PUSH) {
                    int32_t b = (Iop == JOP_PUSH_2) ? Ie : Ib;
                    NumberedSlot brep = replacements[b];
                    if (brep.num >= 0) {
                        def->bytecode[i] = def->bytecode[i] & 0xFF00FFFFU;
                        def->bytecode[i] = def->bytecode[i] | (((uint32_t)brep.slot & 0xFF) << 16);
                    }
                }
                if (Iop == JOP_PUSH_3) {
                    NumberedSlot crep = replacements[Ic];
                    if (crep.num >= 0) {
                        def->bytecode[i] = def->bytecode[i] & 0x00FFFFFFU;
                        def->bytecode[i] = def->bytecode[i] | (((uint32_t)crep.slot) << 24);
                    }
                }
            }
            continue;
            case JOP_MOVE_NEAR:
            case JOP_MOVE_FAR: {
                int32_t src = (Iop == JOP_MOVE_NEAR) ? Ie : Ia;
                int32_t dest = (Iop == JOP_MOVE_NEAR) ? Ia : Ie;
                if (src == dest) {
                    def->bytecode[i] = JOP_NOOP;
                    continue;
                }
                if (constants[src].num >= 0) { /* Propagate constants */
                    constants[dest].value = constants[src].value;
                    constants[dest].num = num++;
                }
                NumberedSlot src_rep = replacements[src];
                if (src_rep.num >= 0) src = src_rep.slot;
                if (constants[src].num >= 0) {
                    def->bytecode[i] = lvn_bytecode_loadconst(def, constants[src].value, dest);
                }
                if (constants[src].num >= 0) { /* Propagate constants */
                    constants[dest].value = constants[src].value;
                    constants[dest].num = num++;
                }
                if (src == dest) {
                    def->bytecode[i] = JOP_NOOP;
                    continue;
                }
            }
            continue;
            case JOP_LOAD_NIL:
            case JOP_LOAD_TRUE:
            case JOP_LOAD_FALSE:
            case JOP_LOAD_INTEGER: {
                Janet x =
                    (Iop == JOP_LOAD_NIL) ? janet_wrap_nil() :
                    (Iop == JOP_LOAD_TRUE) ? janet_wrap_true() :
                    (Iop == JOP_LOAD_FALSE) ? janet_wrap_false() :
                    janet_wrap_integer(Ies);
                uint32_t dest = (Iop == JOP_LOAD_INTEGER) ? Ia : Id;
                constants[dest].num = num++;
                constants[dest].value = x;
                replacements[dest].num = num++;
                replacements[dest].slot = find_slot_for_constant(def->slotcount, constants, x);
            }
            continue;
        }
    }

    janet_free(replacements);
    janet_free(constants);
}

/* Traverse backwards and remove writes that are never read.
 * Algorithm:
 * 1. Keep a bitset for all slots
 * 2. For every instruction, extract touched slots.
 *    if slot is written to, check bit. If bit is already 1, remove instruction. Otherwise, mark bit to 1
 *    if slot is read from, mark bit to 0
 * 3. If we encounter a function call, set all bits to 0 for now. We can probably use the closure_bitset
 *    for more fine-grained tracking.
 *
 * As is, this can't do much since most primitives have side effects. We should combine with the lattice_types
 * analysis to allow removal of instructions that don't have side effects.
 */
static void bb_remove_redundant_writes(JanetFuncDef *def, JanetBB bb) {
    if (bb.start >= bb.end) return; /* Degenerate block */
    uint32_t *bitset = make_bitset(def->slotcount);
    /* If no successors, clean up writes even more - all final writes are redundant. */
    if (bb.succ[0] == -1 && bb.succ[1] == -1) {
        memset(bitset, 0xFF, (def->slotcount + 7) / 8);
    }
    for (int32_t i = bb.end - 1; i >= bb.start; i--) {
        int32_t ins[3];
        int32_t out = -1;
        int nins = 0;
        int pin = 0; /* side effects, don't remove */
        uint32_t I = def->bytecode[i];
        uint32_t Iop = I & 0x7F;
        uint32_t Ia = (I >> 8) & 0xFF;
        uint32_t Ib = (I >> 16) & 0xFF;
        uint32_t Ic = (I >> 24) & 0xFF;
        uint32_t Id = (I >> 8);
        uint32_t Ie = (I >> 16);
        /*int32_t Ies = ((int32_t) I >> 16);*/
        /* Whenever we execute an instruction that can yield or await, clear the bitset.
         * Closures could read some slots that seem to be unused, and then control could
         * return to our function. */
        switch (Iop) {
            default:
                fprintf(stderr, "opcode = %u\n", Iop);
                janet_assert(0, "unhandled instruction");
                continue;
            case JOP_JUMP:
            case JOP_NOOP:
            case JOP_RETURN_NIL:
                continue;

            /* Side effects, don't remove */

            /* Write A, Read E */
            case JOP_CALL:
                /* Similar logic applies to tail calls, but they are always at the end of blocks anyway */
                out = Ia;
                pin = 1;
                nins = 1;
                ins[0] = Ie;
                break;
            /* Write A, Read B */
            case JOP_SIGNAL:
                out = Ia;
                nins = 1;
                ins[0] = Ib;
                pin = 1;
                break;
            /* Read A */
            case JOP_ERROR:
            case JOP_TYPECHECK:
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT_NIL:
            case JOP_SET_UPVALUE:
                nins = 1;
                pin = 1;
                ins[0] = Ia;
                break;
            /* Read D */
            case JOP_RETURN:
            case JOP_PUSH:
            case JOP_PUSH_ARRAY:
            case JOP_TAILCALL:
                pin = 1;
                nins = 1;
                ins[0] = Id;
                break;
            case JOP_PUT:
            case JOP_PUSH_3:
                nins = 3;
                pin = 1;
                ins[0] = Ia;
                ins[1] = Ib;
                ins[2] = Ic;
                break;
            /* Write A */
            case JOP_MAKE_ARRAY:
            case JOP_MAKE_BUFFER:
            case JOP_MAKE_STRING:
            case JOP_MAKE_STRUCT:
            case JOP_MAKE_TABLE:
            case JOP_MAKE_TUPLE:
            case JOP_MAKE_BRACKET_TUPLE:
                out = Ia;
                pin = 1; /* TODO - we need an instruction (or psuedo instruction) that can clear the stack but do nothing */
                break;
            case JOP_LOAD_INTEGER:
            case JOP_LOAD_CONSTANT:
            case JOP_LOAD_UPVALUE:
            case JOP_CLOSURE:
                out = Ia;
                break;
            /* Read A, B */
            case JOP_PUT_INDEX:
                nins = 2;
                ins[0] = Ia;
                ins[1] = Ib;
                pin = 1;
                break;
            /* Read A, E */
            case JOP_PUSH_2:
                nins = 2;
                ins[0] = Ia;
                ins[1] = Ie;
                pin = 1;
                break;
            /* A = B op C */
            case JOP_PROPAGATE:
            case JOP_RESUME:
                out = Ia;
                nins = 2;
                ins[0] = Ib;
                ins[1] = Ic;
                pin = 1;
                break;
            case JOP_CANCEL:
            case JOP_NEXT:
                pin = 1;
                out = Ia;
                nins = 2;
                ins[0] = Ib;
                ins[1] = Ic;
                break;

            /* No side effects below, can remove.
             * Some of these instructions may have side effects if
             * the inputs are abstracts. */

            /* Loads that trite D */
            case JOP_LOAD_NIL:
            case JOP_LOAD_TRUE:
            case JOP_LOAD_FALSE:
            case JOP_LOAD_SELF:
                out = Id;
                break;

            /* Moves and similar */
            case JOP_MOVE_FAR:
                out = Ie;
                nins = 1;
                ins[0] = Ia;
                break;
            case JOP_MOVE_NEAR:
            case JOP_LENGTH:
            case JOP_BNOT:
                out = Ia;
                nins = 1;
                ins[0] = Ie;
                break;

            /* A = B op C */
            case JOP_BAND:
            case JOP_BOR:
            case JOP_BXOR:
            case JOP_ADD:
            case JOP_SUBTRACT:
            case JOP_MULTIPLY:
            case JOP_DIVIDE:
            case JOP_DIVIDE_FLOOR:
            case JOP_MODULO:
            case JOP_REMAINDER:
            case JOP_SHIFT_LEFT:
            case JOP_SHIFT_RIGHT:
            case JOP_SHIFT_RIGHT_UNSIGNED:
            case JOP_GREATER_THAN:
            case JOP_LESS_THAN:
            case JOP_EQUALS:
            case JOP_COMPARE:
            case JOP_IN:
            case JOP_GET:
            case JOP_GREATER_THAN_EQUAL:
            case JOP_LESS_THAN_EQUAL:
            case JOP_NOT_EQUALS:
                out = Ia;
                nins = 2;
                ins[0] = Ib;
                ins[1] = Ic;
                break;

            /* A = op B imm */
            case JOP_ADD_IMMEDIATE:
            case JOP_SUBTRACT_IMMEDIATE:
            case JOP_MULTIPLY_IMMEDIATE:
            case JOP_DIVIDE_IMMEDIATE:
            case JOP_SHIFT_LEFT_IMMEDIATE:
            case JOP_SHIFT_RIGHT_IMMEDIATE:
            case JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
            case JOP_GREATER_THAN_IMMEDIATE:
            case JOP_LESS_THAN_IMMEDIATE:
            case JOP_EQUALS_IMMEDIATE:
            case JOP_NOT_EQUALS_IMMEDIATE:
            case JOP_GET_INDEX:
                out = Ia;
                nins = 1;
                ins[0] = Ib;
                break;
        }
        /* Test and set output bit in bitmap. If already set, change to noop. */
        /* Add check to avoid messing with upvalues */
        if (out != -1) {
            if (!pin && bs_read_bit(bitset, out) &&
                    (!def->closure_bitset || !bs_read_bit(def->closure_bitset, out))) {
                def->bytecode[i] = JOP_NOOP;
            }
            bs_set_bit(bitset, out);
        }
        /* Clear input slots from bitmap */
        for (int j = 0; j < nins; j++) {
            uint32_t mask = bs_mask(ins[j]);
            uint32_t index = bs_indx(ins[j]);
            bitset[index] &= ~mask;
        }
    }
    janet_free(bitset);
}

/* Debug */

static Janet debug_mask(uint16_t mask) {
    if (mask == 0xFFFF) return janet_ckeywordv("any");
    if (mask == (0xFFFF ^ JANET_TFLAG_NIL)) return janet_ckeywordv("not-nil");
    if (mask == (0xFFFF ^ JANET_TFLAG_NIL ^ JANET_TFLAG_BOOLEAN)) return janet_ckeywordv("any-truthy");
    /* Maybe a bit more succinct will be clearer */
    JanetString x = janet_formatc("%T", (int32_t) mask);
    return janet_wrap_string(x);
}

/* Convert to Janet values for debugging */
static Janet debug_lattice_types_instruction(JanetTypeflowInstruction i, int prepend, uint32_t bcode) {
    char buf[128] = { 0 };
    Janet tbuf[20];
    char *c = buf;
    Janet *t = tbuf;
    if (i.flags & TYPEFLOW_OUTPUT_A) {
        *c++ = 'A';
        *t++ = debug_mask(i.a_types);
    }
    if (i.flags & TYPEFLOW_OUTPUT_B) {
        *c++ = 'B';
        *t++ = debug_mask(i.b_types);
    }
    if (i.flags & TYPEFLOW_OUTPUT_C) {
        *c++ = 'C';
        *t++ = debug_mask(i.c_types);
    }
    if (i.flags & TYPEFLOW_OUTPUT_D) {
        *c++ = 'D';
        *t++ = debug_mask(i.d_types);
    }
    if (i.flags & TYPEFLOW_OUTPUT_E) {
        *c++ = 'E';
        *t++ = debug_mask(i.e_types);
    }
    if (i.flags & TYPEFLOW_OUTPUT_STACK) {
        *c++ = 'S';
    }
    if (c > buf) *c++ = ':';
    char *cc = c;
    if (i.flags & TYPEFLOW_INPUT_A) {
        *c++ = 'a';
        *t++ = debug_mask(i.a_types);
    }
    if (i.flags & TYPEFLOW_INPUT_B) {
        *c++ = 'b';
        *t++ = debug_mask(i.b_types);
    }
    if (i.flags & TYPEFLOW_INPUT_C) {
        *c++ = 'c';
        *t++ = debug_mask(i.c_types);
    }
    if (i.flags & TYPEFLOW_INPUT_D) {
        *c++ = 'd';
        *t++ = debug_mask(i.d_types);
    }
    if (i.flags & TYPEFLOW_INPUT_E) {
        *c++ = 'e';
        *t++ = debug_mask(i.e_types);
    }
    if (i.flags & TYPEFLOW_INPUT_STACK) {
        *c++ = 's';
    }
    if (c > cc) *c++ = ':';
    if (i.flags & TYPEFLOW_LIVE_CODE) {
        *c++ = 'l';
    } else {
        /* No other flags, should be clear an unambiguous */
        *c++ = 'd';
        *c++ = 'e';
        *c++ = 'a';
        *c++ = 'd';
    }
    if (i.flags & TYPEFLOW_DID_PROCEED) *c++ = 'p';
    if (i.flags & TYPEFLOW_DID_EXIT) *c++ = 'x';
    if (i.flags & TYPEFLOW_MAY_SIGNAL) *c++ = '?';
    int tbufsize = t - tbuf;
    if (prepend) {
        const Janet *asmcode = janet_unwrap_tuple(janet_asm_decode_instruction(bcode));
        Janet *tup = janet_tuple_begin(janet_tuple_length(asmcode) + 1 + (t - tbuf));
        int cursor = 0;
        tup[cursor++] = asmcode[0];
        for (int i = 0; i < tbufsize; i++) {
            tup[cursor++] = asmcode[i + 1]; /* add slots */
            tup[cursor++] = tbuf[i]; /* slot types */
        }
        for (int i = tbufsize; i < janet_tuple_length(asmcode) - 1; i++) {
            tup[cursor++] = asmcode[i + 1]; /* immediates, jump targets, etc. */
        }
        tup[cursor++] = janet_ckeywordv(buf);
        return janet_wrap_tuple(janet_tuple_end(tup));
    } else {
        Janet *tup = janet_tuple_begin(1 + tbufsize);
        for (int i = 0; i < tbufsize; i++) {
            tup[i] = tbuf[i];
        }
        tup[tbufsize] = janet_ckeywordv(buf);
        return janet_wrap_tuple(janet_tuple_end(tup));
    }
}

/* Create a debug structure for basic blocks */
static Janet debug_basic_blocks(JanetBB *bbs) {
    Janet startkw = janet_ckeywordv("start");
    Janet endkw = janet_ckeywordv("end");
    Janet preds = janet_ckeywordv("preds");
    Janet succs = janet_ckeywordv("succs");
    JanetArray *array = janet_array(janet_v_count(bbs));
    for (int32_t i = 0; i < janet_v_count(bbs); i++) {
        JanetBB *bb = bbs + i;
        JanetKV *st = janet_struct_begin(4);
        janet_struct_put(st, startkw, janet_wrap_number(bb->start));
        janet_struct_put(st, endkw, janet_wrap_number(bb->end));
        JanetArray *succarray = janet_array(2);
        JanetArray *predarray = janet_array(2);
        if (bb->succ[0] != -1) janet_array_push(succarray, janet_wrap_number(bb->succ[0]));
        if (bb->succ[1] != -1) janet_array_push(succarray, janet_wrap_number(bb->succ[1]));
        if (bb->pred[0] != -1) janet_array_push(predarray, janet_wrap_number(bb->pred[0]));
        if (bb->pred[1] != -1) janet_array_push(predarray, janet_wrap_number(bb->pred[1]));
        janet_struct_put(st, preds, janet_wrap_array(predarray));
        janet_struct_put(st, succs, janet_wrap_array(succarray));
        janet_array_push(array, janet_wrap_struct(janet_struct_end(st)));
    }
    return janet_wrap_array(array);
}

/* Built-in optimizer */

/* Debug counter */
static int32_t total_before = 0;
static int32_t total_removed = 0;

void janet_bytecode_ovm_optimize(JanetFuncDef *def) {
    int32_t initial_length = def->bytecode_length;
    total_before += initial_length;

    /* Basic optimization */
    janet_bytecode_movopt(def);
    janet_jump_threading(def);
    {
        JanetBB *bbs = janet_basic_blocks(def);
        for (int32_t i = 0; i < janet_v_count(bbs); i++) {
            //janet_ovm_value_numbering(def, bbs[i]);
            bb_remove_redundant_writes(def, bbs[i]); // slightly wrong
        }
        bb_dead_to_noop(def, bbs);
        janet_v_free(bbs);
    }
    janet_jump_threading(def);
    janet_bytecode_remove_noops(def);

    /* Lattice analysis */
    //uint16_t rettypes = 0;
    //JanetTypeflowInstruction *instrs = janet_bytecode_lattice_types(func->def, &rettypes);
    //if (!instrs) janet_panic("function too complicated");
    //JanetArray *ret = janet_array(func->def->bytecode_length + 1);
    //for (int32_t i = 0; i < func->def->bytecode_length; i++) {
    //    Janet x = debug_lattice_types_instruction(instrs[i]);
    //    janet_array_push(ret, x);
    //}

    /* Info */
    int32_t final_length = def->bytecode_length;
    total_removed += initial_length - final_length;
    const char *name = (const char *) def->name;
    janet_eprintf("Optimizing %-30s %04d-%03d (total instructions removed: %d of %d)\n", name ? name : "<anon>",
                  initial_length, initial_length - final_length, total_removed, total_before);
}

/* C Functions */

/* Test type flow analysis */
JANET_CORE_FN(cfun_ovm_lattice_types,
              "(ovm/lattice-types func &opt prepend-instruction)",
              "Do some static analysis on a function.") {
    janet_arity(argc, 1, 2);
    JanetFunction *func = janet_getfunction(argv, 0);
    int prepend = 0;
    if (argc >= 2 && janet_truthy(argv[1])) {
        prepend = 1;
    }
    uint16_t rettypes = 0;
    JanetTypeflowInstruction *instrs = janet_bytecode_lattice_types(func->def, &rettypes);
    if (!instrs) janet_panic("function too complicated");
    JanetArray *ret = janet_array(func->def->bytecode_length + 1);
    for (int32_t i = 0; i < func->def->bytecode_length; i++) {
        Janet x = debug_lattice_types_instruction(instrs[i], prepend, func->def->bytecode[i]);
        janet_array_push(ret, x);
    }
    janet_array_push(ret, debug_mask(rettypes));
    janet_free(instrs);
    return janet_wrap_array(ret);
}

/* Test type flow analysis */
JANET_CORE_FN(cfun_ovm_basic_blocks,
              "(ovm/basic-blocks func)",
              "Do some static analysis on a function.") {
    janet_fixarity(argc, 1);
    JanetFunction *func = janet_getfunction(argv, 0);
    JanetBB *bbs = janet_basic_blocks(func->def);
    Janet debug_rep = debug_basic_blocks(bbs);
    janet_v_free(bbs);
    return debug_rep;
}

/* Module entry point */
void janet_lib_ovm(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("ovm/lattice-types", cfun_ovm_lattice_types),
        JANET_CORE_REG("ovm/basic-blocks", cfun_ovm_basic_blocks),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}

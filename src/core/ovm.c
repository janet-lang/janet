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
#endif

/* The OVM (optimistic virtual machine) is an internal interpreter for an
 * overlapping set of the base VM bytecode (in vm.c), focusing on the fast
 * path. At any point, instructions the ovm may deoptimize and fall back to the
 * default vm.c. The focus for performance will be tight loops, numerical code,
 * and hints for JIT compilation.
 *
 * The OVM bytecode is an optimized version of the interpreter bytecode, so
 * will not have a 1-1 instruction correspondence. For deopt, we will need to
 * revert PC back to where it would be in original bytecode as well as preserve
 * slot mappings. We can make some trade-offs here to allow for better
 * optimization, where VM bytecode behavior is not perfectly preserved in the
 * OVM in the case of errors. This also let's us optimize the interpreter while
 * preserving the bytecode format.
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
 * - Disable software breakpointing
 */

/* Type-checking and static analysis */

/* Allow easily saving states in hashtable by packing them to a Janet string */
/* TODO - use a representation that doesn't require touching GC and we can more easily cleanup.
 * Both the table and the strings will work fine not being on the heap and cleaned up immediately. */

static JanetString save_typeflow_state(int32_t pc, uint16_t *types) {
    int32_t slotcount = janet_v_count(types);
    uint8_t *buf = janet_string_begin(4 + sizeof(uint16_t) * 2);
    ((int32_t *)buf)[0] = pc;
    safe_memcpy(buf + 4, types, sizeof(uint16_t) * (size_t) slotcount);
    return janet_string_end(buf);
}

static void load_typeflow_state(JanetString saved_state, int32_t *pc, uint16_t *types) {
    int32_t slot_count = (janet_string_length(saved_state) - 4) / 2;
    /* We know this work only in this case. If we have seen a state before such that we are loading it, types
     * has enough backing capacity already since we never decrease the backing store. */
    janet_v__cnt(types) = slot_count;
    safe_memcpy(types, saved_state + 4, sizeof(uint16_t) * (size_t) slot_count);
    *pc = ((int32_t *)saved_state)[0];
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

/* Flags for typeflow instruction */
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
 * def->bytecode_length elements. The caller must free the returned pointer with janet_free. */

JanetTypeflowInstruction *janet_bytecode_typeflow(JanetFuncDef *def, uint16_t *ret_types) {
    JanetTable *states = janet_table(0);
    JanetString *state_stack = NULL;
    uint16_t *types = NULL;
    uint16_t rettype = 0;
    int pc = 0;
    /* Setup return buffer */
    JanetTypeflowInstruction *flow = array_allocate(sizeof(JanetTypeflowInstruction), def->bytecode_length);
    memset(flow, 0, sizeof(JanetTypeflowInstruction) * def->bytecode_length);
    /* Setup initial state */
    /* TODO - allow priming this */
    for (int32_t i = 0; i < def->slotcount; i++) {
        janet_v_push(types, (uint16_t) 0xFFFF);
    }
    janet_v_push(state_stack, save_typeflow_state(0, types));
    /* While we have more states to visit, traverse them */
    while (janet_v_count(state_stack)) {
        JanetString state = janet_v_last(state_stack);
        janet_v_pop(state_stack);
        load_typeflow_state(state, &pc, types);
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
            int32_t Ies = ((int32_t) I >> 16);
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
                    rettype |= JANET_TFLAG_NIL | TYPEFLOW_DID_EXIT;
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
                    if (Ids <= 0) {
                        /* backwards jump */
                        Janet state = janet_wrap_string(save_typeflow_state(pc, types));
                        if (!janet_checktype(janet_table_get(states, state), JANET_STRING)) {
                            janet_table_put(states, state, state);
                            janet_v_push(state_stack, janet_unwrap_string(state));
                        }
                        break;
                    } else {
                        /* forward jump */
                        pc += Ids;
                    }
                    continue;
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT_NIL:
                    /* Traverse both branches. Use type-info for dead code elimination. */
                    flow[pc].flags |= TYPEFLOW_INPUT_A;
                    flow[pc].a_types |= types[Ia];
                    {
                        int nilcheck = (Iop == JOP_JUMP_IF_NIL) || (Iop == JOP_JUMP_IF_NOT_NIL);
                        int invert = (Iop == JOP_JUMP_IF_NOT) || (Iop == JOP_JUMP_IF_NOT_NIL);
                        uint16_t oldt = types[Ia];
                        uint16_t true_t = oldt & ~JANET_TFLAG_NIL; /* truthy can't be nil */
                        uint16_t false_t = oldt & (nilcheck ? JANET_TFLAG_NIL : (JANET_TFLAG_BOOLEAN | JANET_TFLAG_NIL)); /* falsey is either false or nil */
                        janet_assert(true_t | false_t, "branch is both always and never taken");
                        uint16_t taken = invert ? false_t : true_t;
                        uint16_t not_taken = invert ? true_t : false_t;
                        if (taken != 0) { /* taken == 0 means we will never take the branch */
                            int32_t target = pc + Ies;
                            Janet state = janet_wrap_string(save_typeflow_state(target, types));
                            if (!janet_checktype(janet_table_get(states, state), JANET_STRING)) {
                                janet_table_put(states, state, state);
                                janet_v_push(state_stack, janet_unwrap_string(state));
                            }
                        }
                        if (not_taken == 0) { /* not_taken == 0 means we always take the branch */
                            break;
                        }
                    }
                    flow[pc].flags |= TYPEFLOW_DID_PROCEED;
                    pc++;
                    continue;
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
                    flow[pc].flags |= TYPEFLOW_OUTPUT_D;
                    types[Id] = JANET_TFLAG_NUMBER;
                    flow[pc].d_types |= types[Id];
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
                        // TODO - track push counts
                        janet_v__cnt(types) = def->slotcount; /* Reset pushed arg counts */
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
                        int might_call = types[Ie] & JANET_TFLAG_CALLABLE;
                        if (!might_call) {
                            /* Bad callee type, early exit */
                            flow[pc].flags |= TYPEFLOW_ERR;
                            break;
                        }
                        // TODO - see JOP_CALL above
                        janet_v__cnt(types) = def->slotcount; /* Reset pushed arg counts */
                        types[Ia] = 0xFFFF; /* TODO Type infer across functions */
                        flow[pc].flags |= TYPEFLOW_MAY_SIGNAL | TYPEFLOW_DID_EXIT;
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
                case JOP_SIGNAL:
                    {
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
                        flow[pc].a_types |= 0xFFFF; /* Maybe can do better? The yield could be anything. */
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
                        if (types[Ib] & (JANET_TFLAG_NIL | JANET_TFLAG_BOOLEAN | JANET_TFLAG_FUNCTION |
                                    JANET_TFLAG_CFUNCTION | JANET_TFLAG_NUMBER | JANET_TFLAG_POINTER)) {
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
    janet_v_free(types);
    janet_v_free(state_stack);
    *ret_types = rettype;
    return flow;
}

typedef enum {
    JOVM_CHECK_RESULT_OK,
    JOVM_CHECK_RESULT_DEOPT
} JanetOVMCheckResult;

typedef enum {
    JOVM_RUN_RESULT_RETURNED,
    JOVM_RUN_RESULT_ERRORED
} JanetOVMRunResult;

/* Verify input and state before committing to optimization, or do any calculations that can be done
 * without committing to the optimization. */
typedef enum {
    JOVM_CHECK_NUMBERS, /* Assert bitset of input slots have numbers */
    JOVM_CHECK_BOOLS, /* Assert bitset of input slots have booleans */
    JOVM_CHECK_ARRAYS,
    JOVM_CHECK_TABLES,
    JOVM_CHECK_STRINGS,
    JOVM_CHECK_BUFFERS,
    JOVM_COMMIT, /* Deoptimization after here is an error instead of a deoptimization */
} JanetOVMSetupOpcode;

/* Opcodes for committed optimistic interpretation. Invariants that fail here are panics (or potentially janet_assert) */
typedef enum {
    JOVM_NOOP,
    JOVM_LOAD_TRUE,
    JOVM_LOAD_FALSE,
    JOVM_LOAD_NIL,
    JOVM_LOAD_INTEGER,
    JOVM_LOAD_CONSTANT,
    JOVM_PUSH,
    JOVM_PUSH_2,
    JOVM_PUSH_3,
    JOVM_CALLK,
    JOVM_TCALLK,
    JOVM_CFUN_CALLK,
    JOVM_CFUN_TCALLK,
    JOVM_RET,
    JOVM_RETN,
    JOVM_RETI, /* Return integer */
    JOVM_RETK, /* Return true/false/constant? */
    JOVM_MOVE,

    /* Operations */
    JOVM_ADD,
    JOVM_ADD_IMM,
    JOVM_SUB,
    JOVM_MUL,
    JOVM_MUL_IMM,

    /* TODO shifts, bitops, other math ops, etc, inline functions in math/ and core, etc. */

    /* Comparison w/o branching */
    JOVM_IF_LT,
    JOVM_IF_LT_IMM,
    JOVM_IF_LTE,
    JOVM_IF_LTE_IMM,
    JOVM_IF_EQ,
    JOVM_IF_EQ_IMM,
    JOVM_IF_NEQ,
    JOVM_IF_NEQ_IMM,
    JOVM_IF_GT,
    JOVM_IF_GT_IMM,
    JOVM_IF_GTE,
    JOVM_IF_GTE_IMM,
    JOVM_IF_NIL,
    JOVM_IF_NNIL,
    JOVM_IF,
    JOVM_IF_NOT,

    /* Conditional branching (keep exact order with above comparisons) */
    JOVM_BLT,
    JOVM_BLT_IMM,
    JOVM_BLTE,
    JOVM_BLTE_IMM,
    JOVM_BEQ,
    JOVM_BEQ_IMM,
    JOVM_BNEQ,
    JOVM_BNEQ_IMM,
    JOVM_BGT,
    JOVM_BGT_IMM,
    JOVM_BGTE,
    JOVM_BGTE_IMM,
    JOVM_BNIL,
    JOVM_BNNIL,
    JOVM_BIF,
    JOVM_BIFN,

    /* Load/store */
    JOVM_PUT,
    JOVM_PUTK,
    JOVM_GET,
    JOVM_GETK,
    JOVM_IN,
    JOVM_INK,

    /* Other */
    JOVM_JUMP,
    JOVM_ERROR
} JanetOVMOpcode;

#if 0

static void janet_ovm_dump(JanetFunction *func) {
    for (size_t i = 0; i < func->ovm_bytecode_size; i++) {
        janet_eprintf("%.4u %X\n", i, func->ovm_bytecode[i]); /* TODO - better debug print */
    }
}

/* Virtual registers
 *
 * One instruction word
 * CC | BB | AA | OP
 * DD | DD | DD | OP
 * EE | EE | AA | OP
 */
/* Function versions of the macros in vm.c for extracting instruction parameters */
static uint8_t xA(uint32_t instr) { return (uint8_t) ((instr >> 8) & 0xFF); }
static uint8_t xB(uint32_t instr) { return (uint8_t) ((instr >> 16) & 0xFF); }
static uint8_t xC(uint32_t instr) { return (uint8_t) (instr >> 24); }
static uint32_t xD(uint32_t instr) { return instr >> 8; }
static uint16_t xE(uint32_t instr) { return (uint16_t) (instr >> 16); }
static int8_t xCS(uint32_t instr) { return (int8_t)(instr >> 24); }
static int32_t xDS(uint32_t instr) { return (int32_t)(instr >> 8); }
static int16_t xES(uint32_t instr) { return (int16_t)(instr >> 16); }

/* Build instructions more easily */
static uint32_t makeir_abc(JanetOVMOpcode opcode, uint8_t a, uint8_t b, uint8_t c) {
    uint32_t op = (uint32_t) opcode;
    uint32_t A = (uint32_t) a;
    uint32_t B = (uint32_t) b;
    uint32_t C = (uint32_t) c;
    return op | (A << 8) | (B << 16) | (C << 24);
}
static uint32_t makeir_d(JanetOVMOpcode opcode, uint32_t d) {
    uint32_t op = (uint32_t) opcode;
    uint32_t D = (uint32_t) d;
    return op | (D << 8);
}
static uint32_t makeir_ae(JanetOVMOpcode opcode, uint8_t a, uint16_t e) {
    uint32_t op = (uint32_t) opcode;
    uint32_t A = (uint32_t) a;
    uint32_t E = (uint32_t) e;
    return op | (A << 8) | (E << 16);
}

/* Generate OVM Bytecode if possible. If we return DEOPT, nothing was done, otherwise
 * we generated ovm bytecode. */
JanetOVMCheckResult janet_ovm_optimize(JanetFunction *func) {

    JanetFuncDef *def = func->def;
    if (def->environments_length > 0) return JOVM_CHECK_RESULT_DEOPT;
    if (def->named_args_count > 0) return JOVM_CHECK_RESULT_DEOPT; /* Probably not totally needed but will make it easier to start */
    if (def->slotcount > 256) return JOVM_CHECK_RESULT_DEOPT; /* Also could be improved later */

    uint32_t *instructions = NULL;
    const uint32_t *instr_end = def->bytecode + def->bytecode_length;

    /* Keep track of last load for fusion during initial lowering */
    int32_t last_load_slot = -1;
    int32_t last_load_payload = 0;
    int32_t last_load_index = 0;
    enum {
        LAST_LOAD_NONE
        LAST_LOAD_CONSTANT,
        LAST_LOAD_INTEGER
    } last_load = LAST_LOAD_NONE;

    /* Keep track of last comparison as well */
    int32_t last_compare_slot = -1;
    int32_t last_compare_index = 0;

    /* Initial lowering of Janet abstract machine code to OVM code */
    for (uint32_t *instr = def->bytecode; instr < instr_end; instr++) {
        uint32_t opcode = *instr & 0x7F;
        switch (opcode) {
            default:
                /* Clean up and return deopt - TODO - scan once before allocations to avoid allocation in deopt case */
                janet_v_free(instructions);
                return JOVM_CHECK_RESULT_DEOPT;
            case JOP_NOOP:
                janet_v_push(instructions, JOVM_NOOP);
                break;
            case JOP_LOAD_NIL:
                last_load = LAST_LOAD_NONE;
                janet_v_push(instructions, JOVM_LOAD_NIL);
                break;
            case JOP_LOAD_TRUE:
                last_load = LAST_LOAD_NONE;
                janet_v_push(instructions, JOVM_LOAD_TRUE);
                break;
            case JOP_LOAD_FALSE:
                last_load = LAST_LOAD_NONE;
                janet_v_push(instructions, JOVM_LOAD_FALSE);
                break;
            case JOP_LOAD_INTEGER:
                last_load = LAST_LOAD_INTEGER;
                last_load_slot = xA(*instr);
                last_load_index = janet_v_count(instructions);
                last_load_payload = xES(*instr);
                janet_v_push(instructions, JOVM_LOAD_INTEGER | (*instr & 0xFFFFFF00U));
                break;
            case JOP_RETURN:
                /* TODO - emit JOVM_RETK and JOVM_RETI when possible */
                janet_v_push(instructions, JOVM_RET | (*instr & 0xFFFFFF00U));
                break;
            case JOP_RETURN_NIL:
                last_load = LAST_LOAD_NONE;
                janet_v_push(instructions, JOVM_RETN);
                break;
            case JOP_LOAD_CONSTANT:
                last_load = LAST_LOAD_CONSTANT;
                last_load_slot = xA(*instr);
                last_load_index = janet_v_count(instructions);
                last_load_payload = xES(*instr);
                janet_v_push(instructions, JOVM_LOAD_CONSTANT | (*instr & 0xFFFFFF00U));
                break;
            case JOP_PUSH:
                janet_v_push(instructions, JOVM_PUSH | (*instr & 0xFFFFFF00U));
                break;
            case JOP_PUSH_2:
                janet_v_push(instructions, JOVM_PUSH_2 | (*instr & 0xFFFFFF00U));
                break;
            case JOP_PUSH_3:
                janet_v_push(instructions, JOVM_PUSH_3 | (*instr & 0xFFFFFF00U));
                break;
            case JOP_MOVE_NEAR:
                /* We currently disallow slots over 256 */
                janet_v_push(instructions, JOVM_MOVE | (*instr & 0xFFFFFF00U));
                break;
            case JOP_MOVE_FAR:
                {
                    /* We currently disallow slots over 256, so the order swap is safe */
                    uint8_t to = (uint8_t) xA(*instr);
                    uint16_t from = (uint16_t) xE(*instr);
                    janet_v_push(instructions, makeir_ae(JOVM_MOVE, to, from));
                }
                break;
#define BINOP(NAME, NAME2) \
            case NAME: \
                { \
                    uint8_t a = xA(*instr); \
                    uint8_t b = xB(*instr); \
                    uint8_t c = xC(*instr); \
                    if (a == last_load_slot) last_load = LAST_LOAD_NONE; \
                    if (a == last_compare_index) last_compare_index = -1; \
                    janet_v_push(instructions, makeir_abc(NAME2, a, b, c)); \
                } \
                break;
            BINOP(JOP_ADD, JOVM_ADD);
            BINOP(JOP_ADD_IMMEDIATE, JOVM_ADD_IMM);
            BINOP(JOP_SUBTRACT, JOVM_SUB);
            BINOP(JOP_MULTIPLY, JOVM_MUL);
            BINOP(JOP_MULTIPLY_IMMEDIATE, JOVM_MUL_IMM);
            case JOP_CALL:
                {
                    /* Only support optimization when callee is obviously statically known.
                     * The current compiler tends to generate code like this so we take advantage. */
                    if (LAST_LOAD_CONSTANT != last_load) {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                    uint16_t actual_callee_slot = xE(*instr);
                    if (actual_callee_slot != last_load_slot) {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                    uint8_t ret_reg = xA(*instr);
                    Janet callee = def->constants[last_load_payload];
                    if (janet_checktype(callee, JANET_FUNCTION)) {
                        instructions[last_load_index] = JOVM_NOOP;
                        janet_v_push(instructions, makeir_ae(JOVM_CALLK, ret_reg, last_load_payload));
                        break;
                    } else if (janet_checktype(callee, JANET_CFUNCTION)) {
                        instructions[last_load_index] = JOVM_NOOP;
                        janet_v_push(instructions, makeir_ae(JOVM_CFUN_CALLK, ret_reg, last_load_payload));
                        break;
                    } else {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                }
            case JOP_TCALL:
                {
                    /* Only support optimization when callee is obviously statically known.
                     * The current compiler tends to generate code like this so we take advantage. */
                    if (LAST_LOAD_CONSTANT != last_load) {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                    uint32_t actual_callee_slot = xD(*instr);
                    if (actual_callee_slot != last_load_slot) {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                    Janet callee = def->constants[last_load_payload];
                    if (janet_checktype(callee, JANET_FUNCTION)) {
                        instructions[last_load_index] = JOVM_NOOP;
                        janet_v_push(instructions, makeir_c(JOVM_TCALLK, last_load_payload));
                        break;
                    } else if (janet_checktype(callee, JANET_CFUNCTION)) {
                        instructions[last_load_index] = JOVM_NOOP;
                        janet_v_push(instructions, makeir_c(JOVM_CFUN_TCALLK, last_load_payload));
                        break;
                    } else {
                        janet_v_free(instructions);
                        return JOVM_CHECK_RESULT_DEOPT;
                    }
                }
        }
    }

    /* Debug */
    janet_ovm_dump(func);
    return JOVM_CHECK_RESULT_DEOPT;
}

/* Check if we can attempt ovm interpretation */
JanetOVMCheckResult janet_check_ovm(JanetFiber *fiber, JanetFunction *func) {
    JanetFuncDef *def = func->def;

    /* TODO - handle setting and removing breakpoints */
    if (func->gc.flags & JANET_FUNCFLAG_TRACE) return JOVM_CHECK_RESULT_DEOPT;
    if (!def->ovm_bytecode) return JOVM_CHECK_RESULT_DEOPT;

    return JOVM_CHECK_RESULT_OK;
}

/* Attempt ovm interpretation */
JanetOVMRunResult janet_run_ovm(JanetFiber *fiber, JanetFunction *func, Janet *output) {

}

#endif

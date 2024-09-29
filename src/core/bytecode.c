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
#include "gc.h"
#include "util.h"
#include "regalloc.h"
#endif

/* Look up table for instructions */
enum JanetInstructionType janet_instructions[JOP_INSTRUCTION_COUNT] = {
    JINT_0, /* JOP_NOOP, */
    JINT_S, /* JOP_ERROR, */
    JINT_ST, /* JOP_TYPECHECK, */
    JINT_S, /* JOP_RETURN, */
    JINT_0, /* JOP_RETURN_NIL, */
    JINT_SSI, /* JOP_ADD_IMMEDIATE, */
    JINT_SSS, /* JOP_ADD, */
    JINT_SSI, /* JOP_SUBTRACT_IMMEDIATE, */
    JINT_SSS, /* JOP_SUBTRACT, */
    JINT_SSI, /* JOP_MULTIPLY_IMMEDIATE, */
    JINT_SSS, /* JOP_MULTIPLY, */
    JINT_SSI, /* JOP_DIVIDE_IMMEDIATE, */
    JINT_SSS, /* JOP_DIVIDE, */
    JINT_SSS, /* JOP_DIVIDE_FLOOR */
    JINT_SSS, /* JOP_MODULO, */
    JINT_SSS, /* JOP_REMAINDER, */
    JINT_SSS, /* JOP_BAND, */
    JINT_SSS, /* JOP_BOR, */
    JINT_SSS, /* JOP_BXOR, */
    JINT_SS, /* JOP_BNOT, */
    JINT_SSS, /* JOP_SHIFT_LEFT, */
    JINT_SSI, /* JOP_SHIFT_LEFT_IMMEDIATE, */
    JINT_SSS, /* JOP_SHIFT_RIGHT, */
    JINT_SSI, /* JOP_SHIFT_RIGHT_IMMEDIATE, */
    JINT_SSS, /* JOP_SHIFT_RIGHT_UNSIGNED, */
    JINT_SSU, /* JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE, */
    JINT_SS, /* JOP_MOVE_FAR, */
    JINT_SS, /* JOP_MOVE_NEAR, */
    JINT_L, /* JOP_JUMP, */
    JINT_SL, /* JOP_JUMP_IF, */
    JINT_SL, /* JOP_JUMP_IF_NOT, */
    JINT_SL, /* JOP_JUMP_IF_NIL, */
    JINT_SL, /* JOP_JUMP_IF_NOT_NIL, */
    JINT_SSS, /* JOP_GREATER_THAN, */
    JINT_SSI, /* JOP_GREATER_THAN_IMMEDIATE, */
    JINT_SSS, /* JOP_LESS_THAN, */
    JINT_SSI, /* JOP_LESS_THAN_IMMEDIATE, */
    JINT_SSS, /* JOP_EQUALS, */
    JINT_SSI, /* JOP_EQUALS_IMMEDIATE, */
    JINT_SSS, /* JOP_COMPARE, */
    JINT_S, /* JOP_LOAD_NIL, */
    JINT_S, /* JOP_LOAD_TRUE, */
    JINT_S, /* JOP_LOAD_FALSE, */
    JINT_SI, /* JOP_LOAD_INTEGER, */
    JINT_SC, /* JOP_LOAD_CONSTANT, */
    JINT_SES, /* JOP_LOAD_UPVALUE, */
    JINT_S, /* JOP_LOAD_SELF, */
    JINT_SES, /* JOP_SET_UPVALUE, */
    JINT_SD, /* JOP_CLOSURE, */
    JINT_S, /* JOP_PUSH, */
    JINT_SS, /* JOP_PUSH_2, */
    JINT_SSS, /* JOP_PUSH_3, */
    JINT_S, /* JOP_PUSH_ARRAY, */
    JINT_SS, /* JOP_CALL, */
    JINT_S, /* JOP_TAILCALL, */
    JINT_SSS, /* JOP_RESUME, */
    JINT_SSU, /* JOP_SIGNAL, */
    JINT_SSS, /* JOP_PROPAGATE */
    JINT_SSS, /* JOP_IN, */
    JINT_SSS, /* JOP_GET, */
    JINT_SSS, /* JOP_PUT, */
    JINT_SSU, /* JOP_GET_INDEX, */
    JINT_SSU, /* JOP_PUT_INDEX, */
    JINT_SS, /* JOP_LENGTH */
    JINT_S, /* JOP_MAKE_ARRAY */
    JINT_S, /* JOP_MAKE_BUFFER */
    JINT_S, /* JOP_MAKE_STRING */
    JINT_S, /* JOP_MAKE_STRUCT */
    JINT_S, /* JOP_MAKE_TABLE */
    JINT_S, /* JOP_MAKE_TUPLE */
    JINT_S, /* JOP_MAKE_BRACKET_TUPLE */
    JINT_SSS, /* JOP_GREATER_THAN_EQUAL */
    JINT_SSS, /* JOP_LESS_THAN_EQUAL */
    JINT_SSS, /* JOP_NEXT */
    JINT_SSS, /* JOP_NOT_EQUALS, */
    JINT_SSI, /* JOP_NOT_EQUALS_IMMEDIATE, */
    JINT_SSS /* JOP_CANCEL, */
};

/* Remove all noops while preserving jumps and debugging information.
 * Useful as part of a filtering compiler pass. */
void janet_bytecode_remove_noops(JanetFuncDef *def) {

    /* Get an instruction rewrite map so we can rewrite jumps */
    uint32_t *pc_map = janet_smalloc(sizeof(uint32_t) * (1 + def->bytecode_length));
    uint32_t new_bytecode_length = 0;
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        uint32_t opcode = instr & 0x7F;
        pc_map[i] = new_bytecode_length;
        if (opcode != JOP_NOOP) {
            new_bytecode_length++;
        }
    }
    pc_map[def->bytecode_length] = new_bytecode_length;

    /* Linear scan rewrite bytecode and sourcemap. Also fix jumps. */
    int32_t j = 0;
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        uint32_t opcode = instr & 0x7F;
        int32_t old_jump_target = 0;
        int32_t new_jump_target = 0;
        switch (opcode) {
            case JOP_NOOP:
                continue;
            case JOP_JUMP:
                /* relative pc is in DS field of instruction */
                old_jump_target = i + (((int32_t)instr) >> 8);
                new_jump_target = pc_map[old_jump_target];
                instr += (uint32_t)(new_jump_target - old_jump_target + (i - j)) << 8;
                break;
            case JOP_JUMP_IF:
            case JOP_JUMP_IF_NIL:
            case JOP_JUMP_IF_NOT:
            case JOP_JUMP_IF_NOT_NIL:
                /* relative pc is in ES field of instruction */
                old_jump_target = i + (((int32_t)instr) >> 16);
                new_jump_target = pc_map[old_jump_target];
                instr += (uint32_t)(new_jump_target - old_jump_target + (i - j)) << 16;
                break;
            default:
                break;
        }
        def->bytecode[j] = instr;
        if (def->sourcemap != NULL) {
            def->sourcemap[j] = def->sourcemap[i];
        }
        j++;
    }

    /* Rewrite symbolmap */
    for (int32_t i = 0; i < def->symbolmap_length; i++) {
        JanetSymbolMap *sm = def->symbolmap + i;
        /* Don't rewrite upvalue mappings */
        if (sm->birth_pc < UINT32_MAX) {
            sm->birth_pc = pc_map[sm->birth_pc];
            sm->death_pc = pc_map[sm->death_pc];
        }
    }

    def->bytecode_length = new_bytecode_length;
    def->bytecode = janet_realloc(def->bytecode, def->bytecode_length * sizeof(uint32_t));
    janet_sfree(pc_map);
}

/* Remove redundant loads, moves and other instructions if possible and convert them to
 * noops. Input is assumed valid bytecode. */
void janet_bytecode_movopt(JanetFuncDef *def) {
    JanetcRegisterAllocator ra;
    int recur = 1;

    /* Iterate this until no more instructions can be removed. */
    while (recur) {
        janetc_regalloc_init(&ra);

        /* Look for slots that have writes but no reads (and aren't in the closure bitset). */
        if (def->closure_bitset != NULL) {
            for (int32_t i = 0; i < def->slotcount; i++) {
                int32_t index = i >> 5;
                uint32_t mask = 1U << (((uint32_t) i) & 31);
                if (def->closure_bitset[index] & mask) {
                    janetc_regalloc_touch(&ra, i);
                }
            }
        }

#define AA ((instr >> 8)  & 0xFF)
#define BB ((instr >> 16) & 0xFF)
#define CC (instr >> 24)
#define DD (instr >> 8)
#define EE (instr >> 16)

        /* Check reads and writes */
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            uint32_t instr = def->bytecode[i];
            switch (instr & 0x7F) {

                /* Group instructions my how they read from slots */

                /* No reads or writes */
                default:
                    janet_assert(0, "unhandled instruction");
                case JOP_JUMP:
                case JOP_NOOP:
                case JOP_RETURN_NIL:
                /* Write A */
                case JOP_LOAD_INTEGER:
                case JOP_LOAD_CONSTANT:
                case JOP_LOAD_UPVALUE:
                case JOP_CLOSURE:
                /* Write D */
                case JOP_LOAD_NIL:
                case JOP_LOAD_TRUE:
                case JOP_LOAD_FALSE:
                case JOP_LOAD_SELF:
                    break;
                case JOP_MAKE_ARRAY:
                case JOP_MAKE_BUFFER:
                case JOP_MAKE_STRING:
                case JOP_MAKE_STRUCT:
                case JOP_MAKE_TABLE:
                case JOP_MAKE_TUPLE:
                case JOP_MAKE_BRACKET_TUPLE:
                    /* Reads from the stack, don't remove */
                    janetc_regalloc_touch(&ra, DD);
                    break;

                /* Read A */
                case JOP_ERROR:
                case JOP_TYPECHECK:
                case JOP_JUMP_IF:
                case JOP_JUMP_IF_NOT:
                case JOP_JUMP_IF_NIL:
                case JOP_JUMP_IF_NOT_NIL:
                case JOP_SET_UPVALUE:
                /* Write E, Read A */
                case JOP_MOVE_FAR:
                    janetc_regalloc_touch(&ra, AA);
                    break;

                /* Read B */
                case JOP_SIGNAL:
                /* Write A, Read B */
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
                    janetc_regalloc_touch(&ra, BB);
                    break;

                /* Read D */
                case JOP_RETURN:
                case JOP_PUSH:
                case JOP_PUSH_ARRAY:
                case JOP_TAILCALL:
                    janetc_regalloc_touch(&ra, DD);
                    break;

                /* Write A, Read E */
                case JOP_MOVE_NEAR:
                case JOP_LENGTH:
                case JOP_BNOT:
                case JOP_CALL:
                    janetc_regalloc_touch(&ra, EE);
                    break;

                /* Read A, B */
                case JOP_PUT_INDEX:
                    janetc_regalloc_touch(&ra, AA);
                    janetc_regalloc_touch(&ra, BB);
                    break;

                /* Read A, E */
                case JOP_PUSH_2:
                    janetc_regalloc_touch(&ra, AA);
                    janetc_regalloc_touch(&ra, EE);
                    break;

                /* Read B, C */
                case JOP_PROPAGATE:
                /* Write A, Read B and C */
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
                case JOP_CANCEL:
                case JOP_RESUME:
                case JOP_NEXT:
                    janetc_regalloc_touch(&ra, BB);
                    janetc_regalloc_touch(&ra, CC);
                    break;

                /* Read A, B, C */
                case JOP_PUT:
                case JOP_PUSH_3:
                    janetc_regalloc_touch(&ra, AA);
                    janetc_regalloc_touch(&ra, BB);
                    janetc_regalloc_touch(&ra, CC);
                    break;
            }
        }

        /* Iterate and set noops on instructions that make writes that no one ever reads.
         * Only set noops for instructions with no side effects - moves, loads, etc. that can't
         * raise errors (outside of systemic errors like oom or stack overflow). */
        recur = 0;
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            uint32_t instr = def->bytecode[i];
            switch (instr & 0x7F) {
                default:
                    break;
                /* Write D */
                case JOP_LOAD_NIL:
                case JOP_LOAD_TRUE:
                case JOP_LOAD_FALSE:
                case JOP_LOAD_SELF:
                case JOP_MAKE_ARRAY:
                case JOP_MAKE_TUPLE:
                case JOP_MAKE_BRACKET_TUPLE: {
                    if (!janetc_regalloc_check(&ra, DD)) {
                        def->bytecode[i] = JOP_NOOP;
                        recur = 1;
                    }
                }
                break;
                /* Write E, Read A */
                case JOP_MOVE_FAR: {
                    if (!janetc_regalloc_check(&ra, EE)) {
                        def->bytecode[i] = JOP_NOOP;
                        recur = 1;
                    }
                }
                break;
                /* Write A, Read E */
                case JOP_MOVE_NEAR:
                /* Write A, Read B */
                case JOP_GET_INDEX:
                /* Write A */
                case JOP_LOAD_INTEGER:
                case JOP_LOAD_CONSTANT:
                case JOP_LOAD_UPVALUE:
                case JOP_CLOSURE: {
                    if (!janetc_regalloc_check(&ra, AA)) {
                        def->bytecode[i] = JOP_NOOP;
                        recur = 1;
                    }
                }
                break;
            }
        }

        janetc_regalloc_deinit(&ra);
#undef AA
#undef BB
#undef CC
#undef DD
#undef EE
    }
}

/* Verify some bytecode */
int janet_verify(JanetFuncDef *def) {
    int vargs = !!(def->flags & JANET_FUNCDEF_FLAG_VARARG);
    int32_t i;
    int32_t maxslot = def->arity + vargs;
    int32_t sc = def->slotcount;

    if (def->bytecode_length == 0) return 1;

    if (maxslot > sc) return 2;

    /* Verify each instruction */
    for (i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        /* Check for invalid instructions */
        if ((instr & 0x7F) >= JOP_INSTRUCTION_COUNT) {
            return 3;
        }
        enum JanetInstructionType type = janet_instructions[instr & 0x7F];
        switch (type) {
            case JINT_0:
                continue;
            case JINT_S: {
                if ((int32_t)(instr >> 8) >= sc) return 4;
                continue;
            }
            case JINT_SI:
            case JINT_SU:
            case JINT_ST: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_L: {
                int32_t jumpdest = i + (((int32_t)instr) >> 8);
                if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                continue;
            }
            case JINT_SS: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)(instr >> 16) >= sc) return 4;
                continue;
            }
            case JINT_SSI:
            case JINT_SSU: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)((instr >> 16) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_SL: {
                int32_t jumpdest = i + (((int32_t)instr) >> 16);
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                continue;
            }
            case JINT_SSS: {
                if (((int32_t)(instr >> 8) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 16) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 24) & 0xFF) >= sc) return 4;
                continue;
            }
            case JINT_SD: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)(instr >> 16) >= def->defs_length) return 6;
                continue;
            }
            case JINT_SC: {
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)(instr >> 16) >= def->constants_length) return 7;
                continue;
            }
            case JINT_SES: {
                /* How can we check the last slot index? We need info parent funcdefs. Resort
                 * to runtime checks for now. Maybe invalid upvalue references could be defaulted
                 * to nil? (don't commit to this in the long term, though) */
                if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                if ((int32_t)((instr >> 16) & 0xFF) >= def->environments_length) return 8;
                continue;
            }
        }
    }

    /* Verify last instruction is either a jump, return, return-nil, or tailcall. Eventually,
     * some real flow analysis would be ideal, but this should be very effective. Will completely
     * prevent running over the end of bytecode. However, valid functions with dead code will
     * be rejected. */
    {
        uint32_t lastop = def->bytecode[def->bytecode_length - 1] & 0xFF;
        switch (lastop) {
            default:
                return 9;
            case JOP_RETURN:
            case JOP_RETURN_NIL:
            case JOP_JUMP:
            case JOP_ERROR:
            case JOP_TAILCALL:
                break;
        }
    }

    return 0;
}

/* Allocate an empty funcdef. This function may have added functionality
 * as commonalities between asm and compile arise. */
JanetFuncDef *janet_funcdef_alloc(void) {
    JanetFuncDef *def = janet_gcalloc(JANET_MEMORY_FUNCDEF, sizeof(JanetFuncDef));
    def->environments = NULL;
    def->constants = NULL;
    def->bytecode = NULL;
    def->closure_bitset = NULL;
    def->flags = 0;
    def->slotcount = 0;
    def->symbolmap = NULL;
    def->arity = 0;
    def->min_arity = 0;
    def->max_arity = INT32_MAX;
    def->source = NULL;
    def->sourcemap = NULL;
    def->name = NULL;
    def->defs = NULL;
    def->defs_length = 0;
    def->constants_length = 0;
    def->bytecode_length = 0;
    def->environments_length = 0;
    def->symbolmap_length = 0;
    return def;
}

/* Create a simple closure from a funcdef */
JanetFunction *janet_thunk(JanetFuncDef *def) {
    JanetFunction *func = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction));
    func->def = def;
    janet_assert(def->environments_length == 0, "tried to create thunk that needs upvalues");
    return func;
}

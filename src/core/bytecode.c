/*
* Copyright (c) 2020 Calvin Rose
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
    JINT_SSS, /* JOP_SUBTRACT, */
    JINT_SSI, /* JOP_MULTIPLY_IMMEDIATE, */
    JINT_SSS, /* JOP_MULTIPLY, */
    JINT_SSI, /* JOP_DIVIDE_IMMEDIATE, */
    JINT_SSS, /* JOP_DIVIDE, */
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
    return def;
}

/* Create a simple closure from a funcdef */
JanetFunction *janet_thunk(JanetFuncDef *def) {
    JanetFunction *func = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction));
    func->def = def;
    janet_assert(def->environments_length == 0, "tried to create thunk that needs upvalues");
    return func;
}

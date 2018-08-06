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
#include "gc.h"

/* Look up table for instructions */
enum DstInstructionType dst_instructions[DOP_INSTRUCTION_COUNT] = {
    DIT_0, /* DOP_NOOP, */
    DIT_S, /* DOP_ERROR, */
    DIT_ST, /* DOP_TYPECHECK, */
    DIT_S, /* DOP_RETURN, */
    DIT_0, /* DOP_RETURN_NIL, */
    DIT_SSS, /* DOP_ADD_INTEGER, */
    DIT_SSI, /* DOP_ADD_IMMEDIATE, */
    DIT_SSS, /* DOP_ADD_REAL, */
    DIT_SSS, /* DOP_ADD, */
    DIT_SSS, /* DOP_SUBTRACT_INTEGER, */
    DIT_SSS, /* DOP_SUBTRACT_REAL, */
    DIT_SSS, /* DOP_SUBTRACT, */
    DIT_SSS, /* DOP_MULTIPLY_INTEGER, */
    DIT_SSI, /* DOP_MULTIPLY_IMMEDIATE, */
    DIT_SSS, /* DOP_MULTIPLY_REAL, */
    DIT_SSS, /* DOP_MULTIPLY, */
    DIT_SSS, /* DOP_DIVIDE_INTEGER, */
    DIT_SSI, /* DOP_DIVIDE_IMMEDIATE, */
    DIT_SSS, /* DOP_DIVIDE_REAL, */
    DIT_SSS, /* DOP_DIVIDE, */
    DIT_SSS, /* DOP_BAND, */
    DIT_SSS, /* DOP_BOR, */
    DIT_SSS, /* DOP_BXOR, */
    DIT_SS, /* DOP_BNOT, */
    DIT_SSS, /* DOP_SHIFT_LEFT, */
    DIT_SSI, /* DOP_SHIFT_LEFT_IMMEDIATE, */
    DIT_SSS, /* DOP_SHIFT_RIGHT, */
    DIT_SSI, /* DOP_SHIFT_RIGHT_IMMEDIATE, */
    DIT_SSS, /* DOP_SHIFT_RIGHT_UNSIGNED, */
    DIT_SSU, /* DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE, */
    DIT_SS, /* DOP_MOVE_FAR, */
    DIT_SS, /* DOP_MOVE_NEAR, */
    DIT_L, /* DOP_JUMP, */
    DIT_SL, /* DOP_JUMP_IF, */
    DIT_SL, /* DOP_JUMP_IF_NOT, */
    DIT_SSS, /* DOP_GREATER_THAN, */
    DIT_SSS, /* DOP_GREATER_THAN_INTEGER, */
    DIT_SSI, /* DOP_GREATER_THAN_IMMEDIATE, */
    DIT_SSS, /* DOP_GREATER_THAN_REAL, */
    DIT_SSS, /* DOP_GREATER_THAN_EQUAL_REAL, */
    DIT_SSS, /* DOP_LESS_THAN, */
    DIT_SSS, /* DOP_LESS_THAN_INTEGER, */
    DIT_SSI, /* DOP_LESS_THAN_IMMEDIATE, */
    DIT_SSS, /* DOP_LESS_THAN_REAL, */
    DIT_SSS, /* DOP_LESS_THAN_EQUAL_REAL, */
    DIT_SSS, /* DOP_EQUALS, */
    DIT_SSS, /* DOP_EQUALS_INTEGER, */
    DIT_SSI, /* DOP_EQUALS_IMMEDIATE, */
    DIT_SSS, /* DOP_EQUALS_REAL, */
    DIT_SSS, /* DOP_COMPARE, */
    DIT_S, /* DOP_LOAD_NIL, */
    DIT_S, /* DOP_LOAD_TRUE, */
    DIT_S, /* DOP_LOAD_FALSE, */
    DIT_SI, /* DOP_LOAD_INTEGER, */
    DIT_SC, /* DOP_LOAD_CONSTANT, */
    DIT_SES, /* DOP_LOAD_UPVALUE, */
    DIT_S, /* DOP_LOAD_SELF, */
    DIT_SES, /* DOP_SET_UPVALUE, */
    DIT_SD, /* DOP_CLOSURE, */
    DIT_S, /* DOP_PUSH, */
    DIT_SS, /* DOP_PUSH_2, */
    DIT_SSS, /* DOP_PUSH_3, */
    DIT_S, /* DOP_PUSH_ARRAY, */
    DIT_SS, /* DOP_CALL, */
    DIT_S, /* DOP_TAILCALL, */
    DIT_SSS, /* DOP_RESUME, */
    DIT_SSU, /* DOP_SIGNAL, */
    DIT_SSS, /* DOP_GET, */
    DIT_SSS, /* DOP_PUT, */
    DIT_SSU, /* DOP_GET_INDEX, */
    DIT_SSU, /* DOP_PUT_INDEX, */
    DIT_SS, /* DOP_LENGTH */
    DIT_S, /* DOP_MAKE_ARRAY */
    DIT_S, /* DOP_MAKE_BUFFER */
    DIT_S, /* DOP_MAKE_TUPLE */
    DIT_S, /* DOP_MAKE_STRUCT */
    DIT_S, /* DOP_MAKE_TABLE */
    DIT_S, /* DOP_MAKE_STRING */
    DIT_SSS, /* DOP_NUMERIC_LESS_THAN */
    DIT_SSS, /* DOP_NUMERIC_LESS_THAN_EQUAL */
    DIT_SSS, /* DOP_NUMERIC_GREATER_THAN */
    DIT_SSS, /* DOP_NUMERIC_GREATER_THAN_EQUAL */
    DIT_SSS /* DOP_NUMERIC_EQUAL */
};

/* Verify some bytecode */
int32_t dst_verify(DstFuncDef *def) {
    int vargs = def->flags & DST_FUNCDEF_FLAG_VARARG;
    int32_t i;
    int32_t maxslot = def->arity + vargs;
    int32_t sc = def->slotcount;

    if (def->bytecode_length == 0) return 1;

    if (maxslot > sc) return 2;

    /* Verify each instruction */
    for (i = 0; i < def->bytecode_length; i++) {
        uint32_t instr = def->bytecode[i];
        /* Check for invalid instructions */
        if ((instr & 0xFF) >= DOP_INSTRUCTION_COUNT) {
            return 3;
        }
        enum DstInstructionType type = dst_instructions[instr & 0xFF];
        switch (type) {
            case DIT_0:
                continue;
            case DIT_S:
                {
                    if ((int32_t)(instr >> 8) >= sc) return 4;
                    continue;
                }
            case DIT_SI:
            case DIT_SU:
            case DIT_ST:
                {
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                    continue;
                }
            case DIT_L:
                {
                    int32_t jumpdest = i + (((int32_t)instr) >> 8);
                    if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                    continue;
                }
            case DIT_SS:
                {
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)(instr >> 16) >= sc) return 4;
                    continue;
                }
            case DIT_SSI:
            case DIT_SSU:
                {
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc ||
                        (int32_t)((instr >> 16) & 0xFF) >= sc) return 4;
                    continue;
                }
            case DIT_SL:
                {
                    int32_t jumpdest = i + (((int32_t)instr) >> 16);
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                    if (jumpdest < 0 || jumpdest >= def->bytecode_length) return 5;
                    continue;
                }
            case DIT_SSS:
                {
                    if (((int32_t)(instr >> 8) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 16) & 0xFF) >= sc ||
                        ((int32_t)(instr >> 24) & 0xFF) >= sc) return 4;
                    continue;
                }
            case DIT_SD:
                {
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                    if ((int32_t)(instr >> 16) >= def->defs_length) return 6;
                    continue;
                }
            case DIT_SC:
                {
                    if ((int32_t)((instr >> 8) & 0xFF) >= sc) return 4;
                    if ((int32_t)(instr >> 16) >= def->constants_length) return 7;
                    continue;
                }
            case DIT_SES:
                {
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
            case DOP_RETURN:
            case DOP_RETURN_NIL:
            case DOP_JUMP:
            case DOP_ERROR:
            case DOP_TAILCALL:
                break;
        }
    }

    return 0;
}

/* Allocate an empty funcdef. This function may have added functionality
 * as commonalities between asm and compile arise. */
DstFuncDef *dst_funcdef_alloc() {
    DstFuncDef *def = dst_gcalloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->environments = NULL;
    def->constants = NULL;
    def->bytecode = NULL;
    def->flags = 0;
    def->slotcount = 0;
    def->arity = 0;
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
DstFunction *dst_thunk(DstFuncDef *def) {
    DstFunction *func = dst_gcalloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
    func->def = def;
    dst_assert(def->environments_length == 0, "tried to create thunk that needs upvalues");
    return func;
}

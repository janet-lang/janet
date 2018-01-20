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

#include <dst/dsttypes.h>
#include <dst/dstopcodes.h>

DstInstructionType dst_instructions[DOP_INSTRUCTION_COUNT] = {
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
    DIT_SSS, /* DOP_LESS_THAN, */
    DIT_SSS, /* DOP_EQUALS, */
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
    DIT_SSS, /* DOP_TRANSFER, */
    DIT_SSS, /* DOP_GET, */
    DIT_SSS, /* DOP_PUT, */
    DIT_SSU, /* DOP_GET_INDEX, */
    DIT_SSU, /* DOP_PUT_INDEX, */
    DIT_SS /* DOP_LENGTH */
};

/* Hold state in stack during the breadth first traversal */
typedef struct Node Node;
struct Node {
    DstFuncDef *def;
    int32_t index;
};

/* An in memory stack of FuncDefs to verify */

/* Thread local */
static Node *stack = NULL;
static int32_t stackcap = 0;
static int32_t stackcount = 0;

/* Push a Node to the stack */
static void push(DstFuncDef *def, int32_t index) {
    Node n;
    n.def = def;
    n.index = index;
    if (stackcount >= stackcap) {
        stackcap = 2 * stackcount + 2;
        stack = realloc(stack, sizeof(Node) * stackcap);
        if (!stack) {
            DST_OUT_OF_MEMORY;
        }
    }
    stack[stackcount++] = n;
}

/* Verify some bytecode */
static int32_t dst_verify_one(DstFuncDef *def) {
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
        DstInstructionType type = dst_instructions[instr & 0xFF];
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

    /* Verify sub funcdefs by pushing next node to stack */
    if (def->defs_length) push(def, 0);

    return 0;
}

/* Verify */
int32_t dst_verify(DstFuncDef *def) {
    int32_t status;
    stackcount = 0;
    status = dst_verify_one(def);
    while (!status && stackcount) {
        Node n = stack[--stackcount];
        if (n.index < n.def->defs_length) {
            status = dst_verify_one(n.def->defs[n.index]);
            push(n.def, n.index + 1);
        }
    }
    return status;
}

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

#ifndef DST_OPCODES_H_defined
#define DST_OPCODES_H_defined

#ifdef __cplusplus
extern "C" {
#endif

/* Bytecode op argument types */
enum DstOpArgType {
    DST_OAT_SLOT,
    DST_OAT_ENVIRONMENT,
    DST_OAT_CONSTANT,
    DST_OAT_INTEGER,
    DST_OAT_TYPE,
    DST_OAT_SIMPLETYPE,
    DST_OAT_LABEL,
    DST_OAT_FUNCDEF
};

/* Various types of instructions */
enum DstInstructionType {
    DIT_0, /* No args */
    DIT_S, /* Slot(3) */
    DIT_L, /* Label(3) */
    DIT_SS, /* Slot(1), Slot(2) */
    DIT_SL, /* Slot(1), Label(2) */
    DIT_ST, /* Slot(1), Slot(2) */
    DIT_SI, /* Slot(1), Immediate(2) */
    DIT_SD, /* Slot(1), Closure(2) */
    DIT_SU, /* Slot(1), Unsigned Immediate(2) */
    DIT_SSS, /* Slot(1), Slot(1), Slot(1) */
    DIT_SSI, /* Slot(1), Slot(1), Immediate(1) */
    DIT_SSU, /* Slot(1), Slot(1), Unsigned Immediate(1) */
    DIT_SES, /* Slot(1), Environment(1), Far Slot(1) */
    DIT_SC /* Slot(1), Constant(2) */
};

enum DstOpCode {
    DOP_NOOP,
    DOP_ERROR,
    DOP_TYPECHECK,
    DOP_RETURN,
    DOP_RETURN_NIL,
    DOP_ADD_INTEGER,
    DOP_ADD_IMMEDIATE,
    DOP_ADD_REAL,
    DOP_ADD,
    DOP_SUBTRACT_INTEGER,
    DOP_SUBTRACT_REAL,
    DOP_SUBTRACT,
    DOP_MULTIPLY_INTEGER,
    DOP_MULTIPLY_IMMEDIATE,
    DOP_MULTIPLY_REAL,
    DOP_MULTIPLY,
    DOP_DIVIDE_INTEGER,
    DOP_DIVIDE_IMMEDIATE,
    DOP_DIVIDE_REAL,
    DOP_DIVIDE,
    DOP_BAND,
    DOP_BOR,
    DOP_BXOR,
    DOP_BNOT,
    DOP_SHIFT_LEFT,
    DOP_SHIFT_LEFT_IMMEDIATE,
    DOP_SHIFT_RIGHT,
    DOP_SHIFT_RIGHT_IMMEDIATE,
    DOP_SHIFT_RIGHT_UNSIGNED,
    DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE,
    DOP_MOVE_FAR,
    DOP_MOVE_NEAR,
    DOP_JUMP,
    DOP_JUMP_IF,
    DOP_JUMP_IF_NOT,
    DOP_GREATER_THAN,
    DOP_GREATER_THAN_INTEGER,
    DOP_GREATER_THAN_IMMEDIATE,
    DOP_GREATER_THAN_REAL,
    DOP_GREATER_THAN_EQUAL_REAL,
    DOP_LESS_THAN,
    DOP_LESS_THAN_INTEGER,
    DOP_LESS_THAN_IMMEDIATE,
    DOP_LESS_THAN_REAL,
    DOP_LESS_THAN_EQUAL_REAL,
    DOP_EQUALS,
    DOP_EQUALS_INTEGER,
    DOP_EQUALS_IMMEDIATE,
    DOP_EQUALS_REAL,
    DOP_COMPARE,
    DOP_LOAD_NIL,
    DOP_LOAD_TRUE,
    DOP_LOAD_FALSE,
    DOP_LOAD_INTEGER,
    DOP_LOAD_CONSTANT,
    DOP_LOAD_UPVALUE,
    DOP_LOAD_SELF,
    DOP_SET_UPVALUE,
    DOP_CLOSURE,
    DOP_PUSH,
    DOP_PUSH_2,
    DOP_PUSH_3,
    DOP_PUSH_ARRAY,
    DOP_CALL,
    DOP_TAILCALL,
    DOP_RESUME,
    DOP_SIGNAL,
    DOP_GET,
    DOP_PUT,
    DOP_GET_INDEX,
    DOP_PUT_INDEX,
    DOP_LENGTH,
    DOP_INSTRUCTION_COUNT
};

/* Info about all instructions */
extern enum DstInstructionType dst_instructions[DOP_INSTRUCTION_COUNT];

#ifdef __cplusplus
}
#endif

#endif

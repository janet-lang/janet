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

#ifndef DST_OPCODES_H_defined
#define DST_OPCODES_H_defined

typedef enum DstOpCode DstOpCode;
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
    DOP_MOVE,
    DOP_JUMP,
    DOP_JUMP_IF,
    DOP_JUMP_IF_NOT,
    DOP_GREATER_THAN,
    DOP_LESS_THAN,
    DOP_EQUALS,
    DOP_COMPARE,
    DOP_LOAD_NIL,
    DOP_LOAD_BOOLEAN,
    DOP_LOAD_INTEGER,
    DOP_LOAD_CONSTANT,
    DOP_LOAD_UPVALUE,
    DOP_SET_UPVALUE,
    DOP_CLOSURE,
    DOP_PUSH,
    DOP_PUSH_2,
    DOP_PUSH_3,
    DOP_PUSH_ARRAY,
    DOP_CALL,
    DOP_TAILCALL,
    DOP_SYSCALL,
    DOP_LOAD_SYSCALL,
    DOP_TRANSFER,
    DOP_GET,
    DOP_PUT,
    DOP_GET_INDEX,
    DOP_PUT_INDEX,
    DOP_LENGTH
};

#endif

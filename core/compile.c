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

#include <dst/dst.h>

#define DST_LOCAL_FLAG_MUTABLE 1

/* Compiler typedefs */
typedef struct DstCompiler DstCompiler;
typedef struct FormOptions FormOptions;
typedef struct SlotTracker SlotTracker;
typedef struct DstScope DstScope;
typedef struct DstCFunctionOptimizer DstCFunctionOptimizer;

#define DST_SLOT_CONSTANT 1
#define DST_SLOT_TEMP 2
#define DST_SLOT_RETURNED 4
#define DST_SLOT_NIL 8

/* A stack slot */
struct DstSlot {
    int32_t index;
    uint32_t flags;
    uint32_t types; /* bit set of possible primitive types */
    DstValue constant; /* If the slot has a constant value */
}

/* Most forms that return a constant will not generate any bytecode */

/* Special forms that need support */
/* cond
 * while (continue, break)
 * quote
 * fn
 * def
 * var
 * do
 */

#define DST_OPTIMIZER_CONSTANTS 1
#define DST_OPTIMIZER_BYTECODE 2
#define DST_OPTIMIZER_PARTIAL_CONSTANTS 4

/* A grouping of optimization on a cfunction given certain conditions
 * on the arguments (such as all constants, or some known types). The appropriate
 * optimizations should be tried before compiling a normal function call. */
struct DstCFunctionOptimizer {
    uint32_t flags; /* Indicate what kind of optimizations can be performed */
}

/* A lexical scope during compilation */
struct DstScope {
    DstArray slots;
    DstArray freeslots;
    DstArray constants;
    DstTable symbols; /* Positive numbers are stack slots, negative are negative constant indices */
    uint32_t flags;
    int32_t nextslot;
}

/* Compilation state */
struct DstCompiler {
    DstValue error;
    jmp_buf on_error;
    DstArray scopes;
    DstBuffer buffer;
    int recursion_guard;
};

/* Compiler state */
struct DstFormOptions {
    DstCompiler *compiler;
    DstValue x;
    uint32_t flags;
    uint32_t types; /* bit set of accepted primitive types */
    int32_t target_slot;
};

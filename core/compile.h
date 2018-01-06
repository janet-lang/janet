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

#ifndef DST_COMPILE_H
#define DST_COMPILE_H

#include <dst/dst.h>
#include <setjmp.h>
#include "opcodes.h"

/* Compiler typedefs */
typedef struct DstCompiler DstCompiler;
typedef struct FormOptions FormOptions;
typedef struct SlotTracker SlotTracker;
typedef struct DstScope DstScope;
typedef struct DstSlot DstSlot;
typedef struct DstFopts DstFopts;
typedef struct DstCFunctionOptimizer DstCFunctionOptimizer;

#define DST_SLOT_CONSTANT 0x10000
#define DST_SLOT_NAMED 0x20000
#define DST_SLOT_MUTABLE 0x40000
#define DST_SLOT_REF 0x80000
#define DST_SLOT_RETURNED 0x100000
/* Needed for handling single element arrays as global vars. */

#define DST_SLOTTYPE_ANY 0xFFFF

/* A stack slot */
struct DstSlot {
    int32_t index;
    int32_t envindex; /* 0 is local, positive number is an upvalue */
    uint32_t flags;
    Dst constant; /* If the slot has a constant value */
};

/* Special forms that need support */
/* cond
 * while (continue, break)
 * quote
 * fn
 * def
 * var
 * varset
 * do
 * apply (overloaded with normal function)
 */

#define DST_SCOPE_FUNCTION 1
#define DST_SCOPE_ENV 2
#define DST_SCOPE_TOP 4
#define DST_SCOPE_UNUSED 8

/* A symbol and slot pair */
typedef struct SymPair {
    const uint8_t *sym;
    DstSlot slot;
} SymPair;

/* A lexical scope during compilation */
struct DstScope {

    /* Constants for this funcdef */
    Dst *consts;

    /* Map of symbols to slots. Use a simple linear scan for symbols. */
    SymPair *syms;

    /* Bit vector with allocated slot indices. Used to allocate new slots */
    uint32_t *slots;
    int32_t smax;

    /* FuncDefs */
    DstFuncDef **defs;

    /* Referenced closure environents. The values at each index correspond
     * to which index to get the environment from in the parent. The environment
     * that corresponds to the direct parent's stack will always have value 0. */
    int32_t *envs;

    int32_t bytecode_start;
    int flags;
};

/* Compilation state */
struct DstCompiler {
    int recursion_guard;
    DstScope *scopes;
    
    uint32_t *buffer;
    int32_t *mapbuffer;

    /* Hold the environment */
    Dst env;

    DstCompileResult result;
};

#define DST_FOPTS_TAIL 0x10000
#define DST_FOPTS_HINT 0x20000
#define DST_FOPTS_DROP 0x40000

/* Options for compiling a single form */
struct DstFopts {
    DstCompiler *compiler;
    Dst x;
    const Dst *sourcemap;
    uint32_t flags; /* bit set of accepted primitive types */
    DstSlot hint;
};

/* A grouping of optimizations on a cfunction given certain conditions
 * on the arguments (such as all constants, or some known types). The appropriate
 * optimizations should be tried before compiling a normal function call. */
struct DstCFunctionOptimizer {
    DstCFunction cfun;
    DstSlot (*optimize)(DstFopts opts, int32_t argn, const Dst *argv);
};
typedef struct DstSpecial {
    const char *name;
    DstSlot (*compile)(DstFopts opts, int32_t argn, const Dst *argv);
} DstSpecial;

/* An array of optimizers sorted by key */
extern DstCFunctionOptimizer dstcr_optimizers[255];

/* Dispatch to correct form compiler */
DstSlot dstc_value(DstFopts opts);

/****************************************************/

void dstc_error(DstCompiler *c, const Dst *sourcemap, const uint8_t *m);
void dstc_cerror(DstCompiler *c, const Dst *sourcemap, const char *m);

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFopts dstc_getindex(DstFopts opts, int32_t index);
DstFopts dstc_getkey(DstFopts opts, Dst key);
DstFopts dstc_getvalue(DstFopts opts, Dst key);

void dstc_scope(DstCompiler *c, int newfn);
void dstc_popscope(DstCompiler *c);

DstSlot dstc_cslot(Dst x);
void dstc_freeslot(DstCompiler *c, DstSlot slot);

/* Search for a symbol */
DstSlot dstc_resolve(DstCompiler *c, const Dst *sourcemap, const uint8_t *sym);

/* Emit instructions. */
void dstc_emit(DstCompiler *c, const Dst *sourcemap, uint32_t instr);

#endif

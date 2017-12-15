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

/* Compiler typedefs */
typedef struct DstCompiler DstCompiler;
typedef struct FormOptions FormOptions;
typedef struct SlotTracker SlotTracker;
typedef struct DstScope DstScope;
typedef struct DstCFunctionOptimizer DstCFunctionOptimizer;

#define DST_SLOT_CONSTANT 0x10000
#define DST_SLOT_TEMP 0x20000
#define DST_SLOT_RETURNED 0x40000
#define DST_SLOT_NIL 0x80000
#define DST_SLOT_MUTABLE 0x100000
#define DST_SLOT_ERROR 0x200000

#define DST_SLOTTYPE_ANY 0xFFFF

/* A stack slot */
struct DstSlot {
    int32_t index;
    int32_t envindex; /* 0 is local, positive number is an upvalue */
    uint32_t flags;
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
 * varset
 * do
 */

#define DST_OPTIMIZER_CONSTANTS 0x10000
#define DST_OPTIMIZER_BYTECODE 0x20000
#define DST_OPTIMIZER_PARTIAL_CONSTANTS 0x40000
#define DST_OPTIMIZER_SYSCALL 0x80000

/* A grouping of optimization on a cfunction given certain conditions
 * on the arguments (such as all constants, or some known types). The appropriate
 * optimizations should be tried before compiling a normal function call. */
struct DstCFunctionOptimizer {
    uint32_t flags; /* Indicate what kind of optimizations can be performed. */
        /*Also what kind of types can be returned*/
    int32_t syscall;
}

#define DST_SCOPE_FUNCTION 1
#define DST_SCOPE_LASTSLOT 2
#define DST_SCOPE_FIRSTSLOT 4
#define DST_SCOPE_ENV

/* A lexical scope during compilation */
struct DstScope {
    DstArray constants; /* Constants for the funcdef */
    DstTable symbols; /* Map symbols -> Slot inidices */

    /* Hold all slots in use. Data structures that store
     * slots should link them to this datatstructure */
    DstSlot *slots;
    int32_t slotcount;
    int32_t slotcap;

    /* A vector of freed slots. */
    int32_t *freeslots;
    int32_t freeslotcount;
    int32_t freeslotcap;

    int32_t lastslot;
    int32_t nextslot;

    /* Referenced closure environemts. The values at each index correspond
     * to which index to get the environment from in the parent. The enironment
     * that corresponds to the direct parent's stack will always have value 0. */
    int32_t *envs;
    int32_t envcount;
    int32_t envcap;

    int32_t buffer_offset; /* Where the bytecode for this scope begins */

    uint32_t flags;
}

/* Compilation state */
struct DstCompiler {
    int32_t scopecount;
    int32_t scopecap;
    DstScope *scopes;
    DstBuffer buffer;
    DstBuffer mapbuffer;
    int32_t error_start;
    int32_t error_end;
    DstValue error;
    int recursion_guard;
};

#define DST_FOPTS_TAIL 0x10000
#define DST_FOPTS_FORCESLOT 0x20000

/* Compiler state */
struct DstFormOptions {
    DstCompiler *compiler;
    DstValue x;
    const DstValue *sourcemap;
    uint32_t flags; /* bit set of accepted primitive types */
};

typedef enum DstCompileStatus {
    DST_COMPILE_OK,
    DST_COMPILE_ERROR
} DstCompileStatus;

/* Results of compilation */
typedef struct DstCompileResults {
    DstCompileStatus status;
    DstFuncDef *funcdef;
    const uint8_t *error;
} DstCompileResults;

typedef struct DstCompileOptions {
    uint32_t flags;
    const DstValue *sourcemap;
    DstValue src;
    int32_t target;
};

/* Compiler handlers. Used to compile different kinds of expressions. */
typedef DstSlot (*DstFormCompiler)(DstFormOptions opts);

/* Dispatch to correct form compiler */
DstSlot dst_compile_value(DstFormOptions opts);

/* Compile basic types */
DstSlot dst_compile_constant(DstFormOptions opts);
DstSlot dst_compile_symbol(DstFormOptions opts);
DstSlot dst_copmile_array(DstFormOptions opts);
DstSlot dst_copmile_struct(DstFormOptions opts);
DstSlot dst_copmile_table(DstFormOptions opts);

/* Tuple compiliation will handle much of the work */
DstSlot dst_compile_tuple(DstFormOptions opts);

/* Compile special forms */
DstSlot dst_compile_do(DstFormOptions opts);
DstSlot dst_compile_fn(DstFormOptions opts);
DstSlot dst_compile_cond(DstFormOptions opts);
DstSlot dst_compile_while(DstFormOptions opts);
DstSlot dst_compile_quote(DstFormOptions opts);
DstSlot dst_compile_def(DstFormOptions opts);
DstSlot dst_compile_var(DstFormOptions opts);
DstSlot dst_compile_varset(DstFormOptions opts);

/* Compile source code into FuncDef. */
DstCompileResults dst_compile(DstCompileOptions opts);

/****************************************************/

DstSlot dst_compile_error(DstCompiler *c, const DstValue *sourcemap, const uint8_t *m);
DstSlot dst_compile_cerror(DstCompiler *c, const DstValue *sourcemap, const char *m);

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFormOptions dst_compile_getopts_index(DstFormOptions opts, int32_t index);
DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key);
DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key);

void dst_compile_scope(DstCompiler *c, int newfn);
DstSlot dst_compile_popscope(DstCompiler *c);

int dst_compile_slotmatch(DstFormOptions opts, DstSlot slot);
DstSlot dst_compile_normalslot(DstCompiler *c, uint32_t types);
DstSlot dst_compile_constantslot(DstCompiler *c, DstValue x);
void dst_compile_freeslot(DstCompiler *c, DstSlot slot);
void dst_compile_freeslotarray(DstCompiler *c, DstArray *slots);

/* Search for a symbol */
DstSlot dst_compile_resolve(DstCompiler *c, const DstValue *sourcemap, const uint8_t *sym);

/* Get a local slot that can be used as the desination for whatever is compiling. */
DstSlot dst_compile_targetslot(DstFormOptions opts, DstSlot s);

/* Coerce any slot into the target slot. If no target is specified, return
 * the slot unaltered. Otherwise, move and load upvalues as necesarry to set the slot. */
DstSlot dst_compile_coercetargetslot(DstFormOptions opts, DstSlot s);

DstSlot dst_compile_realizeslot(DstCompiler *c, DstSlot s);
DstSlot dst_compile_returnslot(DstCompiler *c, DstSlot s);

#endif

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
typedef struct DstFormOptions DstFormOptions;
typedef struct DstCFunctionOptimizer DstCFunctionOptimizer;

#define DST_SLOT_CONSTANT 0x10000
#define DST_SLOT_NAMED 0x20000
#define DST_SLOT_MUTABLE 0x40000
#define DST_SLOT_REF 0x80000
/* Needed for handling single element arrays as global vars. */

#define DST_SLOTTYPE_ANY 0xFFFF

/* A stack slot */
struct DstSlot {
    int32_t index;
    int32_t envindex; /* 0 is local, positive number is an upvalue */
    uint32_t flags;
    DstValue constant; /* If the slot has a constant value */
};

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
 * apply (overloaded with normal function)
 */

#define DST_SCOPE_FUNCTION 1
#define DST_SCOPE_LASTSLOT 2
#define DST_SCOPE_FIRSTSLOT 4
#define DST_SCOPE_ENV 8
#define DST_SCOPE_TOP 16

/* A lexical scope during compilation */
struct DstScope {

    /* Constants for this funcdef */
    int32_t ccount;
    int32_t ccap;
    DstValue *consts;

    /* Map of symbols to slots. Use a simple linear scan for symbols. */
    int32_t symcap;
    int32_t symcount;
    struct {
        const uint8_t *sym;
        DstSlot slot;
    } *syms;

    /* Bit vector with allocated slot indices. Used to allocate new slots */
    uint32_t *slots;
    int32_t scap;
    int32_t smax;

    /* Referenced closure environents. The values at each index correspond
     * to which index to get the environment from in the parent. The enironment
     * that corresponds to the direct parent's stack will always have value 0. */
    int32_t *envs;
    int32_t envcount;
    int32_t envcap;

    int32_t bytecode_start;
    uint32_t flags;
};

#define dst_compile_topscope(c) ((c)->scopes + (c)->scopecount - 1)

/* Compilation state */
struct DstCompiler {
    jmp_buf on_error;
    int recursion_guard;
    int32_t scopecount;
    int32_t scopecap;
    DstScope *scopes;
    
    int32_t buffercap;
    int32_t buffercount;
    uint32_t *buffer;
    int32_t *mapbuffer;

    /* Hold the environment */
    DstValue env;

    DstCompileResults results;
};

#define DST_FOPTS_TAIL 0x10000
#define DST_FOPTS_HINT 0x20000

/* Compiler state */
struct DstFormOptions {
    DstCompiler *compiler;
    DstValue x;
    const DstValue *sourcemap;
    uint32_t flags; /* bit set of accepted primitive types */
    DstSlot hint;
};

/* A grouping of optimizations on a cfunction given certain conditions
 * on the arguments (such as all constants, or some known types). The appropriate
 * optimizations should be tried before compiling a normal function call. */
struct DstCFunctionOptimizer {
    DstCFunction cfun;
    DstSlot (*optimize)(DstFormOptions opts, int32_t argn, const DstValue *argv);
};
typedef struct DstSpecial {
    const char *name;
    DstSlot (*compile)(DstFormOptions opts, int32_t argn, const DstValue *argv);
} DstSpecial;

/* An array of optimizers sorted by key */
extern DstCFunctionOptimizer dst_compiler_optimizers[255];

/* An array of special forms */
extern DstSpecial dst_compiler_specials[16];

/* Dispatch to correct form compiler */
DstSlot dst_compile_value(DstFormOptions opts);

/* Compile special forms */
DstSlot dst_compile_do(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_fn(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_cond(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_while(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_quote(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_def(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_var(DstFormOptions opts, int32_t argn, const DstValue *argv);
DstSlot dst_compile_varset(DstFormOptions opts, int32_t argn, const DstValue *argv);

/****************************************************/

void dst_compile_error(DstCompiler *c, const DstValue *sourcemap, const uint8_t *m);
void dst_compile_cerror(DstCompiler *c, const DstValue *sourcemap, const char *m);

/* Use these to get sub options. They will traverse the source map so
 * compiler errors make sense. Then modify the returned options. */
DstFormOptions dst_compile_getopts_index(DstFormOptions opts, int32_t index);
DstFormOptions dst_compile_getopts_key(DstFormOptions opts, DstValue key);
DstFormOptions dst_compile_getopts_value(DstFormOptions opts, DstValue key);

void dst_compile_scope(DstCompiler *c, int newfn);
void dst_compile_popscope(DstCompiler *c);

DstSlot dst_compile_constantslot(DstValue x);
void dst_compile_freeslot(DstCompiler *c, DstSlot slot);

/* Search for a symbol */
DstSlot dst_compile_resolve(DstCompiler *c, const DstValue *sourcemap, const uint8_t *sym);

/* Emit instructions. */
void dst_compile_emit(DstCompiler *c, const DstValue *sourcemap, uint32_t instr);

#endif

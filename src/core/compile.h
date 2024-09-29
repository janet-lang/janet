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

#ifndef JANET_COMPILE_H
#define JANET_COMPILE_H

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "regalloc.h"
#endif

/* Levels for compiler warnings */
typedef enum {
    JANET_C_LINT_RELAXED,
    JANET_C_LINT_NORMAL,
    JANET_C_LINT_STRICT
} JanetCompileLintLevel;

/* Tags for some functions for the prepared inliner */
#define JANET_FUN_DEBUG 1
#define JANET_FUN_ERROR 2
#define JANET_FUN_APPLY 3
#define JANET_FUN_YIELD 4
#define JANET_FUN_RESUME 5
#define JANET_FUN_IN 6
#define JANET_FUN_PUT 7
#define JANET_FUN_LENGTH 8
#define JANET_FUN_ADD 9
#define JANET_FUN_SUBTRACT 10
#define JANET_FUN_MULTIPLY 11
#define JANET_FUN_DIVIDE 12
#define JANET_FUN_BAND 13
#define JANET_FUN_BOR 14
#define JANET_FUN_BXOR 15
#define JANET_FUN_LSHIFT 16
#define JANET_FUN_RSHIFT 17
#define JANET_FUN_RSHIFTU 18
#define JANET_FUN_BNOT 19
#define JANET_FUN_GT 20
#define JANET_FUN_LT 21
#define JANET_FUN_GTE 22
#define JANET_FUN_LTE 23
#define JANET_FUN_EQ 24
#define JANET_FUN_NEQ 25
#define JANET_FUN_PROP 26
#define JANET_FUN_GET 27
#define JANET_FUN_NEXT 28
#define JANET_FUN_MODULO 29
#define JANET_FUN_REMAINDER 30
#define JANET_FUN_CMP 31
#define JANET_FUN_CANCEL 32
#define JANET_FUN_DIVIDE_FLOOR 33

/* Compiler typedefs */
typedef struct JanetCompiler JanetCompiler;
typedef struct FormOptions FormOptions;
typedef struct SlotTracker SlotTracker;
typedef struct JanetScope JanetScope;
typedef struct JanetSlot JanetSlot;
typedef struct JanetFopts JanetFopts;
typedef struct JanetFunOptimizer JanetFunOptimizer;
typedef struct JanetSpecial JanetSpecial;

#define JANET_SLOT_CONSTANT 0x10000
#define JANET_SLOT_NAMED 0x20000
#define JANET_SLOT_MUTABLE 0x40000
#define JANET_SLOT_REF 0x80000
#define JANET_SLOT_RETURNED 0x100000
#define JANET_SLOT_DEP_NOTE 0x200000
#define JANET_SLOT_DEP_WARN 0x400000
#define JANET_SLOT_DEP_ERROR 0x800000
#define JANET_SLOT_SPLICED 0x1000000

#define JANET_SLOTTYPE_ANY 0xFFFF

/* A stack slot */
struct JanetSlot {
    Janet constant; /* If the slot has a constant value */
    int32_t index;
    int32_t envindex; /* 0 is local, positive number is an upvalue */
    uint32_t flags;
};

#define JANET_SCOPE_FUNCTION 1
#define JANET_SCOPE_ENV 2
#define JANET_SCOPE_TOP 4
#define JANET_SCOPE_UNUSED 8
#define JANET_SCOPE_CLOSURE 16
#define JANET_SCOPE_WHILE 32

/* A symbol and slot pair */
typedef struct SymPair {
    JanetSlot slot;
    const uint8_t *sym;
    const uint8_t *sym2;
    int keep;
    uint32_t birth_pc;
    uint32_t death_pc;
} SymPair;

typedef struct JanetEnvRef {
    int32_t envindex;
    JanetScope *scope;
} JanetEnvRef;

/* A lexical scope during compilation */
struct JanetScope {

    /* For debugging the compiler */
    const char *name;

    /* Scopes are doubly linked list */
    JanetScope *parent;
    JanetScope *child;

    /* Constants for this funcdef */
    Janet *consts;

    /* Map of symbols to slots. Use a simple linear scan for symbols. */
    SymPair *syms;

    /* FuncDefs */
    JanetFuncDef **defs;

    /* Register allocator */
    JanetcRegisterAllocator ra;

    /* Upvalue allocator */
    JanetcRegisterAllocator ua;

    /* Referenced closure environments. The values at each index correspond
     * to which index to get the environment from in the parent. The environment
     * that corresponds to the direct parent's stack will always have value 0. */
    JanetEnvRef *envs;

    int32_t bytecode_start;
    int flags;
};

/* Compilation state */
struct JanetCompiler {

    /* Pointer to current scope */
    JanetScope *scope;

    uint32_t *buffer;
    JanetSourceMapping *mapbuffer;

    /* Hold the environment */
    JanetTable *env;

    /* Name of source to attach to generated functions */
    const uint8_t *source;

    /* The result of compilation */
    JanetCompileResult result;

    /* Keep track of where we are in the source */
    JanetSourceMapping current_mapping;

    /* Prevent unbounded recursion */
    int recursion_guard;

    /* Collect linting results */
    JanetArray *lints;
};

#define JANET_FOPTS_TAIL 0x10000
#define JANET_FOPTS_HINT 0x20000
#define JANET_FOPTS_DROP 0x40000
#define JANET_FOPTS_ACCEPT_SPLICE 0x80000

/* Options for compiling a single form */
struct JanetFopts {
    JanetCompiler *compiler;
    JanetSlot hint;
    uint32_t flags; /* bit set of accepted primitive types */
};

/* Get the default form options */
JanetFopts janetc_fopts_default(JanetCompiler *c);

/* For optimizing builtin normal functions. */
struct JanetFunOptimizer {
    int (*can_optimize)(JanetFopts opts, JanetSlot *args);
    JanetSlot(*optimize)(JanetFopts opts, JanetSlot *args);
};

/* A grouping of a named special and the corresponding compiler fragment */
struct JanetSpecial {
    const char *name;
    JanetSlot(*compile)(JanetFopts opts, int32_t argn, const Janet *argv);
};

/****************************************************/

/* Get an optimizer if it exists, otherwise NULL */
const JanetFunOptimizer *janetc_funopt(uint32_t flags);

/* Get a special. Return NULL if none exists */
const JanetSpecial *janetc_special(const uint8_t *name);

void janetc_freeslot(JanetCompiler *c, JanetSlot s);
void janetc_nameslot(JanetCompiler *c, const uint8_t *sym, JanetSlot s);
JanetSlot janetc_farslot(JanetCompiler *c);

/* Throw away some code after checking that it is well formed. */
void janetc_throwaway(JanetFopts opts, Janet x);

/* Get a target slot for emitting an instruction. Will always return
 * a local slot. */
JanetSlot janetc_gettarget(JanetFopts opts);

/* Get a bunch of slots for function arguments */
JanetSlot *janetc_toslots(JanetCompiler *c, const Janet *vals, int32_t len);

/* Get a bunch of slots for function arguments */
JanetSlot *janetc_toslotskv(JanetCompiler *c, Janet ds);

/* Push slots loaded via janetc_toslots. */
int32_t janetc_pushslots(JanetCompiler *c, JanetSlot *slots);

/* Free slots loaded via janetc_toslots */
void janetc_freeslots(JanetCompiler *c, JanetSlot *slots);

/* Generate the return instruction for a slot. */
JanetSlot janetc_return(JanetCompiler *c, JanetSlot s);

/* Store an error */
void janetc_error(JanetCompiler *c, const uint8_t *m);
void janetc_cerror(JanetCompiler *c, const char *m);

/* Linting */
void janetc_lintf(JanetCompiler *C, JanetCompileLintLevel level, const char *format, ...);

/* Dispatch to correct form compiler */
JanetSlot janetc_value(JanetFopts opts, Janet x);

/* Push and pop from the scope stack */
void janetc_scope(JanetScope *s, JanetCompiler *c, int flags, const char *name);
void janetc_popscope(JanetCompiler *c);
void janetc_popscope_keepslot(JanetCompiler *c, JanetSlot retslot);
JanetFuncDef *janetc_pop_funcdef(JanetCompiler *c);

/* Create a destroy slot */
JanetSlot janetc_cslot(Janet x);

/* Search for a symbol */
JanetSlot janetc_resolve(JanetCompiler *c, const uint8_t *sym);

/* Bytecode optimization */
void janet_bytecode_movopt(JanetFuncDef *def);
void janet_bytecode_remove_noops(JanetFuncDef *def);

#endif

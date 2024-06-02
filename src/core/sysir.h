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

/****
 * The System Dialect Intermediate Representation (sysir) is a compiler intermediate representation
 * that for "System Janet" a dialect for "System Programming". Sysir can then be retargeted to C or direct to machine
 * code for JIT or AOT compilation.
 */
#ifndef JANET_SYSIR_H
#define JANET_SYSIR_H

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "state.h"
#endif

typedef enum {
    JANET_PRIM_U8,
    JANET_PRIM_S8,
    JANET_PRIM_U16,
    JANET_PRIM_S16,
    JANET_PRIM_U32,
    JANET_PRIM_S32,
    JANET_PRIM_U64,
    JANET_PRIM_S64,
    JANET_PRIM_F32,
    JANET_PRIM_F64,
    JANET_PRIM_POINTER,
    JANET_PRIM_BOOLEAN,
    JANET_PRIM_STRUCT,
    JANET_PRIM_UNION,
    JANET_PRIM_ARRAY,
    JANET_PRIM_VOID,
    JANET_PRIM_UNKNOWN
} JanetPrim;

typedef struct {
    const char *name;
    JanetPrim prim;
} JanetPrimName;

typedef enum {
    JANET_SYSOP_LINK_NAME,
    JANET_SYSOP_PARAMETER_COUNT,
    JANET_SYSOP_MOVE,
    JANET_SYSOP_CAST,
    JANET_SYSOP_ADD,
    JANET_SYSOP_SUBTRACT,
    JANET_SYSOP_MULTIPLY,
    JANET_SYSOP_DIVIDE,
    JANET_SYSOP_BAND,
    JANET_SYSOP_BOR,
    JANET_SYSOP_BXOR,
    JANET_SYSOP_BNOT,
    JANET_SYSOP_SHL,
    JANET_SYSOP_SHR,
    JANET_SYSOP_LOAD,
    JANET_SYSOP_STORE,
    JANET_SYSOP_GT,
    JANET_SYSOP_LT,
    JANET_SYSOP_EQ,
    JANET_SYSOP_NEQ,
    JANET_SYSOP_GTE,
    JANET_SYSOP_LTE,
    JANET_SYSOP_CONSTANT,
    JANET_SYSOP_CALL,
    JANET_SYSOP_RETURN,
    JANET_SYSOP_JUMP,
    JANET_SYSOP_BRANCH,
    JANET_SYSOP_BRANCH_NOT,
    JANET_SYSOP_ADDRESS,
    JANET_SYSOP_CALLK,
    JANET_SYSOP_TYPE_PRIMITIVE,
    JANET_SYSOP_TYPE_STRUCT,
    JANET_SYSOP_TYPE_BIND,
    JANET_SYSOP_ARG,
    JANET_SYSOP_FIELD_GETP,
    JANET_SYSOP_ARRAY_GETP,
    JANET_SYSOP_ARRAY_PGETP,
    JANET_SYSOP_TYPE_POINTER,
    JANET_SYSOP_TYPE_ARRAY,
    JANET_SYSOP_TYPE_UNION,
    JANET_SYSOP_POINTER_ADD,
    JANET_SYSOP_POINTER_SUBTRACT,
    JANET_SYSOP_LABEL
} JanetSysOp;

typedef struct {
    JanetPrim prim;
    union {
        struct {
            uint32_t field_count;
            uint32_t field_start;
        } st;
        struct {
            uint32_t type;
        } pointer;
        struct {
            uint32_t type;
            uint64_t fixed_count;
        } array;
    };
} JanetSysTypeInfo;

typedef struct {
    uint32_t type;
} JanetSysTypeField;

typedef struct {
    JanetSysOp opcode;
    union {
        struct {
            uint32_t dest;
            uint32_t lhs;
            uint32_t rhs;
        } three;
        struct {
            uint32_t dest;
            uint32_t callee;
            uint32_t arg_count;
            uint32_t has_dest;
        } call;
        struct {
            uint32_t dest;
            uint32_t constant;
            uint32_t arg_count;
            uint32_t has_dest;
        } callk;
        struct {
            uint32_t dest;
            uint32_t src;
        } two;
        struct {
            uint32_t src;
        } one;
        struct {
            uint32_t to;
        } jump;
        struct {
            uint32_t cond;
            uint32_t to;
        } branch;
        struct {
            uint32_t dest;
            uint32_t constant;
        } constant;
        struct {
            uint32_t dest_type;
            uint32_t prim;
        } type_prim;
        struct {
            uint32_t dest_type;
            uint32_t arg_count;
        } type_types;
        struct {
            uint32_t dest;
            uint32_t type;
        } type_bind;
        struct {
            uint32_t args[3];
        } arg;
        struct {
            uint32_t r;
            uint32_t st;
            uint32_t field;
        } field;
        struct {
            uint32_t dest_type;
            uint32_t type;
        } pointer;
        struct {
            uint32_t dest_type;
            uint32_t type;
            uint64_t fixed_count;
        } array;
        struct {
            uint32_t id;
        } label;
    };
    int32_t line;
    int32_t column;
} JanetSysInstruction;

/* Shared data between multiple
 * IR Function bodies. Used to link
 * multiple functions together in a
 * single executable or shared object with
 * multiple entry points. Contains shared
 * type declarations, as well as a table of linked
 * functions. */
typedef struct {
    uint32_t old_type_def_count;
    uint32_t type_def_count;
    uint32_t field_def_count;
    JanetSysTypeInfo *type_defs;
    JanetString *type_names;
    JanetSysTypeField *field_defs;
    JanetTable *irs;
    JanetArray *ir_ordered;
    JanetTable *type_name_lookup;
} JanetSysIRLinkage;

/* IR representation for a single function.
 * Allow for incremental compilation and linking. */
typedef struct {
    JanetSysIRLinkage *linkage;
    JanetString link_name;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t constant_count;
    uint32_t return_type;
    uint32_t parameter_count;
    uint32_t label_count;
    uint32_t *types;
    JanetSysInstruction *instructions;
    JanetString *register_names;
    Janet *constants;
    JanetTable *register_name_lookup;
    JanetTable *labels;
} JanetSysIR;

/* Represent register spills after doing register allocation. Before lowering
 * individual instructions, check if any spills occur and possibly insert extra
 * reads and writes from/to the stack. Up to 6 spills per instruction (3 registers
 * a load and store each) */
typedef struct {
    enum {
        JANET_SYS_SPILL_NONE,
        JANET_SYS_SPILL_READ,
        JANET_SYS_SPILL_WRITE,
        JANET_SYS_SPILL_BOTH
    } spills[3];
    uint32_t regs[3];
} JanetSysSpill;

/* Keep track of names for each instruction */
extern const char *janet_sysop_names[];

/* Lowering */
void janet_sys_ir_lower_to_ir(JanetSysIRLinkage *linkage, JanetArray *into);
void janet_sys_ir_lower_to_c(JanetSysIRLinkage *linkage, JanetBuffer *buffer);

void janet_sys_ir_lower_to_x64(JanetSysIRLinkage *linkage, JanetBuffer *buffer);

#endif

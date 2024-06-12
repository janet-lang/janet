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

/* TODO
 * [x] encode constants directly in 3 address codes - makes codegen easier
 * [x] typed constants
 * [x] named registers and types
 * [x] better type errors (perhaps mostly for compiler debugging - full type system goes on top)
 * [-] x86/x64 machine code target - in progress
 *   [ ] handle floating point types
 *   [ ] handle array types
 *   [ ] emit machine code directly
 * [ ] target specific extensions - custom instructions and custom primitives
 * [ ] better casting semantics
 * [x] separate pointer arithmetic from generalized arithmetic (easier to instrument code for safety)?
 * [x] fixed-size array types
 * [x] recursive pointer types
 * [ ] global and thread local state
 * [x] union types?
 * [x] incremental compilation - save type definitions for later
 * [ ] Extension to C target for interfacing with Janet
 * [x] pointer math, pointer types
 * [x] composite types - support for load, store, move, and function args.
 * [x] Have some mechanism for field access (dest = src.offset)
 * [x] Related, move type creation as opcodes like in SPIRV - have separate virtual "type slots" and value slots for this.
 * [x] support for stack allocation of arrays
 * [ ] more math intrinsics
 * [x] source mapping (using built in Janet source mapping metadata on tuples)
 * [x] unit type or void type
 * [ ] (typed) function pointer types and remove calling untyped pointers
 * [x] APL array semantics for binary operands (maybe?)
 * [ ] a few built-in array combinators (maybe?)
 * [ ] multiple error messages in one pass
 * [ ] better verification of constants
 * [x] don't allow redefining types
 * [ ] generate elf/mach-o/pe directly
 *   [ ] elf
 *   [ ] mach-o
 *   [ ] pe
 * [ ] generate dwarf info
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
    JANET_SYSOP_CALL,
    JANET_SYSOP_SYSCALL,
    JANET_SYSOP_RETURN,
    JANET_SYSOP_JUMP,
    JANET_SYSOP_BRANCH,
    JANET_SYSOP_BRANCH_NOT,
    JANET_SYSOP_ADDRESS,
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

#define JANET_SYS_CALLFLAG_HAS_DEST 1
#define JANET_SYS_CALLFLAG_VARARGS 2

/* Allow read arguments to be constants to allow
 * encoding immediates. This makes codegen easier. */
#define JANET_SYS_MAX_OPERAND      0x7FFFFFFFU
#define JANET_SYS_CONSTANT_PREFIX  0x80000000U

typedef enum {
    JANET_SYS_CC_DEFAULT, /* Reasonable default - maps to a specific cc based on target */
    JANET_SYS_CC_SYSCALL, /* Reasonable default for platform syscalls - maps to a specific cc based on target */
    JANET_SYS_CC_X86_CDECL,
    JANET_SYS_CC_X86_STDCALL,
    JANET_SYS_CC_X86_FASTCALL,
    JANET_SYS_CC_X64_SYSV,
    JANET_SYS_CC_X64_SYSV_SYSCALL,
    JANET_SYS_CC_X64_WINDOWS,
} JanetSysCallingConvention;

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
            uint8_t flags;
            JanetSysCallingConvention calling_convention;
        } call;
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
            // Include address space?
        } pointer;
        struct {
            uint32_t dest_type;
            uint32_t type;
            uint64_t fixed_count;
        } array;
        struct {
            uint32_t id;
        } label;
        struct {
            uint32_t value;
            uint32_t has_value;
        } ret;
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

/* Keep source code information as well as
 * typing information along with constants */
typedef struct {
    uint32_t type;
    Janet value;
    // TODO - source and line
} JanetSysConstant;

/* IR representation for a single function.
 * Allow for incremental compilation and linking. */
typedef struct {
    JanetSysIRLinkage *linkage;
    JanetString link_name;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t constant_count;
    uint32_t return_type;
    uint32_t has_return_type;
    uint32_t parameter_count;
    uint32_t label_count;
    uint32_t *types;
    JanetSysInstruction *instructions;
    JanetString *register_names;
    JanetSysConstant *constants;
    JanetTable *register_name_lookup;
    JanetTable *labels;
} JanetSysIR;

/* Delay alignment info for the most part to the lowering phase */
typedef struct {
    uint32_t size;
    uint32_t alignment;
} JanetSysTypeLayout;

/* Keep track of names for each instruction */
extern const char *janet_sysop_names[];
extern const char *prim_to_prim_name[];

/* Utilities */

uint32_t janet_sys_optype(JanetSysIR *ir, uint32_t op);

/* Get list of uint32_t instruction arguments from a call or other variable length instruction.
   Needs to be free with janet_sfree (or you can leak it and the garbage collector will eventually clean
 * it up). */
uint32_t *janet_sys_callargs(JanetSysInstruction *instr, uint32_t *count);

/* Lowering */
void janet_sys_ir_lower_to_ir(JanetSysIRLinkage *linkage, JanetArray *into);
void janet_sys_ir_lower_to_c(JanetSysIRLinkage *linkage, JanetBuffer *buffer);

void janet_sys_ir_lower_to_x64(JanetSysIRLinkage *linkage, JanetBuffer *buffer);

#endif

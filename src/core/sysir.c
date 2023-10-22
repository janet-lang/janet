/*
* Copyright (c) 2023 Calvin Rose
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
 * [ ] named fields (for debugging mostly)
 * [x] named registers and types
 * [x] better type errors (perhaps mostly for compiler debugging - full type system goes on top)
 * [ ] support for switch-case
 * [ ] x86/x64 machine code target
 * [ ] target specific extensions - custom instructions and custom primitives
 * [ ] better casting semantics
 * [x] separate pointer arithmetic from generalized arithmetic (easier to instrument code for safety)?
 * [x] fixed-size array types
 * [ ] recursive pointer types
 * [ ] global and thread local state
 * [x] union types?
 * [x] incremental compilation - save type definitions for later
 * [ ] Extension to C target for interfacing with Janet
 * [ ] malloc/alloca exposure (only some targets)
 * [x] pointer math, pointer types
 * [x] callk - allow linking to other named functions
 * [x] composite types - support for load, store, move, and function args.
 * [x] Have some mechanism for field access (dest = src.offset)
 * [x] Related, move type creation as opcodes like in SPIRV - have separate virtual "type slots" and value slots for this.
 * [x] support for stack allocation of arrays
 * [ ] more math intrinsics
 * [x] source mapping (using built in Janet source mapping metadata on tuples)
 * [ ] unit type or void type
 * [ ] (typed) function pointer types and remove calling untyped pointers
 * [x] APL array semantics for binary operands (maybe?)
 * [ ] a few built-in array combinators (maybe?)
 * [ ] multiple error messages in one pass
 * [ ] better verification of constants
 * [ ] forward type inference
 * [x] don't allow redefining types
 */

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#include "vector.h"
#include <math.h>
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
    JANET_PRIM_UNKNOWN
} JanetPrim;

typedef struct {
    const char *name;
    JanetPrim prim;
} JanetPrimName;

static const JanetPrimName prim_names[] = {
    {"array", JANET_PRIM_ARRAY},
    {"boolean", JANET_PRIM_BOOLEAN},
    {"f32", JANET_PRIM_F32},
    {"f64", JANET_PRIM_F64},
    {"pointer", JANET_PRIM_POINTER},
    {"s16", JANET_PRIM_S16},
    {"s32", JANET_PRIM_S32},
    {"s64", JANET_PRIM_S64},
    {"s8", JANET_PRIM_S8},
    {"struct", JANET_PRIM_STRUCT},
    {"u16", JANET_PRIM_U16},
    {"u32", JANET_PRIM_U32},
    {"u64", JANET_PRIM_U64},
    {"u8", JANET_PRIM_U8},
    {"union", JANET_PRIM_UNION},
};

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
} JanetSysOp;

typedef struct {
    const char *name;
    JanetSysOp op;
} JanetSysInstrName;

static const JanetSysInstrName sys_op_names[] = {
    {"add", JANET_SYSOP_ADD},
    {"address", JANET_SYSOP_ADDRESS},
    {"agetp", JANET_SYSOP_ARRAY_GETP},
    {"apgetp", JANET_SYSOP_ARRAY_PGETP},
    {"band", JANET_SYSOP_BAND},
    {"bind", JANET_SYSOP_TYPE_BIND},
    {"bnot", JANET_SYSOP_BNOT},
    {"bor", JANET_SYSOP_BOR},
    {"branch", JANET_SYSOP_BRANCH},
    {"bxor", JANET_SYSOP_BXOR},
    {"call", JANET_SYSOP_CALL},
    {"cast", JANET_SYSOP_CAST},
    {"constant", JANET_SYSOP_CONSTANT},
    {"divide", JANET_SYSOP_DIVIDE},
    {"eq", JANET_SYSOP_EQ},
    {"fgetp", JANET_SYSOP_FIELD_GETP},
    {"gt", JANET_SYSOP_GT},
    {"gte", JANET_SYSOP_GTE},
    {"jump", JANET_SYSOP_JUMP},
    {"link-name", JANET_SYSOP_LINK_NAME},
    {"load", JANET_SYSOP_LOAD},
    {"lt", JANET_SYSOP_LT},
    {"lte", JANET_SYSOP_LTE},
    {"move", JANET_SYSOP_MOVE},
    {"multiply", JANET_SYSOP_MULTIPLY},
    {"neq", JANET_SYSOP_NEQ},
    {"parameter-count", JANET_SYSOP_PARAMETER_COUNT},
    {"pointer-add", JANET_SYSOP_POINTER_ADD},
    {"pointer-subtract", JANET_SYSOP_POINTER_SUBTRACT},
    {"return", JANET_SYSOP_RETURN},
    {"shl", JANET_SYSOP_SHL},
    {"shr", JANET_SYSOP_SHR},
    {"store", JANET_SYSOP_STORE},
    {"subtract", JANET_SYSOP_SUBTRACT},
    {"type-array", JANET_SYSOP_TYPE_ARRAY},
    {"type-pointer", JANET_SYSOP_TYPE_POINTER},
    {"type-prim", JANET_SYSOP_TYPE_PRIMITIVE},
    {"type-struct", JANET_SYSOP_TYPE_STRUCT},
    {"type-union", JANET_SYSOP_TYPE_UNION},
};

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
        } call;
        struct {
            uint32_t dest;
            uint32_t src;
        } two;
        struct {
            uint32_t src;
        } one;
        struct {
            union {
                uint32_t to;
                Janet temp_label;
            };
        } jump;
        struct {
            uint32_t cond;
            union {
                uint32_t to;
                Janet temp_label;
            };
        } branch;
        struct {
            uint32_t dest;
            uint32_t constant;
        } constant;
        struct {
            uint32_t dest;
            uint32_t constant;
            uint32_t arg_count;
        } callk;
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
    uint32_t *types;
    JanetSysInstruction *instructions;
    JanetString *register_names;
    Janet *constants;

    /* Can/should we remove this info after initial compilation? */
    JanetTable *register_name_lookup;
    JanetTable *labels;
} JanetSysIR;

/* Utilities */

static JanetString *table_to_string_array(JanetTable *strings_to_indices, int32_t count) {
    if (0 == count) {
        return NULL;
    }
    janet_assert(count > 0, "bad count");
    JanetString *strings = janet_malloc(count * sizeof(JanetString));
    for (int32_t i = 0; i < count; i++) {
        strings[i] = NULL;
    }
    for (int32_t i = 0; i < strings_to_indices->capacity; i++) {
        JanetKV *kv = strings_to_indices->data + i;
        if (!janet_checktype(kv->key, JANET_NIL)) {
            uint32_t index = (uint32_t) janet_unwrap_number(kv->value);
            janet_assert(index < (uint32_t) count, "bad index");
            strings[index] = janet_unwrap_string(kv->key);
        }
    }
    return strings;
}

/* Parse assembly */

static void instr_assert_length(JanetTuple tup, int32_t len, Janet x) {
    if (janet_tuple_length(tup) != len) {
        janet_panicf("expected instruction of length %d, got %v", len, x);
    }
}

static void instr_assert_min_length(JanetTuple tup, int32_t minlen, Janet x) {
    if (janet_tuple_length(tup) < minlen) {
        janet_panicf("expected instruction of at least length %d, got %v", minlen, x);
    }
}

static uint32_t instr_read_operand(Janet x, JanetSysIR *ir) {
    if (janet_checktype(x, JANET_SYMBOL)) {
        Janet check = janet_table_get(ir->register_name_lookup, x);
        if (janet_checktype(check, JANET_NUMBER)) {
            return (uint32_t) janet_unwrap_number(check);
        } else {
            uint32_t operand = ir->register_count++;
            janet_table_put(ir->register_name_lookup, x, janet_wrap_number(operand));
            return operand;
        }
    }
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->register_count) {
        ir->register_count = operand + 1;
    }
    return operand;
}

static uint32_t instr_read_field(Janet x, JanetSysIR* ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative field index, got %v", x);
    (void) ir; /* Perhaps support syntax for named fields instead of numbered */
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    return operand;
}

static uint64_t instr_read_u64(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint64(x)) janet_panicf("expected unsigned 64 bit integer, got %v", x);
    (void) ir;
    return janet_getuinteger64(&x, 0);
}

static uint32_t instr_read_type_operand(Janet x, JanetSysIR *ir) {
    JanetSysIRLinkage *linkage = ir->linkage;
    if (janet_checktype(x, JANET_SYMBOL)) {
        Janet check = janet_table_get(linkage->type_name_lookup, x);
        if (janet_checktype(check, JANET_NUMBER)) {
            return (uint32_t) janet_unwrap_number(check);
        } else {
            uint32_t operand = linkage->type_def_count++;
            janet_table_put(linkage->type_name_lookup, x, janet_wrap_number(operand));
            return operand;
        }
    }
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= linkage->type_def_count) {
        linkage->type_def_count = operand + 1;
    }
    return operand;
}

static JanetPrim instr_read_prim(Janet x) {
    if (!janet_checktype(x, JANET_SYMBOL)) {
        janet_panicf("expected primitive type, got %v", x);
    }
    JanetSymbol sym_type = janet_unwrap_symbol(x);
    const JanetPrimName *namedata = janet_strbinsearch(prim_names,
                                    sizeof(prim_names) / sizeof(prim_names[0]), sizeof(prim_names[0]), sym_type);
    if (NULL == namedata) {
        janet_panicf("unknown primitive type %v", x);
    }
    return namedata->prim;
}

static uint32_t instr_read_label(JanetSysIR *sysir, Janet x) {
    (void) sysir;
    uint32_t ret = 0;
    Janet check = janet_table_get(sysir->labels, x);
    if (!janet_checktype(check, JANET_NIL)) {
        ret = (uint32_t) janet_unwrap_number(check);
    } else {
        if (janet_checktype(x, JANET_KEYWORD)) janet_panicf("unknown label %v", x);
        if (!janet_checkuint(x)) janet_panicf("expected non-negative integer label, got %v", x);
        ret = (uint32_t) janet_unwrap_number(x);
    }
    return ret;
}

static void janet_sysir_init_instructions(JanetSysIR *out, JanetView instructions) {

    JanetSysInstruction *ir = NULL;
    JanetTable *labels = out->labels;
    JanetTable *constant_cache = janet_table(0);
    uint32_t next_constant = 0;
    int found_parameter_count = 0;

    /* Parse instructions */
    Janet x = janet_wrap_nil();
    for (int32_t i = 0; i < instructions.len; i++) {
        x = instructions.items[i];
        if (janet_checktype(x, JANET_KEYWORD)) {
            janet_table_put(labels, x, janet_wrap_number(janet_v_count(ir)));
            continue;
        }
        if (!janet_checktype(x, JANET_TUPLE)) {
            janet_panicf("expected instruction to be tuple, got %V", x);
        }
        JanetTuple tuple = janet_unwrap_tuple(x);
        if (janet_tuple_length(tuple) < 1) {
            janet_panic("invalid instruction, no opcode");
        }
        int32_t line = janet_tuple_sm_line(tuple);
        int32_t column = janet_tuple_sm_column(tuple);
        Janet opvalue = tuple[0];
        if (!janet_checktype(opvalue, JANET_SYMBOL)) {
            janet_panicf("expected opcode symbol, found %V", opvalue);
        }
        JanetSymbol opsymbol = janet_unwrap_symbol(opvalue);
        const JanetSysInstrName *namedata = janet_strbinsearch(sys_op_names,
                                            sizeof(sys_op_names) / sizeof(sys_op_names[0]), sizeof(sys_op_names[0]), opsymbol);
        if (NULL == namedata) {
            janet_panicf("unknown instruction %.4p", x);
        }
        JanetSysOp opcode = namedata->op;
        JanetSysInstruction instruction;
        instruction.opcode = opcode;
        instruction.line = line;
        instruction.column = column;
        switch (opcode) {
            case JANET_SYSOP_CALLK:
            case JANET_SYSOP_ARG:
                janet_assert(0, "not reachable");
                break;
            case JANET_SYSOP_LINK_NAME:
                instr_assert_length(tuple, 2, opvalue);
                if (out->link_name) {
                    janet_panicf("cannot rename function %s", out->link_name);
                }
                out->link_name = janet_getstring(tuple, 1);
                break;
            case JANET_SYSOP_PARAMETER_COUNT:
                instr_assert_length(tuple, 2, opvalue);
                if (found_parameter_count) {
                    janet_panic("duplicate parameter-count");
                }
                found_parameter_count = 1;
                out->parameter_count = janet_getnat(tuple, 1);
                break;
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
            case JANET_SYSOP_GT:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_LTE:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
            case JANET_SYSOP_ARRAY_GETP:
            case JANET_SYSOP_ARRAY_PGETP:
            case JANET_SYSOP_POINTER_ADD:
            case JANET_SYSOP_POINTER_SUBTRACT:
                instr_assert_length(tuple, 4, opvalue);
                instruction.three.dest = instr_read_operand(tuple[1], out);
                instruction.three.lhs = instr_read_operand(tuple[2], out);
                instruction.three.rhs = instr_read_operand(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_CALL:
                instr_assert_min_length(tuple, 2, opvalue);
                instruction.call.dest = instr_read_operand(tuple[1], out);
                Janet c = tuple[2];
                if (janet_checktype(c, JANET_SYMBOL)) {
                    instruction.callk.arg_count = janet_tuple_length(tuple) - 3;
                    Janet check = janet_table_get(constant_cache, c);
                    if (janet_checktype(check, JANET_NUMBER)) {
                        instruction.callk.constant = (uint32_t) janet_unwrap_number(check);
                    } else {
                        instruction.callk.constant = next_constant;
                        janet_table_put(constant_cache, c, janet_wrap_integer(next_constant));
                        next_constant++;
                    }
                    opcode = JANET_SYSOP_CALLK;
                    instruction.opcode = opcode;
                } else {
                    instruction.call.arg_count = janet_tuple_length(tuple) - 3;
                    instruction.call.callee = instr_read_operand(tuple[2], out);
                }
                janet_v_push(ir, instruction);
                for (int32_t j = 3; j < janet_tuple_length(tuple); j += 3) {
                    JanetSysInstruction arginstr;
                    arginstr.opcode = JANET_SYSOP_ARG;
                    arginstr.line = line;
                    arginstr.column = column;
                    arginstr.arg.args[0] = 0;
                    arginstr.arg.args[1] = 0;
                    arginstr.arg.args[2] = 0;
                    int32_t remaining = janet_tuple_length(tuple) - j;
                    if (remaining > 3) remaining = 3;
                    for (int32_t k = 0; k < remaining; k++) {
                        arginstr.arg.args[k] = instr_read_operand(tuple[j + k], out);
                    }
                    janet_v_push(ir, arginstr);
                }
                break;
            case JANET_SYSOP_LOAD:
            case JANET_SYSOP_STORE:
            case JANET_SYSOP_MOVE:
            case JANET_SYSOP_CAST:
            case JANET_SYSOP_BNOT:
            case JANET_SYSOP_ADDRESS:
                instr_assert_length(tuple, 3, opvalue);
                instruction.two.dest = instr_read_operand(tuple[1], out);
                instruction.two.src = instr_read_operand(tuple[2], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_FIELD_GETP:
                instr_assert_length(tuple, 4, opvalue);
                instruction.field.r = instr_read_operand(tuple[1], out);
                instruction.field.st = instr_read_operand(tuple[2], out);
                instruction.field.field = instr_read_field(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_RETURN:
                instr_assert_length(tuple, 2, opvalue);
                instruction.one.src = instr_read_operand(tuple[1], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_BRANCH:
                instr_assert_length(tuple, 3, opvalue);
                instruction.branch.cond = instr_read_operand(tuple[1], out);
                instruction.branch.temp_label = tuple[2];
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_JUMP:
                instr_assert_length(tuple, 2, opvalue);
                instruction.jump.temp_label = tuple[1];
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_CONSTANT: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.constant.dest = instr_read_operand(tuple[1], out);
                Janet c = tuple[2];
                Janet check = janet_table_get(constant_cache, c);
                if (janet_checktype(check, JANET_NUMBER)) {
                    instruction.constant.constant = (uint32_t) janet_unwrap_number(check);
                } else {
                    instruction.constant.constant = next_constant;
                    janet_table_put(constant_cache, c, janet_wrap_number(next_constant));
                    next_constant++;
                }
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_prim.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.type_prim.prim = instr_read_prim(tuple[2]);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_POINTER: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.pointer.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.pointer.type = instr_read_type_operand(tuple[2], out);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_ARRAY: {
                instr_assert_length(tuple, 4, opvalue);
                instruction.array.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.array.type = instr_read_type_operand(tuple[2], out);
                instruction.array.fixed_count = instr_read_u64(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_UNION: {
                instr_assert_min_length(tuple, 1, opvalue);
                instruction.type_types.dest_type = instr_read_type_operand(tuple[1], out);
                instruction.type_types.arg_count = janet_tuple_length(tuple) - 2;
                janet_v_push(ir, instruction);
                for (int32_t j = 2; j < janet_tuple_length(tuple); j += 3) {
                    JanetSysInstruction arginstr;
                    arginstr.opcode = JANET_SYSOP_ARG;
                    arginstr.line = line;
                    arginstr.column = column;
                    arginstr.arg.args[0] = 0;
                    arginstr.arg.args[1] = 0;
                    arginstr.arg.args[2] = 0;
                    int32_t remaining = janet_tuple_length(tuple) - j;
                    if (remaining > 3) remaining = 3;
                    for (int32_t k = 0; k < remaining; k++) {
                        arginstr.arg.args[k] = instr_read_type_operand(tuple[j + k], out);
                    }
                    janet_v_push(ir, arginstr);
                }
                break;
            }
            case JANET_SYSOP_TYPE_BIND: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_bind.dest = instr_read_operand(tuple[1], out);
                instruction.type_bind.type = instr_read_type_operand(tuple[2], out);
                janet_v_push(ir, instruction);
                break;
            }
        }
    }

    uint32_t ircount = (uint32_t) janet_v_count(ir);
    out->instructions = janet_v_flatten(ir);
    out->instruction_count = ircount;

    /* Types only */
    if (!out->link_name) {
        if (out->register_count) {
            janet_panic("cannot have runtime instructions in this context");
        }
        if (out->parameter_count) {
            janet_panic("cannot have parameters in this context");
        }
        if (out->constant_count) {
            janet_panic("cannot have constants in this context");
        }
        out->constants = NULL;
        out->constant_count = 0;
        return;
    }

    /* Check last instruction is jump or return */
    if (ircount == 0) {
        janet_panic("empty ir");
    }
    int32_t lasti = ircount - 1;
    if ((ir[lasti].opcode != JANET_SYSOP_JUMP) && (ir[lasti].opcode != JANET_SYSOP_RETURN)) {
        janet_panicf("last instruction must be jump or return, got %v", x);
    }



    /* Check for valid number of function parameters */
    if (out->parameter_count > out->register_count) {
        janet_panicf("too many parameters, only %u registers for %u parameters.",
                out->register_count, out->parameter_count);
    }

    /* Fix up labels */
    for (uint32_t i = 0; i < ircount; i++) {
        JanetSysInstruction instruction = out->instructions[i];
        uint32_t label_target;
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_BRANCH:
                label_target = instr_read_label(out, instruction.branch.temp_label);
                out->instructions[i].branch.to = label_target;
                break;
            case JANET_SYSOP_JUMP:
                label_target = instr_read_label(out, instruction.jump.temp_label);
                out->instructions[i].jump.to = label_target;
                break;
        }
    }

    /* Build constants */
    out->constant_count = next_constant;
    out->constants = next_constant ? janet_malloc(sizeof(Janet) * out->constant_count) : NULL;
    for (int32_t i = 0; i < constant_cache->capacity; i++) {
        JanetKV kv = constant_cache->data[i];
        if (!janet_checktype(kv.key, JANET_NIL)) {
            uint32_t index = (uint32_t) janet_unwrap_number(kv.value);
            out->constants[index] = kv.key;
        }
    }
}

/* Get a printable representation of a type on type failure */
static Janet tname(JanetSysIR *ir, uint32_t typeid) {
    JanetSysIRLinkage *linkage = ir->linkage;
    JanetString name = linkage->type_names[typeid];
    if (NULL != name) {
        return janet_wrap_string(name);
    }
    return janet_wrap_string(janet_formatc("type-id:%d", typeid));
}

static void tcheck_redef(JanetSysIR *ir, uint32_t typeid) {
    JanetSysIRLinkage *linkage = ir->linkage;
    if (linkage->type_defs[typeid].prim != JANET_PRIM_UNKNOWN) {
        janet_panicf("cannot redefine type %V", tname(ir, typeid));
    }
}

/* Build up type tables */
static void janet_sysir_init_types(JanetSysIR *ir) {
    JanetSysIRLinkage *linkage = ir->linkage;
    JanetSysTypeField *fields = NULL;
    JanetSysTypeInfo *type_defs = janet_realloc(linkage->type_defs, sizeof(JanetSysTypeInfo) * (linkage->type_def_count));
    uint32_t field_offset = linkage->field_def_count;
    uint32_t *types = janet_malloc(sizeof(uint32_t) * ir->register_count);
    linkage->type_defs = type_defs;
    ir->types = types;
    for (uint32_t i = 0; i < ir->register_count; i++) {
        ir->types[i] = 0;
    }
    for (uint32_t i = linkage->old_type_def_count; i < linkage->type_def_count; i++) {
        type_defs[i].prim = JANET_PRIM_UNKNOWN;
    }
    linkage->old_type_def_count = linkage->type_def_count;

    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        JanetSysInstruction instruction = ir->instructions[i];
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                uint32_t type_def = instruction.type_prim.dest_type;
                tcheck_redef(ir, type_def);
                type_defs[type_def].prim = instruction.type_prim.prim;
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_UNION: {
                uint32_t type_def = instruction.type_types.dest_type;
                tcheck_redef(ir, type_def);
                type_defs[type_def].prim = (instruction.opcode == JANET_SYSOP_TYPE_STRUCT)
                                           ? JANET_PRIM_STRUCT
                                           : JANET_PRIM_UNION;
                type_defs[type_def].st.field_count = instruction.type_types.arg_count;
                type_defs[type_def].st.field_start = field_offset + (uint32_t) janet_v_count(fields);
                for (uint32_t j = 0; j < instruction.type_types.arg_count; j++) {
                    uint32_t offset = j / 3 + 1;
                    uint32_t index = j % 3;
                    JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                    uint32_t arg = arg_instruction.arg.args[index];
                    JanetSysTypeField field;
                    field.type = arg;
                    janet_v_push(fields, field);
                }
                break;
            }
            case JANET_SYSOP_TYPE_POINTER: {
                uint32_t type_def = instruction.pointer.dest_type;
                tcheck_redef(ir, type_def);
                type_defs[type_def].prim = JANET_PRIM_POINTER;
                type_defs[type_def].pointer.type = instruction.pointer.type;
                break;
            }
            case JANET_SYSOP_TYPE_ARRAY: {
                uint32_t type_def = instruction.array.dest_type;
                tcheck_redef(ir, type_def);
                type_defs[type_def].prim = JANET_PRIM_ARRAY;
                type_defs[type_def].array.type = instruction.array.type;
                type_defs[type_def].array.fixed_count = instruction.array.fixed_count;
                break;
            }
            case JANET_SYSOP_TYPE_BIND: {
                uint32_t type = instruction.type_bind.type;
                uint32_t dest = instruction.type_bind.dest;
                types[dest] = type;
                break;
            }
        }
    }

    /* Append new fields to linkage */
    if (janet_v_count(fields)) {
        uint32_t new_field_count = field_offset + janet_v_count(fields);
        linkage->field_defs = janet_realloc(linkage->field_defs, sizeof(JanetSysTypeField) * new_field_count);
        memcpy(linkage->field_defs + field_offset, fields, janet_v_count(fields) * sizeof(JanetSysTypeField));
        linkage->field_def_count = new_field_count;
        janet_v_free(fields);
    }
}

/* Type checking */

static uint32_t tcheck_array_element(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    /* Dereference at most one pointer */
    if (linkage->type_defs[t].prim == JANET_PRIM_POINTER) {
        t = linkage->type_defs[t].pointer.type;
    }
    while (linkage->type_defs[t].prim == JANET_PRIM_ARRAY) {
        t = linkage->type_defs[t].array.type;
    }
    return t;
}

static void tcheck_boolean(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (linkage->type_defs[t].prim != JANET_PRIM_BOOLEAN) {
        janet_panicf("type failure, expected boolean, got %V", tname(sysir, t));
    }
}

static void tcheck_array(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (linkage->type_defs[t].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected array, got %V", tname(sysir, t));
    }
}

static void tcheck_number(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim t1 = linkage->type_defs[t].prim;
    if (t1 == JANET_PRIM_BOOLEAN ||
            t1 == JANET_PRIM_POINTER ||
            t1 == JANET_PRIM_UNION ||
            t1 == JANET_PRIM_STRUCT ||
            t1 == JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected numeric type, got %V", tname(sysir, t1));
    }
}

static void tcheck_number_or_pointer(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim t1 = linkage->type_defs[t].prim;
    if (t1 == JANET_PRIM_BOOLEAN ||
            t1 == JANET_PRIM_UNION ||
            t1 == JANET_PRIM_STRUCT ||
            t1 == JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected pointer or numeric type, got %V", tname(sysir, t1));
    }
}

static void tcheck_integer(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim t1 = linkage->type_defs[t].prim;
    if (t1 != JANET_PRIM_S32 &&
            t1 != JANET_PRIM_S64 &&
            t1 != JANET_PRIM_S16 &&
            t1 != JANET_PRIM_S8 &&
            t1 != JANET_PRIM_U32 &&
            t1 != JANET_PRIM_U64 &&
            t1 != JANET_PRIM_U16 &&
            t1 != JANET_PRIM_U8) {
        janet_panicf("type failure, expected integer type, got %V", tname(sysir, t1));
    }
}

static void tcheck_pointer(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (linkage->type_defs[t].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure, expected pointer, got %V", tname(sysir, t));
    }
}

static void tcheck_pointer_equals(JanetSysIR *sysir, uint32_t preg, uint32_t elreg) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t t1 = sysir->types[preg];
    if (linkage->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure, expected pointer, got %V", tname(sysir, t1));
    }
    uint32_t tp = linkage->type_defs[t1].pointer.type;
    uint32_t t2 = sysir->types[elreg];
    if (t2 != tp) {
        janet_panicf("type failure, %V is not compatible with a pointer to %V",
                     tname(sysir, t2),
                     tname(sysir, tp));
    }
}

static void tcheck_struct_or_union(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim prim = linkage->type_defs[t].prim;
    if (prim != JANET_PRIM_STRUCT && prim != JANET_PRIM_UNION) {
        janet_panicf("type failure, expected struct or union, got %v", tname(sysir, t));
    }
}

static void tcheck_equal(JanetSysIR *sysir, uint32_t reg1, uint32_t reg2) {
    uint32_t t1 = sysir->types[reg1];
    uint32_t t2 = sysir->types[reg2];
    if (t1 != t2) {
        janet_panicf("type failure, %V does not match %V",
                     tname(sysir, t1),
                     tname(sysir, t2));
    }
}

static void tcheck_cast(JanetSysIR *sysir, uint32_t dest, uint32_t src) {
    (void) sysir;
    (void) dest;
    (void) src;
    /* TODO - casting rules */
}

static void tcheck_constant(JanetSysIR *sysir, uint32_t dest, Janet c) {
    (void) sysir;
    (void) dest;
    (void) c;
    /* TODO - validate the the constant C can be represented as dest */
}

static void tcheck_array_getp(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    tcheck_array(sysir, sysir->types[lhs]);
    tcheck_integer(sysir, sysir->types[rhs]);
    tcheck_pointer(sysir, sysir->types[dest]);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t dtype = linkage->type_defs[sysir->types[dest]].pointer.type;
    uint32_t eltype = linkage->type_defs[sysir->types[lhs]].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure, %V does not match %V", tname(sysir, dtype), tname(sysir, eltype));
    }
}

static void tcheck_array_pgetp(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    tcheck_pointer(sysir, sysir->types[lhs]);
    tcheck_integer(sysir, sysir->types[rhs]);
    tcheck_pointer(sysir, sysir->types[dest]);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t aptype = linkage->type_defs[sysir->types[lhs]].pointer.type;
    if (linkage->type_defs[aptype].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected array type but got %V", tname(sysir, aptype));
    }
    uint32_t dtype = linkage->type_defs[sysir->types[dest]].pointer.type;
    uint32_t eltype = linkage->type_defs[aptype].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure, %V does not match %V", tname(sysir, dtype), tname(sysir, eltype));
    }
}

static void tcheck_fgetp(JanetSysIR *sysir, uint32_t dest, uint32_t st, uint32_t field) {
    tcheck_pointer(sysir, sysir->types[dest]);
    tcheck_struct_or_union(sysir, sysir->types[st]);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t struct_type = sysir->types[st];
    if (field >= linkage->type_defs[struct_type].st.field_count) {
        janet_panicf("invalid field index %u", field);
    }
    uint32_t field_type = linkage->type_defs[struct_type].st.field_start + field;
    uint32_t tfield = linkage->field_defs[field_type].type;
    uint32_t tdest = sysir->types[dest];
    uint32_t tpdest = linkage->type_defs[tdest].pointer.type;
    if (tfield != tpdest) {
        janet_panicf("field of type %V does not match %V",
                     tname(sysir, tfield),
                     tname(sysir, tpdest));
    }
}

/* Unlike C, only allow pointer on lhs for addition and subtraction */
static void tcheck_pointer_math(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    tcheck_pointer_equals(sysir, dest, lhs);
    tcheck_integer(sysir, sysir->types[rhs]);
}

static JanetString rname(JanetSysIR *sysir, uint32_t regid) {
    JanetString name = sysir->register_names[regid];
    if (NULL == name) {
        return janet_formatc("value%u", regid);
    }
    return name;
}

static void janet_sysir_type_check(JanetSysIR *sysir) {

    /* TODO: Simple forward type inference */

    /* Assert no unknown types */
    JanetSysIRLinkage *linkage = sysir->linkage;
    for (uint32_t i = 0; i < sysir->register_count; i++) {
        uint32_t type = sysir->types[i];
        JanetSysTypeInfo tinfo = linkage->type_defs[type];
        if (tinfo.prim == JANET_PRIM_UNKNOWN) {
            janet_panicf("unable to infer type for %s", rname(sysir, i));
        }
    }

    int found_return = 0;
    for (uint32_t i = 0; i < sysir->instruction_count; i++) {
        JanetSysInstruction instruction = sysir->instructions[i];
        switch (instruction.opcode) {
            case JANET_SYSOP_TYPE_PRIMITIVE:
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_UNION:
            case JANET_SYSOP_TYPE_POINTER:
            case JANET_SYSOP_TYPE_ARRAY:
            case JANET_SYSOP_TYPE_BIND:
            case JANET_SYSOP_ARG:
            case JANET_SYSOP_LINK_NAME:
            case JANET_SYSOP_PARAMETER_COUNT:
                break;
            case JANET_SYSOP_JUMP:
                ;
                if (instruction.jump.to >= sysir->instruction_count) {
                    janet_panicf("label outside of range [0, %u), got %u", sysir->instruction_count, instruction.jump.to);
                }
                break;
            case JANET_SYSOP_RETURN: {
                uint32_t ret_type = sysir->types[instruction.one.src];
                if (found_return) {
                    if (sysir->return_type != ret_type) {
                        janet_panicf("multiple return types are not allowed: %V and %V",
                                     tname(sysir, ret_type),
                                     tname(sysir, sysir->return_type));
                    }
                } else {
                    sysir->return_type = ret_type;
                }
                found_return = 1;
                break;
            }
            case JANET_SYSOP_MOVE:
                tcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_CAST:
                tcheck_cast(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_POINTER_ADD:
            case JANET_SYSOP_POINTER_SUBTRACT:
                tcheck_pointer_math(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                break;
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
                tcheck_number(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.dest]));
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.dest]));
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BNOT:
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.two.src]));
                tcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.lhs]));
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_LOAD:
                tcheck_pointer_equals(sysir, instruction.two.src, instruction.two.dest);
                break;
            case JANET_SYSOP_STORE:
                tcheck_pointer_equals(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_GT:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_LTE:
                /* TODO - allow arrays */
                tcheck_number_or_pointer(sysir, sysir->types[instruction.three.lhs]);
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                tcheck_boolean(sysir, sysir->types[instruction.three.dest]);
                break;
            case JANET_SYSOP_ADDRESS:
                tcheck_pointer(sysir, sysir->types[instruction.two.dest]);
                break;
            case JANET_SYSOP_BRANCH:
                tcheck_boolean(sysir, sysir->types[instruction.branch.cond]);
                if (instruction.branch.to >= sysir->instruction_count) {
                    janet_panicf("label outside of range [0, %u), got %u", sysir->instruction_count, instruction.branch.to);
                }
                break;
            case JANET_SYSOP_CONSTANT:
                tcheck_constant(sysir, instruction.constant.dest, sysir->constants[instruction.constant.constant]);
                break;
            case JANET_SYSOP_CALL:
                tcheck_pointer(sysir, sysir->types[instruction.call.callee]);
                break;
            case JANET_SYSOP_ARRAY_GETP:
                tcheck_array_getp(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.lhs);
                break;
            case JANET_SYSOP_ARRAY_PGETP:
                tcheck_array_pgetp(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.lhs);
                break;
            case JANET_SYSOP_FIELD_GETP:
                tcheck_fgetp(sysir, instruction.field.r, instruction.field.st, instruction.field.field);
                break;
            case JANET_SYSOP_CALLK:
                /* TODO - check function return type */
                break;
        }
    }
}

static void janet_sys_ir_linkage_init(JanetSysIRLinkage *linkage) {
    linkage->old_type_def_count = 0;
    linkage->type_def_count = 1; /* first type is always unknown by default */
    linkage->field_def_count = 0;
    linkage->type_defs = NULL;
    linkage->field_defs = NULL;
    linkage->type_name_lookup = janet_table(0);
    linkage->irs = janet_table(0);
    linkage->ir_ordered = janet_array(0);
    linkage->type_names = NULL;
}

static void janet_sys_ir_init(JanetSysIR *out, JanetView instructions, JanetSysIRLinkage *linkage) {
    JanetSysIR ir;
    memset(&ir, 0, sizeof(ir));
    memset(out, 0, sizeof(*out));

    ir.instructions = NULL;
    ir.types = NULL;
    ir.constants = NULL;
    ir.link_name = NULL;
    ir.register_count = 0;
    ir.constant_count = 0;
    ir.return_type = 0;
    ir.parameter_count = 0;
    ir.register_name_lookup = janet_table(0);
    ir.labels = janet_table(0);
    ir.register_names = NULL;
    ir.linkage = linkage;
    ir.parameter_count = 0;
    ir.link_name = NULL;

    janet_sysir_init_instructions(&ir, instructions);

    /* Patch up name mapping arrays */
    /* TODO - make more efficient, don't rebuild from scratch every time */
    if (linkage->type_names) janet_free(linkage->type_names);
    linkage->type_names = table_to_string_array(linkage->type_name_lookup, linkage->type_def_count);
    ir.register_names = table_to_string_array(ir.register_name_lookup, ir.register_count);

    janet_sysir_init_types(&ir);
    janet_sysir_type_check(&ir);

    *out = ir;
    if (ir.link_name != NULL) {
        janet_table_put(linkage->irs, janet_wrap_string(ir.link_name), janet_wrap_abstract(out));
    }
    janet_array_push(linkage->ir_ordered, janet_wrap_abstract(out));
}

/* Lowering to C */

static const char *c_prim_names[] = {
    "uint8_t",
    "int8_t",
    "uint16_t",
    "int16_t",
    "uint32_t",
    "int32_t",
    "uint64_t",
    "int64_t",
    "float",
    "double",
    "void *",
    "bool"
};

static void emit_binop(JanetSysIR *ir, JanetBuffer *buffer, JanetBuffer *tempbuf, JanetSysInstruction instruction, const char *op) {
    uint32_t operand_type = ir->types[instruction.three.dest];
    tempbuf->count = 0;
    uint32_t index_index = 0;
    int is_pointer = 0;
    JanetSysIRLinkage *linkage = ir->linkage;

    /* Top-level pointer semantics */
    if (linkage->type_defs[operand_type].prim == JANET_PRIM_POINTER) {
        operand_type = linkage->type_defs[operand_type].pointer.type;
        is_pointer = 1;
    }

    /* Add nested for loops for any dimensionality of array */
    while (linkage->type_defs[operand_type].prim == JANET_PRIM_ARRAY) {
        /* TODO - handle fixed_count == SIZE_MAX */
        janet_formatb(buffer, "for (size_t _j%u = 0; _j%u < %u; _j%u++) ",
                      index_index, index_index,
                      linkage->type_defs[operand_type].array.fixed_count,
                      index_index);
        if (is_pointer) {
            janet_formatb(tempbuf, "->els[_j%u]", index_index);
            is_pointer = 0;
        } else {
            janet_formatb(tempbuf, ".els[_j%u]", index_index);
        }
        operand_type = linkage->type_defs[operand_type].array.type;
        index_index++;
    }

    if (is_pointer) {
        janet_formatb(buffer, "*_r%u = *_r%u %s *_r%u;\n",
                      instruction.three.dest,
                      instruction.three.lhs,
                      op,
                      instruction.three.rhs);
    } else {
        Janet index_part = janet_wrap_buffer(tempbuf);
        janet_formatb(buffer, "_r%u%V = _r%u%V %s _r%u%V;\n",
                      instruction.three.dest, index_part,
                      instruction.three.lhs, index_part,
                      op,
                      instruction.three.rhs, index_part);
    }
}

void janet_sys_ir_lower_to_c(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {

    JanetBuffer *tempbuf = janet_buffer(0);

#define EMITBINOP(OP) emit_binop(ir, buffer, tempbuf, instruction, OP)

    /* Prelude */
    janet_formatb(buffer, "#include <stdint.h>\n\n");

    /* Emit type defs */
    for (uint32_t j = 0; j < (uint32_t) linkage->ir_ordered->count; j++) {
        JanetSysIR *ir = janet_unwrap_abstract(linkage->ir_ordered->data[j]);
        for (uint32_t i = 0; i < ir->instruction_count; i++) {
            JanetSysInstruction instruction = ir->instructions[i];
            switch (instruction.opcode) {
                default:
                    continue;
                case JANET_SYSOP_TYPE_PRIMITIVE:
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_UNION:
                case JANET_SYSOP_TYPE_POINTER:
                case JANET_SYSOP_TYPE_ARRAY:
                    break;
            }
            if (instruction.line > 0) {
                janet_formatb(buffer, "#line %d\n", instruction.line);
            }
            switch (instruction.opcode) {
                default:
                    break;
                case JANET_SYSOP_TYPE_PRIMITIVE:
                    janet_formatb(buffer, "typedef %s _t%u;\n", c_prim_names[instruction.type_prim.prim], instruction.type_prim.dest_type);
                    break;
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_UNION:
                    janet_formatb(buffer, (instruction.opcode == JANET_SYSOP_TYPE_STRUCT) ? "typedef struct {\n" : "typedef union {\n");
                    for (uint32_t j = 0; j < instruction.type_types.arg_count; j++) {
                        uint32_t offset = j / 3 + 1;
                        uint32_t index = j % 3;
                        JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                        janet_formatb(buffer, "    _t%u _f%u;\n", arg_instruction.arg.args[index], j);
                    }
                    janet_formatb(buffer, "} _t%u;\n", instruction.type_types.dest_type);
                    break;
                case JANET_SYSOP_TYPE_POINTER:
                    janet_formatb(buffer, "typedef _t%u *_t%u;\n", instruction.pointer.type, instruction.pointer.dest_type);
                    break;
                case JANET_SYSOP_TYPE_ARRAY:
                    janet_formatb(buffer, "typedef struct { _t%u els[%u]; } _t%u;\n", instruction.array.type, instruction.array.fixed_count, instruction.array.dest_type);
                    break;
            }
        }
    }

    /* Emit function header */
    for (uint32_t j = 0; j < (uint32_t) linkage->ir_ordered->count; j++) {
        JanetSysIR *ir = janet_unwrap_abstract(linkage->ir_ordered->data[j]);
        if (ir->link_name == NULL) {
            continue;
        }
        janet_formatb(buffer, "_t%u %s(", ir->return_type, (ir->link_name != NULL) ? ir->link_name : janet_cstring("_thunk"));
        for (uint32_t i = 0; i < ir->parameter_count; i++) {
            if (i) janet_buffer_push_cstring(buffer, ", ");
            janet_formatb(buffer, "_t%u _r%u", ir->types[i], i);
        }
        janet_buffer_push_cstring(buffer, ")\n{\n");
        for (uint32_t i = ir->parameter_count; i < ir->register_count; i++) {
            janet_formatb(buffer, "    _t%u _r%u;\n", ir->types[i], i);
        }
        janet_buffer_push_cstring(buffer, "\n");

        /* Emit body */
        for (uint32_t i = 0; i < ir->instruction_count; i++) {
            JanetSysInstruction instruction = ir->instructions[i];
            /* Skip instruction label for some opcodes */
            switch (instruction.opcode) {
                case JANET_SYSOP_TYPE_PRIMITIVE:
                case JANET_SYSOP_TYPE_BIND:
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_UNION:
                case JANET_SYSOP_TYPE_POINTER:
                case JANET_SYSOP_TYPE_ARRAY:
                case JANET_SYSOP_ARG:
                    continue;
                default:
                    break;
            }
            janet_formatb(buffer, "_i%u:\n", i);
            if (instruction.line > 0) {
                janet_formatb(buffer, "#line %d\n  ", instruction.line);
            }
            janet_buffer_push_cstring(buffer, "  ");
            switch (instruction.opcode) {
                case JANET_SYSOP_TYPE_PRIMITIVE:
                case JANET_SYSOP_TYPE_BIND:
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_UNION:
                case JANET_SYSOP_TYPE_POINTER:
                case JANET_SYSOP_TYPE_ARRAY:
                case JANET_SYSOP_ARG:
                case JANET_SYSOP_LINK_NAME:
                case JANET_SYSOP_PARAMETER_COUNT:
                    break;
                case JANET_SYSOP_CONSTANT: {
                                               uint32_t cast = ir->types[instruction.two.dest];
                                               janet_formatb(buffer, "_r%u = (_t%u) %j;\n", instruction.two.dest, cast, ir->constants[instruction.two.src]);
                                               break;
                                           }
                case JANET_SYSOP_ADDRESS:
                                           janet_formatb(buffer, "_r%u = (char *) &_r%u;\n", instruction.two.dest, instruction.two.src);
                                           break;
                case JANET_SYSOP_JUMP:
                                           janet_formatb(buffer, "goto _i%u;\n", instruction.jump.to);
                                           break;
                case JANET_SYSOP_BRANCH:
                                           janet_formatb(buffer, "if (_r%u) goto _i%u;\n", instruction.branch.cond, instruction.branch.to);
                                           break;
                case JANET_SYSOP_RETURN:
                                           janet_formatb(buffer, "return _r%u;\n", instruction.one.src);
                                           break;
                case JANET_SYSOP_ADD:
                case JANET_SYSOP_POINTER_ADD:
                                           EMITBINOP("+");
                                           break;
                case JANET_SYSOP_SUBTRACT:
                case JANET_SYSOP_POINTER_SUBTRACT:
                                           EMITBINOP("-");
                                           break;
                case JANET_SYSOP_MULTIPLY:
                                           EMITBINOP("*");
                                           break;
                case JANET_SYSOP_DIVIDE:
                                           EMITBINOP("/");
                                           break;
                case JANET_SYSOP_GT:
                                           EMITBINOP(">");
                                           break;
                case JANET_SYSOP_GTE:
                                           EMITBINOP(">");
                                           break;
                case JANET_SYSOP_LT:
                                           EMITBINOP("<");
                                           break;
                case JANET_SYSOP_LTE:
                                           EMITBINOP("<=");
                                           break;
                case JANET_SYSOP_EQ:
                                           EMITBINOP("==");
                                           break;
                case JANET_SYSOP_NEQ:
                                           EMITBINOP("!=");
                                           break;
                case JANET_SYSOP_BAND:
                                           EMITBINOP("&");
                                           break;
                case JANET_SYSOP_BOR:
                                           EMITBINOP("|");
                                           break;
                case JANET_SYSOP_BXOR:
                                           EMITBINOP("^");
                                           break;
                case JANET_SYSOP_SHL:
                                           EMITBINOP("<<");
                                           break;
                case JANET_SYSOP_SHR:
                                           EMITBINOP(">>");
                                           break;
                case JANET_SYSOP_CALL:
                                           janet_formatb(buffer, "_r%u = _r%u(", instruction.call.dest, instruction.call.callee);
                                           for (uint32_t j = 0; j < instruction.call.arg_count; j++) {
                                               uint32_t offset = j / 3 + 1;
                                               uint32_t index = j % 3;
                                               JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                                               janet_formatb(buffer, j ? ", _r%u" : "_r%u", arg_instruction.arg.args[index]);
                                           }
                                           janet_formatb(buffer, ");\n");
                                           break;
                case JANET_SYSOP_CALLK:
                                           janet_formatb(buffer, "_r%u = %j(", instruction.callk.dest, ir->constants[instruction.callk.constant]);
                                           for (uint32_t j = 0; j < instruction.callk.arg_count; j++) {
                                               uint32_t offset = j / 3 + 1;
                                               uint32_t index = j % 3;
                                               JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                                               janet_formatb(buffer, j ? ", _r%u" : "_r%u", arg_instruction.arg.args[index]);
                                           }
                                           janet_formatb(buffer, ");\n");
                                           break;
                case JANET_SYSOP_CAST:
                                           janet_formatb(buffer, "_r%u = (_t%u) _r%u;\n", instruction.two.dest, ir->types[instruction.two.dest], instruction.two.src);
                                           break;
                case JANET_SYSOP_MOVE:
                                           janet_formatb(buffer, "_r%u = _r%u;\n", instruction.two.dest, instruction.two.src);
                                           break;
                case JANET_SYSOP_BNOT:
                                           janet_formatb(buffer, "_r%u = ~_r%u;\n", instruction.two.dest, instruction.two.src);
                                           break;
                case JANET_SYSOP_LOAD:
                                           janet_formatb(buffer, "_r%u = *(_r%u);\n", instruction.two.dest, instruction.two.src);
                                           break;
                case JANET_SYSOP_STORE:
                                           janet_formatb(buffer, "*(_r%u) = _r%u;\n", instruction.two.dest, instruction.two.src);
                                           break;
                case JANET_SYSOP_FIELD_GETP:
                                           janet_formatb(buffer, "_r%u = &(_r%u._f%u);\n", instruction.field.r, instruction.field.st, instruction.field.field);
                                           break;
                case JANET_SYSOP_ARRAY_GETP:
                                           janet_formatb(buffer, "_r%u = &(_r%u.els[_r%u]);\n", instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                                           break;
                case JANET_SYSOP_ARRAY_PGETP:
                                           janet_formatb(buffer, "_r%u = &(_r%u->els[_r%u]);\n", instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                                           break;
            }
        }

        janet_buffer_push_cstring(buffer, "}\n");
#undef EMITBINOP
    }

}

static int sysir_gc(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    janet_free(ir->constants);
    janet_free(ir->types);
    janet_free(ir->instructions);
    janet_free(ir->register_names);
    return 0;
}

static int sysir_gcmark(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    for (uint32_t i = 0; i < ir->register_count; i++) {
        if (ir->register_names[i] != NULL) {
            janet_mark(janet_wrap_string(ir->register_names[i]));
        }
    }
    for (uint32_t i = 0; i < ir->constant_count; i++) {
        janet_mark(ir->constants[i]);
    }
    if (ir->link_name != NULL) {
        janet_mark(janet_wrap_string(ir->link_name));
    }
    return 0;
}


static int sysir_context_gc(void *p, size_t s) {
    JanetSysIRLinkage *linkage = (JanetSysIRLinkage *)p;
    (void) s;
    janet_free(linkage->field_defs);
    janet_free(linkage->type_defs);
    janet_free(linkage->type_names);
    return 0;
}

static int sysir_context_gcmark(void *p, size_t s) {
    JanetSysIRLinkage *linkage = (JanetSysIRLinkage *)p;
    (void) s;
    janet_mark(janet_wrap_table(linkage->type_name_lookup));
    janet_mark(janet_wrap_table(linkage->irs));
    janet_mark(janet_wrap_array(linkage->ir_ordered));
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        if (linkage->type_names[i] != NULL) {
            janet_mark(janet_wrap_string(linkage->type_names[i]));
        }
    }
    return 0;
}

static const JanetAbstractType janet_sysir_type = {
    "core/sysir",
    sysir_gc,
    sysir_gcmark,
    JANET_ATEND_GCMARK
};

static const JanetAbstractType janet_sysir_context_type = {
    "core/sysir-context",
    sysir_context_gc,
    sysir_context_gcmark,
    JANET_ATEND_GCMARK
};

JANET_CORE_FN(cfun_sysir_context,
        "(sysir/context)",
        "Create a linkage context to compile functions in. All functions that share a context can be linked against one another, share "
        "type declarations, share global state, and be compiled to a single object or executable. Returns a new context.") {
    janet_fixarity(argc, 0);
    (void) argv;
    JanetSysIRLinkage *linkage = janet_abstract(&janet_sysir_context_type, sizeof(JanetSysIRLinkage));
    janet_sys_ir_linkage_init(linkage);
    return janet_wrap_abstract(linkage);
}

JANET_CORE_FN(cfun_sysir_asm,
              "(sysir/asm context ir)",
              "Compile the system dialect IR into an object that can be manipulated, optimized, or lowered to other targets like C.") {
    janet_fixarity(argc, 2);
    JanetSysIRLinkage *linkage = janet_getabstract(argv, 0, &janet_sysir_context_type);
    JanetView instructions = janet_getindexed(argv, 1);
    JanetSysIR *sysir = janet_abstract(&janet_sysir_type, sizeof(JanetSysIR));
    janet_sys_ir_init(sysir, instructions, linkage);
    return janet_wrap_abstract(sysir);
}

JANET_CORE_FN(cfun_sysir_toc,
              "(sysir/to-c context &opt buffer)",
              "Lower some IR to a C function. Return a modified buffer that can be passed to a C compiler.") {
    janet_arity(argc, 1, 2);
    JanetSysIRLinkage *ir = janet_getabstract(argv, 0, &janet_sysir_context_type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 1, 0);
    janet_sys_ir_lower_to_c(ir, buffer);
    return janet_wrap_buffer(buffer);
}

void janet_lib_sysir(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("sysir/context", cfun_sysir_context),
        JANET_CORE_REG("sysir/asm", cfun_sysir_asm),
        JANET_CORE_REG("sysir/to-c", cfun_sysir_toc),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}

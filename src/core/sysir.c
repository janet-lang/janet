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

/* TODO
 * [ ] named fields (for debugging mostly)
 * [x] named registers and types
 * [ ] better type errors (perhaps mostly for compiler debugging - full type system goes on top)
 * [ ] x86/x64 machine code target
 * [ ] target specific extensions - custom instructions and custom primitives
 * [ ] better casting semantics
 * [ ] separate pointer arithmetic from generalized arithmetic (easier to instrument code for safety)?
 * [x] fixed-size array types
 * [ ] recursive pointer types
 * [x] union types?
 * [ ] incremental compilation - save type definitions for later
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
 * [ ] better C interface for building up IR
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
};

typedef enum {
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
    {"load", JANET_SYSOP_LOAD},
    {"lt", JANET_SYSOP_LT},
    {"lte", JANET_SYSOP_LTE},
    {"move", JANET_SYSOP_MOVE},
    {"multiply", JANET_SYSOP_MULTIPLY},
    {"neq", JANET_SYSOP_NEQ},
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

typedef struct {
    JanetString link_name;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t type_def_count;
    uint32_t field_def_count;
    uint32_t constant_count;
    uint32_t return_type;
    uint32_t *types;
    JanetSysTypeInfo *type_defs;
    JanetSysTypeField *field_defs;
    JanetSysInstruction *instructions;
    JanetString *register_names;
    JanetString *type_names;
    Janet *constants;
    uint32_t parameter_count;
} JanetSysIR;

typedef struct {
    JanetSysIR ir;
    JanetTable *register_names;
    JanetTable *type_names;
    JanetTable *labels;
} JanetSysIRBuilder;

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
        janet_panicf("expected instruction of at least ength %d, got %v", minlen, x);
    }
}

static uint32_t instr_read_operand(Janet x, JanetSysIRBuilder *ir) {
    if (janet_checktype(x, JANET_SYMBOL)) {
        Janet check = janet_table_get(ir->register_names, x);
        if (janet_checktype(check, JANET_NUMBER)) {
            return (uint32_t) janet_unwrap_number(check);
        } else {
            uint32_t operand = ir->ir.register_count++;
            janet_table_put(ir->register_names, x, janet_wrap_number(operand));
            return operand;
        }
    }
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->ir.register_count) {
        ir->ir.register_count = operand + 1;
    }
    return operand;
}

static uint32_t instr_read_field(Janet x, JanetSysIRBuilder *ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative field index, got %v", x);
    (void) ir; /* Perhaps support syntax for named fields instead of numbered */
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    return operand;
}

static uint64_t instr_read_u64(Janet x, JanetSysIRBuilder *ir) {
    if (!janet_checkuint64(x)) janet_panicf("expected unsigned 64 bit integer, got %v", x);
    (void) ir;
    return janet_getuinteger64(&x, 0);
}

static uint32_t instr_read_type_operand(Janet x, JanetSysIRBuilder *ir) {
    if (janet_checktype(x, JANET_SYMBOL)) {
        Janet check = janet_table_get(ir->type_names, x);
        if (janet_checktype(check, JANET_NUMBER)) {
            return (uint32_t) janet_unwrap_number(check);
        } else {
            uint32_t operand = ir->ir.type_def_count++;
            janet_table_put(ir->type_names, x, janet_wrap_number(operand));
            return operand;
        }
    }
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %v", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->ir.type_def_count) {
        ir->ir.type_def_count = operand + 1;
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
        janet_panicf("unknown type %v", x);
    }
    return namedata->prim;
}

static uint32_t instr_read_label(JanetSysIRBuilder *sysir, Janet x) {
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

static void janet_sysir_init_instructions(JanetSysIRBuilder *out, JanetView instructions) {

    JanetSysInstruction *ir = NULL;
    JanetTable *labels = out->labels;
    JanetTable *constant_cache = janet_table(0);
    uint32_t next_constant = 0;

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

    /* Check last instruction is jump or return */
    uint32_t ircount = (uint32_t) janet_v_count(ir);
    if (ircount == 0) {
        janet_panic("empty ir");
    }
    int32_t lasti = ircount - 1;
    if ((ir[lasti].opcode != JANET_SYSOP_JUMP) && (ir[lasti].opcode != JANET_SYSOP_RETURN)) {
        janet_panicf("last instruction must be jump or return, got %v", x);
    }

    /* Fix up instructions table */
    out->ir.instructions = janet_v_flatten(ir);
    out->ir.instruction_count = ircount;

    /* Fix up labels */
    for (uint32_t i = 0; i < ircount; i++) {
        JanetSysInstruction instruction = out->ir.instructions[i];
        uint32_t label_target;
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_BRANCH:
                label_target = instr_read_label(out, instruction.branch.temp_label);
                out->ir.instructions[i].branch.to = label_target;
                break;
            case JANET_SYSOP_JUMP:
                label_target = instr_read_label(out, instruction.jump.temp_label);
                out->ir.instructions[i].jump.to = label_target;
                break;
        }
    }

    /* Build constants */
    out->ir.constant_count = next_constant;
    out->ir.constants = next_constant ? janet_malloc(sizeof(Janet) * out->ir.constant_count) : NULL;
    for (int32_t i = 0; i < constant_cache->capacity; i++) {
        JanetKV kv = constant_cache->data[i];
        if (!janet_checktype(kv.key, JANET_NIL)) {
            uint32_t index = (uint32_t) janet_unwrap_number(kv.value);
            out->ir.constants[index] = kv.key;
        }
    }
}

/* Build up type tables */
static void janet_sysir_init_types(JanetSysIR *ir) {
    JanetSysTypeField *fields = NULL;
    if (ir->type_def_count == 0) {
        ir->type_def_count++;
    }
    JanetSysTypeInfo *type_defs = janet_malloc(sizeof(JanetSysTypeInfo) * (ir->type_def_count));
    uint32_t *types = janet_malloc(sizeof(uint32_t) * ir->register_count);
    ir->type_defs = type_defs;
    ir->types = types;
    ir->type_defs[0].prim = JANET_PRIM_S32;
    for (uint32_t i = 0; i < ir->register_count; i++) {
        ir->types[i] = 0;
    }

    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        JanetSysInstruction instruction = ir->instructions[i];
        switch (instruction.opcode) {
            default:
                break;
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                uint32_t type_def = instruction.type_prim.dest_type;
                type_defs[type_def].prim = instruction.type_prim.prim;
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_UNION: {
                uint32_t type_def = instruction.type_types.dest_type;
                type_defs[type_def].prim = (instruction.opcode == JANET_SYSOP_TYPE_STRUCT)
                                           ? JANET_PRIM_STRUCT
                                           : JANET_PRIM_UNION;
                type_defs[type_def].st.field_count = instruction.type_types.arg_count;
                type_defs[type_def].st.field_start = (uint32_t) janet_v_count(fields);
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
                type_defs[type_def].prim = JANET_PRIM_POINTER;
                type_defs[type_def].pointer.type = instruction.pointer.type;
                break;
            }
            case JANET_SYSOP_TYPE_ARRAY: {
                uint32_t type_def = instruction.array.dest_type;
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

    ir->field_defs = janet_v_flatten(fields);
}

/* Type checking */

/* Get a printable representation of a type on type failure */
static Janet tname(JanetSysIR *ir, uint32_t typeid) {
    JanetString name = ir->type_names[typeid];
    if (NULL != name) {
        return janet_wrap_string(name);
    }
    return janet_wrap_string(janet_formatc("type-id:%d", typeid));
}

static void tcheck_boolean(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_BOOLEAN) {
        janet_panicf("type failure, expected boolean, got %V", tname(sysir, t1));
    }
}

static void tcheck_array(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected array, got %V", tname(sysir, t1));
    }
}

static void tcheck_number(JanetSysIR *sysir, uint32_t reg1) {
    JanetPrim t1 = sysir->type_defs[sysir->types[reg1]].prim;
    if (t1 == JANET_PRIM_BOOLEAN ||
            t1 == JANET_PRIM_POINTER ||
            t1 == JANET_PRIM_UNION ||
            t1 == JANET_PRIM_STRUCT) {
        janet_panicf("type failure, expected numeric type, got %V", tname(sysir, t1));
    }
}

static void tcheck_integer(JanetSysIR *sysir, uint32_t reg1) {
    JanetPrim t1 = sysir->type_defs[sysir->types[reg1]].prim;
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

static void tcheck_pointer(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    if (sysir->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure, expected pointer, got %V", tname(sysir, t1));
    }
}

static void tcheck_pointer_equals(JanetSysIR *sysir, uint32_t preg, uint32_t elreg) {
    uint32_t t1 = sysir->types[preg];
    if (sysir->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure, expected pointer, got %V", tname(sysir, t1));
    }
    uint32_t tp = sysir->type_defs[t1].pointer.type;
    uint32_t t2 = sysir->types[elreg];
    if (t2 != tp) {
        janet_panicf("type failure, %V is not compatible with a pointer to %V",
                     tname(sysir, t2),
                     tname(sysir, tp));
    }
}

static void tcheck_struct_or_union(JanetSysIR *sysir, uint32_t reg1) {
    uint32_t t1 = sysir->types[reg1];
    JanetPrim prim = sysir->type_defs[t1].prim;
    if (prim != JANET_PRIM_STRUCT && prim != JANET_PRIM_UNION) {
        janet_panicf("type failure, expected struct or union, got %v", tname(sysir, t1));
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
    tcheck_array(sysir, lhs);
    tcheck_integer(sysir, rhs);
    tcheck_pointer(sysir, dest);
    uint32_t dtype = sysir->type_defs[sysir->types[dest]].pointer.type;
    uint32_t eltype = sysir->type_defs[sysir->types[lhs]].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure, %V does not match %V", tname(sysir, dtype), tname(sysir, eltype));
    }
}

static void tcheck_array_pgetp(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    tcheck_pointer(sysir, lhs);
    tcheck_integer(sysir, rhs);
    tcheck_pointer(sysir, dest);
    uint32_t aptype = sysir->type_defs[sysir->types[lhs]].pointer.type;
    if (sysir->type_defs[aptype].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure, expected array type but got %V", tname(sysir, aptype));
    }
    uint32_t dtype = sysir->type_defs[sysir->types[dest]].pointer.type;
    uint32_t eltype = sysir->type_defs[aptype].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure, %V does not match %V", tname(sysir, dtype), tname(sysir, eltype));
    }
}

/* Add and subtract can be used for pointer math as well as normal arithmetic. Unlike C, only
 * allow pointer on lhs for addition. */
static void tcheck_pointer_math(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    uint32_t tdest = sysir->types[dest];
    uint32_t tlhs = sysir->types[lhs];
    if (tdest != tlhs) {
        janet_panicf("type failure, %V does not match %V", tname(sysir, tdest),
                     tname(sysir, tlhs));
    }
    uint32_t pdest = sysir->type_defs[tdest].prim;
    if (pdest == JANET_PRIM_POINTER) {
        tcheck_integer(sysir, rhs);
    } else {
        tcheck_equal(sysir, lhs, rhs);
    }
}

static void janet_sysir_type_check(JanetSysIR *sysir) {
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
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
                tcheck_pointer_math(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                break;
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
                tcheck_number(sysir, instruction.three.dest);
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
                tcheck_integer(sysir, instruction.three.lhs);
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BNOT:
                tcheck_integer(sysir, instruction.two.src);
                tcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
                tcheck_integer(sysir, instruction.three.lhs);
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
                tcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                tcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                tcheck_boolean(sysir, instruction.three.dest);
                break;
            case JANET_SYSOP_ADDRESS:
                tcheck_pointer(sysir, instruction.two.dest);
                break;
            case JANET_SYSOP_BRANCH:
                tcheck_boolean(sysir, instruction.branch.cond);
                if (instruction.branch.to >= sysir->instruction_count) {
                    janet_panicf("label outside of range [0, %u), got %u", sysir->instruction_count, instruction.branch.to);
                }
                break;
            case JANET_SYSOP_CONSTANT:
                tcheck_constant(sysir, instruction.constant.dest, sysir->constants[instruction.constant.constant]);
                break;
            case JANET_SYSOP_CALL:
                tcheck_pointer(sysir, instruction.call.callee);
                break;
            case JANET_SYSOP_ARRAY_GETP:
                tcheck_array_getp(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.lhs);
                break;
            case JANET_SYSOP_ARRAY_PGETP:
                tcheck_array_pgetp(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.lhs);
                break;
            case JANET_SYSOP_FIELD_GETP:
                tcheck_pointer(sysir, instruction.field.r);
                tcheck_struct_or_union(sysir, instruction.field.st);
                uint32_t struct_type = sysir->types[instruction.field.st];
                if (instruction.field.field >= sysir->type_defs[struct_type].st.field_count) {
                    janet_panicf("invalid field index %u", instruction.field.field);
                }
                uint32_t field_type = sysir->type_defs[struct_type].st.field_start + instruction.field.field;
                uint32_t tfield = sysir->field_defs[field_type].type;
                uint32_t tdest = sysir->types[instruction.field.r];
                uint32_t tpdest = sysir->type_defs[tdest].pointer.type;
                if (tfield != tpdest) {
                    janet_panicf("field of type %V does not match %V",
                                 tname(sysir, tfield),
                                 tname(sysir, tpdest));
                }
                break;
            case JANET_SYSOP_CALLK:
                /* TODO - check function return type */
                break;
        }
    }
}

void janet_sys_ir_init_from_table(JanetSysIR *out, JanetTable *table) {
    JanetSysIRBuilder b;

    b.ir.instructions = NULL;
    b.ir.types = NULL;
    b.ir.type_defs = NULL;
    b.ir.field_defs = NULL;
    b.ir.constants = NULL;
    b.ir.link_name = NULL;
    b.ir.register_count = 0;
    b.ir.type_def_count = 0;
    b.ir.field_def_count = 0;
    b.ir.constant_count = 0;
    b.ir.return_type = 0;
    b.ir.parameter_count = 0;

    b.register_names = janet_table(0);
    b.type_names = janet_table(0);
    b.labels = janet_table(0);

    Janet assembly = janet_table_get(table, janet_ckeywordv("instructions"));
    Janet param_count = janet_table_get(table, janet_ckeywordv("parameter-count"));
    Janet link_namev = janet_table_get(table, janet_ckeywordv("link-name"));
    JanetView asm_view = janet_getindexed(&assembly, 0);
    JanetString link_name = janet_getstring(&link_namev, 0);
    int32_t parameter_count = janet_getnat(&param_count, 0);
    b.ir.parameter_count = parameter_count;
    b.ir.link_name = link_name;

    janet_sysir_init_instructions(&b, asm_view);

    b.ir.type_names = table_to_string_array(b.type_names, b.ir.type_def_count);
    b.ir.register_names = table_to_string_array(b.register_names, b.ir.register_count);

    janet_sysir_init_types(&b.ir);
    janet_sysir_type_check(&b.ir);

    *out = b.ir;
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

void janet_sys_ir_lower_to_c(JanetSysIR *ir, JanetBuffer *buffer) {

#define EMITBINOP(OP) \
    janet_formatb(buffer, "_r%u = _r%u " OP " _r%u;\n", instruction.three.dest, instruction.three.lhs, instruction.three.rhs)

    janet_formatb(buffer, "#include <stdint.h>\n\n");

    /* Emit type defs */
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

    /* Emit function header */
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
                EMITBINOP("+");
                break;
            case JANET_SYSOP_SUBTRACT:
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
                /* TODO - make casting rules explicit instead of just whatever C does */
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

static int sysir_gc(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    janet_free(ir->constants);
    janet_free(ir->types);
    janet_free(ir->instructions);
    janet_free(ir->type_defs);
    janet_free(ir->field_defs);
    janet_free(ir->register_names);
    janet_free(ir->type_names);
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
    for (uint32_t i = 0; i < ir->type_def_count; i++) {
        if (ir->type_names[i] != NULL) {
            janet_mark(janet_wrap_string(ir->type_names[i]));
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

static const JanetAbstractType janet_sysir_type = {
    "core/sysir",
    sysir_gc,
    sysir_gcmark,
    JANET_ATEND_GCMARK
};

JANET_CORE_FN(cfun_sysir_asm,
              "(sysir/asm assembly)",
              "Compile the system dialect IR into an object that can be manipulated, optimized, or lowered to other targets like C.") {
    janet_fixarity(argc, 1);
    JanetTable *tab = janet_gettable(argv, 0);
    JanetSysIR *sysir = janet_abstract(&janet_sysir_type, sizeof(JanetSysIR));
    janet_sys_ir_init_from_table(sysir, tab);
    return janet_wrap_abstract(sysir);
}

JANET_CORE_FN(cfun_sysir_toc,
              "(sysir/to-c sysir &opt buffer)",
              "Lower some IR to a C function. Return a modified buffer that can be passed to a C compiler.") {
    janet_arity(argc, 1, 2);
    JanetSysIR *ir = janet_getabstract(argv, 0, &janet_sysir_type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 1, 0);
    janet_sys_ir_lower_to_c(ir, buffer);
    return janet_wrap_buffer(buffer);
}

void janet_lib_sysir(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("sysir/asm", cfun_sysir_asm),
        JANET_CORE_REG("sysir/to-c", cfun_sysir_toc),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}

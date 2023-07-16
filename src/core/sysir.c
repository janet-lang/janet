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
 * [ ] pointer math, pointer types
 * [ ] callk - allow linking to other named functions
 * [ ] composite types - support for load, store, move, and function args.
 * [ ] Have some mechanism for field access (dest = src.offset)
 * [ ] Related, move type creation as opcodes like in SPIRV - have separate virtual "type slots" and value slots for this.
 * [ ] support for stack allocation of arrays
 * [ ] more math intrinsics
 * [ ] source mapping (using built in Janet source mapping metadata on tuples)
 * [ ] better C interface for building up IR
 */

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
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
    JANET_PRIM_BOOLEAN
} JanetPrim;

typedef struct {
    const char *name;
    JanetPrim prim;
} JanetPrimName;

static const JanetPrimName prim_names[] = {
    {"boolean", JANET_PRIM_BOOLEAN},
    {"f32", JANET_PRIM_F32},
    {"f64", JANET_PRIM_F64},
    {"pointer", JANET_PRIM_POINTER},
    {"s16", JANET_PRIM_S16},
    {"s32", JANET_PRIM_S32},
    {"s64", JANET_PRIM_S64},
    {"s8", JANET_PRIM_S8},
    {"u16", JANET_PRIM_U16},
    {"u32", JANET_PRIM_U32},
    {"u64", JANET_PRIM_U64},
    {"u8", JANET_PRIM_U8},
};

static const char *prim_names_by_id[] = {
    "u8",
    "s8",
    "u16",
    "s16",
    "u32",
    "s32",
    "u64",
    "s64",
    "f32",
    "f64",
    "pointer",
    "boolean",
};

typedef enum {
    JANET_SYSOPVAR_THREE,
    JANET_SYSOPVAR_TWO,
    JANET_SYSOPVAR_ONE,
    JANET_SYSOPVAR_JUMP,
    JANET_SYSOPVAR_BRANCH,
    JANET_SYSOPVAR_CALL,
    JANET_SYSOPVAR_CONSTANT
} JanetSysOpVariant;

typedef enum {
    JANET_SYSOP_MOVE,
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
    JANET_SYSOP_PUSH1,
    JANET_SYSOP_PUSH2,
    JANET_SYSOP_PUSH3,
    JANET_SYSOP_ADDRESS,
} JanetSysOp;

static const JanetSysOpVariant op_variants[] = {
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_MOVE */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_ADD */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_SUBTRACT */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_MULTIPLY */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_DIVIDE */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_BAND */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_BOR */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_BXOR */
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_BNOT */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_SHL */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_SHR */
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_LOAD */
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_STORE */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_GT */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_LT */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_EQ */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_NEQ */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_GTE */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_LTE */
    JANET_SYSOPVAR_CONSTANT, /* JANET_SYSOP_CONSTANT */
    JANET_SYSOPVAR_CALL, /* JANET_SYSOP_CALL */
    JANET_SYSOPVAR_ONE, /* JANET_SYSOP_RETURN */
    JANET_SYSOPVAR_JUMP, /* JANET_SYSOP_JUMP */
    JANET_SYSOPVAR_BRANCH, /* JANET_SYSOP_BRANCH */
    JANET_SYSOPVAR_ONE, /* JANET_SYSOP_PUSH1 */
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_PUSH1 */
    JANET_SYSOPVAR_THREE, /* JANET_SYSOP_PUSH1 */
    JANET_SYSOPVAR_TWO, /* JANET_SYSOP_ADDRESS */
};

typedef struct {
    const char *name;
    JanetSysOp op;
} JanetSysInstrName;

static const JanetSysInstrName sys_op_names[] = {
    {"add", JANET_SYSOP_ADD},
    {"address", JANET_SYSOP_ADDRESS},
    {"band", JANET_SYSOP_BAND},
    {"bnot", JANET_SYSOP_BNOT},
    {"bor", JANET_SYSOP_BOR},
    {"branch", JANET_SYSOP_BRANCH},
    {"bxor", JANET_SYSOP_BXOR},
    {"call", JANET_SYSOP_CALL},
    {"constant", JANET_SYSOP_CONSTANT},
    {"divide", JANET_SYSOP_DIVIDE},
    {"eq", JANET_SYSOP_EQ},
    {"gt", JANET_SYSOP_GT},
    {"gte", JANET_SYSOP_GTE},
    {"jump", JANET_SYSOP_JUMP},
    {"load", JANET_SYSOP_LOAD},
    {"lt", JANET_SYSOP_LT},
    {"lte", JANET_SYSOP_LTE},
    {"move", JANET_SYSOP_MOVE},
    {"multiply", JANET_SYSOP_MULTIPLY},
    {"neq", JANET_SYSOP_NEQ},
    {"push1", JANET_SYSOP_PUSH1},
    {"push2", JANET_SYSOP_PUSH2},
    {"push3", JANET_SYSOP_PUSH3},
    {"return", JANET_SYSOP_RETURN},
    {"shl", JANET_SYSOP_SHL},
    {"shr", JANET_SYSOP_SHR},
    {"store", JANET_SYSOP_STORE},
    {"subtract", JANET_SYSOP_SUBTRACT},
};

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
            uint32_t dest;
            uint32_t constant;
        } constant;
    };
    int32_t line;
    int32_t column;
} JanetSysInstruction;

typedef struct {
    JanetString link_name;
    uint32_t instruction_count;
    uint32_t type_count;
    uint32_t constant_count;
    JanetPrim *types;
    JanetPrim return_type;
    JanetSysInstruction *instructions;
    Janet *constants;
    uint32_t parameter_count;
} JanetSysIR;

static void instr_assert_length(JanetTuple tup, int32_t len, Janet x) {
    if (janet_tuple_length(tup) != len) {
        janet_panicf("expected instruction of length %d, got %v", len, x);
    }
}

static uint32_t instr_read_operand(Janet x, int32_t max_operand) {
    int32_t operand = 0;
    int fail = 0;
    if (!janet_checkint(x)) fail = 1;
    if (!fail) {
        operand = janet_unwrap_integer(x);
        if (operand < 0) fail = 1;
        if (operand > max_operand) fail = 1;
    }
    if (fail) janet_panicf("expected integer operand in range [0, %d], got %v", max_operand, x);
    return (uint32_t) operand;
}

static uint32_t instr_read_label(Janet x, JanetTable *labels, int32_t max_label) {
    int32_t operand = 0;
    int fail = 0;
    Janet check = janet_table_get(labels, x);
    if (!janet_checktype(check, JANET_NIL)) return (uint32_t) janet_unwrap_number(check);
    if (!janet_checkint(x)) fail = 1;
    if (!fail) {
        operand = janet_unwrap_integer(x);
        if (operand < 0) fail = 1;
        if (operand > max_label) fail = 1;
    }
    if (fail) janet_panicf("expected label in range [0, %d], got %v", max_label, x);
    return (uint32_t) operand;
}

static void janet_sysir_init_types(JanetSysIR *out, JanetView types) {
    uint32_t type_count = types.len;
    out->types = janet_malloc(sizeof(JanetPrim) * type_count);
    for (int32_t i = 0; i < types.len; i++) {
        Janet x = types.items[i];
        if (!janet_checktype(x, JANET_SYMBOL)) {
            janet_panicf("expected primitive type, got %v", x);
        }
        JanetSymbol sym_type = janet_unwrap_symbol(x);
        const JanetPrimName *namedata = janet_strbinsearch(prim_names,
                                        sizeof(prim_names) / sizeof(prim_names[0]), sizeof(prim_names[0]), sym_type);
        if (NULL == namedata) {
            janet_panicf("unknown type %v", x);
        }
        out->types[i] = namedata->prim;
    }
    out->type_count = type_count;
}

#define U_FLAGS ((1u << JANET_PRIM_U8) | (1u << JANET_PRIM_U16) | (1u << JANET_PRIM_U32) | (1u << JANET_PRIM_U64))
#define S_FLAGS ((1u << JANET_PRIM_S8) | (1u << JANET_PRIM_S16) | (1u << JANET_PRIM_S32) | (1u << JANET_PRIM_S64))
#define F_FLAGS ((1u << JANET_PRIM_F32) | (1u << JANET_PRIM_F64))
#define NUMBER_FLAGS (U_FLAGS | S_FLAGS | F_FLAGS)
#define INTEGER_FLAGS (U_FLAGS | S_FLAGS)

/* Mainly check the instruction arguments are of compatible types */
static void check_instruction_well_formed(JanetSysInstruction instruction, Janet x, JanetSysIR *ir) {
    int fail = 0;
    switch (instruction.opcode) {
        /* TODO */
        /* case JANET_SYSOP_CALL: */
        /* case JANET_SYSOP_CONSTANT: */
        /* case JANET_SYSOP_JUMP: */
        /* case JANET_SYSOP_ADDRESS: */
        default:
            break;
        case JANET_SYSOP_ADD:
        case JANET_SYSOP_SUBTRACT:
        case JANET_SYSOP_MULTIPLY:
        case JANET_SYSOP_DIVIDE: {
            JanetPrim pdest = ir->types[instruction.three.dest];
            JanetPrim plhs = ir->types[instruction.three.lhs];
            JanetPrim prhs = ir->types[instruction.three.rhs];
            if ((pdest != prhs) || (prhs != plhs)) fail = 1;
            if (!((1u << pdest) & NUMBER_FLAGS)) fail = 1;
            break;
        }
        case JANET_SYSOP_LT:
        case JANET_SYSOP_LTE:
        case JANET_SYSOP_GT:
        case JANET_SYSOP_GTE:
        case JANET_SYSOP_EQ:
        case JANET_SYSOP_NEQ: {
            JanetPrim pdest = ir->types[instruction.three.dest];
            JanetPrim plhs = ir->types[instruction.three.lhs];
            JanetPrim prhs = ir->types[instruction.three.rhs];
            if ((pdest != JANET_PRIM_BOOLEAN) || (prhs != plhs)) fail = 1;
            if (!((1u << pdest) & NUMBER_FLAGS)) fail = 1;
            break;
        }
        case JANET_SYSOP_BAND:
        case JANET_SYSOP_BOR:
        case JANET_SYSOP_BXOR: {
            JanetPrim pdest = ir->types[instruction.three.dest];
            JanetPrim plhs = ir->types[instruction.three.lhs];
            JanetPrim prhs = ir->types[instruction.three.rhs];
            if (pdest != plhs) fail = 1;
            if (pdest != prhs) fail = 1;
            if (!((1u << pdest) & INTEGER_FLAGS)) fail = 1;
            break;
        }
        case JANET_SYSOP_SHR:
        case JANET_SYSOP_SHL: {
            JanetPrim pdest = ir->types[instruction.three.dest];
            JanetPrim plhs = ir->types[instruction.three.lhs];
            JanetPrim prhs = ir->types[instruction.three.rhs];
            if (pdest != plhs) fail = 1;
            if (!((1u << pdest) & INTEGER_FLAGS)) fail = 1;
            if (!((1u << prhs) & U_FLAGS)) fail = 1;
            break;
        }
        case JANET_SYSOP_BRANCH: {
            JanetPrim pcond = ir->types[instruction.branch.cond];
            if (!((1u << pcond) & ((1u << JANET_PRIM_BOOLEAN) | INTEGER_FLAGS))) fail = 1;
            break;
        }
        case JANET_SYSOP_MOVE: {
            JanetPrim pdest = ir->types[instruction.two.dest];
            JanetPrim psrc = ir->types[instruction.two.src];
            if (pdest != psrc) fail = 1;
            break;
        }
        case JANET_SYSOP_BNOT: {
            JanetPrim pdest = ir->types[instruction.two.dest];
            JanetPrim psrc = ir->types[instruction.two.src];
            if (pdest != psrc) fail = 1;
            if (!((1u << pdest) & INTEGER_FLAGS)) fail = 1;
            break;
        }
        case JANET_SYSOP_ADDRESS: {
            JanetPrim pdest = ir->types[instruction.two.dest];
            if (pdest != JANET_PRIM_POINTER) fail = 1;
            break;
        }
    }
    if (fail) janet_panicf("invalid types for instruction %V", x);
}

static void janet_sysir_init_instructions(JanetSysIR *out, JanetView instructions) {
    uint32_t pending_count = instructions.len;
    JanetSysInstruction *ir = janet_malloc(sizeof(JanetSysInstruction) * pending_count);
    out->instructions = ir;
    uint32_t cursor = 0;
    int32_t max_op = out->type_count - 1;
    int32_t max_label = 0;
    int inside_call = false;
    /* TODO - preserve labels in generated output (c) */
    JanetTable *labels = janet_table(0);
    JanetTable *constant_cache = janet_table(0);
    uint32_t next_constant = 0;
    for (int32_t i = 0; i < instructions.len; i++) {
        Janet x = instructions.items[i];
        if (janet_checktype(x, JANET_KEYWORD)) {
            janet_table_put(labels, x, janet_wrap_integer(max_label));
        } else {
            max_label++;
        }
    }
    pending_count = max_label;
    max_label--;
    Janet x = janet_wrap_nil();
    for (int32_t i = 0; i < instructions.len; i++) {
        x = instructions.items[i];
        if (janet_checktype(x, JANET_KEYWORD)) continue;
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
        JanetSysOpVariant variant = op_variants[opcode];
        JanetSysInstruction instruction;
        instruction.opcode = opcode;
        if (inside_call) {
            if (opcode == JANET_SYSOP_CALL) {
                inside_call = 0;
            } else if (opcode != JANET_SYSOP_PUSH1 &&
                       opcode != JANET_SYSOP_PUSH2 &&
                       opcode != JANET_SYSOP_PUSH3) {
                janet_panicf("push instructions may only be followed by other push instructions until a call, got %v",
                             x);
            }
        }
        switch (variant) {
            case JANET_SYSOPVAR_THREE:
                instr_assert_length(tuple, 4, opvalue);
                instruction.three.dest = instr_read_operand(tuple[1], max_op);
                instruction.three.lhs = instr_read_operand(tuple[2], max_op);
                instruction.three.rhs = instr_read_operand(tuple[3], max_op);
                break;
            case JANET_SYSOPVAR_CALL:
            /* TODO - fallthrough for now */
            case JANET_SYSOPVAR_TWO:
                instr_assert_length(tuple, 3, opvalue);
                instruction.two.dest = instr_read_operand(tuple[1], max_op);
                instruction.two.src = instr_read_operand(tuple[2], max_op);
                break;
            case JANET_SYSOPVAR_ONE:
                instr_assert_length(tuple, 2, opvalue);
                instruction.one.src = instr_read_operand(tuple[1], max_op);
                break;
            case JANET_SYSOPVAR_BRANCH:
                instr_assert_length(tuple, 3, opvalue);
                instruction.branch.cond = instr_read_operand(tuple[1], max_op);
                instruction.branch.to = instr_read_label(tuple[2], labels, max_label);
                break;
            case JANET_SYSOPVAR_JUMP:
                instr_assert_length(tuple, 2, opvalue);
                instruction.jump.to = instr_read_label(tuple[1], labels, max_label);
                break;
            case JANET_SYSOPVAR_CONSTANT: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.constant.dest = instr_read_operand(tuple[1], max_op);
                Janet c = tuple[2];
                Janet check = janet_table_get(constant_cache, c);
                if (janet_checktype(check, JANET_NUMBER)) {
                    instruction.constant.constant = (uint32_t) janet_unwrap_number(check);
                } else {
                    instruction.constant.constant = next_constant;
                    janet_table_put(constant_cache, c, janet_wrap_integer(next_constant));
                    next_constant++;
                }
                break;
            }
        }
        check_instruction_well_formed(instruction, x, out);
        instruction.line = line;
        instruction.column = column;
        ir[cursor++] = instruction;
    }
    /* Check last instruction is jump or return */
    if ((ir[cursor - 1].opcode != JANET_SYSOP_JUMP) && (ir[cursor - 1].opcode != JANET_SYSOP_RETURN)) {
        janet_panicf("last instruction must be jump or return, got %v", x);
    }

    /* Detect return type */
    int found_return = 0;
    for (uint32_t i = 0; i < pending_count; i++) {
        JanetSysInstruction instruction = ir[i];
        if (instruction.opcode == JANET_SYSOP_RETURN) {
            JanetPrim ret_type = out->types[instruction.one.src];
            if (found_return) {
                if (out->return_type != ret_type) {
                    janet_panicf("multiple return types is not allowed: %s and %s", prim_names_by_id[ret_type], prim_names_by_id[out->return_type]);
                }
            } else {
                out->return_type = ret_type;
            }
            found_return = 1;
        }
    }

    ir = janet_realloc(ir, sizeof(JanetSysInstruction) * pending_count);
    out->instructions = ir;
    out->instruction_count = pending_count;

    /* Build constants */
    out->constant_count = next_constant;
    out->constants = janet_malloc(sizeof(Janet) * out->constant_count);
    for (int32_t i = 0; i < constant_cache->capacity; i++) {
        JanetKV kv = constant_cache->data[i];
        if (!janet_checktype(kv.key, JANET_NIL)) {
            int32_t index = janet_unwrap_integer(kv.value);
            out->constants[index] = kv.key;
        }
    }

    /* TODO - check if constants are valid since they aren't convered in check_instruction_well_formed */
}

void janet_sys_ir_init_from_table(JanetSysIR *ir, JanetTable *table) {
    ir->instructions = NULL;
    ir->types = NULL;
    ir->constants = NULL;
    ir->link_name = NULL;
    ir->type_count = 0;
    ir->constant_count = 0;
    ir->return_type = JANET_PRIM_S32;
    ir->parameter_count = 0;
    Janet assembly = janet_table_get(table, janet_ckeywordv("instructions"));
    Janet types = janet_table_get(table, janet_ckeywordv("types"));
    Janet param_count = janet_table_get(table, janet_ckeywordv("parameter-count"));
    Janet link_namev = janet_table_get(table, janet_ckeywordv("link-name"));
    JanetView asm_view = janet_getindexed(&assembly, 0);
    JanetView type_view = janet_getindexed(&types, 0);
    JanetString link_name = janet_getstring(&link_namev, 0);
    int32_t parameter_count = janet_getnat(&param_count, 0);
    ir->parameter_count = parameter_count;
    ir->link_name = link_name;
    janet_sysir_init_types(ir, type_view);
    janet_sysir_init_instructions(ir, asm_view);
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
    "char *",
    "bool"
};

void janet_sys_ir_lower_to_c(JanetSysIR *ir, JanetBuffer *buffer) {

#define EMITBINOP(OP) \
    janet_formatb(buffer, "_r%u = _r%u " OP " _r%u;\n", instruction.three.dest, instruction.three.lhs, instruction.three.rhs)

    janet_formatb(buffer, "%s %s(", c_prim_names[ir->return_type], (ir->link_name != NULL) ? ir->link_name : janet_cstring("_thunk"));
    for (uint32_t i = 0; i < ir->parameter_count; i++) {
        if (i) janet_buffer_push_cstring(buffer, ", ");
        janet_formatb(buffer, "%s _r%u", c_prim_names[ir->types[i]], i);
    }
    janet_buffer_push_cstring(buffer, ")\n{\n");
    for (uint32_t i = ir->parameter_count; i < ir->type_count; i++) {
        janet_formatb(buffer, "  %s _r%u;\n", c_prim_names[ir->types[i]], i);
    }
    janet_buffer_push_cstring(buffer, "\n");
    JanetBuffer *call_buffer = janet_buffer(0);
    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        janet_formatb(buffer, "_i%u:\n  ", i);
        JanetSysInstruction instruction = ir->instructions[i];
        if (instruction.line > 0) {
            janet_formatb(buffer, "#line %d\n  ", instruction.line);
        }
        switch (instruction.opcode) {
            case JANET_SYSOP_CONSTANT: {
                const char *cast = c_prim_names[ir->types[instruction.two.dest]];
                janet_formatb(buffer, "_r%u = (%s) %j;\n", instruction.two.dest, cast, ir->constants[instruction.two.src]);
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
            case JANET_SYSOP_PUSH1:
                janet_formatb(call_buffer, "%s_r%u", call_buffer->count ? ", " : "", instruction.one.src);
                janet_buffer_push_cstring(buffer, "/* push1 */\n");
                break;
            case JANET_SYSOP_PUSH2:
                janet_formatb(call_buffer, "%s_r%u, _r%u", call_buffer->count ? ", " : "", instruction.two.dest, instruction.two.src);
                janet_buffer_push_cstring(buffer, "/* push2 */\n");
                break;
            case JANET_SYSOP_PUSH3:
                janet_formatb(call_buffer, "%s_r%u, _r%u, _r%u", call_buffer->count ? ", " : "", instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                janet_buffer_push_cstring(buffer, "/* push3 */\n");
                break;
            case JANET_SYSOP_CALL:
                janet_formatb(buffer, "_r%u = _r%u(%s);\n", instruction.call.dest, instruction.call.callee, call_buffer->data);
                call_buffer->count = 0;
                break;
            case JANET_SYSOP_MOVE:
                janet_formatb(buffer, "_r%u = _r%u;\n", instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_BNOT:
                janet_formatb(buffer, "_r%u = ~_r%u;\n", instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_LOAD:
                janet_formatb(buffer, "_r%u = *((%s *) _r%u)", instruction.two.dest, c_prim_names[ir->types[instruction.two.dest]], instruction.two.src);
                break;
            case JANET_SYSOP_STORE:
                janet_formatb(buffer, "*((%s *) _r%u) = _r%u", c_prim_names[ir->types[instruction.two.src]], instruction.two.dest, instruction.two.src);
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
    return 0;
}

static int sysir_gcmark(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
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

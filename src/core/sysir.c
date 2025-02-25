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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "sysir.h"
#include "util.h"
#include "vector.h"
#endif

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
    {"void", JANET_PRIM_VOID},
};

const char *prim_to_prim_name[] = {
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
    "struct",
    "union",
    "array",
    "void",
    "unknown"
};

/* Map sysops to names */
const char *janet_sysop_names[] = {
    "link-name", /* JANET_SYSOP_LINK_NAME */
    "parameter-count", /* JANET_SYSOP_PARAMETER_COUNT */
    "calling-convention", /* JANET_SYSOP_CALLING_CONVENTION */
    "move", /* JANET_SYSOP_MOVE */
    "cast", /* JANET_SYSOP_CAST */
    "add", /* JANET_SYSOP_ADD */
    "subtract", /* JANET_SYSOP_SUBTRACT */
    "multiply", /* JANET_SYSOP_MULTIPLY */
    "divide", /* JANET_SYSOP_DIVIDE */
    "band", /* JANET_SYSOP_BAND */
    "bor", /* JANET_SYSOP_BOR */
    "bxor", /* JANET_SYSOP_BXOR */
    "bnot", /* JANET_SYSOP_BNOT */
    "shl", /* JANET_SYSOP_SHL */
    "shr", /* JANET_SYSOP_SHR */
    "load", /* JANET_SYSOP_LOAD */
    "store", /* JANET_SYSOP_STORE */
    "gt", /* JANET_SYSOP_GT */
    "lt", /* JANET_SYSOP_LT */
    "eq", /* JANET_SYSOP_EQ */
    "neq", /* JANET_SYSOP_NEQ */
    "gte", /* JANET_SYSOP_GTE */
    "lte", /* JANET_SYSOP_LTE */
    "call", /* JANET_SYSOP_CALL */
    "syscall", /* JANET_SYSOP_SYSCALL */
    "return", /* JANET_SYSOP_RETURN */
    "jump", /* JANET_SYSOP_JUMP */
    "branch", /* JANET_SYSOP_BRANCH */
    "branch-not", /* JANET_SYSOP_BRANCH_NOT */
    "address", /* JANET_SYSOP_ADDRESS */
    "type-prim", /* JANET_SYSOP_TYPE_PRIMITIVE */
    "type-struct", /* JANET_SYSOP_TYPE_STRUCT */
    "type-bind", /* JANET_SYSOP_TYPE_BIND */
    "arg", /* JANET_SYSOP_ARG */
    "fgetp", /* JANET_SYSOP_FIELD_GETP */
    "agetp", /* JANET_SYSOP_ARRAY_GETP */
    "apgetp", /* JANET_SYSOP_ARRAY_PGETP */
    "type-pointer", /* JANET_SYSOP_TYPE_POINTER */
    "type-array", /* JANET_SYSOP_TYPE_ARRAY */
    "type-union", /* JANET_SYSOP_TYPE_UNION */
    "pointer-add", /* JANET_SYSOP_POINTER_ADD */
    "pointer-subtract", /* JANET_SYSOP_POINTER_SUBTRACT */
    "label", /* JANET_SYSOP_LABEL */
};

static const char *calling_convention_names[] = {
    "default", /* JANET_SYS_CC_DEFAULT */
    "syscall", /* JANET_SYS_CC_SYSCALL */
    "cdecl", /* JANET_SYS_CC_X86_CDECL */
    "stdcall", /* JANET_SYS_CC_X86_STDCALL */
    "fastcall", /* JANET_SYS_CC_X86_FASTCALL */
    "sysv", /* JANET_SYS_CC_X64_SYSV */
    "sysv-syscall", /* JANET_SYS_CC_X64_SYSV_SYSCALL */
    "windows-x64", /* JANET_SYS_CC_X64_WINDOWS */
};

typedef struct {
    const char *name;
    JanetSysCallingConvention cc;
} JanetSysCCName;

static JanetSysCCName sys_calling_convention_names[] = {
    {"cdecl", JANET_SYS_CC_X86_CDECL},
    {"default", JANET_SYS_CC_DEFAULT},
    {"fastcall", JANET_SYS_CC_X86_FASTCALL},
    {"stdcall", JANET_SYS_CC_X86_STDCALL},
    {"syscall", JANET_SYS_CC_SYSCALL},
    {"sysv", JANET_SYS_CC_X64_SYSV},
    {"sysv-syscall", JANET_SYS_CC_X64_SYSV_SYSCALL},
    {"windows-x64", JANET_SYS_CC_X64_WINDOWS},
};

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
    {"branch-not", JANET_SYSOP_BRANCH_NOT},
    {"bxor", JANET_SYSOP_BXOR},
    {"call", JANET_SYSOP_CALL},
    {"calling-convention", JANET_SYSOP_CALLING_CONVENTION},
    {"cast", JANET_SYSOP_CAST},
    {"divide", JANET_SYSOP_DIVIDE},
    {"eq", JANET_SYSOP_EQ},
    {"fgetp", JANET_SYSOP_FIELD_GETP},
    {"gt", JANET_SYSOP_GT},
    {"gte", JANET_SYSOP_GTE},
    {"jump", JANET_SYSOP_JUMP},
    {"label", JANET_SYSOP_LABEL},
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
    {"syscall", JANET_SYSOP_SYSCALL},
    {"type-array", JANET_SYSOP_TYPE_ARRAY},
    {"type-pointer", JANET_SYSOP_TYPE_POINTER},
    {"type-prim", JANET_SYSOP_TYPE_PRIMITIVE},
    {"type-struct", JANET_SYSOP_TYPE_STRUCT},
    {"type-union", JANET_SYSOP_TYPE_UNION},
};

/* Utilities */

static JanetString *table_to_string_array(JanetTable *strings_to_indices, int32_t count) {
    if (0 == count) {
        return NULL;
    }
    janet_assert(count > 0, "bad count");
    JanetString *strings = NULL;
    for (int32_t i = 0; i < count; i++) {
        janet_v_push(strings, NULL);
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
        janet_panicf("expected instruction of length %d, got %p", len, x);
    }
}

static void instr_assert_min_length(JanetTuple tup, int32_t minlen, Janet x) {
    if (janet_tuple_length(tup) < minlen) {
        janet_panicf("expected instruction of at least length %d, got %p", minlen, x);
    }
}

static void instr_assert_max_length(JanetTuple tup, int32_t maxlen, Janet x) {
    if (janet_tuple_length(tup) > maxlen) {
        janet_panicf("expected instruction of at most length %d, got %p", maxlen, x);
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
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %p", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= ir->register_count) {
        ir->register_count = operand + 1;
    }
    return operand;
}

static uint32_t instr_read_field(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint(x)) janet_panicf("expected non-negative field index, got %p", x);
    (void) ir; /* Perhaps support syntax for named fields instead of numbered */
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    return operand;
}

static uint64_t instr_read_u64(Janet x, JanetSysIR *ir) {
    if (!janet_checkuint64(x)) janet_panicf("expected unsigned 64 bit integer, got %p", x);
    (void) ir;
    return janet_getuinteger64(&x, 0);
}

typedef enum {
    READ_TYPE_REFERENCE,
    READ_TYPE_DEFINITION,
    READ_TYPE_FORWARD_REF,
} ReadOpMode;

static uint32_t instr_read_type_operand(Janet x, JanetSysIR *ir, ReadOpMode rmode) {
    JanetSysIRLinkage *linkage = ir->linkage;
    if (janet_checktype(x, JANET_SYMBOL)) {
        Janet check = janet_table_get(linkage->type_name_lookup, x);
        if (janet_checktype(check, JANET_NUMBER)) {
            double n = janet_unwrap_number(check);
            if (n < 0) {
                if (rmode == READ_TYPE_DEFINITION) {
                    janet_table_put(linkage->type_name_lookup, x, janet_wrap_number(-n - 1));
                }
                return (uint32_t)(-n - 1);
            }
            if (rmode == READ_TYPE_DEFINITION) {
                janet_panicf("cannot redefine type %p", x);
            }
            return (uint32_t) n;
        } else if (rmode == READ_TYPE_FORWARD_REF) {
            uint32_t operand = linkage->type_def_count++;
            janet_table_put(linkage->type_name_lookup, x, janet_wrap_number(-(double)operand - 1.0));
            return operand;
        } else if (rmode == READ_TYPE_DEFINITION) {
            uint32_t operand = linkage->type_def_count++;
            janet_table_put(linkage->type_name_lookup, x, janet_wrap_number(operand));
            return operand;
        } else {
            janet_panicf("unknown type %p", x);
        }
    }
    if (!janet_checkuint(x)) janet_panicf("expected non-negative integer operand, got %p", x);
    uint32_t operand = (uint32_t) janet_unwrap_number(x);
    if (operand >= linkage->type_def_count) {
        linkage->type_def_count = operand + 1;
    }
    return operand;
}

static uint32_t janet_sys_makeconst(JanetSysIR *sysir, uint32_t type, Janet x) {
    JanetSysConstant jsc;
    jsc.type = type;
    jsc.value = x;
    for (int32_t i = 0; i < janet_v_count(sysir->constants); i++) {
        if (sysir->constants[i].type != jsc.type) continue;
        if (!janet_equals(sysir->constants[i].value, x)) continue;
        /* Found a constant */
        return JANET_SYS_CONSTANT_PREFIX + i;
    }
    uint32_t index = (uint32_t) janet_v_count(sysir->constants);
    janet_v_push(sysir->constants, jsc);
    sysir->constant_count++;
    return JANET_SYS_CONSTANT_PREFIX + index;
}

static uint32_t instr_read_operand_or_const(Janet x, JanetSysIR *ir) {
    if (janet_checktype(x, JANET_TUPLE)) {
        const Janet *tup = janet_unwrap_tuple(x);
        if (janet_tuple_length(tup) != 2) janet_panicf("expected constant wrapped in tuple, got %p", x);
        Janet c = tup[1];
        uint32_t t = instr_read_type_operand(tup[0], ir, READ_TYPE_REFERENCE);
        return janet_sys_makeconst(ir, t, c);
    }
    return instr_read_operand(x, ir);
}

static JanetPrim instr_read_prim(Janet x) {
    if (!janet_checktype(x, JANET_SYMBOL)) {
        janet_panicf("expected primitive type, got %p", x);
    }
    JanetSymbol sym_type = janet_unwrap_symbol(x);
    const JanetPrimName *namedata = janet_strbinsearch(prim_names,
                                    sizeof(prim_names) / sizeof(prim_names[0]), sizeof(prim_names[0]), sym_type);
    if (NULL == namedata) {
        janet_panicf("unknown primitive type %p", x);
    }
    return namedata->prim;
}

static JanetSysCallingConvention instr_read_cc(Janet x) {
    if (!janet_checktype(x, JANET_KEYWORD)) {
        janet_panicf("expected calling convention keyword, got %p", x);
    }
    JanetKeyword cc_name = janet_unwrap_keyword(x);
    const JanetSysCCName *namedata = janet_strbinsearch(sys_calling_convention_names,
                                     sizeof(sys_calling_convention_names) / sizeof(sys_calling_convention_names[0]),
                                     sizeof(sys_calling_convention_names[0]), cc_name);
    if (NULL == namedata) {
        janet_panicf("unknown calling convention %p", x);
    }
    return namedata->cc;
}

static uint32_t instr_read_label(JanetSysIR *sysir, Janet x) {
    uint32_t ret = 0;
    Janet check = janet_table_get(sysir->labels, x);
    if (janet_checktype(check, JANET_NIL)) {
        ret = sysir->label_count++;
        janet_table_put(sysir->labels, x, janet_wrap_number(ret));
    } else {
        ret = (uint32_t) janet_unwrap_number(check);
    }
    return ret;
}

static void janet_sysir_init_instructions(JanetSysIR *out, JanetView instructions) {

    JanetSysInstruction *ir = NULL;
    JanetTable *labels = out->labels;
    int found_parameter_count = 0;
    int found_calling_convention = 0;

    /* Parse instructions */
    Janet x = janet_wrap_nil();
    for (int32_t i = 0; i < instructions.len; i++) {
        x = instructions.items[i];
        /* Shorthand for labels */
        if (janet_checktype(x, JANET_KEYWORD)) {
            /* Check for existing label */
            JanetSysInstruction instruction;
            instruction.opcode = JANET_SYSOP_LABEL;
            instruction.line = -1;
            instruction.column = -1;
            instruction.label.id = instr_read_label(out, x);
            Janet label_id = janet_wrap_number(instruction.label.id);
            Janet check_defined = janet_table_get(labels, label_id);
            if (janet_checktype(check_defined, JANET_NIL)) {
                janet_table_put(labels, label_id, x);
            } else {
                janet_panicf("label %v already defined", x);
            }
            janet_v_push(ir, instruction);
            continue;
        }
        if (!janet_checktype(x, JANET_TUPLE)) {
            janet_panicf("expected instruction to be tuple, got %p", x);
        }
        JanetTuple tuple = janet_unwrap_tuple(x);
        if (janet_tuple_length(tuple) < 1) {
            janet_panic("invalid instruction, no opcode");
        }
        int32_t line = janet_tuple_sm_line(tuple);
        int32_t column = janet_tuple_sm_column(tuple);
        Janet opvalue = tuple[0];
        if (!janet_checktype(opvalue, JANET_SYMBOL)) {
            janet_panicf("expected opcode symbol, found %p", opvalue);
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
            case JANET_SYSOP_CALLING_CONVENTION:
                instr_assert_length(tuple, 2, opvalue);
                if (found_calling_convention) {
                    janet_panic("duplicate calling-convention");
                }
                found_calling_convention = 1;
                out->calling_convention = instr_read_cc(tuple[1]);
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
            case JANET_SYSOP_POINTER_ADD:
            case JANET_SYSOP_POINTER_SUBTRACT:
                instr_assert_length(tuple, 4, opvalue);
                instruction.three.dest = instr_read_operand(tuple[1], out);
                instruction.three.lhs = instr_read_operand_or_const(tuple[2], out);
                instruction.three.rhs = instr_read_operand_or_const(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_ARRAY_GETP:
            case JANET_SYSOP_ARRAY_PGETP:
                instr_assert_length(tuple, 4, opvalue);
                instruction.three.dest = instr_read_operand(tuple[1], out);
                instruction.three.lhs = instr_read_operand(tuple[2], out);
                instruction.three.rhs = instr_read_operand_or_const(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_CALL:
            case JANET_SYSOP_SYSCALL:
                instr_assert_min_length(tuple, 4, opvalue);
                instruction.call.calling_convention = instr_read_cc(tuple[1]);
                instruction.call.flags = 0;
                if (janet_checktype(tuple[2], JANET_NIL)) {
                    instruction.call.dest = 0;
                } else {
                    instruction.call.dest = instr_read_operand(tuple[2], out);
                    instruction.call.flags |= JANET_SYS_CALLFLAG_HAS_DEST;
                }
                instruction.call.arg_count = janet_tuple_length(tuple) - 4;
                instruction.call.callee = instr_read_operand_or_const(tuple[3], out);
                janet_v_push(ir, instruction);
                for (int32_t j = 4; j < janet_tuple_length(tuple); j += 3) {
                    JanetSysInstruction arginstr;
                    arginstr.opcode = JANET_SYSOP_ARG;
                    arginstr.line = line;
                    arginstr.column = column;
                    arginstr.arg.args[0] = 0xcafe;
                    arginstr.arg.args[1] = 0xcafe;
                    arginstr.arg.args[2] = 0xcafe;
                    int32_t remaining = janet_tuple_length(tuple) - j;
                    if (remaining > 3) remaining = 3;
                    for (int32_t k = 0; k < remaining; k++) {
                        arginstr.arg.args[k] = instr_read_operand_or_const(tuple[j + k], out);
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
                instruction.two.src = instr_read_operand_or_const(tuple[2], out);
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
                instr_assert_max_length(tuple, 2, opvalue);
                if (janet_tuple_length(tuple) == 2) {
                    instruction.ret.value = instr_read_operand_or_const(tuple[1], out);
                    instruction.ret.has_value = 1;
                } else {
                    instruction.ret.has_value = 0;
                }
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_BRANCH:
            case JANET_SYSOP_BRANCH_NOT:
                instr_assert_length(tuple, 3, opvalue);
                instruction.branch.cond = instr_read_operand_or_const(tuple[1], out);
                instruction.branch.to = instr_read_label(out, tuple[2]);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_JUMP:
                instr_assert_length(tuple, 2, opvalue);
                instruction.jump.to = instr_read_label(out, tuple[1]);
                janet_v_push(ir, instruction);
                break;
            case JANET_SYSOP_TYPE_PRIMITIVE: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_prim.dest_type = instr_read_type_operand(tuple[1], out, READ_TYPE_DEFINITION);
                instruction.type_prim.prim = instr_read_prim(tuple[2]);
                if (instruction.type_prim.prim == JANET_PRIM_UNKNOWN ||
                        instruction.type_prim.prim == JANET_PRIM_STRUCT ||
                        instruction.type_prim.prim == JANET_PRIM_UNION ||
                        instruction.type_prim.prim == JANET_PRIM_POINTER ||
                        instruction.type_prim.prim == JANET_PRIM_ARRAY) {
                    janet_panic("bad primitive");
                }
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_POINTER: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.pointer.dest_type = instr_read_type_operand(tuple[1], out, READ_TYPE_DEFINITION);
                instruction.pointer.type = instr_read_type_operand(tuple[2], out, READ_TYPE_FORWARD_REF);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_ARRAY: {
                instr_assert_length(tuple, 4, opvalue);
                instruction.array.dest_type = instr_read_type_operand(tuple[1], out, READ_TYPE_DEFINITION);
                instruction.array.type = instr_read_type_operand(tuple[2], out, READ_TYPE_REFERENCE);
                instruction.array.fixed_count = instr_read_u64(tuple[3], out);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_TYPE_STRUCT:
            case JANET_SYSOP_TYPE_UNION: {
                instr_assert_min_length(tuple, 1, opvalue);
                instruction.type_types.dest_type = instr_read_type_operand(tuple[1], out, READ_TYPE_DEFINITION);
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
                        arginstr.arg.args[k] = instr_read_type_operand(tuple[j + k], out, READ_TYPE_REFERENCE);
                    }
                    janet_v_push(ir, arginstr);
                }
                break;
            }
            case JANET_SYSOP_TYPE_BIND: {
                instr_assert_length(tuple, 3, opvalue);
                instruction.type_bind.dest = instr_read_operand(tuple[1], out);
                instruction.type_bind.type = instr_read_type_operand(tuple[2], out, READ_TYPE_REFERENCE);
                janet_v_push(ir, instruction);
                break;
            }
            case JANET_SYSOP_LABEL: {
                instr_assert_length(tuple, 2, opvalue);
                Janet x = tuple[1];
                if (!janet_checktype(x, JANET_KEYWORD)) {
                    janet_panicf("expected keyword label, got %p", x);
                }
                instruction.label.id = instr_read_label(out, x);
                Janet label_id = janet_wrap_number(instruction.label.id);
                Janet check_defined = janet_table_get(labels, label_id);
                if (janet_checktype(check_defined, JANET_NIL)) {
                    janet_table_put(labels, label_id, janet_wrap_number(janet_v_count(ir)));
                } else {
                    janet_panicf("label %v already defined", x);
                }
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
        janet_panicf("last instruction must be jump or return, got %p", x);
    }

    /* Check for valid number of function parameters */
    if (out->parameter_count > out->register_count) {
        janet_panicf("too many parameters, only %u registers for %u parameters.",
                     out->register_count, out->parameter_count);
    }

    /* Build constants */
    out->constant_count = janet_v_count(out->constants);
}

/* Get a type index given an operand */
uint32_t janet_sys_optype(JanetSysIR *ir, uint32_t op) {
    if (op <= JANET_SYS_MAX_OPERAND) {
        return ir->types[op];
    } else {
        if (op - JANET_SYS_CONSTANT_PREFIX >= ir->constant_count) {
            janet_panic("invalid constant");
        }
        return ir->constants[op - JANET_SYS_CONSTANT_PREFIX].type;
    }
}

uint32_t *janet_sys_callargs(JanetSysInstruction *instr, uint32_t *count) {
    uint32_t arg_count = 0;
    if (instr->opcode == JANET_SYSOP_CALL) {
        arg_count = instr->call.arg_count;
    } else if (instr->opcode == JANET_SYSOP_SYSCALL) {
        arg_count = instr->call.arg_count;
    } else if (instr->opcode == JANET_SYSOP_TYPE_UNION) {
        arg_count = instr->type_types.arg_count;
    } else if (instr->opcode == JANET_SYSOP_TYPE_STRUCT) {
        arg_count = instr->type_types.arg_count;
    } else {
        janet_assert(0, "bad callargs");
    }
    uint32_t *args = janet_smalloc(sizeof(uint32_t) * arg_count);
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t instr_index = 1 + i / 3;
        uint32_t sub_index =  i % 3;
        args[i] = instr[instr_index].arg.args[sub_index];
    }
    if (count != NULL) *count = arg_count;
    return args;
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
        janet_panicf("in %p, cannot redefine type %V", ir->error_ctx, tname(ir, typeid));
    }
}

/* Build up type tables */
static void janet_sysir_init_types(JanetSysIR *ir) {
    JanetSysIRLinkage *linkage = ir->linkage;
    JanetSysTypeField *fields = NULL;
    JanetSysTypeInfo td;
    memset(&td, 0, sizeof(td));
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        janet_v_push(linkage->type_defs, td);
    }
    JanetSysTypeInfo *type_defs = linkage->type_defs;
    uint32_t field_offset = linkage->field_def_count;
    uint32_t *types = NULL;
    linkage->type_defs = type_defs;
    for (uint32_t i = 0; i < ir->register_count; i++) {
        janet_v_push(types, 0);
    }
    ir->types = types;
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
                uint32_t count = 0;
                uint32_t *args = janet_sys_callargs(ir->instructions + i, &count);
                for (uint32_t j = 0; j < count; j++) {
                    JanetSysTypeField field;
                    field.type = args[j];
                    janet_v_push(fields, field);
                }
                janet_sfree(args);
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
        safe_memcpy(linkage->field_defs + field_offset, fields, janet_v_count(fields) * sizeof(JanetSysTypeField));
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
        janet_panicf("type failure in %p, expected boolean, got %p", sysir->error_ctx, tname(sysir, t));
    }
}

static void rcheck_boolean(JanetSysIR *sysir, uint32_t reg) {
    tcheck_boolean(sysir, janet_sys_optype(sysir, reg));
}

static void tcheck_array(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (linkage->type_defs[t].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure in %p, expected array, got %p", sysir->error_ctx, tname(sysir, t));
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
        janet_panicf("type failure in %p, expected numeric type, got %p", sysir->error_ctx, tname(sysir, t1));
    }
}

static void tcheck_number_or_pointer(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim t1 = linkage->type_defs[t].prim;
    if (t1 == JANET_PRIM_BOOLEAN ||
            t1 == JANET_PRIM_UNION ||
            t1 == JANET_PRIM_STRUCT ||
            t1 == JANET_PRIM_ARRAY) {
        janet_panicf("type failure in %p, expected pointer or numeric type, got %p", sysir->error_ctx, tname(sysir, t1));
    }
}

static void rcheck_number_or_pointer(JanetSysIR *sysir, uint32_t reg) {
    tcheck_number_or_pointer(sysir, janet_sys_optype(sysir, reg));
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
        janet_panicf("type failure in %p, expected integer type, got %p", sysir->error_ctx, tname(sysir, t1));
    }
}

static void rcheck_integer(JanetSysIR *sysir, uint32_t reg) {
    tcheck_integer(sysir, janet_sys_optype(sysir, reg));
}

static void tcheck_pointer(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (linkage->type_defs[t].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure in %p, expected pointer, got %p", sysir->error_ctx, tname(sysir, t));
    }
}

static void rcheck_pointer(JanetSysIR *sysir, uint32_t reg) {
    tcheck_pointer(sysir, janet_sys_optype(sysir, reg));
}

static void rcheck_pointer_equals(JanetSysIR *sysir, uint32_t preg, uint32_t elreg) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t t1 = janet_sys_optype(sysir, preg);
    if (linkage->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure in %p, expected pointer, got %p", sysir->error_ctx, tname(sysir, t1));
    }
    uint32_t tp = linkage->type_defs[t1].pointer.type;
    uint32_t t2 = janet_sys_optype(sysir, elreg);
    if (t2 != tp) {
        janet_panicf("type failure in %p, %p is not compatible with a pointer to %p (%p)",
                     sysir->error_ctx,
                     tname(sysir, t2),
                     tname(sysir, tp),
                     tname(sysir, t1));
    }
}

static void tcheck_struct_or_union(JanetSysIR *sysir, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetPrim prim = linkage->type_defs[t].prim;
    if (prim != JANET_PRIM_STRUCT && prim != JANET_PRIM_UNION) {
        janet_panicf("type failure in %p expected struct or union, got %p", sysir->error_ctx, tname(sysir, t));
    }
}

static void rcheck_equal(JanetSysIR *sysir, uint32_t reg1, uint32_t reg2) {
    if (reg1 == reg2) return;
    uint32_t t1 = janet_sys_optype(sysir, reg1);
    uint32_t t2 = janet_sys_optype(sysir, reg2);
    if (t1 != t2) {
        janet_panicf("type failure in %p, %p does not match %p",
                     sysir->error_ctx,
                     tname(sysir, t1),
                     tname(sysir, t2));
    }
}

static int tcheck_cast(JanetSysIR *sysir, uint32_t td, uint32_t ts) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (td == ts) return 0; /* trivial case */
    JanetPrim primd = linkage->type_defs[td].prim;
    JanetPrim prims = linkage->type_defs[ts].prim;
    if (primd == prims) {
        /* Pointer casting should be stricter than in C - for now, we
         * allow casts as long as size and aligment are identical. Also
         * no casting between arrays and pointers
         *
         * TODO - check array and pointer types have same alignment

        switch (primd) {
            default:
                return -1;
            case JANET_PRIM_ARRAY:
                return tcheck_cast(sysir, linkage->type_defs[td].array.type, linkage->type_defs[ts].array.type);
            case JANET_PRIM_POINTER:
                return tcheck_cast(sysir, linkage->type_defs[td].pointer.type, linkage->type_defs[ts].pointer.type);
        }
        return 0;
        */
        if (primd == JANET_PRIM_POINTER) {
            return 0;
        }
        return -1; /* TODO */
    }
    /* Check that both src and dest are primitive numerics */
    for (int i = 0; i < 2; i++) {
        JanetPrim p = i ? prims : primd;
        switch (p) {
            default:
                break;
            case JANET_PRIM_STRUCT:
            case JANET_PRIM_UNION:
            case JANET_PRIM_UNKNOWN:
            case JANET_PRIM_ARRAY:
            case JANET_PRIM_POINTER:
            case JANET_PRIM_VOID:
                return -1;
        }
    }
    return 0;
}

static void rcheck_cast(JanetSysIR *sysir, uint32_t dest, uint32_t src) {
    (void) sysir;
    (void) dest;
    (void) src;
    uint32_t td = janet_sys_optype(sysir, dest);
    uint32_t ts = janet_sys_optype(sysir, src);
    int notok = tcheck_cast(sysir, td, ts);
    if (notok) {
        janet_panicf("type failure in %p, %p cannot be cast to %p",
                     sysir->error_ctx,
                     tname(sysir, ts),
                     tname(sysir, td));
    }
}

static void rcheck_array_getp(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    uint32_t tlhs = janet_sys_optype(sysir, lhs);
    uint32_t trhs = janet_sys_optype(sysir, rhs);
    uint32_t tdest = janet_sys_optype(sysir, dest);
    tcheck_array(sysir, tlhs);
    tcheck_integer(sysir, trhs);
    tcheck_pointer(sysir, tdest);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t dtype = linkage->type_defs[tdest].pointer.type;
    uint32_t eltype = linkage->type_defs[tlhs].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure in %p, %p does not match %p", sysir->error_ctx, tname(sysir, dtype), tname(sysir, eltype));
    }
}

static void rcheck_array_pgetp(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    uint32_t tlhs = janet_sys_optype(sysir, lhs);
    uint32_t trhs = janet_sys_optype(sysir, rhs);
    uint32_t tdest = janet_sys_optype(sysir, dest);
    tcheck_pointer(sysir, tlhs);
    tcheck_integer(sysir, trhs);
    tcheck_pointer(sysir, tdest);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t aptype = linkage->type_defs[tlhs].pointer.type;
    if (linkage->type_defs[aptype].prim != JANET_PRIM_ARRAY) {
        janet_panicf("type failure in %p, expected array type but got %p", sysir->error_ctx, tname(sysir, aptype));
    }
    uint32_t dtype = linkage->type_defs[tdest].pointer.type;
    uint32_t eltype = linkage->type_defs[aptype].array.type;
    if (dtype != eltype) {
        janet_panicf("type failure in %p, %p does not match %p", sysir->error_ctx, tname(sysir, dtype), tname(sysir, eltype));
    }
}

static void rcheck_fgetp(JanetSysIR *sysir, uint32_t dest, uint32_t st, uint32_t field) {
    uint32_t tst = janet_sys_optype(sysir, st);
    uint32_t tdest = janet_sys_optype(sysir, dest);
    tcheck_pointer(sysir, dest);
    tcheck_struct_or_union(sysir, tst);
    JanetSysIRLinkage *linkage = sysir->linkage;
    if (field >= linkage->type_defs[tst].st.field_count) {
        janet_panicf("in %p, invalid field index %u", sysir->error_ctx, field);
    }
    uint32_t field_type = linkage->type_defs[tst].st.field_start + field;
    uint32_t tfield = linkage->field_defs[field_type].type;
    uint32_t tpdest = linkage->type_defs[tdest].pointer.type;
    if (tfield != tpdest) {
        janet_panicf("in %p, field of type %p does not match %p",
                     sysir->error_ctx,
                     tname(sysir, tfield),
                     tname(sysir, tpdest));
    }
}

/* Unlike C, only allow pointer on lhs for addition and subtraction */
static void rcheck_pointer_math(JanetSysIR *sysir, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    rcheck_equal(sysir, dest, lhs);
    JanetSysIRLinkage *linkage = sysir->linkage;
    uint32_t t1 = janet_sys_optype(sysir, dest);
    if (linkage->type_defs[t1].prim != JANET_PRIM_POINTER) {
        janet_panicf("type failure in %p, expected pointer, got %p", sysir->error_ctx, tname(sysir, t1));
    }
    rcheck_integer(sysir, rhs);
}

static JanetString rname(JanetSysIR *sysir, uint32_t regid) {
    if (regid > JANET_SYS_MAX_OPERAND) {
        uint32_t id = regid - JANET_SYS_CONSTANT_PREFIX;
        return janet_formatc("constant:%u[%p]", id, sysir->constants[id]);
    }
    JanetString name = sysir->register_names[regid];
    if (NULL == name) {
        return janet_formatc("value[%u]", regid);
    }
    return name;
}

/* Check if a constant value is compatible with a type.
 * - Janet numbers are compatible with all numeric types (if in range)
 * - Booleans are compatible with booleans
 * - Signed integers are compatible with all integer types (if in range)
 * - Strings and Pointers are compatible with pointers.
 * - Tuples of the correct length are compatible with array types if all elements are compatible with the element type
 * - TODO - structs and tables */
static int check_const_valid(JanetSysIR *sysir, Janet constant, uint32_t t) {
    JanetSysIRLinkage *linkage = sysir->linkage;
    JanetSysTypeInfo *tinfo = &linkage->type_defs[t];
    JanetPrim p = tinfo->prim;
    switch (janet_type(constant)) {
        default:
            return 0;
        case JANET_TUPLE:
            {
                const Janet *elements = janet_unwrap_tuple(constant);
                int32_t len = janet_tuple_length(elements);
                if (p != JANET_PRIM_ARRAY) return 0;
                if ((uint64_t) len != tinfo->array.fixed_count) return 0;
                for (int32_t i = 0; i < len; i++) {
                    if (!check_const_valid(sysir, elements[i], tinfo->array.type)) return 0;
                }
                return 1;
            }
        case JANET_BOOLEAN:
            return p == JANET_PRIM_BOOLEAN;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_POINTER:
            return p == JANET_PRIM_POINTER;
        case JANET_NUMBER:
            {
                double x = janet_unwrap_number(constant);
                if (p == JANET_PRIM_F64) return 1;
                if (p == JANET_PRIM_F32) return 1;
                if (x != floor(x)) return 0; /* Filter out non-integers */
                if (p == JANET_PRIM_U8 && (x >= 0 && x <= UINT8_MAX)) return 1;
                if (p == JANET_PRIM_S8 && (x >= INT8_MIN && x <= INT8_MAX)) return 1;
                if (p == JANET_PRIM_U16 && (x >= 0 && x <= UINT16_MAX)) return 1;
                if (p == JANET_PRIM_S16 && (x >= INT16_MIN && x <= INT16_MAX)) return 1;
                if (p == JANET_PRIM_U32 && (x >= 0 && x <= UINT32_MAX)) return 1;
                if (p == JANET_PRIM_S32 && (x >= INT32_MIN && x <= INT32_MAX)) return 1;
                if (p == JANET_PRIM_U64 && (x >= 0 && x <= UINT64_MAX)) return 1;
                if (p == JANET_PRIM_S64 && (x >= INT64_MIN && x <= INT64_MAX)) return 1;
                return 0;
            }
        case JANET_ABSTRACT:
            {
                void *point = janet_unwrap_abstract(constant);
                const JanetAbstractType *at = janet_abstract_type(point);
                if (at == &janet_s64_type && p == JANET_PRIM_S64) return 1;
                if (at == &janet_u64_type && p == JANET_PRIM_U64) return 1;
                return 0;
            }
    }
}

/* Check if a register is const-valid. Const-valid here means
 * that, if the register is a constant, the constant literal type-matches
 * the register type. */
static void rcheck_const_valid(JanetSysIR *sysir, uint32_t r) {
    if (r >= JANET_SYS_MAX_OPERAND) {
        /* r is a constant */
        JanetSysConstant jsc = sysir->constants[r - JANET_SYS_CONSTANT_PREFIX];
        int valid = check_const_valid(sysir, jsc.value, jsc.type);
        if (!valid) {
            janet_panicf("invalid constant %v, expected value of type %p", jsc.value, tname(sysir, jsc.type));
        }
    }
}

static void janet_sysir_type_check(JanetSysIR *sysir) {

    /* Assert no unknown types */
    JanetSysIRLinkage *linkage = sysir->linkage;
    for (uint32_t i = 0; i < sysir->register_count; i++) {
        uint32_t type = sysir->types[i];
        JanetSysTypeInfo tinfo = linkage->type_defs[type];
        sysir->error_ctx = janet_wrap_number(i);
        if (tinfo.prim == JANET_PRIM_UNKNOWN) {
            janet_panicf("unable to infer type for %s (type %p), %d", rname(sysir, i), tname(sysir, type), type);
        }
    }

    int found_return = 0;
    for (uint32_t i = 0; i < sysir->instruction_count; i++) {
        JanetSysInstruction instruction = sysir->instructions[i];
        sysir->error_ctx = janet_cstringv(janet_sysop_names[instruction.opcode]);
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
            case JANET_SYSOP_CALLING_CONVENTION:
            case JANET_SYSOP_JUMP:
            case JANET_SYSOP_LABEL:
                break;
            case JANET_SYSOP_RETURN: {
                uint32_t ret_type = 0;
                if (instruction.ret.has_value) {
                    rcheck_const_valid(sysir, instruction.ret.value);
                    ret_type = janet_sys_optype(sysir, instruction.ret.value);
                }
                if (found_return) {
                    if (instruction.ret.has_value && !sysir->has_return_type) {
                        janet_panicf("in %p, void return type not compatible with non-void return type", sysir->error_ctx);
                    }
                    if ((!instruction.ret.has_value) && sysir->has_return_type) {
                        janet_panicf("in %p, void return type not compatible with non-void return type", sysir->error_ctx);
                    }
                    if (sysir->has_return_type && sysir->return_type != ret_type) {
                        janet_panicf("in %p, multiple return types are not allowed: %p and %p",
                                     sysir->error_ctx,
                                     tname(sysir, ret_type),
                                     tname(sysir, sysir->return_type));
                    }
                } else {
                    sysir->return_type = ret_type;
                }
                sysir->has_return_type = instruction.ret.has_value;
                found_return = 1;
                break;
            }
            case JANET_SYSOP_MOVE:
                rcheck_const_valid(sysir, instruction.two.src);
                rcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_CAST:
                rcheck_const_valid(sysir, instruction.two.src);
                rcheck_cast(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_POINTER_ADD:
            case JANET_SYSOP_POINTER_SUBTRACT:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                rcheck_pointer_math(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                break;
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                tcheck_number(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.dest]));
                rcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                rcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.dest]));
                rcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                rcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_BNOT:
                rcheck_const_valid(sysir, instruction.two.src);
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.two.src]));
                rcheck_equal(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                tcheck_integer(sysir, tcheck_array_element(sysir, sysir->types[instruction.three.lhs]));
                rcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                rcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                break;
            case JANET_SYSOP_LOAD:
                rcheck_const_valid(sysir, instruction.two.src);
                rcheck_pointer_equals(sysir, instruction.two.src, instruction.two.dest);
                break;
            case JANET_SYSOP_STORE:
                rcheck_const_valid(sysir, instruction.two.src);
                rcheck_pointer_equals(sysir, instruction.two.dest, instruction.two.src);
                break;
            case JANET_SYSOP_GT:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_LTE:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                /* TODO - allow arrays */
                rcheck_number_or_pointer(sysir, instruction.three.lhs);
                rcheck_equal(sysir, instruction.three.lhs, instruction.three.rhs);
                //rcheck_equal(sysir, instruction.three.dest, instruction.three.lhs);
                tcheck_boolean(sysir, sysir->types[instruction.three.dest]);
                break;
            case JANET_SYSOP_ADDRESS:
                rcheck_pointer(sysir, instruction.two.dest);
                break;
            case JANET_SYSOP_BRANCH:
            case JANET_SYSOP_BRANCH_NOT:
                rcheck_boolean(sysir, instruction.branch.cond);
                rcheck_const_valid(sysir, instruction.branch.cond);
                break;
            case JANET_SYSOP_SYSCALL:
                rcheck_integer(sysir, instruction.call.callee);
                rcheck_const_valid(sysir, instruction.call.callee);
                break;
            case JANET_SYSOP_CALL:
                rcheck_pointer(sysir, instruction.call.callee);
                rcheck_const_valid(sysir, instruction.call.callee);
                break;
            case JANET_SYSOP_ARRAY_GETP:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                rcheck_array_getp(sysir, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
                break;
            case JANET_SYSOP_ARRAY_PGETP:
                rcheck_const_valid(sysir, instruction.three.lhs);
                rcheck_const_valid(sysir, instruction.three.rhs);
                rcheck_array_pgetp(sysir, instruction.three.dest, instruction.three.rhs, instruction.three.rhs);
                break;
            case JANET_SYSOP_FIELD_GETP:
                rcheck_fgetp(sysir, instruction.field.r, instruction.field.st, instruction.field.field);
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
    ir.error_ctx = janet_wrap_nil();
    ir.calling_convention = JANET_SYS_CC_DEFAULT;

    janet_sysir_init_instructions(&ir, instructions);

    /* Patch up name mapping arrays */
    /* TODO - make more efficient, don't rebuild from scratch every time */
    if (linkage->type_names) janet_v_free((void *) linkage->type_names);
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

/*
 * Passes
 */

static JanetSysInstruction makethree(JanetSysInstruction source, JanetSysOp opcode, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    source.opcode = opcode;
    source.three.dest = dest;
    source.three.lhs = lhs;
    source.three.rhs = rhs;
    return source;
}

static JanetSysInstruction maketwo(JanetSysInstruction source, JanetSysOp opcode, uint32_t dest, uint32_t src) {
    source.opcode = opcode;
    source.two.dest = dest;
    source.two.src = src;
    return source;
}

static JanetSysInstruction makejmp(JanetSysInstruction source, JanetSysOp opcode, uint32_t to) {
    source.opcode = opcode;
    source.jump.to = to;
    return source;
}

static JanetSysInstruction makebranch(JanetSysInstruction source, JanetSysOp opcode, uint32_t cond, uint32_t labelid) {
    source.opcode = opcode;
    source.branch.cond = cond;
    source.branch.to = labelid;
    return source;
}

static JanetSysInstruction makelabel(JanetSysInstruction source, JanetSysOp opcode, uint32_t id) {
    source.opcode = opcode;
    source.label.id = id;
    return source;
}

static JanetSysInstruction makebind(JanetSysInstruction source, JanetSysOp opcode, uint32_t reg, uint32_t type) {
    source.opcode = opcode;
    source.type_bind.dest = reg;
    source.type_bind.type = type;
    return source;
}


static uint32_t janet_sysir_getreg(JanetSysIR *sysir, uint32_t type) {
    uint32_t ret = sysir->register_count++;
    janet_v_push(sysir->types, type);
    return ret;
}

/* Find primitive types in the current linkage to avoid creating tons
 * of copies of duplicate types. */
static uint32_t janet_sysir_findprim(JanetSysIRLinkage *linkage, JanetPrim prim, const char *type_name) {
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        if (linkage->type_defs[i].prim == prim) {
            return i;
        }
    }
    /* Add new type */
    JanetSysTypeInfo td;
    memset(&td, 0, sizeof(td));
    td.prim = prim;
    janet_v_push(linkage->type_defs, td);
    janet_table_put(linkage->type_name_lookup,
            janet_csymbolv(type_name),
            janet_wrap_number(linkage->type_def_count));
    janet_v_push(linkage->type_names, janet_csymbol(type_name));
    return linkage->type_def_count++;
}

/* Get a type that is a pointer to another type */
static uint32_t janet_sysir_findpointer(JanetSysIRLinkage *linkage, uint32_t to, const char *type_name) {
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        if (linkage->type_defs[i].prim == JANET_PRIM_POINTER) {
            if (linkage->type_defs[i].pointer.type == to) {
                return i;
            }
        }
    }
    /* Add new type */
    JanetSysTypeInfo td;
    memset(&td, 0, sizeof(td));
    td.prim = JANET_PRIM_POINTER;
    td.pointer.type = to;
    janet_v_push(linkage->type_defs, td);
    janet_table_put(linkage->type_name_lookup,
            janet_csymbolv(type_name),
            janet_wrap_number(linkage->type_def_count));
    janet_v_push(linkage->type_names, janet_csymbol(type_name));
    return linkage->type_def_count++;
}

/* Unwrap vectorized binops to scalars in one pass to make certain lowering easier. */
static void janet_sysir_scalarize(JanetSysIRLinkage *linkage) {
    uint32_t index_type = janet_sysir_findprim(linkage, JANET_PRIM_U32, "U32Index");
    uint32_t boolean_type = janet_sysir_findprim(linkage, JANET_PRIM_BOOLEAN, "Boolean");
    for (int32_t j = 0; j < linkage->ir_ordered->count; j++) {
        JanetSysIR *sysir = janet_unwrap_abstract(linkage->ir_ordered->data[j]);
        for (uint32_t i = 0; i < sysir->instruction_count; i++) {
            JanetSysInstruction instruction = sysir->instructions[i];
            sysir->error_ctx = janet_cstringv(janet_sysop_names[instruction.opcode]);
            switch (instruction.opcode) {
                default:
                    break;
                case JANET_SYSOP_ADD:
                case JANET_SYSOP_SUBTRACT:
                case JANET_SYSOP_MULTIPLY:
                case JANET_SYSOP_DIVIDE:
                case JANET_SYSOP_BAND:
                case JANET_SYSOP_BOR:
                case JANET_SYSOP_BXOR:
                case JANET_SYSOP_GT:
                case JANET_SYSOP_LT:
                case JANET_SYSOP_EQ:
                case JANET_SYSOP_NEQ:
                case JANET_SYSOP_GTE:
                case JANET_SYSOP_LTE:
                case JANET_SYSOP_SHL:
                case JANET_SYSOP_SHR:
                    ;
                    {
                        uint32_t dest_type = janet_sys_optype(sysir, instruction.three.dest);
                        uint32_t test_type = dest_type;
                        if (linkage->type_defs[dest_type].prim == JANET_PRIM_POINTER) {
                            test_type = linkage->type_defs[dest_type].pointer.type;
                        }
                        if (linkage->type_defs[test_type].prim != JANET_PRIM_ARRAY) {
                            break;
                        }
                        uint32_t pel_type = janet_sysir_findpointer(linkage, linkage->type_defs[test_type].array.type, "PointerTo"); // fixme - type name would need to be unique
                        uint32_t lhs_type = janet_sys_optype(sysir, instruction.three.lhs);
                        uint32_t rhs_type = janet_sys_optype(sysir, instruction.three.rhs);
                        uint32_t array_size = linkage->type_defs[dest_type].array.fixed_count;
                        uint32_t index_reg = janet_sysir_getreg(sysir, index_type);
                        uint32_t compare_reg = janet_sysir_getreg(sysir, boolean_type);
                        uint32_t temp_lhs = janet_sysir_getreg(sysir, pel_type);
                        uint32_t temp_rhs = janet_sysir_getreg(sysir, pel_type);
                        uint32_t temp_dest = janet_sysir_getreg(sysir, pel_type);
                        uint32_t loopstart_label = sysir->label_count++;
                        uint32_t loopend_label = sysir->label_count++;
                        Janet labelkw_loopstart = janet_wrap_keyword(janet_symbol_gen());
                        Janet labelkw_loopend = janet_wrap_keyword(janet_symbol_gen());
                        JanetSysOp lhs_getp = (linkage->type_defs[lhs_type].prim == JANET_PRIM_POINTER) ? JANET_SYSOP_ARRAY_PGETP : JANET_SYSOP_ARRAY_GETP;
                        JanetSysOp rhs_getp = (linkage->type_defs[rhs_type].prim == JANET_PRIM_POINTER) ? JANET_SYSOP_ARRAY_PGETP : JANET_SYSOP_ARRAY_GETP;
                        JanetSysOp dest_getp = (linkage->type_defs[dest_type].prim == JANET_PRIM_POINTER) ? JANET_SYSOP_ARRAY_PGETP : JANET_SYSOP_ARRAY_GETP;
                        JanetSysInstruction patch[] = {
                            makebind(instruction, JANET_SYSOP_TYPE_BIND, index_reg, index_type),
                            makebind(instruction, JANET_SYSOP_TYPE_BIND, temp_lhs, pel_type),
                            makebind(instruction, JANET_SYSOP_TYPE_BIND, temp_rhs, pel_type),
                            makebind(instruction, JANET_SYSOP_TYPE_BIND, temp_dest, pel_type),
                            makebind(instruction, JANET_SYSOP_TYPE_BIND, compare_reg, boolean_type),
                            maketwo(instruction, JANET_SYSOP_LOAD, index_reg, janet_sys_makeconst(sysir, index_type, janet_wrap_number(0))),
                            makelabel(instruction, JANET_SYSOP_LABEL, loopstart_label),
                            makethree(instruction, JANET_SYSOP_GTE, compare_reg, index_reg, janet_sys_makeconst(sysir, index_type, janet_wrap_number(array_size))),
                            makebranch(instruction, JANET_SYSOP_BRANCH, compare_reg, loopend_label),
                            makethree(instruction, lhs_getp, temp_lhs, instruction.three.lhs, index_reg),
                            makethree(instruction, rhs_getp, temp_rhs, instruction.three.rhs, index_reg),
                            makethree(instruction, dest_getp, temp_dest, instruction.three.dest, index_reg),
                            makethree(instruction, instruction.opcode, temp_dest, temp_lhs, temp_rhs),
                            makethree(instruction, JANET_SYSOP_ADD, index_reg, index_reg, janet_sys_makeconst(sysir, index_type, janet_wrap_number(1))),
                            makejmp(instruction, JANET_SYSOP_JUMP, loopstart_label),
                            makelabel(instruction, JANET_SYSOP_LABEL, loopend_label)
                        };
                        size_t patchcount = sizeof(patch) / sizeof(patch[0]);
                        janet_table_put(sysir->labels, labelkw_loopstart, janet_wrap_number(loopstart_label));
                        janet_table_put(sysir->labels, labelkw_loopend, janet_wrap_number(loopend_label));
                        janet_table_put(sysir->labels, janet_wrap_number(loopstart_label), janet_wrap_number(i + 1));
                        janet_table_put(sysir->labels, janet_wrap_number(loopend_label), janet_wrap_number(i + patchcount - 1));
                        size_t remaining = (sysir->instruction_count - i - 1) * sizeof(JanetSysInstruction);
                        sysir->instructions = janet_realloc(sysir->instructions, (sysir->instruction_count + patchcount - 1) * sizeof(JanetSysInstruction));
                        if (remaining) memmove(sysir->instructions + i + patchcount, sysir->instructions + i + 1, remaining);
                        safe_memcpy(sysir->instructions + i, patch, sizeof(patch));
                        i += patchcount - 2;
                        sysir->instruction_count += patchcount - 1;
                        break;
                    }
            }
        }
    }
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
    "bool",
    "!!!struct",
    "!!!union",
    "!!!array",
    "void",
    "!!!unknown"
};

/* Print a C constant */
static void print_const_c(JanetSysIR *ir, JanetBuffer *buf, Janet c, uint32_t tid) {
    /* JanetSysTypeInfo *tinfo = &ir->linkage->type_defs[tid]; */
    if (janet_checktype(c, JANET_TUPLE)) {
        const Janet *elements = janet_unwrap_tuple(c);
        janet_formatb(buf, "((_t%d){", tid);
        for (int32_t i = 0; i < janet_tuple_length(elements); i++) {
            if (i > 0) janet_formatb(buf, ", ");
            /* TODO - limit recursion? */
            uint32_t sub_type = ir->linkage->type_defs[tid].array.type;
            print_const_c(ir, buf, elements[i], sub_type);
        }
        janet_formatb(buf, "})");
    } else if (janet_checktype(c, JANET_ABSTRACT)) {
        /* Allow printing int types */
        janet_formatb(buf, "%V", c);
    } else {
        janet_formatb(buf, "%v", c);
    }
}

static void op_or_const(JanetSysIR *ir, JanetBuffer *buf, uint32_t reg) {
    if (reg < JANET_SYS_MAX_OPERAND) {
        janet_formatb(buf, "_r%u", reg);
    } else {
        uint32_t constant_id = reg - JANET_SYS_CONSTANT_PREFIX;
        uint32_t tid = ir->constants[constant_id].type;
        Janet c = ir->constants[constant_id].value;
        print_const_c(ir, buf, c, tid);
    }
}

static void emit_binop(JanetSysIR *ir, JanetBuffer *buffer, JanetBuffer *tempbuf, JanetSysInstruction instruction, const char *op, int pointer_sugar) {
    uint32_t operand_type = ir->types[instruction.three.dest];
    tempbuf->count = 0;
    uint32_t index_index = 0;
    int is_pointer = 0;
    JanetSysIRLinkage *linkage = ir->linkage;

    /* Top-level pointer semantics */
    if (pointer_sugar && janet_sys_optype(ir, instruction.three.dest) == JANET_PRIM_POINTER) {
        operand_type = linkage->type_defs[operand_type].pointer.type;
        is_pointer = 1;
    }

    /* Add nested for loops for any dimensionality of array */
    while (linkage->type_defs[operand_type].prim == JANET_PRIM_ARRAY) {
        janet_formatb(buffer, "  for (size_t _j%u = 0; _j%u < %u; _j%u++) ",
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
        janet_formatb(buffer, "  *_r%u = *_r%u %s *_r%u;\n",
                      instruction.three.dest,
                      instruction.three.lhs,
                      op,
                      instruction.three.rhs);
        janet_formatb(buffer, "  *_r%u = *", instruction.three.dest);
        op_or_const(ir, buffer, instruction.three.lhs);
        janet_formatb(buffer, " %s ", op);
        op_or_const(ir, buffer, instruction.three.rhs);
        janet_formatb(buffer, ";\n");
    } else {
        Janet index_part = janet_wrap_buffer(tempbuf);
        janet_formatb(buffer, "  _r%u%V = ", instruction.three.dest, index_part);
        op_or_const(ir, buffer, instruction.three.lhs);
        janet_formatb(buffer, "%V %s ", index_part, op);
        op_or_const(ir, buffer, instruction.three.rhs);
        janet_formatb(buffer, "%V;\n", index_part);
    }
}

void janet_sys_ir_lower_to_c(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {

    JanetBuffer *tempbuf = janet_buffer(0);

#define EMITBINOP(OP) emit_binop(ir, buffer, tempbuf, instruction, OP, 1)
#define EMITBINOP_NOSUGAR(OP) emit_binop(ir, buffer, tempbuf, instruction, OP, 0)

    /* Prelude */
    janet_formatb(buffer, "#include <stddef.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <sys/syscall.h>\n#define _t0 void\n\n");

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
        janet_formatb(buffer, "\n\n_t%u %s(", ir->return_type, (ir->link_name != NULL) ? ir->link_name : janet_cstring("_thunk"));
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
            if (instruction.line > 0) {
                janet_formatb(buffer, "#line %d\n", instruction.line);
            }
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
                case JANET_SYSOP_CALLING_CONVENTION:
                    break;
                case JANET_SYSOP_LABEL: {
                    janet_formatb(buffer, "\n_label_%u:\n", instruction.label.id);
                    break;
                }
                case JANET_SYSOP_ADDRESS:
                    janet_formatb(buffer, "  _r%u = (void *) &", instruction.two.dest);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_JUMP:
                    janet_formatb(buffer, "  goto _label_%u;\n", instruction.jump.to);
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    janet_formatb(buffer, instruction.opcode == JANET_SYSOP_BRANCH ? "  if (" : "  if (!");
                    op_or_const(ir, buffer, instruction.branch.cond);
                    janet_formatb(buffer, ") goto _label_%u;\n", instruction.branch.to);
                    break;
                case JANET_SYSOP_RETURN:
                    if (instruction.ret.has_value) {
                        janet_buffer_push_cstring(buffer, "  return ");
                        op_or_const(ir, buffer, instruction.ret.value);
                        janet_buffer_push_cstring(buffer, ";\n");
                    } else {
                        janet_buffer_push_cstring(buffer, "  return;\n");
                    }
                    break;
                case JANET_SYSOP_ADD:
                    EMITBINOP("+");
                    break;
                case JANET_SYSOP_POINTER_ADD:
                    EMITBINOP_NOSUGAR("+");
                    break;
                case JANET_SYSOP_SUBTRACT:
                    EMITBINOP("-");
                    break;
                case JANET_SYSOP_POINTER_SUBTRACT:
                    EMITBINOP_NOSUGAR("-");
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
                case JANET_SYSOP_SYSCALL:
                case JANET_SYSOP_CALL: {
                    if (instruction.call.flags & JANET_SYS_CALLFLAG_HAS_DEST) {
                        janet_formatb(buffer, "  _r%u = ", instruction.call.dest);
                    } else {
                        janet_formatb(buffer, "  ");
                    }
                    if (instruction.opcode == JANET_SYSOP_SYSCALL) {
                        janet_formatb(buffer, "syscall(");
                        op_or_const(ir, buffer, instruction.call.callee);
                    } else {
                        op_or_const(ir, buffer, instruction.call.callee);
                        janet_formatb(buffer, "(");
                    }
                    uint32_t count;
                    uint32_t *args = janet_sys_callargs(ir->instructions + i, &count);
                    for (uint32_t j = 0; j < count; j++) {
                        if (j || instruction.opcode == JANET_SYSOP_SYSCALL) janet_formatb(buffer, ", ");
                        op_or_const(ir, buffer, args[j]);
                    }
                    janet_formatb(buffer, ");\n");
                    break;
                }
                case JANET_SYSOP_CAST: {
                    uint32_t to = ir->types[instruction.two.dest];
                    janet_formatb(buffer, "  _r%u = (_t%u) ", instruction.two.dest, to);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                }
                case JANET_SYSOP_MOVE:
                    janet_formatb(buffer, "  _r%u = ", instruction.two.dest);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_BNOT:
                    janet_formatb(buffer, "  _r%u = ~", instruction.two.dest);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_LOAD:
                    janet_formatb(buffer, "  _r%u = *(", instruction.two.dest);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ");\n");
                    break;
                case JANET_SYSOP_STORE:
                    janet_formatb(buffer, "  *(_r%u) = ", instruction.two.dest);
                    op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_FIELD_GETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u._f%u);\n", instruction.field.r, instruction.field.st, instruction.field.field);
                    janet_formatb(buffer, "  _r%u = &(", instruction.field.r);
                    janet_formatb(buffer, "._f%u);\n", instruction.field.field);
                    break;
                case JANET_SYSOP_ARRAY_GETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u.els[", instruction.three.dest, instruction.three.lhs);
                    op_or_const(ir, buffer, instruction.three.rhs);
                    janet_buffer_push_cstring(buffer, "]);\n");
                    break;
                case JANET_SYSOP_ARRAY_PGETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u->els[", instruction.three.dest, instruction.three.lhs);
                    op_or_const(ir, buffer, instruction.three.rhs);
                    janet_buffer_push_cstring(buffer, "]);\n");
                    break;
            }
        }

        janet_buffer_push_cstring(buffer, "}\n");
#undef EMITBINOP
#undef EMITBINOP_NOSUGAR
    }

}

/* Convert IR linkage back to Janet ASM */

static Janet janet_sys_get_desttype(JanetSysIRLinkage *linkage, uint32_t destt) {
    if (linkage->type_names[destt] == NULL) {
        return janet_wrap_number(destt);
    } else {
        return janet_wrap_symbol(linkage->type_names[destt]);
    }
}

static Janet wrap_op(JanetSysIR *ir, uint32_t reg) {
    if (reg <= JANET_SYS_MAX_OPERAND) {
        return janet_wrap_number(reg);
    }
    Janet *tuple = janet_tuple_begin(2);
    JanetSysConstant jsc = ir->constants[reg - JANET_SYS_CONSTANT_PREFIX];
    tuple[0] = janet_sys_get_desttype(ir->linkage, jsc.type);
    tuple[1] = jsc.value;
    janet_tuple_flag(tuple) |= JANET_TUPLE_FLAG_BRACKETCTOR;
    return janet_wrap_tuple(janet_tuple_end(tuple));
}

static Janet wrap_dest(uint32_t reg) {
    return janet_wrap_number(reg);
}

void janet_sys_ir_lower_to_ir(JanetSysIRLinkage *linkage, JanetArray *into) {

    /* Emit type defs */
    JanetArray *typedefs = janet_array(0);
    for (uint32_t j = 0; j < (uint32_t) linkage->ir_ordered->count; j++) {
        JanetSysIR *ir = janet_unwrap_abstract(linkage->ir_ordered->data[j]);
        for (uint32_t i = 0; i < ir->instruction_count; i++) {
            JanetSysInstruction instruction = ir->instructions[i];
            Janet *build_tuple = NULL;
            switch (instruction.opcode) {
                default:
                    continue;
                case JANET_SYSOP_TYPE_PRIMITIVE:
                    build_tuple = janet_tuple_begin(3);
                    build_tuple[0] = janet_csymbolv("type-prim");
                    build_tuple[1] = janet_sys_get_desttype(linkage, instruction.type_prim.dest_type);
                    build_tuple[2] = janet_csymbolv(prim_to_prim_name[instruction.type_prim.prim]);
                    break;
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_UNION:
                    build_tuple = janet_tuple_begin(2 + instruction.type_types.arg_count);
                    build_tuple[0] = janet_csymbolv(instruction.opcode == JANET_SYSOP_TYPE_STRUCT ? "type-struct" : "type-union");
                    build_tuple[1] = janet_sys_get_desttype(linkage, instruction.type_types.dest_type);
                    for (uint32_t j = 0; j < instruction.type_types.arg_count; j++) {
                        uint32_t offset = j / 3 + 1;
                        uint32_t index = j % 3;
                        JanetSysInstruction arg_instruction = ir->instructions[i + offset];
                        build_tuple[j + 2] = janet_sys_get_desttype(linkage, arg_instruction.arg.args[index]);
                    }
                    break;
                case JANET_SYSOP_TYPE_POINTER:
                    build_tuple = janet_tuple_begin(3);
                    build_tuple[0] = janet_csymbolv("type-pointer");
                    build_tuple[1] = janet_sys_get_desttype(linkage, instruction.pointer.dest_type);
                    build_tuple[2] = janet_sys_get_desttype(linkage, instruction.pointer.type);
                    break;
                case JANET_SYSOP_TYPE_ARRAY:
                    build_tuple = janet_tuple_begin(4);
                    build_tuple[0] = janet_csymbolv("type-array");
                    build_tuple[1] = janet_sys_get_desttype(linkage, instruction.array.dest_type);
                    build_tuple[2] = janet_sys_get_desttype(linkage, instruction.array.type);
                    build_tuple[3] = janet_wrap_number((double) instruction.array.fixed_count);
                    break;
            }
            const Janet *tuple = janet_tuple_end(build_tuple);
            if (instruction.line > 0) {
                janet_tuple_sm_line(tuple) = instruction.line;
                janet_tuple_sm_column(tuple) = instruction.column;
            }
            janet_array_push(typedefs, janet_wrap_tuple(tuple));
        }
    }
    janet_array_push(into, janet_wrap_array(typedefs));

    for (uint32_t j = 0; j < (uint32_t) linkage->ir_ordered->count; j++) {
        JanetSysIR *ir = janet_unwrap_abstract(linkage->ir_ordered->data[j]);
        JanetArray *linkage_ir = janet_array(0);

        /* Linkage header */
        if (ir->link_name) {
            Janet *build_tuple = janet_tuple_begin(2);
            build_tuple[0] = janet_csymbolv("link-name");
            build_tuple[1] = janet_wrap_string(ir->link_name);
            janet_array_push(linkage_ir, janet_wrap_tuple(janet_tuple_end(build_tuple)));
        }
        Janet *build_tuple = janet_tuple_begin(2);
        build_tuple[0] = janet_csymbolv("parameter-count");
        build_tuple[1] = janet_wrap_number(ir->parameter_count);
        janet_array_push(linkage_ir, janet_wrap_tuple(janet_tuple_end(build_tuple)));

        /* Lift type bindings to top of IR */
        for (uint32_t i = 0; i < ir->instruction_count; i++) {
            JanetSysInstruction instruction = ir->instructions[i];
            Janet *build_tuple = NULL;
            switch (instruction.opcode) {
                default:
                    continue;
                case JANET_SYSOP_TYPE_BIND:
                    build_tuple = janet_tuple_begin(3);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = janet_wrap_number(instruction.two.dest);
                    build_tuple[2] = janet_sys_get_desttype(linkage, instruction.two.src);
                    /* TODO - use named types if possible */
                    break;
            }
            if (instruction.line > 0) {
                janet_tuple_sm_line(build_tuple) = instruction.line;
                janet_tuple_sm_column(build_tuple) = instruction.column;
            }
            const Janet *tuple = janet_tuple_end(build_tuple);
            janet_array_push(linkage_ir, janet_wrap_tuple(tuple));
        }

        /* Emit other instructions */
        for (uint32_t i = 0; i < ir->instruction_count; i++) {
            JanetSysInstruction instruction = ir->instructions[i];
            Janet *build_tuple = NULL;
            switch (instruction.opcode) {
                default:
                    continue;
                case JANET_SYSOP_MOVE:
                case JANET_SYSOP_CAST:
                case JANET_SYSOP_BNOT:
                case JANET_SYSOP_LOAD:
                case JANET_SYSOP_STORE:
                case JANET_SYSOP_ADDRESS:
                    build_tuple = janet_tuple_begin(3);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = wrap_dest(instruction.two.dest);
                    build_tuple[2] = wrap_op(ir, instruction.two.src);
                    break;
                case JANET_SYSOP_ADD:
                case JANET_SYSOP_SUBTRACT:
                case JANET_SYSOP_MULTIPLY:
                case JANET_SYSOP_DIVIDE:
                case JANET_SYSOP_BAND:
                case JANET_SYSOP_BOR:
                case JANET_SYSOP_BXOR:
                case JANET_SYSOP_GT:
                case JANET_SYSOP_GTE:
                case JANET_SYSOP_LT:
                case JANET_SYSOP_LTE:
                case JANET_SYSOP_EQ:
                case JANET_SYSOP_NEQ:
                case JANET_SYSOP_POINTER_ADD:
                case JANET_SYSOP_POINTER_SUBTRACT:
                case JANET_SYSOP_SHL:
                case JANET_SYSOP_SHR:
                case JANET_SYSOP_ARRAY_GETP:
                case JANET_SYSOP_ARRAY_PGETP:
                    build_tuple = janet_tuple_begin(4);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = wrap_dest(instruction.three.dest);
                    build_tuple[2] = wrap_op(ir, instruction.three.lhs);
                    build_tuple[3] = wrap_op(ir, instruction.three.rhs);
                    break;
                case JANET_SYSOP_LABEL:
                    build_tuple = janet_tuple_begin(2);
                    build_tuple[0] = janet_csymbolv("label");
                    build_tuple[1] = janet_table_get(ir->labels, janet_wrap_number(instruction.label.id));
                    break;
                case JANET_SYSOP_RETURN:
                    build_tuple = janet_tuple_begin(instruction.ret.has_value ? 2 : 1);
                    build_tuple[0] = janet_csymbolv("return");
                    if (instruction.ret.has_value) build_tuple[1] = wrap_op(ir, instruction.ret.value);
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    build_tuple = janet_tuple_begin(3);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = wrap_op(ir, instruction.branch.cond);
                    build_tuple[2] = janet_table_get(ir->labels, janet_wrap_number(instruction.branch.to));
                    break;
                case JANET_SYSOP_JUMP:
                    build_tuple = janet_tuple_begin(2);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = janet_table_get(ir->labels, janet_wrap_number(instruction.jump.to));
                    break;
                case JANET_SYSOP_FIELD_GETP:
                    build_tuple = janet_tuple_begin(4);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = wrap_dest(instruction.field.r);
                    build_tuple[2] = wrap_op(ir, instruction.field.st);
                    build_tuple[3] = janet_wrap_number(instruction.field.field);
                    break;
                case JANET_SYSOP_CALL:
                case JANET_SYSOP_SYSCALL:
                    build_tuple = janet_tuple_begin(4 + instruction.call.arg_count);
                    build_tuple[0] = janet_csymbolv(janet_sysop_names[instruction.opcode]);
                    build_tuple[1] = janet_ckeywordv(calling_convention_names[instruction.call.calling_convention]);
                    build_tuple[2] = (instruction.call.flags & JANET_SYS_CALLFLAG_HAS_DEST) ? wrap_dest(instruction.call.dest) : janet_wrap_nil();
                    build_tuple[3] = wrap_op(ir, instruction.call.callee);
                    uint32_t *args = janet_sys_callargs(ir->instructions + i, NULL);
                    for (uint32_t k = 0; k < instruction.call.arg_count; k++) {
                        build_tuple[4 + k] = wrap_op(ir, args[k]);
                    }
                    janet_sfree(args);
                    break;
            }
            if (instruction.line > 0) {
                janet_tuple_sm_line(build_tuple) = instruction.line;
                janet_tuple_sm_column(build_tuple) = instruction.column;
            }
            const Janet *tuple = janet_tuple_end(build_tuple);
            janet_array_push(linkage_ir, janet_wrap_tuple(tuple));
        }
        /* Can get empty linkage after lifting type defs */
        if (linkage_ir->count > 0) {
            janet_array_push(into, janet_wrap_array(linkage_ir));
        }
    }
}

/* Bindings */

static int sysir_gc(void *p, size_t s) {
    JanetSysIR *ir = (JanetSysIR *)p;
    (void) s;
    janet_v_free(ir->constants);
    janet_v_free(ir->types);
    janet_v_free(ir->register_names);
    janet_free(ir->instructions);
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
        janet_mark(ir->constants[i].value);
    }
    if (ir->link_name != NULL) {
        janet_mark(janet_wrap_string(ir->link_name));
    }
    janet_mark(janet_wrap_table(ir->labels));
    janet_mark(janet_wrap_table(ir->register_name_lookup));
    janet_mark(janet_wrap_abstract(ir->linkage));
    janet_mark(ir->error_ctx);
    return 0;
}

static int sysir_context_gc(void *p, size_t s) {
    JanetSysIRLinkage *linkage = (JanetSysIRLinkage *)p;
    (void) s;
    janet_free(linkage->field_defs);
    janet_v_free(linkage->type_defs);
    janet_v_free((void *) linkage->type_names);
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

JANET_CORE_FN(cfun_sysir_toir,
              "(sysir/to-ir context &opt array)",
              "Raise IR back to IR after running other optimizations on it. Returns an array with various linkages.") {
    janet_arity(argc, 1, 2);
    JanetSysIRLinkage *ir = janet_getabstract(argv, 0, &janet_sysir_context_type);
    JanetArray *array = janet_optarray(argv, argc, 1, 0);
    janet_sys_ir_lower_to_ir(ir, array);
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_sysir_scalarize,
              "(sysir/scalarize context)",
              "Lower all vectorized instrinsics to loops of scalar operations.") {
    janet_fixarity(argc, 1);
    JanetSysIRLinkage *ir = janet_getabstract(argv, 0, &janet_sysir_context_type);
    janet_sysir_scalarize(ir);
    return argv[0];
}

JANET_CORE_FN(cfun_sysir_tox64,
              "(sysir/to-x64 context &opt buffer target)",
              "Lower IR to x64 machine code.") {
    janet_arity(argc, 1, 3);
    JanetSysIRLinkage *ir = janet_getabstract(argv, 0, &janet_sysir_context_type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 1, 0);
    JanetSysTarget target;
    if (argc < 3 || janet_checktype(argv[2], JANET_NIL) || janet_keyeq(argv[2], "native")) {
#ifdef JANET_WINDOWS
        target = JANET_SYS_TARGET_X64_WINDOWS;
#else
        target = JANET_SYS_TARGET_X64_LINUX;
#endif
    } else if (janet_keyeq(argv[2], "windows")) {
        target = JANET_SYS_TARGET_X64_WINDOWS;
    } else if (janet_keyeq(argv[2], "linux")) {
        target = JANET_SYS_TARGET_X64_LINUX;
    } else {
        janet_panicf("unknown target %v", argv[1]);
    }
    janet_sys_ir_lower_to_x64(ir, target, buffer);
    return janet_wrap_buffer(buffer);
}

void janet_lib_sysir(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("sysir/context", cfun_sysir_context),
        JANET_CORE_REG("sysir/asm", cfun_sysir_asm),
        JANET_CORE_REG("sysir/scalarize", cfun_sysir_scalarize),
        JANET_CORE_REG("sysir/to-c", cfun_sysir_toc),
        JANET_CORE_REG("sysir/to-ir", cfun_sysir_toir),
        JANET_CORE_REG("sysir/to-x64", cfun_sysir_tox64),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
}

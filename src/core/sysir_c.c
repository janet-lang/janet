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
#include "vector.h"
#include "util.h"
#endif

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

static void c_op_or_const(JanetSysIR *ir, JanetBuffer *buf, uint32_t reg) {
    if (reg < JANET_SYS_MAX_OPERAND) {
        janet_formatb(buf, "_r%u", reg);
    } else {
        uint32_t constant_id = reg - JANET_SYS_CONSTANT_PREFIX;
        uint32_t tid = ir->constants[constant_id].type;
        Janet c = ir->constants[constant_id].value;
        print_const_c(ir, buf, c, tid);
    }
}

static void c_emit_binop(JanetSysIR *ir, JanetBuffer *buffer, JanetBuffer *tempbuf, JanetSysInstruction instruction, const char *op, int pointer_sugar) {
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
        c_op_or_const(ir, buffer, instruction.three.lhs);
        janet_formatb(buffer, " %s ", op);
        c_op_or_const(ir, buffer, instruction.three.rhs);
        janet_formatb(buffer, ";\n");
    } else {
        Janet index_part = janet_wrap_buffer(tempbuf);
        janet_formatb(buffer, "  _r%u%V = ", instruction.three.dest, index_part);
        c_op_or_const(ir, buffer, instruction.three.lhs);
        janet_formatb(buffer, "%V %s ", index_part, op);
        c_op_or_const(ir, buffer, instruction.three.rhs);
        janet_formatb(buffer, "%V;\n", index_part);
    }
}

void janet_sys_ir_lower_to_c(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {

    JanetBuffer *tempbuf = janet_buffer(0);

#define EMITBINOP(OP) c_emit_binop(ir, buffer, tempbuf, instruction, OP, 1)
#define EMITBINOP_NOSUGAR(OP) c_emit_binop(ir, buffer, tempbuf, instruction, OP, 0)

    /* Prelude */
    janet_formatb(buffer, "#include <stddef.h>\n#include <unistd.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <sys/syscall.h>\n#define _t0 void\n\n");

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
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_JUMP:
                    janet_formatb(buffer, "  goto _label_%u;\n", instruction.jump.to);
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    janet_formatb(buffer, instruction.opcode == JANET_SYSOP_BRANCH ? "  if (" : "  if (!");
                    c_op_or_const(ir, buffer, instruction.branch.cond);
                    janet_formatb(buffer, ") goto _label_%u;\n", instruction.branch.to);
                    break;
                case JANET_SYSOP_RETURN:
                    if (instruction.ret.has_value) {
                        janet_buffer_push_cstring(buffer, "  return ");
                        c_op_or_const(ir, buffer, instruction.ret.value);
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
                        c_op_or_const(ir, buffer, instruction.call.callee);
                    } else {
                        c_op_or_const(ir, buffer, instruction.call.callee);
                        janet_formatb(buffer, "(");
                    }
                    uint32_t count;
                    uint32_t *args = janet_sys_callargs(ir->instructions + i, &count);
                    for (uint32_t j = 0; j < count; j++) {
                        if (j || instruction.opcode == JANET_SYSOP_SYSCALL) janet_formatb(buffer, ", ");
                        c_op_or_const(ir, buffer, args[j]);
                    }
                    janet_formatb(buffer, ");\n");
                    break;
                }
                case JANET_SYSOP_CAST: {
                    uint32_t to = ir->types[instruction.two.dest];
                    janet_formatb(buffer, "  _r%u = (_t%u) ", instruction.two.dest, to);
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                }
                case JANET_SYSOP_MOVE:
                    janet_formatb(buffer, "  _r%u = ", instruction.two.dest);
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_BNOT:
                    janet_formatb(buffer, "  _r%u = ~", instruction.two.dest);
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_LOAD:
                    janet_formatb(buffer, "  _r%u = *(", instruction.two.dest);
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ");\n");
                    break;
                case JANET_SYSOP_STORE:
                    janet_formatb(buffer, "  *(_r%u) = ", instruction.two.dest);
                    c_op_or_const(ir, buffer, instruction.two.src);
                    janet_formatb(buffer, ";\n");
                    break;
                case JANET_SYSOP_FIELD_GETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u._f%u);\n", instruction.field.r, instruction.field.st, instruction.field.field);
                    janet_formatb(buffer, "  _r%u = &(", instruction.field.r);
                    janet_formatb(buffer, "._f%u);\n", instruction.field.field);
                    break;
                case JANET_SYSOP_ARRAY_GETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u.els[", instruction.three.dest, instruction.three.lhs);
                    c_op_or_const(ir, buffer, instruction.three.rhs);
                    janet_buffer_push_cstring(buffer, "]);\n");
                    break;
                case JANET_SYSOP_ARRAY_PGETP:
                    janet_formatb(buffer, "  _r%u = &(_r%u->els[", instruction.three.dest, instruction.three.lhs);
                    c_op_or_const(ir, buffer, instruction.three.rhs);
                    janet_buffer_push_cstring(buffer, "]);\n");
                    break;
            }
        }

        janet_buffer_push_cstring(buffer, "}\n");
#undef EMITBINOP
#undef EMITBINOP_NOSUGAR
    }

}

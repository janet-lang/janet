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

#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSI 4
#define RDI 5
#define RSP 6
#define RBP 7

static const char *register_names[] = {
    "rax", "rcx", "rdx", "rbx", "rsi", "rdi", "rsp", "rbp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char *register_names_32[] = {
    "eax", "ecx", "edx", "ebx", "esi", "edi", "esp", "ebp",
    "r8d", "r9d", "r10d", "rlld", "r12d", "r13d", "r14d", "r15d"
};

static const char *register_names_16[] = {
    "ax", "cx", "dx", "bx", "si", "di", "sp", "bp",
    "r8w", "r9w", "r10w", "rllw", "r12w", "r13w", "r14w", "r15w"
};

static const char *register_names_8[] = {
    "al", "cl", "dl", "bl", "sil", "dil", "spl", "bpl",
    "r8b", "r9b", "r10b", "rllb", "r12b", "r13b", "r14b", "r15b"
};

typedef struct {
    enum {
        JANET_SYSREG_STACK,
        JANET_SYSREG_8,
        JANET_SYSREG_16,
        JANET_SYSREG_32,
        JANET_SYSREG_64,
        JANET_SYSREG_XMM
    } kind;
    uint32_t index;
} x64Reg;

typedef struct {
    JanetSysIRLinkage *linkage;
    JanetSysIR *ir;
    JanetBuffer *buffer;
    x64Reg *regs; /* Map IR virtual registers to hardware register or stack offset */
    JanetSysTypeLayout *layouts;
    JanetSysTypeLayout *ir_layouts;
    uint32_t frame_size;
    uint32_t restore_count;
    uint32_t to_restore[128];
    JanetSysCallingConvention calling_convention;
} JanetSysx64Context;

/* Get the layout for types */
JanetSysTypeLayout get_x64layout(JanetSysTypeInfo info) {
    JanetSysTypeLayout layout;
    switch (info.prim) {
        default:
            layout.size = 1;
            layout.alignment = 1;
            break;
        case JANET_PRIM_S8:
        case JANET_PRIM_U8:
        case JANET_PRIM_BOOLEAN:
            layout.size = 1;
            layout.alignment = 1;
            break;
        case JANET_PRIM_S16:
        case JANET_PRIM_U16:
            layout.size = 2;
            layout.alignment = 2;
            break;
        case JANET_PRIM_S32:
        case JANET_PRIM_U32:
            layout.size = 4;
            layout.alignment = 4;
            break;
        case JANET_PRIM_U64:
        case JANET_PRIM_S64:
        case JANET_PRIM_POINTER:
            layout.size = 8;
            layout.alignment = 8;
            break;
        case JANET_PRIM_F32:
        case JANET_PRIM_F64:
            layout.size = 8;
            layout.alignment = 8;
            break;
    }
    return layout;
}

void assign_registers(JanetSysx64Context *ctx) {

    /* simplest register assignment algorithm - first n variables
     * get registers, rest get assigned temporary registers and spill on every use. */
    /* TODO - linear scan or graph coloring. Require calculating live ranges */
    /* TODO - avoid spills inside loops if possible */
    /* TODO - move into sysir.c and allow reuse for multiple targets */

    /* Make trivial assigments */
    uint32_t next_loc = 0;
    ctx->regs = janet_smalloc(ctx->ir->register_count * sizeof(x64Reg));
    for (uint32_t i = 0; i < ctx->ir->register_count; i++) {
        if (i < 13) { /* skip r15 so we have some temporary registers if needed */
            /* Assign to register */
            uint32_t to = i;
            if (to > 5) {
                to += 2; /* skip rsp and rbp */
            }
            ctx->regs[i].kind = JANET_SYSREG_64;
            ctx->regs[i].index = to;
        } else { // TODO - also assign stack location if src of address IR instruction
            /* Assign to stack location */
            ctx->regs[i].kind = JANET_SYSREG_STACK;
            JanetSysTypeLayout layout = ctx->ir_layouts[i];
            next_loc = (next_loc + layout.alignment - 1) / layout.alignment * layout.alignment;
            ctx->regs[i].index = next_loc;
            next_loc += layout.size;
        }
    }

    next_loc = (next_loc + 15) / 16 * 16;
    ctx->frame_size = next_loc + 16;

    /* Mark which registers need restoration before returning */
    ctx->restore_count = 0;
    unsigned char seen[16] = {0};
    unsigned char tokeep[] = {3, 6, 7, 12, 13, 14, 15};
    for (uint32_t i = 0; i < ctx->ir->register_count; i++) {
        x64Reg reg = ctx->regs[i];
        if (reg.kind == JANET_SYSREG_STACK) continue;
        for (int j = 0; j < sizeof(tokeep); j++) {
            if (!seen[j] && reg.index == tokeep[j]) {
                ctx->to_restore[ctx->restore_count++] = reg.index;
                seen[j] = 1;
            }
        }
    }
}

static int operand_isstack(JanetSysx64Context *ctx, uint32_t o) {
    if (o > JANET_SYS_MAX_OPERAND) return 0; /* constant */
    x64Reg reg = ctx->regs[o];
    return reg.kind == JANET_SYSREG_STACK;
}

static int operand_isreg(JanetSysx64Context *ctx, uint32_t o, uint32_t regindex) {
    if (o > JANET_SYS_MAX_OPERAND) return 0; /* constant */
    x64Reg reg = ctx->regs[o];
    if (reg.kind == JANET_SYSREG_STACK) return 0;
    return reg.index == regindex;
}

static void sysemit_operand(JanetSysx64Context *ctx, uint32_t o, const char *after) {
    if (o <= JANET_SYS_MAX_OPERAND) {
        /* Virtual register */
        x64Reg reg = ctx->regs[o];
        if (reg.kind == JANET_SYSREG_STACK) {
            janet_formatb(ctx->buffer, "[rbp - %u]", reg.index);
        } else if (reg.kind == JANET_SYSREG_64) {
            janet_formatb(ctx->buffer, "%s", register_names[reg.index]);
        }
    } else {
        /* Constant */
        uint32_t index = o - JANET_SYS_CONSTANT_PREFIX;
        Janet c = ctx->ir->constants[index];
        if (janet_checktype(c, JANET_STRING)) {
            janet_formatb(ctx->buffer, "[rel CONST%u]", index);
        } else {
            janet_formatb(ctx->buffer, "%V", c);
        }
    }
    janet_buffer_push_cstring(ctx->buffer, after);
}

/* A = A op B */
static void sysemit_binop(JanetSysx64Context *ctx, const char *op, uint32_t dest, uint32_t src) {
    if (operand_isstack(ctx, dest) && operand_isstack(ctx, src)) {
        /* Use a temporary register for src */
        janet_formatb(ctx->buffer, "mov r15, ");
        sysemit_operand(ctx, src, "\n");
        janet_formatb(ctx->buffer, "%s ", op);
        sysemit_operand(ctx, dest, ", r15\n");
    } else {
        janet_formatb(ctx->buffer, "%s ", op);
        sysemit_operand(ctx, dest, ", ");
        sysemit_operand(ctx, src, "\n");
    }
}

static void sysemit_mov(JanetSysx64Context *ctx, uint32_t dest, uint32_t src) {
    if (dest == src) return;
    sysemit_binop(ctx, "mov", dest, src);
}

/* Move a value to a register, and save the contents of the old register on the stack */
static void sysemit_mov_save(JanetSysx64Context *ctx, uint32_t dest_reg, uint32_t src) {
    janet_formatb(ctx->buffer, "push %s\n", register_names[dest_reg]);
    if (!operand_isreg(ctx, src, dest_reg)) {
        janet_formatb(ctx->buffer, "mov %s, ", register_names[dest_reg]);
        sysemit_operand(ctx, src, "\n");
    }
}

static void sysemit_mov_restore(JanetSysx64Context *ctx, uint32_t dest_reg) {
    janet_formatb(ctx->buffer, "pop %s\n", register_names[dest_reg]);
}

static void sysemit_threeop(JanetSysx64Context *ctx, const char *op, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    sysemit_mov(ctx, dest, lhs);
    sysemit_binop(ctx, op, dest, rhs);
}

static void sysemit_ret(JanetSysx64Context *ctx, uint32_t arg, int has_return) {
    if (has_return && !operand_isreg(ctx, arg, RAX)) {
        janet_formatb(ctx->buffer, "mov rax, ");
        sysemit_operand(ctx, arg, "\n");
    }
    janet_formatb(ctx->buffer, "add rsp, %u\n", ctx->frame_size);
    for (uint32_t k = 0; k < ctx->restore_count; k++) {
        /* Pop in reverse order */
        janet_formatb(ctx->buffer, "pop %s\n", register_names[ctx->to_restore[ctx->restore_count - k - 1]]);
    }
    janet_formatb(ctx->buffer, "leave\n");
    janet_formatb(ctx->buffer, "ret\n");
}

void janet_sys_ir_lower_to_x64(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {

    /* Partially setup context */
    JanetSysx64Context ctx;
    ctx.linkage = linkage;
    ctx.buffer = buffer;
    ctx.layouts = janet_smalloc(linkage->type_def_count * sizeof(JanetSysTypeLayout));
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        ctx.layouts[i] = get_x64layout(linkage->type_defs[i]);
    }

    /* Emit prelude */
    janet_formatb(buffer, "bits 64\ndefault rel\n\n");
    janet_formatb(buffer, "segment .text\n");

    /* Do register allocation  */
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);

        /* Setup conttext */
        ctx.ir_layouts = janet_smalloc(ir->register_count * sizeof(JanetSysTypeLayout));
        for (uint32_t i = 0; i < ir->register_count; i++) {
            ctx.ir_layouts[i] = ctx.layouts[ir->types[i]];
        }
        ctx.ir = ir;
        assign_registers(&ctx);

        /* Emit constant strings */
        for (uint32_t j = 0; j < ir->constant_count; j++) {
            if (janet_checktype(ir->constants[j], JANET_STRING)) {
                janet_formatb(buffer, "\nCONST%u: db %p\n", j, ir->constants[j]);
            }
        }

        /* Emit prelude */
        if (ir->link_name != NULL) {
            janet_formatb(buffer, "\n%s:\n", ir->link_name);
        } else {
            janet_formatb(buffer, "\n_section_%d:\n", i);
        }
        janet_formatb(buffer, "push rbp\nmov rbp, rsp\nsub rsp, %u\n", ctx.frame_size);

        for (uint32_t k = 0; k < ctx.restore_count; k++) {
            /* Pop in reverse order */
            janet_formatb(buffer, "push %s\n", register_names[ctx.to_restore[k]]);
        }

        /* Function body */
        for (uint32_t j = 0; j < ir->instruction_count; j++) {
            JanetSysInstruction instruction = ir->instructions[j];
            switch (instruction.opcode) {
                default:
                    janet_formatb(buffer, "; nyi: %s\n", janet_sysop_names[instruction.opcode]);
                    break;
                case JANET_SYSOP_TYPE_PRIMITIVE:
                case JANET_SYSOP_TYPE_UNION:
                case JANET_SYSOP_TYPE_STRUCT:
                case JANET_SYSOP_TYPE_BIND:
                case JANET_SYSOP_TYPE_ARRAY:
                case JANET_SYSOP_TYPE_POINTER:
                case JANET_SYSOP_ARG:
                    /* Non synthesized instructions */
                    break;
                case JANET_SYSOP_POINTER_ADD:
                case JANET_SYSOP_ADD:
                    sysemit_threeop(&ctx, "add", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_POINTER_SUBTRACT:
                case JANET_SYSOP_SUBTRACT:
                    sysemit_threeop(&ctx, "sub", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_MULTIPLY:
                    sysemit_threeop(&ctx, "mul", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_DIVIDE:
                    sysemit_threeop(&ctx, "div", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_BAND:
                    sysemit_threeop(&ctx, "and", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_BOR:
                    sysemit_threeop(&ctx, "or", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_BXOR:
                    sysemit_threeop(&ctx, "xor", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_SHL:
                    sysemit_threeop(&ctx, "shl", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_SHR:
                    sysemit_threeop(&ctx, "shr", instruction.three.dest,
                                    instruction.three.lhs,
                                    instruction.three.rhs);
                    break;
                case JANET_SYSOP_MOVE:
                    sysemit_mov(&ctx, instruction.two.dest, instruction.two.src);
                    break;
                case JANET_SYSOP_RETURN:
                    sysemit_ret(&ctx, instruction.ret.value, instruction.ret.has_value);
                    break;
                case JANET_SYSOP_LABEL:
                    janet_formatb(buffer, "label_%u:\n", instruction.label.id);
                    break;
                case JANET_SYSOP_EQ:
                case JANET_SYSOP_NEQ:
                case JANET_SYSOP_LT:
                case JANET_SYSOP_LTE:
                case JANET_SYSOP_GT:
                case JANET_SYSOP_GTE:
                    ;
                    JanetSysInstruction nexti = ir->instructions[j + 1];
                    /* Combine compare and branch into one instruction */
                    /* TODO - specialize when lhs or rhs is 0 */
                    /* TODO - handle floats */
                    sysemit_binop(&ctx, "cmp", instruction.three.lhs, instruction.three.rhs);
                    if ((nexti.opcode == JANET_SYSOP_BRANCH ||
                            nexti.opcode == JANET_SYSOP_BRANCH_NOT)
                            && nexti.branch.cond == instruction.three.dest) {
                        int invert = nexti.opcode == JANET_SYSOP_BRANCH_NOT;
                        if (instruction.opcode == JANET_SYSOP_EQ) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "jne" : "je", nexti.branch.to);
                        } else if (instruction.opcode == JANET_SYSOP_NEQ) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "je" : "jne", nexti.branch.to);
                        } else if (instruction.opcode == JANET_SYSOP_GT) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "jle" : "jg", nexti.branch.to);
                        } else if (instruction.opcode == JANET_SYSOP_GTE) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "jl" : "jge", nexti.branch.to);
                        } else if (instruction.opcode == JANET_SYSOP_LT) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "jge" : "jl", nexti.branch.to);
                        } else if (instruction.opcode == JANET_SYSOP_LTE) {
                            janet_formatb(buffer, "%s label_%u\n", invert ? "jg" : "jle", nexti.branch.to);
                        } else {
                            janet_panic("unreachable");
                        }
                        j++; /* Skip next branch instruction */
                        break;
                    }
                    /* Fallback to set* instructions */
                    if (instruction.opcode == JANET_SYSOP_EQ) {
                        janet_formatb(buffer, "sete ");
                    } else if (instruction.opcode == JANET_SYSOP_NEQ) {
                        janet_formatb(buffer, "setne ");
                    } else if (instruction.opcode == JANET_SYSOP_GT) {
                        janet_formatb(buffer, "setg ");
                    } else if (instruction.opcode == JANET_SYSOP_GTE) {
                        janet_formatb(buffer, "setge ");
                    } else if (instruction.opcode == JANET_SYSOP_LT) {
                        janet_formatb(buffer, "setl ");
                    } else if (instruction.opcode == JANET_SYSOP_LTE) {
                        janet_formatb(buffer, "setle ");
                    } else {
                        janet_panic("unreachable");
                    }
                    sysemit_operand(&ctx, instruction.three.dest, "\n");
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    janet_formatb(buffer, instruction.opcode == JANET_SYSOP_BRANCH ? "jnz " : "jz ");
                    sysemit_operand(&ctx, instruction.branch.cond, " ");
                    janet_formatb(buffer, "label_%u\n", instruction.branch.to);
                    break;
                case JANET_SYSOP_JUMP:
                    janet_formatb(buffer, "jmp label_%u\n", instruction.jump.to);
                    break;
                case JANET_SYSOP_CALL:
                    ;
                    /* Push first 6 arguments to particular registers */
                    JanetSysInstruction args1 = ir->instructions[j + 1];
                    JanetSysInstruction args2 = ir->instructions[j + 2];
                    if (instruction.call.arg_count >= 1) sysemit_mov_save(&ctx, RDI, args1.arg.args[0]);
                    if (instruction.call.arg_count >= 2) sysemit_mov_save(&ctx, RSI, args1.arg.args[1]);
                    if (instruction.call.arg_count >= 3) sysemit_mov_save(&ctx, RDX, args1.arg.args[2]);
                    if (instruction.call.arg_count >= 4) sysemit_mov_save(&ctx, RCX, args2.arg.args[0]);
                    if (instruction.call.arg_count >= 5) sysemit_mov_save(&ctx, 8,   args2.arg.args[1]);
                    if (instruction.call.arg_count >= 6) sysemit_mov_save(&ctx, 9,   args2.arg.args[2]);
                    /* Strange iteration is to iterate the arguments in reverse order */
                    for (int32_t argo = instruction.call.arg_count - 1; argo >= 5; argo--) {
                        int32_t offset = argo / 3;
                        int32_t x = argo % 3;
                        janet_formatb(buffer, "push ");
                        sysemit_operand(&ctx, ir->instructions[j + offset + 1].arg.args[x], "\n");
                    }
                    janet_formatb(buffer, "call ");
                    sysemit_operand(&ctx, instruction.call.callee, "\n");
                    if (instruction.call.has_dest) {
                        /* Get result from rax */
                        janet_formatb(buffer, "mov ");
                        sysemit_operand(&ctx, instruction.call.dest, ", rax\n");
                    }
                    if (instruction.call.arg_count >= 6) sysemit_mov_restore(&ctx, 9);
                    if (instruction.call.arg_count >= 5) sysemit_mov_restore(&ctx, 8);
                    if (instruction.call.arg_count >= 4) sysemit_mov_restore(&ctx, RCX);
                    if (instruction.call.arg_count >= 3) sysemit_mov_restore(&ctx, RDX);
                    if (instruction.call.arg_count >= 2) sysemit_mov_restore(&ctx, RSI);
                    if (instruction.call.arg_count >= 1) sysemit_mov_restore(&ctx, RDI);
                    break;
                    // On a comparison, if next instruction is branch that reads from dest, combine into a single op.
            }
        }
    }
}

#undef RAX
#undef RCX
#undef RDX
#undef RBX
#undef RSI
#undef RDI
#undef RSP
#undef RBP

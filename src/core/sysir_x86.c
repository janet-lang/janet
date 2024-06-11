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
            uint32_t to = i + 1; /* skip rax */
            if (i > 5) {
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
        for (unsigned int j = 0; j < sizeof(tokeep); j++) {
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
            janet_formatb(ctx->buffer, "CONST%u", index);
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

static void sysemit_movreg(JanetSysx64Context *ctx, uint32_t regdest, uint32_t src) {
    if (operand_isreg(ctx, src, regdest)) return;
    janet_formatb(ctx->buffer, "mov %s, ", register_names[regdest]);
    sysemit_operand(ctx, src, "\n");
}

static void sysemit_pushreg(JanetSysx64Context *ctx, uint32_t dest_reg) {
    janet_formatb(ctx->buffer, "push %s\n", register_names[dest_reg]);
}

/* Move a value to a register, and save the contents of the old register on the stack */
static void sysemit_mov_save(JanetSysx64Context *ctx, uint32_t dest_reg, uint32_t src) {
    sysemit_pushreg(ctx, dest_reg);
    sysemit_movreg(ctx, dest_reg, src);
}

static void sysemit_popreg(JanetSysx64Context *ctx, uint32_t dest_reg) {
    janet_formatb(ctx->buffer, "pop %s\n", register_names[dest_reg]);
}

static void sysemit_threeop(JanetSysx64Context *ctx, const char *op, uint32_t dest, uint32_t lhs, uint32_t rhs) {
    sysemit_mov(ctx, dest, lhs);
    sysemit_binop(ctx, op, dest, rhs);
}

static void sysemit_three_inst(JanetSysx64Context *ctx, const char *op, JanetSysInstruction instruction) {
    sysemit_threeop(ctx, op, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
}

static void sysemit_ret(JanetSysx64Context *ctx, uint32_t arg, int has_return) {
    if (has_return) sysemit_movreg(ctx, RAX, arg);
    janet_formatb(ctx->buffer, "add rsp, %u\n", ctx->frame_size);
    for (uint32_t k = 0; k < ctx->restore_count; k++) {
        /* Pop in reverse order */
        janet_formatb(ctx->buffer, "pop %s\n", register_names[ctx->to_restore[ctx->restore_count - k - 1]]);
    }
    janet_formatb(ctx->buffer, "leave\n");
    janet_formatb(ctx->buffer, "ret\n");
}

static int sysemit_comp(JanetSysx64Context *ctx, uint32_t index,
                        const char *branch, const char *branch_invert,
                        const char *set) {
    JanetSysInstruction instruction = ctx->ir->instructions[index];
    sysemit_binop(ctx, "cmp", instruction.three.lhs, instruction.three.rhs);
    int has_next = index < ctx->ir->instruction_count - 1;
    JanetSysInstruction nexti;
    if (has_next) nexti = ctx->ir->instructions[index + 1];
    if (has_next &&
            (nexti.opcode == JANET_SYSOP_BRANCH ||
             nexti.opcode == JANET_SYSOP_BRANCH_NOT) &&
            nexti.branch.cond == instruction.three.dest) {
        /* Combine compare and branch */
        int invert = nexti.opcode == JANET_SYSOP_BRANCH_NOT;
        janet_formatb(ctx->buffer, "%s label_%u\n", invert ? branch_invert : branch, nexti.branch.to);
        /* Skip next branch IR instruction */
        return 1;
    } else {
        /* Set register instead */
        janet_formatb(ctx->buffer, "%s ", set);
        sysemit_operand(ctx, instruction.three.dest, "\n");
        return 0;
    }
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
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        if (ir->link_name != NULL) {
            janet_formatb(buffer, "global %s\n", ir->link_name);
        }
    }
    janet_formatb(buffer, "section .text\n");

    /* Do register allocation  */
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        if (ir->link_name == NULL) {
            continue;
        }

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
                JanetString str = janet_unwrap_string(ir->constants[j]);
                janet_formatb(buffer, "\nCONST%u: db ", j);
                /* Nasm syntax */
                int in_string = 0;
                for (int32_t ci = 0; ci < janet_string_length(str); ci++) {
                    int c = str[ci];
                    if (c < 32) {
                        if (in_string) {
                            janet_formatb(buffer, "\", %d", c);
                        } else {
                            janet_formatb(buffer, ci ? ", %d" : "%d", c);
                        }
                        in_string = 0;
                    } else {
                        if (!in_string) {
                            janet_buffer_push_cstring(buffer, ci ? ", \"" : "\"");
                        }
                        janet_buffer_push_u8(buffer, c);
                        in_string = 1;
                    }
                }
                if (!in_string) {
                    janet_buffer_push_cstring(buffer, "\n");
                } else {
                    janet_buffer_push_cstring(buffer, "\"\n");
                }
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
                    sysemit_three_inst(&ctx, "add", instruction);
                    break;
                case JANET_SYSOP_POINTER_SUBTRACT:
                case JANET_SYSOP_SUBTRACT:
                    sysemit_three_inst(&ctx, "sub", instruction);
                    break;
                case JANET_SYSOP_MULTIPLY:
                    sysemit_three_inst(&ctx, "mul", instruction);
                    break;
                case JANET_SYSOP_DIVIDE:
                    sysemit_three_inst(&ctx, "div", instruction);
                    break;
                case JANET_SYSOP_BAND:
                    sysemit_three_inst(&ctx, "and", instruction);
                    break;
                case JANET_SYSOP_BOR:
                    sysemit_three_inst(&ctx, "or", instruction);
                    break;
                case JANET_SYSOP_BXOR:
                    sysemit_three_inst(&ctx, "xor", instruction);
                    break;
                case JANET_SYSOP_SHL:
                    sysemit_three_inst(&ctx, "shl", instruction);
                    break;
                case JANET_SYSOP_SHR:
                    sysemit_three_inst(&ctx, "shr", instruction);
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
                    j += sysemit_comp(&ctx, j, "je", "jne", "sete");
                    break;
                case JANET_SYSOP_NEQ:
                    j += sysemit_comp(&ctx, j, "jne", "je", "setne");
                    break;
                case JANET_SYSOP_LT:
                    j += sysemit_comp(&ctx, j, "jl", "jge", "setl");
                    break;
                case JANET_SYSOP_LTE:
                    j += sysemit_comp(&ctx, j, "jle", "jg", "setle");
                    break;
                case JANET_SYSOP_GT:
                    j += sysemit_comp(&ctx, j, "jg", "jle", "setg");
                    break;
                case JANET_SYSOP_GTE:
                    j += sysemit_comp(&ctx, j, "jge", "jl", "setge");
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
                case JANET_SYSOP_SYSCALL:
                case JANET_SYSOP_CALL:
                    ;
                    /* Push first 6 arguments to particular registers */
                    uint32_t argcount = 0;
                    uint32_t *args = janet_sys_callargs(ir->instructions + j, &argcount);
                    if (argcount >= 1) sysemit_mov_save(&ctx, RDI, args[0]);
                    if (argcount >= 2) sysemit_mov_save(&ctx, RSI, args[1]);
                    if (argcount >= 3) sysemit_mov_save(&ctx, RDX, args[2]);
                    if (argcount >= 4) sysemit_mov_save(&ctx, RCX, args[3]);
                    if (argcount >= 5) sysemit_mov_save(&ctx, 8,   args[4]);
                    if (argcount >= 6) sysemit_mov_save(&ctx, 9,   args[5]);
                    for (int32_t argo = argcount - 1; argo >= 5; argo--) {
                        janet_formatb(buffer, "push ");
                        sysemit_operand(&ctx, args[argo], "\n");
                    }
                    janet_sfree(args);
                    if (instruction.opcode == JANET_SYSOP_SYSCALL) {
                        sysemit_movreg(&ctx, RAX, instruction.call.callee);
                        janet_formatb(buffer, "syscall\n");
                    } else {
                        janet_formatb(buffer, "call ");
                        sysemit_operand(&ctx, instruction.call.callee, "\n");
                    }
                    if (instruction.call.has_dest) sysemit_movreg(&ctx, instruction.call.dest, RAX);
                    if (argcount >= 6) sysemit_popreg(&ctx, 9);
                    if (argcount >= 5) sysemit_popreg(&ctx, 8);
                    if (argcount >= 4) sysemit_popreg(&ctx, RCX);
                    if (argcount >= 3) sysemit_popreg(&ctx, RDX);
                    if (argcount >= 2) sysemit_popreg(&ctx, RSI);
                    if (argcount >= 1) sysemit_popreg(&ctx, RDI);
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

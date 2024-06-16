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
    "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};

static const char *register_names_16[] = {
    "ax", "cx", "dx", "bx", "si", "di", "sp", "bp",
    "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
};

static const char *register_names_8[] = {
    "al", "cl", "dl", "bl", "sil", "dil", "spl", "bpl",
    "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
};

static const char *register_names_xmm[] = {
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
};

typedef enum {
    JANET_SYSREG_8,
    JANET_SYSREG_16,
    JANET_SYSREG_32,
    JANET_SYSREG_64,
    JANET_SYSREG_2x64,
    JANET_SYSREG_XMM
} x64RegKind;

typedef enum {
    JANET_SYSREG_REGISTER,
    JANET_SYSREG_STACK, /* Indexed from base pointer */
    JANET_SYSREG_STACK_PARAMETER, /* Index from base pointer in other direction */
} x64Storage;

typedef struct {
    x64RegKind kind;
    x64Storage storage;
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
    int32_t ir_index;
    uint32_t occupied_registers;
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

/* Get the register type that could store an operand o. Anything that will
 * be forced to the stack will return a 64bit register. */
static x64RegKind get_slot_regkind(JanetSysx64Context *ctx, uint32_t o) {
    JanetPrim prim;
    if (o > JANET_SYS_MAX_OPERAND) {
        prim = ctx->linkage->type_defs[ctx->ir->constants[o - JANET_SYS_CONSTANT_PREFIX].type].prim;
    } else {
        prim = ctx->linkage->type_defs[ctx->ir->types[o]].prim;
    }
    if (prim == JANET_PRIM_S8 || prim == JANET_PRIM_U8) {
        return JANET_SYSREG_8;
    } else if (prim == JANET_PRIM_S16 || prim == JANET_PRIM_U16) {
        return JANET_SYSREG_16;
    } else if (prim == JANET_PRIM_S32 || prim == JANET_PRIM_U32) {
        return JANET_SYSREG_32;
    } else if (prim == JANET_PRIM_F64 || prim == JANET_PRIM_F32) {
        return JANET_SYSREG_XMM;
    } else {
        return JANET_SYSREG_64;
    }
}

void assign_registers(JanetSysx64Context *ctx) {

    /* simplest register assignment algorithm - first n variables
     * get registers, rest get assigned temporary registers and spill on every use. */
    /* TODO - linear scan or graph coloring. Require calculating live ranges */
    /* TODO - avoid spills inside loops if possible i.e. not all spills are equal */
    /* TODO - move into sysir.c and allow reuse for multiple targets */

    /* Make trivial assigments */
    uint32_t next_loc = 16;
    ctx->regs = janet_smalloc(ctx->ir->register_count * sizeof(x64Reg));
    uint32_t assigned = 0;
    uint32_t occupied = 0;
    assigned |= 1 << RSP;
    assigned |= 1 << RBP;
    assigned |= 1 << RAX; // return reg, div, etc.
    for (uint32_t i = 0; i < ctx->ir->register_count; i++) {
        ctx->regs[i].kind = get_slot_regkind(ctx, i);
        if (i < ctx->ir->parameter_count) {
            /* Assign to rdi, rsi, etc. according to ABI */
            if (i == 0) ctx->regs[i].index = RDI;
            if (i == 1) ctx->regs[i].index = RSI;
            if (i == 2) ctx->regs[i].index = RDX;
            if (i == 3) ctx->regs[i].index = RCX;
            if (i == 4) ctx->regs[i].index = 8;
            if (i == 5) ctx->regs[i].index = 9;
            if (i >= 6) {
                janet_panic("nyi");
                ctx->regs[i].storage = JANET_SYSREG_STACK_PARAMETER;
            } else {
                ctx->regs[i].storage = JANET_SYSREG_REGISTER;
                assigned |= 1 << ctx->regs[i].index;
                occupied |= 1 << ctx->regs[i].index;
            }
        } else if (assigned < 0xFFFF) {
            /* Assign to register */
            uint32_t to = 0;
            while ((1 << to) & assigned) to++;
            ctx->regs[i].index = to;
            ctx->regs[i].storage = JANET_SYSREG_REGISTER;
            assigned |= 1 << ctx->regs[i].index;
            occupied |= 1 << ctx->regs[i].index;
        } else { // TODO - also assign stack location if src of address IR instruction
            /* Assign to stack location */
            JanetSysTypeLayout layout = ctx->ir_layouts[i];
            next_loc = (next_loc + layout.alignment - 1) / layout.alignment * layout.alignment;
            ctx->regs[i].index = next_loc;
            ctx->regs[i].storage = JANET_SYSREG_STACK;
            next_loc += layout.size;
        }
    }

    next_loc = (next_loc + 15) / 16 * 16;
    ctx->frame_size = next_loc + 16;
    ctx->occupied_registers = occupied;

    /* Mark which registers need restoration before returning */
    ctx->restore_count = 0;
    unsigned char seen[16] = {0};
    unsigned char tokeep[] = {3, 6, 7, 12, 13, 14, 15};
    for (uint32_t i = 0; i < ctx->ir->register_count; i++) {
        x64Reg reg = ctx->regs[i];
        if (reg.storage != JANET_SYSREG_REGISTER) continue;
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
    return reg.storage != JANET_SYSREG_REGISTER;
}

static int operand_isreg(JanetSysx64Context *ctx, uint32_t o, uint32_t regindex) {
    if (o > JANET_SYS_MAX_OPERAND) return 0; /* constant */
    x64Reg reg = ctx->regs[o];
    if (reg.storage != JANET_SYSREG_REGISTER) return 0;
    return reg.index == regindex;
}

static void sysemit_reg(JanetSysx64Context *ctx, x64Reg reg, const char *after) {
    if (reg.storage == JANET_SYSREG_STACK) {
        // TODO - use LEA for parameters larger than a qword
        janet_formatb(ctx->buffer, "[rbp-%u]", reg.index);
    } else if (reg.storage == JANET_SYSREG_STACK_PARAMETER) {
        // TODO - use LEA for parameters larger than a qword
        janet_formatb(ctx->buffer, "[rbp+%u]", reg.index);
    } else if (reg.kind == JANET_SYSREG_64) {
        janet_formatb(ctx->buffer, "%s", register_names[reg.index]);
    } else if (reg.kind == JANET_SYSREG_32) {
        janet_formatb(ctx->buffer, "%s", register_names_32[reg.index]);
    } else if (reg.kind == JANET_SYSREG_16) {
        janet_formatb(ctx->buffer, "%s", register_names_16[reg.index]);
    } else if (reg.kind == JANET_SYSREG_8) {
        janet_formatb(ctx->buffer, "%s", register_names_8[reg.index]);
    } else {
        janet_formatb(ctx->buffer, "%s", register_names_xmm[reg.index]);
    }
    if (after) janet_buffer_push_cstring(ctx->buffer, after);
}

static void sysemit_operand(JanetSysx64Context *ctx, uint32_t o, const char *after) {
    if (o <= JANET_SYS_MAX_OPERAND) {
        sysemit_reg(ctx, ctx->regs[o], NULL);
    } else {
        /* Constant */
        uint32_t index = o - JANET_SYS_CONSTANT_PREFIX;
        Janet c = ctx->ir->constants[index].value;
        // TODO - do this properly
        if (janet_checktype(c, JANET_STRING)) {
            janet_formatb(ctx->buffer, "CONST_%d_%u", ctx->ir_index, index);
        } else {
            // TODO - do this properly too.
            // Also figure out how to load large constants to a temporary register
            // In x64, only move to register is allowed to take a 64 bit immediate, so
            // our methodology here changes based on what kind of operand we need.
            janet_formatb(ctx->buffer, "%V", c);
        }
    }
    if (after) janet_buffer_push_cstring(ctx->buffer, after);
}

/* A = A op B */
static void sysemit_binop(JanetSysx64Context *ctx, const char *op, uint32_t dest, uint32_t src) {
    if (operand_isstack(ctx, dest) && operand_isstack(ctx, src)) {
        /* Use a temporary register for src */
        x64Reg tempreg;
        tempreg.kind = get_slot_regkind(ctx, dest);
        tempreg.index = RAX;
        janet_formatb(ctx->buffer, "mov ");
        sysemit_reg(ctx, tempreg, ", ");
        sysemit_operand(ctx, src, "\n");
        janet_formatb(ctx->buffer, "%s ", op);
        sysemit_operand(ctx, dest, ", ");
        sysemit_reg(ctx, tempreg, "\n");
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
    x64Reg tempreg;
    tempreg.kind = get_slot_regkind(ctx, src);
    tempreg.index = regdest;
    janet_formatb(ctx->buffer, "mov ");
    sysemit_reg(ctx, tempreg, ", ");
    sysemit_operand(ctx, src, "\n");
}

static void sysemit_movfromreg(JanetSysx64Context *ctx, uint32_t dest, uint32_t srcreg) {
    if (operand_isreg(ctx, dest, srcreg)) return;
    x64Reg tempreg;
    tempreg.kind = get_slot_regkind(ctx, dest);
    tempreg.index = srcreg;
    janet_formatb(ctx->buffer, "mov ");
    sysemit_operand(ctx, dest, ", ");
    sysemit_reg(ctx, tempreg, "\n");
}

static void sysemit_pushreg(JanetSysx64Context *ctx, uint32_t dest_reg) {
    janet_formatb(ctx->buffer, "push %s\n", register_names[dest_reg]);
}

/* Move a value to a register, and save the contents of the old register on fhe stack */
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

static void sysemit_threeop_nodeststack(JanetSysx64Context *ctx, const char *op, uint32_t dest, uint32_t lhs,
                                        uint32_t rhs) {
    if (operand_isstack(ctx, dest)) {
        sysemit_movreg(ctx, RAX, lhs);
        x64Reg tempreg;
        tempreg.kind = get_slot_regkind(ctx, dest);
        tempreg.index = RAX;
        janet_formatb(ctx->buffer, "%s ", op);
        sysemit_reg(ctx, tempreg, ", ");
        sysemit_operand(ctx, lhs, "\n");
        sysemit_movfromreg(ctx, dest, RAX);
    } else {
        sysemit_threeop(ctx, op, dest, lhs, rhs);
    }
}

static void sysemit_three_inst(JanetSysx64Context *ctx, const char *op, JanetSysInstruction instruction) {
    sysemit_threeop(ctx, op, instruction.three.dest, instruction.three.lhs, instruction.three.rhs);
}

static void sysemit_ret(JanetSysx64Context *ctx, uint32_t arg, int has_return) {
    if (has_return) sysemit_movreg(ctx, RAX, arg);
    /* TODO - depends on current calling convention */
    for (uint32_t k = 0; k < ctx->restore_count; k++) {
        /* Pop in reverse order */
        janet_formatb(ctx->buffer, "pop %s\n", register_names[ctx->to_restore[ctx->restore_count - k - 1]]);
    }
    janet_formatb(ctx->buffer, "leave\n");
    janet_formatb(ctx->buffer, "ret\n");
}

static int sysemit_comp(JanetSysx64Context *ctx, uint32_t index,
                        const char *branch, const char *branch_invert,
                        const char *set, const char *set_invert) {
    JanetSysInstruction instruction = ctx->ir->instructions[index];
    if (instruction.three.lhs > JANET_SYS_MAX_OPERAND) {
        /* Constant cannot be first operand to cmp, switch */
        set = set_invert;
        const char *temp = branch;
        branch = branch_invert;
        branch_invert = temp;
        sysemit_binop(ctx, "cmp", instruction.three.rhs, instruction.three.lhs);
    } else {
        sysemit_binop(ctx, "cmp", instruction.three.lhs, instruction.three.rhs);
    }
    int has_next = index < ctx->ir->instruction_count - 1;
    JanetSysInstruction nexti;
    if (has_next) nexti = ctx->ir->instructions[index + 1];
    if (has_next &&
            (nexti.opcode == JANET_SYSOP_BRANCH ||
             nexti.opcode == JANET_SYSOP_BRANCH_NOT) &&
            nexti.branch.cond == instruction.three.dest) {
        /* Combine compare and branch */
        int invert = nexti.opcode == JANET_SYSOP_BRANCH_NOT;
        janet_formatb(ctx->buffer, "%s label_%d_%u\n", invert ? branch_invert : branch, ctx->ir_index, nexti.branch.to);
        /* Skip next branch IR instruction */
        return 1;
    } else {
        /* Set register instead */
        x64RegKind kind = get_slot_regkind(ctx, instruction.three.dest);
        if (kind != JANET_SYSREG_8) {
            /* Zero the upper bits */
            sysemit_binop(ctx, "xor", instruction.three.dest, instruction.three.dest);
        }
        janet_formatb(ctx->buffer, "%s %s\n", set, register_names_8[instruction.three.dest]);
        return 0;
    }
}

static void sysemit_cast(JanetSysx64Context *ctx, JanetSysInstruction instruction) {
    uint32_t dest = instruction.two.dest;
    uint32_t src = instruction.two.src;
    uint32_t dest_type = janet_sys_optype(ctx->ir, dest);
    uint32_t src_type = janet_sys_optype(ctx->ir, src);
    JanetSysTypeInfo destinfo = ctx->linkage->type_defs[dest_type];
    JanetSysTypeInfo srcinfo = ctx->linkage->type_defs[src_type];
    /* For signed -> unsigned of same size, just move */
    /* For casting larger integer to smaller, truncate by just using smaller register class w/ move */
    /* For casting smaller integer to larger, depends.
     *      for 32 -> 64, zeros/sign-extends upper bits
     *      for other sizes, upper bits unchanged, need to be zeroed or oned before hand.
     *      for floating pointer conversions, todo
     */
    (void) destinfo;
    (void) srcinfo;
    janet_formatb(ctx->buffer, "; cast nyi\n");
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
    JanetTable *seen = janet_table(0);
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        if (ir->link_name != NULL) {
            janet_table_put(seen, janet_csymbolv((const char *)ir->link_name), janet_wrap_true());
            janet_formatb(buffer, "global %s\n", ir->link_name);
        }
    }
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        for (uint32_t j = 0; j < ir->constant_count; j++) {
            Janet c = ir->constants[j].value;
            if (janet_checktype(janet_table_get(seen, c), JANET_NIL)) {
                if (janet_checktype(c, JANET_SYMBOL)) {
                    janet_formatb(buffer, "extern %V\n", c);
                    janet_table_put(seen, c, janet_wrap_true());
                }
            }
        }
    }
    janet_formatb(buffer, "section .text\n");

    /* Do register allocation  */
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        ctx.ir_index = i;
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
                    sysemit_threeop_nodeststack(&ctx, "imul", instruction.three.dest,
                                                instruction.three.lhs, instruction.three.rhs);
                    break;
                case JANET_SYSOP_DIVIDE:
                    sysemit_three_inst(&ctx, "idiv", instruction);
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
                    janet_formatb(buffer, "label_%d_%u:\n", i, instruction.label.id);
                    break;
                case JANET_SYSOP_EQ:
                    j += sysemit_comp(&ctx, j, "je", "jne", "sete", "setne");
                    break;
                case JANET_SYSOP_NEQ:
                    j += sysemit_comp(&ctx, j, "jne", "je", "setne", "sete");
                    break;
                case JANET_SYSOP_LT:
                    j += sysemit_comp(&ctx, j, "jl", "jge", "setl", "setge");
                    break;
                case JANET_SYSOP_LTE:
                    j += sysemit_comp(&ctx, j, "jle", "jg", "setle", "setg");
                    break;
                case JANET_SYSOP_GT:
                    j += sysemit_comp(&ctx, j, "jg", "jle", "setg", "setle");
                    break;
                case JANET_SYSOP_GTE:
                    j += sysemit_comp(&ctx, j, "jge", "jl", "setge", "setl");
                    break;
                case JANET_SYSOP_CAST:
                    sysemit_cast(&ctx, instruction);
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    janet_formatb(buffer, "test ");
                    // TODO - ensure branch condition is not a const
                    sysemit_operand(&ctx, instruction.branch.cond, ", 0\n");
                    janet_formatb(buffer,
                                  "%s label_%d_%u\n",
                                  instruction.opcode == JANET_SYSOP_BRANCH ? "jnz " : "jz ",
                                  i, instruction.branch.to);
                    break;
                case JANET_SYSOP_JUMP:
                    janet_formatb(buffer, "jmp label_%d_%u\n", i, instruction.jump.to);
                    break;
                case JANET_SYSOP_SYSCALL:
                case JANET_SYSOP_CALL:
                    ;
                    /* Push first 6 arguments to particular registers */
                    uint32_t argcount = 0;
                    uint32_t *args = janet_sys_callargs(ir->instructions + j, &argcount);
                    int save_rdi = argcount >= 1 || (ctx.occupied_registers & (1 << RDI));
                    int save_rsi = argcount >= 2 || (ctx.occupied_registers & (1 << RSI));
                    int save_rdx = argcount >= 3 || (ctx.occupied_registers & (1 << RDX));
                    int save_rcx = argcount >= 4 || (ctx.occupied_registers & (1 << RCX));
                    int save_r8 = argcount >= 5 || (ctx.occupied_registers & (1 << 8));
                    int save_r9 = argcount >= 6 || (ctx.occupied_registers & (1 << 9));
                    int save_r10 = ctx.occupied_registers & (1 << 10);
                    int save_r11 = ctx.occupied_registers & (1 << 11);
                    if (save_rdi && argcount >= 1) sysemit_mov_save(&ctx, RDI, args[0]);
                    if (save_rdi && argcount < 1)  sysemit_pushreg(&ctx, RDI);
                    if (save_rsi && argcount >= 2) sysemit_mov_save(&ctx, RSI, args[1]);
                    if (save_rsi && argcount < 2) sysemit_pushreg(&ctx, RSI);
                    if (save_rdx && argcount >= 3) sysemit_mov_save(&ctx, RDX, args[2]);
                    if (save_rdx && argcount < 3) sysemit_pushreg(&ctx, RDX);
                    if (save_rcx && argcount >= 4) sysemit_mov_save(&ctx, RCX, args[3]);
                    if (save_rcx && argcount < 4) sysemit_pushreg(&ctx, RCX);
                    if (save_r8 && argcount >= 5) sysemit_mov_save(&ctx, 8, args[4]);
                    if (save_r8 && argcount < 5) sysemit_pushreg(&ctx, 8);
                    if (save_r9 && argcount >= 6) sysemit_mov_save(&ctx, 9, args[5]);
                    if (save_r9 && argcount < 6) sysemit_pushreg(&ctx, 9);
                    if (save_r10) sysemit_pushreg(&ctx, 10);
                    if (save_r11) sysemit_pushreg(&ctx, 11);
                    for (int32_t argo = argcount - 1; argo >= 5; argo--) {
                        janet_panic("nyi");
                        janet_formatb(buffer, "push ");
                        sysemit_operand(&ctx, args[argo], "\n");
                    }
                    janet_sfree(args);
                    if (instruction.opcode == JANET_SYSOP_SYSCALL) {
                        sysemit_movreg(&ctx, RAX, instruction.call.callee);
                        janet_formatb(buffer, "syscall\n");
                    } else {
                        /* Save RAX to number of floating point args for varags - for now, always 0 :) */
                        janet_formatb(buffer, "mov rax, 0\n");
                        janet_formatb(buffer, "call ");
                        sysemit_operand(&ctx, instruction.call.callee, "\n");
                    }
                    if (instruction.call.flags & JANET_SYS_CALLFLAG_HAS_DEST) sysemit_movreg(&ctx, RAX, instruction.call.dest);
                    if (save_r11) sysemit_popreg(&ctx, 11);
                    if (save_r10) sysemit_popreg(&ctx, 10);
                    if (save_r9) sysemit_popreg(&ctx, 9);
                    if (save_r8) sysemit_popreg(&ctx, 8);
                    if (save_rcx) sysemit_popreg(&ctx, RCX);
                    if (save_rdx) sysemit_popreg(&ctx, RDX);
                    if (save_rsi) sysemit_popreg(&ctx, RSI);
                    if (save_rdi) sysemit_popreg(&ctx, RDI);
                    break;
                    // On a comparison, if next instruction is branch that reads from dest, combine into a single op.
            }
        }
    }
    /* End section .text */

    janet_formatb(buffer, "section .rodata\n\n");

    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        /* Emit constant strings */
        for (uint32_t j = 0; j < ir->constant_count; j++) {
            if (janet_checktype(ir->constants[j].value, JANET_STRING)) {
                JanetString str = janet_unwrap_string(ir->constants[j].value);
                janet_formatb(buffer, "\nCONST_%d_%u: db ", i, j);
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
                    janet_buffer_push_cstring(buffer, ", 0\n");
                } else {
                    janet_buffer_push_cstring(buffer, "\", 0\n");
                }
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

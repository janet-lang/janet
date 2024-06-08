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

static const char *register_names[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

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

static uint32_t v2reg_dflt(JanetTable *assignments, uint32_t var, uint32_t dflt) {
    Janet check = janet_table_get(assignments, janet_wrap_number(var));
    if (janet_checktype(check, JANET_NUMBER)) {
        return (uint32_t) janet_unwrap_number(check);
    }
    return dflt;
}

JanetSysSpill *assign_registers(JanetSysIR *ir,
        JanetSysTypeLayout *layouts,
        JanetTable *assignments,
        uint32_t max_reg) {

    /* simplest register assignment algorithm - first n variables
     * get registers, rest get assigned temporary registers and spill on every use. */
    /* TODO - linear scan or graph coloring. Require calculating live ranges */
    /* TODO - avoid spills inside loops if possible */
    /* TODO - move into sysir.c and allow reuse for multiple targets */

    /* Make trivial assigments */
    for (uint32_t i = 0; i < ir->register_count; i++) {
        if (i < max_reg) {
            janet_table_put(assignments, janet_wrap_number(i), janet_wrap_number(i));
        }
    }

    /* Assign all slots a stack location */
    /* TODO - be smarter about this */
    uint32_t *stack_locations = janet_smalloc(ir->register_count * sizeof(uint32_t));
    uint32_t next_loc = 0;
    for (uint32_t i = 0; i < ir->register_count; i++) {
        JanetSysTypeLayout layout = layouts[i];
        next_loc = (next_loc + layout.alignment - 1) / layout.alignment * layout.alignment;
        stack_locations[i] = next_loc;
        next_loc += layout.size;
    }

    /* Generate spills. Spills occur iff using the temporary register (max_reg) */
    JanetSysSpill *spills = NULL;
    for (uint32_t i = 0; i < ir->instruction_count; i++) {
        JanetSysInstruction instruction = ir->instructions[i];
        JanetSysSpill spill;
        spill.spills[0] = JANET_SYS_SPILL_NONE;
        spill.spills[1] = JANET_SYS_SPILL_NONE;
        spill.spills[2] = JANET_SYS_SPILL_NONE;
        uint32_t rega;
        uint32_t regb;
        uint32_t regc;
        switch (instruction.opcode) {
            default:
                break;

            /* DEST = LHS op RHS */
            case JANET_SYSOP_ADD:
            case JANET_SYSOP_SUBTRACT:
            case JANET_SYSOP_MULTIPLY:
            case JANET_SYSOP_DIVIDE:
            case JANET_SYSOP_BAND:
            case JANET_SYSOP_BOR:
            case JANET_SYSOP_BXOR:
            case JANET_SYSOP_SHL:
            case JANET_SYSOP_SHR:
            case JANET_SYSOP_EQ:
            case JANET_SYSOP_NEQ:
            case JANET_SYSOP_LT:
            case JANET_SYSOP_LTE:
            case JANET_SYSOP_GT:
            case JANET_SYSOP_GTE:
            case JANET_SYSOP_POINTER_ADD:
            case JANET_SYSOP_POINTER_SUBTRACT:
                rega = v2reg_dflt(assignments, instruction.three.dest, max_reg);
                regb = v2reg_dflt(assignments, instruction.three.lhs, max_reg + 1);
                regc = v2reg_dflt(assignments, instruction.three.rhs, max_reg + 2);
                spill.regs[0] = rega;
                spill.regs[1] = regb;
                spill.regs[2] = regc;
                if (rega >= max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.stack_offsets[0] = stack_locations[instruction.three.dest];
                    spill.stack_sizes[0] = layouts[instruction.three.dest].size;
                }
                if (regb >= max_reg) {
                    spill.spills[1] = JANET_SYS_SPILL_READ;
                    spill.stack_offsets[1] = stack_locations[instruction.three.lhs];
                    spill.stack_sizes[1] = layouts[instruction.three.lhs].size;
                }
                if (regc >= max_reg) {
                    spill.spills[2] = JANET_SYS_SPILL_READ;
                    spill.stack_offsets[2] = stack_locations[instruction.three.rhs];
                    spill.stack_sizes[2] = layouts[instruction.three.rhs].size;
                }
                break;

            /* DEST = op SRC */
            case JANET_SYSOP_MOVE:
            case JANET_SYSOP_CAST:
            case JANET_SYSOP_BNOT:
                rega = v2reg_dflt(assignments, instruction.two.dest, max_reg);
                regb = v2reg_dflt(assignments, instruction.two.src, max_reg + 1);
                spill.regs[0] = rega;
                spill.regs[1] = regb;
                if (rega >= max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.stack_offsets[0] = stack_locations[instruction.two.dest];
                    spill.stack_sizes[0] = layouts[instruction.two.dest].size;
                }
                if (regb >= max_reg) {
                    spill.spills[1] = JANET_SYS_SPILL_READ;
                    spill.stack_offsets[1] = stack_locations[instruction.two.src];
                    spill.stack_sizes[1] = layouts[instruction.two.src].size;
                }
                break;

            /* branch COND */
            case JANET_SYSOP_BRANCH:
            case JANET_SYSOP_BRANCH_NOT:
                rega = v2reg_dflt(assignments, instruction.branch.cond, max_reg);
                spill.regs[0] = rega;
                if (rega >= max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_READ;
                    spill.stack_offsets[0] = stack_locations[instruction.branch.cond];
                    spill.stack_sizes[0] = layouts[instruction.branch.cond].size;
                }
                break;

            case JANET_SYSOP_CONSTANT:
                rega = v2reg_dflt(assignments, instruction.constant.dest, max_reg);
                spill.regs[0] = rega;
                if (rega >= max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.stack_offsets[0] = stack_locations[instruction.constant.dest];
                    spill.stack_sizes[0] = layouts[instruction.constant.dest].size;
                }
                break;

            case JANET_SYSOP_RETURN:
                rega = v2reg_dflt(assignments, instruction.one.src, max_reg);
                spill.regs[0] = rega;
                if (rega >= max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_READ;
                    spill.stack_offsets[0] = stack_locations[instruction.one.src];
                    spill.stack_sizes[0] = layouts[instruction.one.src].size;
                }
                break;

            /* Should we handle here or per call? */
            case JANET_SYSOP_ARG:
                for (int j = 0; j < 3; j++) {
                    uint32_t var = instruction.arg.args[j];
                    rega = v2reg_dflt(assignments, var, 0);
                    spill.regs[j] = rega;
                    if (rega >= max_reg) { /* Unused elements must be 0 */
                        spill.spills[j] = JANET_SYS_SPILL_READ;
                        spill.stack_offsets[j] = stack_locations[instruction.arg.args[j]];
                        spill.stack_sizes[j] = layouts[instruction.arg.args[j]].size;
                    }
                }
                break;

            /* Variable arg */
            case JANET_SYSOP_CALL:
            case JANET_SYSOP_CALLK:
                /* TODO */
                break;
        }
        janet_v_push(spills, spill);
    }

    janet_sfree(layouts);
    janet_sfree(stack_locations);

    return spills;
}

typedef struct {
    uint32_t temps[3];
} JanetTempRegs;

static JanetTempRegs do_spills_read(JanetBuffer *buffer, JanetSysSpill *spills, uint32_t index) {
    JanetSysSpill spill = spills[index];
    JanetTempRegs temps;
    for (int spi = 0; spi < 3; spi++) {
        uint32_t reg = spill.regs[spi];
        temps.temps[spi] = reg;
        if (spill.spills[spi] == JANET_SYS_SPILL_READ || spill.spills[spi] == JANET_SYS_SPILL_BOTH) {
            // emit load
            uint32_t x = spill.stack_offsets[spi];
            uint32_t s = spill.stack_sizes[spi];
            janet_formatb(buffer, "load%u r%u from stack[%u] ; SPILL\n", s, reg, x);
        }
    }
    return temps;
}

static void do_spills_write(JanetBuffer *buffer, JanetSysSpill *spills, uint32_t index) {
    JanetSysSpill spill = spills[index];
    for (int spi = 0; spi < 3; spi++) {
        if (spill.spills[spi] == JANET_SYS_SPILL_WRITE || spill.spills[spi] == JANET_SYS_SPILL_BOTH) {
            // emit store
            uint32_t reg = spill.regs[spi];
            uint32_t x = spill.stack_offsets[spi];
            uint32_t s = spill.stack_sizes[spi];
            janet_formatb(buffer, "store%u r%u to stack[%u] ; SPILL\n", s, reg, x);
        }
    }
}

void janet_sys_ir_lower_to_x64(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {
    /* For now, emit assembly for nasm. Eventually an assembler for use with Janet would be good. */

    JanetSysTypeLayout *all_layouts = janet_smalloc(linkage->type_def_count * sizeof(JanetSysTypeLayout));
    for (uint32_t i = 0; i < linkage->type_def_count; i++) {
        all_layouts[i] = get_x64layout(linkage->type_defs[i]);
    }

    /* Emit prelude */
    janet_formatb(buffer, "bits 64\ndefault rel\n\n");
    janet_formatb(buffer, "segment .text\n");

    /* Do register allocation  */
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        JanetTable *assignments = janet_table(0);
        JanetTempRegs temps;
        /* Get type layouts */
        JanetSysTypeLayout *layouts = janet_smalloc(ir->register_count * sizeof(JanetSysTypeLayout));
        for (uint32_t i = 0; i < ir->register_count; i++) {
            layouts[i] = all_layouts[ir->types[i]];
        }
        JanetSysSpill *spills = assign_registers(ir, layouts, assignments, 13);

        /* Allow combining compare + branch instructions more easily */
        int skip_branch = 0;

        /* Emit constant strings */
        for (int32_t j = 0; j < ir->constant_count; j++) {
            janet_formatb(buffer, ".CONST%u:\n  .string %p\n", j, ir->constants[j]);
        }

        /* Emit prelude */
        if (ir->link_name != NULL) {
            janet_formatb(buffer, "\n%s:\n", ir->link_name);
        } else {
            janet_formatb(buffer, "\n_section_%d:\n", i);
        }
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
                case JANET_SYSOP_POINTER_SUBTRACT:
                case JANET_SYSOP_ADD:
                case JANET_SYSOP_SUBTRACT:
                case JANET_SYSOP_MULTIPLY:
                case JANET_SYSOP_DIVIDE:
                    temps = do_spills_read(buffer, spills, j);
                    janet_formatb(buffer, "r%u = %s r%u, r%u\n",
                            temps.temps[0],
                            janet_sysop_names[instruction.opcode],
                            temps.temps[1],
                            temps.temps[2]);
                    do_spills_write(buffer, spills, j);
                    break;
                case JANET_SYSOP_MOVE:
                    temps = do_spills_read(buffer, spills, j);
                    //janet_formatb(buffer, "r%u = r%u\n", temps.temps[0], temps.temps[1]);
                    janet_formatb(buffer, "mov %s, %s\n",
                            register_names[temps.temps[0]],
                            register_names[temps.temps[1]]);
                    do_spills_write(buffer, spills, j);
                    break;
                case JANET_SYSOP_RETURN:
                    temps = do_spills_read(buffer, spills, j);
                    //janet_formatb(buffer, "return r%u\n", temps.temps[0]);
                    janet_formatb(buffer, "leave\n mov %s, rax\nret\n", register_names[temps.temps[0]]);
                    break;
                case JANET_SYSOP_CONSTANT:
                    temps = do_spills_read(buffer, spills, j);
                    janet_formatb(buffer, "r%u = constant $%v\n", temps.temps[0], ir->constants[instruction.constant.constant]);
                    do_spills_write(buffer, spills, j);
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
                    temps = do_spills_read(buffer, spills, j);
                    JanetSysInstruction nexti = ir->instructions[j + 1];
                    /* Combine compare and branch into one instruction */
                    /* TODO - handle when lhs or rhs is 0 */
                    /* TODO - handle floats */
                    janet_formatb(buffer, "cmp %s, %s\n", register_names[temps.temps[1]], register_names[temps.temps[2]]);
                    if ((nexti.opcode == JANET_SYSOP_BRANCH ||
                            nexti.opcode == JANET_SYSOP_BRANCH_NOT)
                            && nexti.branch.cond == instruction.three.dest) {
                        skip_branch = 1;
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
                        do_spills_write(buffer, spills, j);
                        break;
                    }
                    /* Fallback to set* instructions */
                    if (instruction.opcode == JANET_SYSOP_EQ) {
                        janet_formatb(buffer, "sete %s\n", register_names[temps.temps[0]]);
                    } else if (instruction.opcode == JANET_SYSOP_NEQ) {
                        janet_formatb(buffer, "setne %s\n", register_names[temps.temps[0]]);
                    } else if (instruction.opcode == JANET_SYSOP_GT) {
                        janet_formatb(buffer, "setg %s\n", register_names[temps.temps[0]]);
                    } else if (instruction.opcode == JANET_SYSOP_GTE) {
                        janet_formatb(buffer, "setge %s\n", register_names[temps.temps[0]]);
                    } else if (instruction.opcode == JANET_SYSOP_LT) {
                        janet_formatb(buffer, "setl %s\n", register_names[temps.temps[0]]);
                    } else if (instruction.opcode == JANET_SYSOP_LTE) {
                        janet_formatb(buffer, "setle %s\n", register_names[temps.temps[0]]);
                    } else {
                        janet_panic("unreachable");
                    }
                    do_spills_write(buffer, spills, j);
                    break;
                case JANET_SYSOP_BRANCH:
                case JANET_SYSOP_BRANCH_NOT:
                    ;
                    if (skip_branch) {
                        skip_branch = 0;
                        break;
                    }
                    temps = do_spills_read(buffer, spills, j);
                    if (instruction.opcode == JANET_SYSOP_BRANCH) {
                        janet_formatb(buffer, "jnz %s label_%u\n",
                                register_names[temps.temps[0]],
                                instruction.branch.to);
                    } else {
                        janet_formatb(buffer, "jz %s label_%u\n",
                                register_names[temps.temps[0]],
                                instruction.branch.to);
                    }
                    break;
                case JANET_SYSOP_JUMP:
                    janet_formatb(buffer, "jmp label_%u\n",
                            instruction.jump.to);
                    break;
                case JANET_SYSOP_CALLK:
                case JANET_SYSOP_CALL:
                    ;
                    /* Strange iteration is to iterate the arguments in reverse order */
                    for (int32_t argo = instruction.call.arg_count - 1; argo >= 0; argo -= 3) {
                        int32_t offset = argo / 3;
                        int32_t sub_count = 3;
                        while (offset * 3 + sub_count >= (int32_t) instruction.call.arg_count) {
                            sub_count--;
                        }
                        temps = do_spills_read(buffer, spills, j + offset);
                        for (int x = sub_count; x >= 0; x--) {
                            janet_formatb(buffer, "push r%u\n", temps.temps[x]);
                        }
                    }
                    temps = do_spills_read(buffer, spills, j);
                    if (instruction.opcode == JANET_SYSOP_CALLK) {
                        if (instruction.callk.has_dest) {
                            janet_formatb(buffer, "r%u = call %p\n", temps.temps[0],
                                    ir->constants[instruction.callk.constant]);
                        } else {
                            janet_formatb(buffer, "call %p\n",
                                    ir->constants[instruction.callk.constant]);
                        }
                    } else {
                        if (instruction.call.has_dest) {
                            janet_formatb(buffer, "r%u = call r%u\n", temps.temps[0], temps.temps[1]);
                        } else {
                            janet_formatb(buffer, "call r%u\n", temps.temps[0]);
                        }
                    }
                    do_spills_write(buffer, spills, j);
                    break;
                // On a comparison, if next instruction is branch that reads from dest, combine into a single op.
            }
        }
    }
}

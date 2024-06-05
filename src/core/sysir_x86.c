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
#endif

static uint32_t v2reg(JanetTable *assignments, uint32_t var) {
    return (uint32_t) janet_unwrap_number(janet_table_get(assignments, janet_wrap_number(var)));
}

JanetSysSpill *assign_registers(JanetSysIR *ir, JanetTable *assignments,
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
        } else {
            janet_table_put(assignments, janet_wrap_number(i), janet_wrap_number(max_reg));
        }
    }

    // TODO - keep track of where we spill to. Simple idea would be to assign each variable
    // a stack location.

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
                rega = v2reg(assignments, instruction.three.dest);
                regb = v2reg(assignments, instruction.three.lhs);
                regc = v2reg(assignments, instruction.three.rhs);
                if (rega == max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.regs[0] = instruction.three.dest;
                }
                if (regb == max_reg) {
                    spill.spills[1] = JANET_SYS_SPILL_READ;
                    spill.regs[1] = instruction.three.lhs;
                }
                if (regc == max_reg) {
                    spill.spills[2] = JANET_SYS_SPILL_READ;
                    spill.regs[2] = instruction.three.rhs;
                }
                break;

            /* DEST = op SRC */
            case JANET_SYSOP_MOVE:
            case JANET_SYSOP_CAST:
            case JANET_SYSOP_BNOT:
                rega = v2reg(assignments, instruction.two.dest);
                regb = v2reg(assignments, instruction.two.src);
                if (rega == max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.regs[0] = instruction.two.dest;
                }
                if (regb == max_reg) {
                    spill.spills[1] = JANET_SYS_SPILL_READ;
                    spill.regs[1] = instruction.two.src;
                }
                break;

            /* branch COND */
            case JANET_SYSOP_BRANCH:
            case JANET_SYSOP_BRANCH_NOT:
                rega = v2reg(assignments, instruction.branch.cond);
                if (rega == max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_READ;
                    spill.regs[0] = instruction.branch.cond;
                }
                break;

            case JANET_SYSOP_CONSTANT:
                rega = v2reg(assignments, instruction.constant.dest);
                if (rega == max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_WRITE;
                    spill.regs[0] = instruction.constant.dest;
                }
                break;

            case JANET_SYSOP_RETURN:
                rega = v2reg(assignments, instruction.one.src);
                if (rega == max_reg) {
                    spill.spills[0] = JANET_SYS_SPILL_READ;
                    spill.regs[0] = instruction.one.src;
                }
                break;

            /* Should we handle here or per call? */
            case JANET_SYSOP_ARG:
                for (int j = 0; j < 3; j++) {
                    uint32_t var = instruction.arg.args[j];
                    rega = v2reg(assignments, var);
                    if (rega == max_reg) {
                        spill.spills[j] = JANET_SYS_SPILL_READ;
                        spill.regs[j] = var;
                    }
                }
                break;

            /* Variable arg */
            case JANET_SYSOP_CALL:
            case JANET_SYSOP_CALLK:
                break;
        }
        janet_v_push(spills, spill);
    }

    return spills;
}

void janet_sys_ir_lower_to_x64(JanetSysIRLinkage *linkage, JanetBuffer *buffer) {

    /* Do register allocation  */
    for (int32_t i = 0; i < linkage->ir_ordered->count; i++) {
        JanetSysIR *ir = janet_unwrap_pointer(linkage->ir_ordered->data[i]);
        JanetTable *assignments = janet_table(0);
        JanetSysSpill *spills = assign_registers(ir, assignments, 15);

        /* Emit prelude */
        if (ir->link_name != NULL) {
            janet_formatb(buffer, ".%s\n", ir->link_name);
        } else {
            janet_formatb(buffer, "._section_%d\n", i);
        }
        for (uint32_t j = 0; j < ir->instruction_count; j++) {
            JanetSysInstruction instruction = ir->instructions[j];
            JanetSysSpill spill = spills[j];
            for (int spi = 0; spi < 3; spi++) {
                if (spill.spills[spi] == JANET_SYS_SPILL_READ || spill.spills[spi] == JANET_SYS_SPILL_BOTH) {
                    // emit load
                    uint32_t reg = spill.regs[spi];
                    void *x = (void *) 0x123456;
                    janet_formatb(buffer, "load r%u from %v ; SPILL\n", reg, janet_wrap_pointer(x));
                }
                if (spill.spills[spi] == JANET_SYS_SPILL_WRITE || spill.spills[spi] == JANET_SYS_SPILL_BOTH) {
                    // emit store
                    uint32_t reg = spill.regs[spi];
                    void *x = (void *) 0x123456;
                    janet_formatb(buffer, "store r%u to %v ; SPILL\n", reg, janet_wrap_pointer(x));
                }
            }
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
                    /* Non synthesized instructions */
                    break;
                case JANET_SYSOP_POINTER_ADD:
                case JANET_SYSOP_POINTER_SUBTRACT:
                case JANET_SYSOP_ADD:
                case JANET_SYSOP_SUBTRACT:
                case JANET_SYSOP_MULTIPLY:
                case JANET_SYSOP_DIVIDE:
                    janet_formatb(buffer, "r%u = %s r%u, r%u\n",
                            v2reg(assignments, instruction.three.dest),
                            janet_sysop_names[instruction.opcode],
                            v2reg(assignments, instruction.three.lhs),
                            v2reg(assignments, instruction.three.rhs));
                    break;
                case JANET_SYSOP_MOVE:
                    janet_formatb(buffer, "r%u = r%u\n",
                            v2reg(assignments, instruction.two.dest),
                            v2reg(assignments, instruction.two.src));
                    break;
                case JANET_SYSOP_RETURN:
                    janet_formatb(buffer, "return r%u\n",
                            v2reg(assignments, instruction.one.src));
                    break;
                case JANET_SYSOP_CONSTANT:
                    janet_formatb(buffer, "r%u = constant $%v\n",
                            v2reg(assignments, instruction.constant.dest),
                            ir->constants[instruction.constant.constant]);
                    break;
                case JANET_SYSOP_LABEL:
                    janet_formatb(buffer, ":label_%u\n",
                            v2reg(assignments, instruction.label.id));
                    break;

                // On a comparison, if next instruction is branch that reads from dest, combine into a single op.
            }
        }
    }
}

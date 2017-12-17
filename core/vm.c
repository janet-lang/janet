/*
* Copyright (c) 2017 Calvin Rose
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

#include <dst/dst.h>
#include "opcodes.h"
#include "symcache.h"

/* VM State */
DstFiber *dst_vm_fiber;

/* Helper to ensure proper fiber is activated after returning */
static int dst_update_fiber() {
    if (dst_vm_fiber->frame == 0) {
        dst_vm_fiber->status = DST_FIBER_DEAD;
    }
    while (dst_vm_fiber->status == DST_FIBER_DEAD ||
            dst_vm_fiber->status == DST_FIBER_ERROR) {
        if (NULL != dst_vm_fiber->parent) {
            dst_vm_fiber = dst_vm_fiber->parent;
            if (dst_vm_fiber->status == DST_FIBER_ALIVE) {
                /* If the parent thread is still alive,
                   we are inside a cfunction */
                return 1;
            }
        } else {
            /* The root thread has termiated */
            return 1;
        }
    }
    dst_vm_fiber->status = DST_FIBER_ALIVE;
    return 0;
}

/* Start running the VM from where it left off. */
int dst_continue() {

    /* VM state */
    DstValue *stack;
    uint32_t *pc;
    DstFunction *func;

/* Eventually use computed gotos for more effient vm loop. */
#define vm_next() continue
#define vm_checkgc_next() dst_maybe_collect(); continue

    /* Used to extract bits from the opcode that correspond to arguments.
     * Pulls out unsigned integers */
#define oparg(shift, mask) (((*pc) >> ((shift) << 3)) & (mask))

#define vm_throw(e) do { dst_vm_fiber->ret = dst_cstringv((e)); goto vm_error; } while (0)
#define vm_assert(cond, e) do {if (!(cond)) vm_throw((e)); } while (0)

#define vm_binop_integer(op) \
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        dst_unwrap_integer(stack[oparg(2, 0xFF)]) op dst_unwrap_integer(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_real(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_real(\
        dst_unwrap_real(stack[oparg(2, 0xFF)]) op dst_unwrap_real(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_immediate(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        dst_unwrap_integer(stack[oparg(2, 0xFF)]) op (*((int32_t *)pc) >> 24)\
    );\
    pc++;\
    vm_next();

#define vm_binop(op)\
    {\
        DstValue op1 = stack[oparg(2, 0xFF)];\
        DstValue op2 = stack[oparg(3, 0xFF)];\
        vm_assert(dst_checktype(op1, DST_INTEGER) || dst_checktype(op1, DST_REAL), "expected number");\
        vm_assert(dst_checktype(op2, DST_INTEGER) || dst_checktype(op2, DST_REAL), "expected number");\
        stack[oparg(1, 0xFF)] = dst_checktype(op1, DST_INTEGER)\
            ? (dst_checktype(op2, DST_INTEGER)\
                ? dst_wrap_integer(dst_unwrap_integer(op1) op dst_unwrap_integer(op2))\
                : dst_wrap_real((double)dst_unwrap_integer(op1) op dst_unwrap_real(op2)))\
            : (dst_checktype(op2, DST_INTEGER)\
                ? dst_wrap_real(dst_unwrap_real(op1) op (double)dst_unwrap_integer(op2))\
                : dst_wrap_real(dst_unwrap_real(op1) op dst_unwrap_real(op2)));\
        pc++;\
        vm_next();\
    }

#define vm_init_fiber_state() \
    dst_vm_fiber->status = DST_FIBER_ALIVE;\
    stack = dst_vm_fiber->data + dst_vm_fiber->frame;\
    pc = dst_stack_frame(stack)->pc;\
    func = dst_stack_frame(stack)->func;

    vm_init_fiber_state();

    /* Main interpreter loop. It is large, but it is
     * is maintainable. Adding new opcodes is mostly just adding newcases
     * to this loop, adding the opcode to opcodes.h, and adding it to the assembler.
     * Some opcodes, especially ones that do arithmetic, are almost entirely
     * templated by the above macros. */
    for (;;) {

        switch (*pc & 0xFF) {

        default:
        vm_throw("unknown opcode");

        case DOP_NOOP:
        pc++;
        vm_next();

        case DOP_ERROR:
        dst_vm_fiber->ret = stack[oparg(1, 0xFF)];
        goto vm_error;

        case DOP_TYPECHECK:
        vm_assert((1 << dst_type(stack[oparg(1, 0xFF)])) & oparg(2, 0xFFFF),
                "typecheck failed");
        pc++;
        vm_next();

        case DOP_RETURN:
        dst_vm_fiber->ret = stack[oparg(1, 0xFFFFFF)];
        goto vm_return;

        case DOP_RETURN_NIL:
        dst_vm_fiber->ret = dst_wrap_nil();
        goto vm_return;

        case DOP_ADD_INTEGER:
        vm_binop_integer(+);

        case DOP_ADD_IMMEDIATE:
        vm_binop_immediate(+);

        case DOP_ADD_REAL:
        vm_binop_real(+);

        case DOP_ADD:
        vm_binop(+);

        case DOP_SUBTRACT_INTEGER:
        vm_binop_integer(-);

        case DOP_SUBTRACT_REAL:
        vm_binop_real(-);

        case DOP_SUBTRACT:
        vm_binop(-);
            
        case DOP_MULTIPLY_INTEGER:
        vm_binop_integer(*);

        case DOP_MULTIPLY_IMMEDIATE:
        vm_binop_immediate(*);

        case DOP_MULTIPLY_REAL:
        vm_binop_real(*);

        case DOP_MULTIPLY:
        vm_binop(*);

        case DOP_DIVIDE_INTEGER:
        vm_assert(dst_unwrap_integer(stack[oparg(3, 0xFF)]) != 0, "integer divide by zero");
        vm_assert(!(dst_unwrap_integer(stack[oparg(3, 0xFF)]) == -1 && 
                    dst_unwrap_integer(stack[oparg(2, 0xFF)]) == DST_INTEGER_MIN),
                "integer divide overflow");
        vm_binop_integer(/);
        
        case DOP_DIVIDE_IMMEDIATE:
        {
            int32_t op1 = dst_unwrap_integer(stack[oparg(2, 0xFF)]);
            int32_t op2 = *((int32_t *)pc) >> 24;
            /* Check for degenerate integer division (divide by zero, and dividing
             * min value by -1). These checks could be omitted if the arg is not 
             * 0 or -1. */
            if (op2 == 0)
                vm_throw("integer divide by zero");
            if (op2 == -1)
                vm_throw("integer divide overflow");
            else
                stack[oparg(1, 0xFF)] = dst_wrap_integer(op1 / op2);
            pc++;
            vm_next();
        }

        case DOP_DIVIDE_REAL:
        vm_binop_real(/);

        case DOP_DIVIDE:
        {
            DstValue op1 = stack[oparg(2, 0xFF)];
            DstValue op2 = stack[oparg(3, 0xFF)];
            vm_assert(dst_checktype(op1, DST_INTEGER) || dst_checktype(op1, DST_REAL), "expected number");
            vm_assert(dst_checktype(op2, DST_INTEGER) || dst_checktype(op2, DST_REAL), "expected number");
            if (dst_checktype(op2, DST_INTEGER) && dst_unwrap_integer(op2) == 0)
                op2 = dst_wrap_real(0.0);
            if (dst_checktype(op2, DST_INTEGER) && dst_unwrap_integer(op2) == -1 &&
                dst_checktype(op1, DST_INTEGER) && dst_unwrap_integer(op1) == DST_INTEGER_MIN)
                op2 = dst_wrap_real(-1.0);
            stack[oparg(1, 0xFF)] = dst_checktype(op1, DST_INTEGER)
                ? (dst_checktype(op2, DST_INTEGER)
                    ? dst_wrap_integer(dst_unwrap_integer(op1) / dst_unwrap_integer(op2))
                    : dst_wrap_real((double)dst_unwrap_integer(op1) / dst_unwrap_real(op2)))
                : (dst_checktype(op2, DST_INTEGER)
                    ? dst_wrap_real(dst_unwrap_real(op1) / (double)dst_unwrap_integer(op2))
                    : dst_wrap_real(dst_unwrap_real(op1) / dst_unwrap_real(op2)));
            pc++;
            vm_next();
        }

        case DOP_BAND:
        vm_binop_integer(&);

        case DOP_BOR:
        vm_binop_integer(|);

        case DOP_BXOR:
        vm_binop_integer(^);

        case DOP_BNOT:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(~dst_unwrap_integer(stack[oparg(2, 0xFFFF)]));
        vm_next();
            
        case DOP_SHIFT_RIGHT_UNSIGNED:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(
            (int32_t)(((uint32_t)dst_unwrap_integer(stack[oparg(2, 0xFF)]))
            >>
            dst_unwrap_integer(stack[oparg(3, 0xFF)]))
        );
        pc++;
        vm_next();

        case DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(
            (int32_t) (((uint32_t)dst_unwrap_integer(stack[oparg(2, 0xFF)])) >> oparg(3, 0xFF))
        );
        pc++;
        vm_next();

        case DOP_SHIFT_RIGHT:
        vm_binop_integer(>>);

        case DOP_SHIFT_RIGHT_IMMEDIATE:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(
            (int32_t)(dst_unwrap_integer(stack[oparg(2, 0xFF)]) >> oparg(3, 0xFF))
        );
        pc++;
        vm_next();

        case DOP_SHIFT_LEFT:
        vm_binop_integer(<<);

        case DOP_SHIFT_LEFT_IMMEDIATE:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) << oparg(3, 0xFF)
        );
        pc++;
        vm_next();

        case DOP_MOVE_NEAR:
        stack[oparg(1, 0xFF)] = stack[oparg(2, 0xFFFF)];
        pc++;
        vm_next();

        case DOP_MOVE_FAR:
        stack[oparg(2, 0xFFFF)] = stack[oparg(1, 0xFF)];
        pc++;
        vm_next();

        case DOP_JUMP:
        pc += (*(int32_t *)pc) >> 8;
        vm_next();

        case DOP_JUMP_IF:
        if (dst_truthy(stack[oparg(1, 0xFF)])) {
            pc += (*(int32_t *)pc) >> 16;
        } else {
            pc++;
        }
        vm_next();

        case DOP_JUMP_IF_NOT:
        if (dst_truthy(stack[oparg(1, 0xFF)])) {
            pc++;
        } else {
            pc += (*(int32_t *)pc) >> 16;
        }
        vm_next();

        case DOP_LESS_THAN:
        stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_compare(
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]
            ) < 0);
        pc++;
        vm_next();

        case DOP_GREATER_THAN:
        stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_compare(
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]
            ) > 0);
        pc++;
        vm_next();

        case DOP_EQUALS:
        stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_equals(
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]
            ));
        pc++;
        vm_next();

        case DOP_COMPARE:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(dst_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ));
        pc++;
        vm_next();

        case DOP_LOAD_NIL:
        stack[oparg(1, 0xFFFFFF)] = dst_wrap_nil();
        pc++;
        vm_next();

        case DOP_LOAD_TRUE:
        stack[oparg(1, 0xFFFFFF)] = dst_wrap_boolean(1);
        pc++;
        vm_next();

        case DOP_LOAD_FALSE:
        stack[oparg(1, 0xFFFFFF)] = dst_wrap_boolean(0);
        pc++;
        vm_next();

        case DOP_LOAD_INTEGER:
        stack[oparg(1, 0xFF)] = dst_wrap_integer(*((int32_t *)pc) >> 16);
        pc++;
        vm_next();

        case DOP_LOAD_CONSTANT:
        vm_assert((int32_t)oparg(2, 0xFFFF) < func->def->constants_length, "invalid constant");
        stack[oparg(1, 0xFF)] = func->def->constants[(int32_t)oparg(2, 0xFFFF)];
        pc++;
        vm_next();

        case DOP_LOAD_SELF:
        stack[oparg(1, 0xFFFFFF)] = dst_wrap_function(func);
        pc++;
        vm_next();

        case DOP_LOAD_UPVALUE:
        {
            int32_t eindex = oparg(2, 0xFF);
            int32_t vindex = oparg(3, 0xFF);
            DstFuncEnv *env;
            vm_assert(func->def->environments_length > eindex, "invalid upvalue");
            env = func->envs[eindex];
            vm_assert(env->length > vindex, "invalid upvalue");
            if (env->offset) {
                /* On stack */
                stack[oparg(1, 0xFF)] = env->as.fiber->data[env->offset + vindex];
            } else {
                /* Off stack */
                stack[oparg(1, 0xFF)] = env->as.values[vindex];
            }
            pc++;
            vm_next();
        }

        case DOP_SET_UPVALUE:
        {
            int32_t eindex = oparg(2, 0xFF);
            int32_t vindex = oparg(3, 0xFF);
            DstFuncEnv *env;
            vm_assert(func->def->environments_length > eindex, "invalid upvalue");
            env = func->envs[eindex];
            vm_assert(env->length > vindex, "invalid upvalue");
            if (env->offset) {
                env->as.fiber->data[env->offset + vindex] = stack[oparg(1, 0xFF)];
            } else {
                env->as.values[vindex] = stack[oparg(1, 0xFF)];
            }
            pc++;
            vm_next();
        }

        case DOP_CLOSURE:
        {
            int32_t i;
            DstFunction *fn;
            DstFuncDef *fd;
            vm_assert((int32_t)oparg(2, 0xFFFF) < func->def->constants_length, "invalid constant");
            vm_assert(dst_checktype(func->def->constants[oparg(2, 0xFFFF)], DST_NIL), "constant must be funcdef");
            fd = (DstFuncDef *)(dst_unwrap_pointer(func->def->constants[(int32_t)oparg(2, 0xFFFF)]));
            fn = dst_alloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
            fn->envs = malloc(sizeof(DstFuncEnv *) * fd->environments_length);
            if (NULL == fn->envs) {
                DST_OUT_OF_MEMORY;
            }
            if (fd->flags & DST_FUNCDEF_FLAG_NEEDSENV) {
                /* Delayed capture of current stack frame */
                DstFuncEnv *env = dst_alloc(DST_MEMORY_FUNCENV, sizeof(DstFuncEnv));
                env->offset = dst_vm_fiber->frame;
                env->as.fiber = dst_vm_fiber;
                env->length = func->def->slotcount;
                fn->envs[0] = env;
            } else {
                fn->envs[0] = NULL;
            }
            for (i = 1; i < fd->environments_length; ++i) {
                int32_t inherit = fd->environments[i];
                fn->envs[i] = func->envs[inherit];
            }
            stack[oparg(1, 0xFF)] = dst_wrap_function(fn);
            pc++;
            vm_checkgc_next();
        }

        case DOP_PUSH:
        dst_fiber_push(dst_vm_fiber, stack[oparg(1, 0xFFFFFF)]);
        pc++;
        vm_checkgc_next();

        case DOP_PUSH_2:
        dst_fiber_push2(dst_vm_fiber, 
            stack[oparg(1, 0xFF)],
            stack[oparg(2, 0xFFFF)]);
        pc++;
        vm_checkgc_next();

        case DOP_PUSH_3:
        dst_fiber_push3(dst_vm_fiber, 
            stack[oparg(1, 0xFF)],
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]);
        pc++;
        vm_checkgc_next();

        case DOP_PUSH_ARRAY:
        {
            int32_t count;
            const DstValue *array;
            if (dst_seq_view(stack[oparg(1, 0xFFFFFF)], &array, &count)) {
                dst_fiber_pushn(dst_vm_fiber, array, count);
            } else {
                vm_throw("expected array or tuple");
            }
            pc++;
            vm_checkgc_next();
        }

        case DOP_CALL:
        {
            DstValue callee = stack[oparg(2, 0xFFFF)];
            if (dst_checktype(callee, DST_FUNCTION)) {
                func = dst_unwrap_function(callee);
                dst_fiber_funcframe(dst_vm_fiber, func);
                stack = dst_vm_fiber->data + dst_vm_fiber->frame;
                pc = func->def->bytecode;
                vm_checkgc_next();
            } else if (dst_checktype(callee, DST_CFUNCTION)) {
                dst_fiber_cframe(dst_vm_fiber);
                dst_vm_fiber->ret = dst_wrap_nil();
                if (dst_unwrap_cfunction(callee)(
                        dst_vm_fiber->data + dst_vm_fiber->frame,
                        dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                    goto vm_error;
                }
                goto vm_return_cfunc;
            }
            vm_throw("cannot call non-function type");
        }

        case DOP_TAILCALL:
        {
            DstValue callee = stack[oparg(2, 0xFFFF)];
            if (dst_checktype(callee, DST_FUNCTION)) {
                func = dst_unwrap_function(callee);
                dst_fiber_funcframe_tail(dst_vm_fiber, func);
                stack = dst_vm_fiber->data + dst_vm_fiber->frame;
                pc = func->def->bytecode;
                vm_checkgc_next();
            } else if (dst_checktype(callee, DST_CFUNCTION)) {
                dst_fiber_cframe_tail(dst_vm_fiber);
                dst_vm_fiber->ret = dst_wrap_nil();
                if (dst_unwrap_cfunction(callee)(
                            dst_vm_fiber->data + dst_vm_fiber->frame, 
                            dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                    goto vm_error;
                }
                goto vm_return_cfunc;
            }
            vm_throw("expected function");
        }

        case DOP_SYSCALL:
        {
            DstCFunction f = dst_vm_syscalls[oparg(2, 0xFF)];
            vm_assert(NULL != f, "invalid syscall");
            dst_fiber_cframe(dst_vm_fiber);
            dst_vm_fiber->ret = dst_wrap_nil();
            if (f(dst_vm_fiber->data + dst_vm_fiber->frame,
                        dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                goto vm_error;
            }
            goto vm_return_cfunc;
        }

        case DOP_LOAD_SYSCALL:
        {
            DstCFunction f = dst_vm_syscalls[oparg(2, 0xFF)];
            vm_assert(NULL != f, "invalid syscall");
            stack[oparg(1, 0xFF)] = dst_wrap_cfunction(f);
            pc++;
            vm_next();
        }

        case DOP_TRANSFER:
        {
            DstFiber *nextfiber;
            DstStackFrame *frame = dst_stack_frame(stack);
            DstValue temp = stack[oparg(2, 0xFF)];
            DstValue retvalue = stack[oparg(3, 0xFF)];
            vm_assert(dst_checktype(temp, DST_FIBER) ||
                      dst_checktype(temp, DST_NIL), "expected fiber");
            nextfiber = dst_checktype(temp, DST_FIBER)
                ? dst_unwrap_fiber(temp)
                : dst_vm_fiber->parent;
            /* Check for root fiber */
            if (NULL == nextfiber) {
                frame->pc = pc;
                dst_vm_fiber->ret = retvalue;
                return 0;
            }
            vm_assert(nextfiber->status == DST_FIBER_PENDING, "can only transfer to pending fiber");
            frame->pc = pc;
            dst_vm_fiber->status = DST_FIBER_PENDING;
            dst_vm_fiber = nextfiber;
            vm_init_fiber_state();
            stack[oparg(1, 0xFF)] = retvalue;
            pc++;
            vm_next();
        }

        case DOP_PUT:
        dst_put(stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
        ++pc;
        vm_checkgc_next();

        case DOP_PUT_INDEX:
        dst_setindex(stack[oparg(1, 0xFF)],
                stack[oparg(3, 0xFF)],
                oparg(3, 0xFF));
        ++pc;
        vm_next();

        case DOP_GET:
        stack[oparg(1, 0xFF)] = dst_get(
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
        ++pc;
        vm_next();

        case DOP_GET_INDEX:
        stack[oparg(1, 0xFF)] = dst_getindex(
                stack[oparg(2, 0xFF)],
                oparg(3, 0xFF));
        ++pc;
        vm_next();

        /* Return from c function. Simpler than retuning from dst function */
        vm_return_cfunc:
        {
            DstValue ret = dst_vm_fiber->ret;
            dst_fiber_popframe(dst_vm_fiber);
            if (dst_update_fiber())
                return 0;
            stack[oparg(1, 0xFF)] = ret;
            pc++;
            vm_checkgc_next();
        }

        /* Handle returning from stack frame. Expect return value in fiber->ret */
        vm_return:
        {
            DstValue ret = dst_vm_fiber->ret;
            dst_fiber_popframe(dst_vm_fiber);
            if (dst_update_fiber())
                return 0;
            stack = dst_vm_fiber->data + dst_vm_fiber->frame;
            pc = dst_stack_frame(stack)->pc;
            stack[oparg(1, 0xFF)] = ret;
            pc++;
            vm_checkgc_next();
        }

        /* Handle errors from c functions and vm opcodes */
        vm_error:
        {
            DstValue ret = dst_vm_fiber->ret;
            dst_vm_fiber->status = DST_FIBER_ERROR;
            if (dst_update_fiber())
                return 1;
            stack = dst_vm_fiber->data + dst_vm_fiber->frame;
            pc = dst_stack_frame(stack)->pc;
            stack[oparg(1, 0xFF)] = ret;
            pc++;
            vm_checkgc_next();
        }
    
        } /* end switch */

    } /* end for */

#undef oparg

#undef vm_error
#undef vm_assert
#undef vm_binop
#undef vm_binop_real
#undef vm_binop_integer
#undef vm_binop_immediate
#undef vm_init_fiber_state

}

/* Run the vm with a given function. This function is
 * called to start the vm. */
int dst_run(DstValue callee) {
    if (NULL == dst_vm_fiber) {
        dst_vm_fiber = dst_fiber(0);
    } else {
        dst_fiber_reset(dst_vm_fiber);
    }
    if (dst_checktype(callee, DST_CFUNCTION)) {
        dst_vm_fiber->ret = dst_wrap_nil();
        dst_fiber_cframe(dst_vm_fiber);
        return dst_unwrap_cfunction(callee)(dst_vm_fiber->data + dst_vm_fiber->frame, 0);
    } else if (dst_checktype(callee, DST_FUNCTION)) {
        dst_fiber_funcframe(dst_vm_fiber, dst_unwrap_function(callee));
        return dst_continue();
    }
    dst_vm_fiber->ret = dst_cstringv("expected function");
    return 1;
}

/* Setup functions */
int dst_init() {
    /* Garbage collection */
    dst_vm_blocks = NULL;
    dst_vm_next_collection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * horrible for performance, but helps ensure
     * there are no memory bugs during dev */
    dst_vm_memory_interval = 0x0000000;
    dst_symcache_init();
    /* Set thread */
    dst_vm_fiber = NULL;
    return 0;
}

/* Clear all memory associated with the VM */
void dst_deinit() {
    dst_clear_memory();
    dst_vm_fiber = NULL;
    dst_symcache_deinit();
}

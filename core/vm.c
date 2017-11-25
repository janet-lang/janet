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

    /* Used to extract bits from the opcode that correspond to arguments.
     * Pulls out unsigned integers */
#define oparg(shift, mask) (((*pc) >> ((shift) << 3)) & (mask))

#define vm_throw(e) do { dst_vm_fiber->ret = dst_wrap_string(dst_cstring((e))); goto vm_error; } while (0)
#define vm_assert(cond, e) do {if (!(cond)) vm_throw((e)); } while (0)

#define vm_binop_integer(op) \
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        stack[oparg(2, 0xFF)].as.integer op stack[oparg(3, 0xFF)].as.integer\
    );\
    pc++;\
    continue;

#define vm_binop_real(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_real(\
        stack[oparg(2, 0xFF)].as.real op stack[oparg(3, 0xFF)].as.real\
    );\
    pc++;\
    continue;

#define vm_binop_immediate(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        stack[oparg(2, 0xFF)].as.integer op (*((int32_t *)pc) >> 24)\
    );\
    pc++;\
    continue;

#define vm_binop(op)\
    {\
        DstValue op1 = stack[oparg(2, 0xFF)];\
        DstValue op2 = stack[oparg(3, 0xFF)];\
        vm_assert(op1.type == DST_INTEGER || op1.type == DST_REAL, "expected number");\
        vm_assert(op2.type == DST_INTEGER || op2.type == DST_REAL, "expected number");\
        stack[oparg(1, 0xFF)] = op1.type == DST_INTEGER\
            ? (op2.type == DST_INTEGER\
                ? dst_wrap_integer(op1.as.integer op op2.as.integer)\
                : dst_wrap_real(dst_integer_to_real(op1.as.integer) op op2.as.real))\
            : (op2.type == DST_INTEGER\
                ? dst_wrap_real(op1.as.real op dst_integer_to_real(op2.as.integer))\
                : dst_wrap_real(op1.as.real op op2.as.real));\
        pc++;\
        continue;\
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
            break;

        case DOP_NOOP:
            pc++;
            continue;

        case DOP_ERROR:
            dst_vm_fiber->ret = stack[oparg(1, 0xFF)];
            goto vm_error;

        case DOP_TYPECHECK:
            vm_assert((1 << stack[oparg(1, 0xFF)].type) & oparg(2, 0xFFFF),
                    "typecheck failed");
            pc++;
            continue;

        case DOP_RETURN:
            dst_vm_fiber->ret = stack[oparg(1, 0xFFFFFF)];
            goto vm_return;

        case DOP_RETURN_NIL:
            dst_vm_fiber->ret.type = DST_NIL;
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
            vm_assert(stack[oparg(3, 0xFF)].as.integer != 0, "integer divide by zero");
            vm_assert(!(stack[oparg(3, 0xFF)].as.integer == -1 && 
                        stack[oparg(2, 0xFF)].as.integer == DST_INTEGER_MIN),
                    "integer divide overflow");
            vm_binop_integer(/);
        
        case DOP_DIVIDE_IMMEDIATE:
            {
                int64_t op1 = stack[oparg(2, 0xFF)].as.integer;
                int64_t op2 = *((int32_t *)pc) >> 24;
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
                continue;
            }

        case DOP_DIVIDE_REAL:
            vm_binop_real(/);

        case DOP_DIVIDE:
            {
                DstValue op1 = stack[oparg(2, 0xFF)];
                DstValue op2 = stack[oparg(3, 0xFF)];
                vm_assert(op1.type == DST_INTEGER || op1.type == DST_REAL, "expected number");
                vm_assert(op2.type == DST_INTEGER || op2.type == DST_REAL, "expected number");
                if (op2.type == DST_INTEGER && op2.as.integer == 0)
                    op2 = dst_wrap_real(0.0);
                if (op2.type == DST_INTEGER && op2.as.integer == -1 &&
                    op1.type == DST_INTEGER && op1.as.integer == DST_INTEGER_MIN)
                    op2 = dst_wrap_real(-1);
                stack[oparg(1, 0xFF)] = op1.type == DST_INTEGER
                    ? op2.type == DST_INTEGER
                        ? dst_wrap_integer(op1.as.integer / op2.as.integer)
                        : dst_wrap_real(dst_integer_to_real(op1.as.integer) / op2.as.real)
                    : op2.type == DST_INTEGER
                        ? dst_wrap_real(op1.as.real / dst_integer_to_real(op2.as.integer))
                        : dst_wrap_real(op1.as.real / op2.as.real);
                pc++;
                continue;
            }

        case DOP_BAND:
            vm_binop_integer(&);

        case DOP_BOR:
            vm_binop_integer(|);

        case DOP_BXOR:
            vm_binop_integer(^);

        case DOP_BNOT:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(~stack[oparg(2, 0xFFFF)].as.integer);
            continue;
            
        case DOP_SHIFT_RIGHT_UNSIGNED:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(
                stack[oparg(2, 0xFF)].as.uinteger
                >>
                stack[oparg(3, 0xFF)].as.uinteger
            );
            pc++;
            continue;

        case DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(
                stack[oparg(2, 0xFF)].as.uinteger >> oparg(3, 0xFF)
            );
            pc++;
            continue;

        case DOP_SHIFT_RIGHT:
            vm_binop_integer(>>);

        case DOP_SHIFT_RIGHT_IMMEDIATE:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(
                (int64_t)(stack[oparg(2, 0xFF)].as.uinteger >> oparg(3, 0xFF))
            );
            pc++;
            continue;

        case DOP_SHIFT_LEFT:
            vm_binop_integer(<<);

        case DOP_SHIFT_LEFT_IMMEDIATE:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(
                stack[oparg(2, 0xFF)].as.integer << oparg(3, 0xFF)
            );
            pc++;
            continue;

        case DOP_MOVE:
            stack[oparg(1, 0xFF)] = stack[oparg(2, 0xFFFF)];
            pc++;
            continue;

        case DOP_JUMP:
            pc += (*(int32_t *)pc) >> 8;
            continue;

        case DOP_JUMP_IF:
            if (dst_truthy(stack[oparg(1, 0xFF)])) {
                pc += (*(int32_t *)pc) >> 16;
            } else {
                pc++;
            }
            continue;

        case DOP_JUMP_IF_NOT:
            if (dst_truthy(stack[oparg(1, 0xFF)])) {
                pc++;
            } else {
                pc += (*(int32_t *)pc) >> 16;
            }
            continue;

        case DOP_LESS_THAN:
            stack[oparg(1, 0xFF)].type = DST_BOOLEAN;
            stack[oparg(1, 0xFF)].as.boolean = dst_compare(
                    stack[oparg(2, 0xFF)],
                    stack[oparg(3, 0xFF)]
                ) < 0;
            pc++;
            continue;

        case DOP_GREATER_THAN:
            stack[oparg(1, 0xFF)].type = DST_BOOLEAN;
            stack[oparg(1, 0xFF)].as.boolean = dst_compare(
                    stack[oparg(2, 0xFF)],
                    stack[oparg(3, 0xFF)]
                ) > 0;
            pc++;
            continue;

        case DOP_EQUALS:
            stack[oparg(1, 0xFF)].type = DST_BOOLEAN;
            stack[oparg(1, 0xFF)].as.boolean = dst_equals(
                    stack[oparg(2, 0xFF)],
                    stack[oparg(3, 0xFF)]
                );
            pc++;
            continue;

        case DOP_COMPARE:
            stack[oparg(1, 0xFF)].type = DST_INTEGER;
            stack[oparg(1, 0xFF)].as.integer = dst_compare(
                    stack[oparg(2, 0xFF)],
                    stack[oparg(3, 0xFF)]
                );
            pc++;
            continue;

        case DOP_LOAD_NIL:
            stack[oparg(1, 0xFFFFFF)].type = DST_NIL;
            pc++;
            continue;

        case DOP_LOAD_BOOLEAN:
            stack[oparg(1, 0xFF)] = dst_wrap_boolean(oparg(2, 0xFFFF));
            pc++;
            continue;

        case DOP_LOAD_INTEGER:
            stack[oparg(1, 0xFF)] = dst_wrap_integer(*((int32_t *)pc) >> 16);
            pc++;
            continue;

        case DOP_LOAD_CONSTANT:
            vm_assert(oparg(2, 0xFFFF) < func->def->constants_length, "invalid constant");
            stack[oparg(1, 0xFF)] = func->def->constants[oparg(2, 0xFFFF)];
            pc++;
            continue;

        case DOP_LOAD_UPVALUE:
            {
                uint32_t eindex = oparg(2, 0xFF);
                uint32_t vindex = oparg(3, 0xFF);
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
                continue;
            }

        case DOP_SET_UPVALUE:
            {
                uint32_t eindex = oparg(2, 0xFF);
                uint32_t vindex = oparg(3, 0xFF);
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
                continue;
            }

        case DOP_CLOSURE:
            {
                uint32_t i;
                DstFunction *fn;
                DstFuncDef *fd;
                vm_assert(oparg(2, 0xFFFF) < func->def->constants_length, "invalid constant");
                vm_assert(func->def->constants[oparg(2, 0xFFFF)].type == DST_NIL, "constant must be funcdef");
                fd = (DstFuncDef *)(func->def->constants[oparg(2, 0xFFFF)].as.pointer);
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
                    uint32_t inherit = fd->environments[i];
                    fn->envs[i] = func->envs[inherit];
                }
                stack[oparg(1, 0xFF)] = dst_wrap_function(fn);
                pc++;
                break;
            }

        case DOP_PUSH:
            dst_fiber_push(dst_vm_fiber, stack[oparg(1, 0xFFFFFF)]);
            pc++;
            break;

        case DOP_PUSH_2:
            dst_fiber_push2(dst_vm_fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFFFF)]);
            pc++;
            break;;

        case DOP_PUSH_3:
            dst_fiber_push3(dst_vm_fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
            pc++;
            break;

        case DOP_PUSH_ARRAY:
            {
                uint32_t count;
                const DstValue *array;
                if (dst_seq_view(stack[oparg(1, 0xFFFFFF)], &array, &count)) {
                    dst_fiber_pushn(dst_vm_fiber, array, count);
                } else {
                    vm_throw("expected array or tuple");
                }
                pc++;
                break;
            }

        case DOP_CALL:
        {
            DstValue callee = stack[oparg(2, 0xFFFF)];
            if (callee.type == DST_FUNCTION) {
                func = callee.as.function;
                dst_fiber_funcframe(dst_vm_fiber, func);
                stack = dst_vm_fiber->data + dst_vm_fiber->frame;
                pc = func->def->bytecode;
                break;
            } else if (callee.type == DST_CFUNCTION) {
                dst_fiber_cframe(dst_vm_fiber);
                dst_vm_fiber->ret.type = DST_NIL;
                if (callee.as.cfunction(
                        dst_vm_fiber->data + dst_vm_fiber->frame,
                        dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                    goto vm_error;
                } else {
                    goto vm_return_cfunc;
                }
            } else {
                vm_throw("cannot call non-function type");
            }
            break;
        }

        case DOP_TAILCALL:
        {
            DstValue callee = stack[oparg(2, 0xFFFF)];
            if (callee.type == DST_FUNCTION) {
                func = callee.as.function;
                dst_fiber_funcframe_tail(dst_vm_fiber, func);
                stack = dst_vm_fiber->data + dst_vm_fiber->frame;
                pc = func->def->bytecode;
                break;
            } else if (callee.type == DST_CFUNCTION) {
                dst_fiber_cframe_tail(dst_vm_fiber);
                dst_vm_fiber->ret.type = DST_NIL;
                if (callee.as.cfunction(
                            dst_vm_fiber->data + dst_vm_fiber->frame, 
                            dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                    goto vm_error;
                } else {
                    goto vm_return_cfunc;
                }
            } else {
                vm_throw("expected function");
            }
            break;
        }

        case DOP_SYSCALL:
            {
                DstCFunction f = dst_vm_syscalls[oparg(2, 0xFF)];
                vm_assert(NULL != f, "invalid syscall");
                dst_fiber_cframe(dst_vm_fiber);
                dst_vm_fiber->ret.type = DST_NIL;
                if (f(dst_vm_fiber->data + dst_vm_fiber->frame,
                            dst_vm_fiber->frametop - dst_vm_fiber->frame)) {
                    goto vm_error;
                } else {
                    goto vm_return_cfunc;
                }
                continue;
            }

        case DOP_LOAD_SYSCALL:
            {
                DstCFunction f = dst_vm_syscalls[oparg(2, 0xFF)];
                vm_assert(NULL != f, "invalid syscall");
                stack[oparg(1, 0xFF)] = dst_wrap_cfunction(f);
                pc++;
                continue;
            }

        case DOP_TRANSFER:
        {
            DstFiber *nextfiber;
            DstStackFrame *frame = dst_stack_frame(stack);
            DstValue temp = stack[oparg(2, 0xFF)];
            DstValue retvalue = stack[oparg(3, 0xFF)];
            vm_assert(temp.type == DST_FIBER ||
                      temp.type == DST_NIL, "expected fiber");
            nextfiber = temp.type == DST_FIBER
                ? temp.as.fiber
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
            continue;
        }

        case DOP_PUT:
            {
                const char *err = dst_try_put(
                        stack[oparg(1, 0xFF)],
                        stack[oparg(2, 0xFF)],
                        stack[oparg(3, 0xFF)]);
                if (NULL != err) {
                    vm_throw(err);
                }
                ++pc;
            }
            continue;

        case DOP_PUT_INDEX:
            dst_setindex(
                stack[oparg(1, 0xFF)],
                stack[oparg(3, 0xFF)],
                oparg(3, 0xFF));
            ++pc;
            continue;

        case DOP_GET:
            {
                const char *err = dst_try_get(
                        stack[oparg(2, 0xFF)],
                        stack[oparg(3, 0xFF)],
                        stack + oparg(1, 0xFF));
                if (NULL != err) {
                    vm_throw(err);
                }
                ++pc;
            }
            continue;

        case DOP_GET_INDEX:
            stack[oparg(1, 0xFF)] = dst_getindex(
                stack[oparg(2, 0xFF)],
                oparg(3, 0xFF));
            ++pc;
            continue;

        /* Return from c function. Simpler than retuning from dst function */
        vm_return_cfunc:
        {
            DstValue ret = dst_vm_fiber->ret;
            dst_fiber_popframe(dst_vm_fiber);
            if (dst_update_fiber())
                return 0;
            stack[oparg(1, 0xFF)] = ret;
            pc++;
            continue;
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
            continue;
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
            continue;
        }
    
        } /* end switch */

        /* Check for collection every cycle. If the instruction definitely does
         * not allocate memory, it can use continue instead of break to
         * skip this check */
        dst_maybe_collect();

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
    if (callee.type == DST_CFUNCTION) {
        dst_vm_fiber->ret.type = DST_NIL;
        dst_fiber_cframe(dst_vm_fiber);
        return callee.as.cfunction(dst_vm_fiber->data + dst_vm_fiber->frame, 0);
    } else if (callee.type == DST_FUNCTION) {
        dst_fiber_funcframe(dst_vm_fiber, callee.as.function);
        return dst_continue();
    }
    dst_vm_fiber->ret = dst_wrap_string(dst_cstring("expected function"));
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
    dst_vm_memory_interval = 0;

    uint32_t initialCacheCapacity = 1024;
    /* Set up the cache */
    dst_vm_cache = calloc(1, initialCacheCapacity * sizeof(DstValue));
    if (NULL == dst_vm_cache) {
        return 1;
    }
    dst_vm_cache_capacity = dst_vm_cache == NULL ? 0 : initialCacheCapacity;
    dst_vm_cache_count = 0;
    dst_vm_cache_deleted = 0;
    /* Set thread */
    dst_vm_fiber = NULL;
    return 0;
}

/* Clear all memory associated with the VM */
void dst_deinit() {
    dst_clear_memory();
    dst_vm_fiber = NULL;
    /* Deinit the cache */
    free(dst_vm_cache);
    dst_vm_cache = NULL;
    dst_vm_cache_count = 0;
    dst_vm_cache_capacity = 0;
    dst_vm_cache_deleted = 0;
}

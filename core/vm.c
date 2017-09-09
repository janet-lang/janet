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

#include "internal.h"

static const char DST_NO_UPVALUE[] = "no upvalue";
static const char DST_EXPECTED_FUNCTION[] = "expected function";

/* Start running the VM from where it left off. */
int dst_continue(Dst *vm) {
    /* VM state */
    DstValue *stack;
    uint16_t *pc;

    /* Some temporary values */
    DstValue temp, v1, v2;

#define dst_exit(vm, r) return ((vm)->ret = (r), DST_RETURN_OK)
#define dst_error(vm, e) do { (vm)->ret = dst_string_cv((vm), (e)); goto vm_error; } while (0)
#define dst_assert(vm, cond, e) do {if (!(cond)){dst_error((vm), (e));}} while (0)

    /* Intialize local state */
    vm->thread->status = DST_THREAD_ALIVE;
    stack = dst_thread_stack(vm->thread);
    pc = dst_frame_pc(stack);

    /* Main interpreter loop */
    for (;;) {

        switch (*pc) {

        default:
            dst_error(vm, "unknown opcode");
            break;

        case DST_OP_FLS: /* Load False */
            temp.type = DST_BOOLEAN;
            temp.data.boolean = 0;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case DST_OP_TRU: /* Load True */
            temp.type = DST_BOOLEAN;
            temp.data.boolean = 1;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case DST_OP_NIL: /* Load Nil */
            temp.type = DST_NIL;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case DST_OP_I16: /* Load Small Integer */
            temp.type = DST_INTEGER;
            temp.data.integer = ((int16_t *)(pc))[2];
            stack[pc[1]] = temp;
            pc += 3;
            continue;

        case DST_OP_UPV: /* Load Up Value */
        case DST_OP_SUV: /* Set Up Value */
            {
                DstValue *upv;
                DstFunction *fn;
                DstFuncEnv *env;
                uint16_t level = pc[2];
                temp = dst_frame_callee(stack);
                dst_assert(vm, temp.type == DST_FUNCTION, DST_EXPECTED_FUNCTION);
                fn = temp.data.function;
                if (level == 0)
                    upv = stack + pc[3];
                else {
                    while (fn && --level)
                        fn = fn->parent;
                    dst_assert(vm, fn, DST_NO_UPVALUE);
                    env = fn->env;
                    if (env->thread)
                        upv = env->thread->data + env->stackOffset + pc[3];
                    else
                        upv = env->values + pc[3];
                }
                if (pc[0] == DST_OP_UPV) {
                    stack[pc[1]] = *upv;
                } else {
                    *upv = stack[pc[1]];
                }
                pc += 4;
            }
            continue;

        case DST_OP_JIF: /* Jump If */
            if (dst_value_truthy(stack[pc[1]])) {
                pc += 4;
            } else {
                pc += *((int32_t *)(pc + 2));
            }
            continue;

        case DST_OP_JMP: /* Jump */
            pc += *((int32_t *)(pc + 1));
            continue;

        case DST_OP_CST: /* Load constant value */
            v1 = dst_frame_callee(stack);
            dst_assert(vm, v1.type == DST_FUNCTION, DST_EXPECTED_FUNCTION);
            if (pc[2] > v1.data.function->def->literalsLen)
                dst_error(vm, DST_NO_UPVALUE);
            stack[pc[1]] = v1.data.function->def->literals[pc[2]];
            pc += 3;
            continue;

        case DST_OP_I32: /* Load 32 bit integer */
            temp.type = DST_INTEGER;
            temp.data.integer = *((int32_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 4;
            continue;

        case DST_OP_I64: /* Load 64 bit integer */
            temp.type = DST_INTEGER;
            temp.data.integer = (DstInteger) *((int64_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            continue;

        case DST_OP_F64: /* Load 64 bit float */
            temp.type = DST_REAL;
            temp.data.real = (DstReal) *((double *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            continue;

        case DST_OP_MOV: /* Move Values */
            stack[pc[1]] = stack[pc[2]];
            pc += 3;
            continue;

        case DST_OP_CLN: /* Create closure from constant FuncDef */
            {
                DstFunction *fn;
                v1 = dst_frame_callee(stack);
                temp = v1.data.function->def->literals[pc[2]];
                if (temp.type != DST_FUNCDEF)
                    dst_error(vm, "cannot create closure from non-funcdef");
                fn = dst_alloc(vm, sizeof(DstFunction));
                fn->def = temp.data.def;
                if (temp.data.def->flags & DST_FUNCDEF_FLAG_NEEDSPARENT)
                    fn->parent = v1.data.function;
                else
                    fn->parent = NULL;
                if (v1.type != DST_FUNCTION)
                    dst_error(vm, DST_EXPECTED_FUNCTION);
                if (dst_frame_env(stack) == NULL && (fn->def->flags & DST_FUNCDEF_FLAG_NEEDSENV)) {
                    dst_frame_env(stack) = dst_alloc(vm, sizeof(DstFuncEnv));
                    dst_frame_env(stack)->thread = vm->thread;
                    dst_frame_env(stack)->stackOffset = vm->thread->count;
                    dst_frame_env(stack)->values = NULL;
                }
                if (pc[2] > v1.data.function->def->literalsLen)
                    dst_error(vm, DST_NO_UPVALUE);
                if (fn->def->flags & DST_FUNCDEF_FLAG_NEEDSENV)
                    fn->env = dst_frame_env(stack);
                else
                    fn->env = NULL;
                temp.type = DST_FUNCTION;
                temp.data.function = fn;
                stack[pc[1]] = temp;
                pc += 3;
            }
            break;

        case DST_OP_RTN: /* Return nil */
            temp.type = DST_NIL;
            goto vm_return;

        case DST_OP_RET: /* Return */
            temp = stack[pc[1]];
            goto vm_return;

        case DST_OP_PSK: /* Push stack */
            {
                uint16_t arity = pc[1];
                uint16_t i;
                uint16_t newBase = dst_frame_size(stack) + DST_FRAME_SIZE;
                dst_frame_args(stack) = newBase;
                dst_thread_ensure_extra(vm, vm->thread, DST_FRAME_SIZE + arity);
                stack = dst_thread_stack(vm->thread);
                dst_frame_size(stack) += DST_FRAME_SIZE + arity;
                /* Nil stuff */
                for (i = 0; i < DST_FRAME_SIZE; ++i)
                    stack[newBase + i - DST_FRAME_SIZE].type = DST_NIL;
                /* Write arguments */
                for (i = 0; i < arity; ++i)
                    stack[newBase + i] = stack[pc[2 + i]];
                pc += 2 + arity;
            }
            break;

        case DST_OP_PAR: /* Push array or tuple */
            {
                uint32_t count, i, oldsize;
                const DstValue *data;
                temp = stack[pc[1]];
                if (temp.type == DST_TUPLE) {
                    count = dst_tuple_length(temp.data.tuple);
                    data = temp.data.tuple;
                } else if (temp.type == DST_ARRAY){
                    count = temp.data.array->count;
                    data = temp.data.array->data;
                } else {
                    dst_error(vm, "expected array or tuple");
                }
                oldsize = dst_frame_size(stack);
                dst_thread_pushnil(vm, vm->thread, count);
                stack = dst_thread_stack(vm->thread);
                for (i = 0; i < count; ++i)
                    stack[oldsize + i] = data[i];
                /*dst_frame_size(stack) += count;*/
                pc += 2;
            }
            break;

        case DST_OP_CAL: /* Call */
            {
                uint16_t newStackIndex = dst_frame_args(stack);
                uint16_t size = dst_frame_size(stack);
                temp = stack[pc[1]];
                dst_frame_size(stack) = newStackIndex - DST_FRAME_SIZE;
                dst_frame_ret(stack) = pc[2];
                dst_frame_pc(stack) = pc + 3;
                if (newStackIndex < DST_FRAME_SIZE)
                    dst_error(vm, "invalid call instruction");
                vm->thread->count += newStackIndex;
                stack = dst_thread_stack(vm->thread);
                dst_frame_size(stack) = size - newStackIndex;
                dst_frame_prevsize(stack) = newStackIndex - DST_FRAME_SIZE;
                dst_frame_callee(stack) = temp;
            }
            goto common_function_call;

        case DST_OP_TCL: /* Tail call */
            {
                uint16_t newStackIndex = dst_frame_args(stack);
                uint16_t size = dst_frame_size(stack);
                uint16_t i;
                temp = stack[pc[1]];
                /* Check for closures */
                if (dst_frame_env(stack)) {
                    DstFuncEnv *env = dst_frame_env(stack);
                    env->thread = NULL;
                    env->stackOffset = size;
                    env->values = dst_alloc(vm, sizeof(DstValue) * size);
                    dst_memcpy(env->values, stack, sizeof(DstValue) * size);
                }
                if (newStackIndex)
                    for (i = 0; i < size - newStackIndex; ++i)
                        stack[i] = stack[newStackIndex + i];
                dst_frame_size(stack) = size - newStackIndex;
                dst_frame_callee(stack) = temp;
            }
            goto common_function_call;

        /* Code common to all function calls */
        common_function_call:
            dst_frame_args(stack) = 0;
            dst_frame_env(stack) = NULL;
            dst_thread_endframe(vm, vm->thread);
            stack = vm->thread->data + vm->thread->count;
            temp = dst_frame_callee(stack);
            if (temp.type == DST_FUNCTION) {
                pc = temp.data.function->def->byteCode;
            } else if (temp.type == DST_CFUNCTION) {
                int status;
                vm->ret.type = DST_NIL;
                status = temp.data.cfunction(vm);
                if (status) {
                    goto vm_error;
                } else {
                    temp = vm->ret;
                    goto vm_return;
                }
            } else {
                dst_error(vm, DST_EXPECTED_FUNCTION);
            }
            break;

        case DST_OP_ARR: /* Array literal */
            {
                uint32_t i;
                uint32_t arrayLen = pc[2];
                DstArray *array = dst_make_array(vm, arrayLen);
                array->count = arrayLen;
                for (i = 0; i < arrayLen; ++i)
                    array->data[i] = stack[pc[3 + i]];
                temp.type = DST_ARRAY;
                temp.data.array = array;
                stack[pc[1]] = temp;
                pc += 3 + arrayLen;
            }
            break;

        case DST_OP_DIC: /* Table literal */
            {
                uint32_t i = 3;
                uint32_t kvs = pc[2];
                DstTable *t = dst_make_table(vm, 2 * kvs);
                kvs = kvs + 3;
                while (i < kvs) {
                    v1 = stack[pc[i++]];
                    v2 = stack[pc[i++]];
                    dst_table_put(vm, t, v1, v2);
                }
                temp.type = DST_TABLE;
                temp.data.table = t;
                stack[pc[1]] = temp;
                pc += kvs;
            }
            break;

        case DST_OP_TUP: /* Tuple literal */
            {
                uint32_t i;
                uint32_t len = pc[2];
                DstValue *tuple = dst_tuple_begin(vm, len);
                for (i = 0; i < len; ++i)
                    tuple[i] = stack[pc[3 + i]];
                temp.type = DST_TUPLE;
                temp.data.tuple = dst_tuple_end(vm, tuple);
                stack[pc[1]] = temp;
                pc += 3 + len;
            }
            break;

        case DST_OP_TRN: /* Transfer */
            temp = stack[pc[2]]; /* The thread */
            v1 = stack[pc[3]]; /* The value to pass in */
            if (temp.type != DST_THREAD && temp.type != DST_NIL)
                dst_error(vm, "expected thread");
            if (temp.type == DST_NIL && vm->thread->parent) {
                temp.type = DST_THREAD;
                temp.data.thread = vm->thread->parent;
            }
            if (temp.type == DST_THREAD) {
                if (temp.data.thread->status != DST_THREAD_PENDING)
                    dst_error(vm, "can only enter pending thread");
            }
            dst_frame_ret(stack) = pc[1];
            vm->thread->status = DST_THREAD_PENDING;
            dst_frame_pc(stack) = pc + 4;
            if (temp.type == DST_NIL) {
                vm->ret = v1;
                return 0;
            }
            temp.data.thread->status = DST_THREAD_ALIVE;
            vm->thread = temp.data.thread;
            stack = dst_thread_stack(temp.data.thread);
            if (dst_frame_callee(stack).type != DST_FUNCTION)
                goto vm_return;
            stack[dst_frame_ret(stack)] = v1;
            pc = dst_frame_pc(stack);
            continue;

        /* Handle returning from stack frame. Expect return value in temp. */
        vm_return:
            stack = dst_thread_popframe(vm, vm->thread);
            while (vm->thread->count < DST_FRAME_SIZE ||
                vm->thread->status == DST_THREAD_DEAD ||
                vm->thread->status == DST_THREAD_ERROR) {
                vm->thread->status = DST_THREAD_DEAD;
                if (vm->thread->parent) {
                    vm->thread = vm->thread->parent;
                    if (vm->thread->status == DST_THREAD_ALIVE) {
                        /* If the parent thread is still alive,
                           we are inside a cfunction */
                        vm->ret = temp;
                        return 0;
                    }
                    stack = vm->thread->data + vm->thread->count;
                } else {
                    vm->ret = temp;
                    return 0;
                }
            }
            vm->thread->status = DST_THREAD_ALIVE;
            pc = dst_frame_pc(stack);
            stack[dst_frame_ret(stack)] = temp;
            continue;

        /* Handle errors from c functions and vm opcodes */
        vm_error:
            vm->thread->status = DST_THREAD_ERROR;
            while (vm->thread->count < DST_FRAME_SIZE ||
                vm->thread->status == DST_THREAD_DEAD ||
                vm->thread->status == DST_THREAD_ERROR) {
                if (vm->thread->parent == NULL)
                    return 1;
                vm->thread = vm->thread->parent;
                if (vm->thread->status == DST_THREAD_ALIVE) {
                    /* If the parent thread is still alive,
                       we are inside a cfunction */
                    return 1;
                }
            }
            vm->thread->status = DST_THREAD_ALIVE;
            stack = vm->thread->data + vm->thread->count;
            stack[dst_frame_ret(stack)] = vm->ret;
            pc = dst_frame_pc(stack);
            continue;

        } /* end switch */

        /* Check for collection every cycle. If the instruction definitely does
         * not allocate memory, it can use continue instead of break to
         * skip this check */
        dst_maybe_collect(vm);

    } /* end for */

}

/* Run the vm with a given function. This function is
 * called to start the vm. */
int dst_run(Dst *vm, DstValue callee) {
    int result;
    if (vm->thread &&
        (vm->thread->status == DST_THREAD_DEAD ||
         vm->thread->status == DST_THREAD_ALIVE)) {
        /* Reuse old thread */
        dst_thread_reset(vm, vm->thread, callee);
    } else {
        /* Create new thread */
        vm->thread = dst_thread(vm, callee, 64);
    }
    if (callee.type == DST_CFUNCTION) {
        vm->ret.type = DST_NIL;
        result = callee.data.cfunction(vm);
    } else if (callee.type == DST_FUNCTION) {
        result = dst_continue(vm);
    } else {
        vm->ret = dst_string_cv(vm, "expected function");
        return 1;
    }
    /* Handle yields */
    while (!result && vm->thread->status == DST_THREAD_PENDING) {
        /* Send back in the value yielded - TODO - do something useful with this */
        DstValue *stack = dst_thread_stack(vm->thread);
        stack[dst_frame_ret(stack)] = vm->ret;
        /* Resume */
        result = dst_continue(vm);
    }
    return result;
}

/* Setup functions */
Dst *dst_init() {
    Dst *vm = dst_raw_alloc(sizeof(Dst));
    vm->ret.type = DST_NIL;
    /* Garbage collection */
    vm->blocks = NULL;
    vm->nextCollection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * horrible for performance, but helps ensure
     * there are no memory bugs during dev */
    vm->memoryInterval = 0;
    vm->black = 0;
    /* Set up the cache */
    vm->cache = dst_raw_calloc(1, 128 * sizeof(DstValue));
    vm->cache_capacity = vm->cache == NULL ? 0 : 128;
    vm->cache_count = 0;
    vm->cache_deleted = 0;
    /* Set up global env */
    vm->modules = dst_make_table(vm, 10);
    vm->registry = dst_make_table(vm, 10);
    vm->env = dst_make_table(vm, 10);
    /* Set thread */
    vm->thread = dst_thread(vm, vm->ret, 100);
    dst_thread_pushnil(vm, vm->thread, 10);
    return vm;
}

/* Clear all memory associated with the VM */
void dst_deinit(Dst *vm) {
    dst_clear_memory(vm);
    vm->thread = NULL;
    vm->modules = NULL;
    vm->registry = NULL;
    vm->ret.type = DST_NIL;
    /* Deinit the cache */
    dst_raw_free(vm->cache);
    vm->cache = NULL;
    vm->cache_count = 0;
    vm->cache_capacity = 0;
    vm->cache_deleted = 0;
    /* Free the vm */
    dst_raw_free(vm);
}

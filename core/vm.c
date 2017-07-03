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

#include <gst/gst.h>

static const char GST_NO_UPVALUE[] = "no upvalue";
static const char GST_EXPECTED_FUNCTION[] = "expected function";

/* Start running the VM from where it left off. */
int gst_continue(Gst *vm) {
    /* VM state */
    GstValue *stack;
    uint16_t *pc;

    /* Some temporary values */
    GstValue temp, v1, v2;

#define gst_exit(vm, r) return ((vm)->ret = (r), GST_RETURN_OK)
#define gst_error(vm, e) do { (vm)->ret = gst_string_cv((vm), (e)); goto vm_error; } while (0)
#define gst_crash(vm, e) return ((vm)->crash = (e), GST_RETURN_CRASH)
#define gst_assert(vm, cond, e) do {if (!(cond)){gst_error((vm), (e));}} while (0)

    /* Intialize local state */
    vm->thread->status = GST_THREAD_ALIVE;
    stack = gst_thread_stack(vm->thread);
    pc = gst_frame_pc(stack);

    /* Main interpreter loop */
    for (;;) {

        switch (*pc) {

        default:
            gst_error(vm, "unknown opcode");
            break;

        case GST_OP_FLS: /* Load False */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 0;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_TRU: /* Load True */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 1;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_NIL: /* Load Nil */
            temp.type = GST_NIL;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_I16: /* Load Small Integer */
            temp.type = GST_INTEGER;
            temp.data.integer = ((int16_t *)(pc))[2];
            stack[pc[1]] = temp;
            pc += 3;
            continue;

        case GST_OP_UPV: /* Load Up Value */
        case GST_OP_SUV: /* Set Up Value */
            {
                GstValue *upv;
                GstFunction *fn;
                GstFuncEnv *env;
                uint16_t level = pc[2];
                temp = gst_frame_callee(stack);
                gst_assert(vm, temp.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
                fn = temp.data.function;
                if (level == 0)
                    upv = stack + pc[3];
                else {
                    while (fn && --level)
                        fn = fn->parent;
                    gst_assert(vm, fn, GST_NO_UPVALUE);
                    env = fn->env;
                    if (env->thread)
                        upv = env->thread->data + env->stackOffset + pc[3];
                    else
                        upv = env->values + pc[3];
                }
                if (pc[0] == GST_OP_UPV) {
                    stack[pc[1]] = *upv;
                } else {
                    *upv = stack[pc[1]];
                }
                pc += 4;
            }
            continue;

        case GST_OP_JIF: /* Jump If */
            if (gst_truthy(stack[pc[1]])) {
                pc += 4;
            } else {
                pc += *((int32_t *)(pc + 2));
            }
            continue;

        case GST_OP_JMP: /* Jump */
            pc += *((int32_t *)(pc + 1));
            continue;

        case GST_OP_CST: /* Load constant value */
            v1 = gst_frame_callee(stack);
            gst_assert(vm, v1.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            if (pc[2] > v1.data.function->def->literalsLen)
                gst_error(vm, GST_NO_UPVALUE);
            stack[pc[1]] = v1.data.function->def->literals[pc[2]];
            pc += 3;
            continue;

        case GST_OP_I32: /* Load 32 bit integer */
            temp.type = GST_INTEGER;
            temp.data.integer = *((int32_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 4;
            continue;

        case GST_OP_I64: /* Load 64 bit integer */
            temp.type = GST_INTEGER;
            temp.data.integer = (GstInteger) *((int64_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            continue;

        case GST_OP_F64: /* Load 64 bit float */
            temp.type = GST_REAL;
            temp.data.real = (GstReal) *((double *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            continue;

        case GST_OP_MOV: /* Move Values */
            stack[pc[1]] = stack[pc[2]];
            pc += 3;
            continue;

        case GST_OP_CLN: /* Create closure from constant FuncDef */
            {
                GstFunction *fn;
                v1 = gst_frame_callee(stack);
                temp = v1.data.function->def->literals[pc[2]];
                if (temp.type != GST_FUNCDEF)
                    gst_error(vm, "cannot create closure from non-funcdef");
                fn = gst_alloc(vm, sizeof(GstFunction));
                fn->def = temp.data.def;
                if (temp.data.def->flags & GST_FUNCDEF_FLAG_NEEDSPARENT)
                    fn->parent = v1.data.function;
                else
                    fn->parent = NULL;
                if (v1.type != GST_FUNCTION)
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                if (gst_frame_env(stack) == NULL && (fn->def->flags & GST_FUNCDEF_FLAG_NEEDSENV)) {
                    gst_frame_env(stack) = gst_alloc(vm, sizeof(GstFuncEnv));
                    gst_frame_env(stack)->thread = vm->thread;
                    gst_frame_env(stack)->stackOffset = vm->thread->count;
                    gst_frame_env(stack)->values = NULL;
                }
                if (pc[2] > v1.data.function->def->literalsLen)
                    gst_error(vm, GST_NO_UPVALUE);
                if (fn->def->flags & GST_FUNCDEF_FLAG_NEEDSENV)
                    fn->env = gst_frame_env(stack);
                else
                    fn->env = NULL;
                temp.type = GST_FUNCTION;
                temp.data.function = fn;
                stack[pc[1]] = temp;
                pc += 3;
            }
            break;

        case GST_OP_RTN: /* Return nil */
            temp.type = GST_NIL;
            goto vm_return;

        case GST_OP_RET: /* Return */
            temp = stack[pc[1]];
            goto vm_return;

        case GST_OP_PSK: /* Push stack */
            {
                uint16_t arity = pc[1];
                uint16_t i;
                uint16_t newBase = gst_frame_size(stack) + GST_FRAME_SIZE;
                gst_frame_args(stack) = newBase;
                gst_thread_ensure_extra(vm, vm->thread, GST_FRAME_SIZE + arity);
                stack = gst_thread_stack(vm->thread);
                gst_frame_size(stack) += GST_FRAME_SIZE + arity;
                /* Nil stuff */
                for (i = 0; i < GST_FRAME_SIZE; ++i)
                    stack[newBase + i - GST_FRAME_SIZE].type = GST_NIL;
                /* Write arguments */
                for (i = 0; i < arity; ++i)
                    stack[newBase + i] = stack[pc[2 + i]];
                pc += 2 + arity;
            }
            break;

        case GST_OP_PAR: /* Push array or tuple */
            {
                uint32_t count, i, oldsize;
                const GstValue *data;
                temp = stack[pc[1]];
                if (temp.type == GST_TUPLE) {
                    count = gst_tuple_length(temp.data.tuple);
                    data = temp.data.tuple;
                } else if (temp.type == GST_ARRAY){
                    count = temp.data.array->count;
                    data = temp.data.array->data;
                } else {
                    gst_error(vm, "expected array or tuple");
                }
                oldsize = gst_frame_size(stack);
                gst_thread_pushnil(vm, vm->thread, count);
                stack = gst_thread_stack(vm->thread);
                for (i = 0; i < count; ++i)
                    stack[oldsize + i] = data[i];
                /*gst_frame_size(stack) += count;*/
                pc += 2;
            }
            break;

        case GST_OP_CAL: /* Call */
            {
                uint16_t newStackIndex = gst_frame_args(stack);
                uint16_t size = gst_frame_size(stack);
                temp = stack[pc[1]];
                gst_frame_size(stack) = newStackIndex - GST_FRAME_SIZE;
                gst_frame_ret(stack) = pc[2];
                gst_frame_pc(stack) = pc + 3;
                if (newStackIndex < GST_FRAME_SIZE)
                    gst_error(vm, "invalid call instruction");
                vm->thread->count += newStackIndex;
                stack = gst_thread_stack(vm->thread);
                gst_frame_size(stack) = size - newStackIndex;
                gst_frame_prevsize(stack) = newStackIndex - GST_FRAME_SIZE;
                gst_frame_callee(stack) = temp;
            }
            goto common_function_call;

        case GST_OP_TCL: /* Tail call */
            {
                uint16_t newStackIndex = gst_frame_args(stack);
                uint16_t size = gst_frame_size(stack);
                uint16_t i;
                temp = stack[pc[1]];
                /* Check for closures */
                if (gst_frame_env(stack)) {
                    GstFuncEnv *env = gst_frame_env(stack);
                    env->thread = NULL;
                    env->stackOffset = size;
                    env->values = gst_alloc(vm, sizeof(GstValue) * size);
                    gst_memcpy(env->values, stack, sizeof(GstValue) * size);
                }
                if (newStackIndex)
                    for (i = 0; i < size - newStackIndex; ++i)
                        stack[i] = stack[newStackIndex + i];
                gst_frame_size(stack) = size - newStackIndex;
                gst_frame_callee(stack) = temp;
            }
            goto common_function_call;

        /* Code common to all function calls */
        common_function_call:
            gst_frame_args(stack) = 0;
            gst_frame_env(stack) = NULL;
            gst_thread_endframe(vm, vm->thread);
            stack = vm->thread->data + vm->thread->count;
            temp = gst_frame_callee(stack);
            if (temp.type == GST_FUNCTION) {
                pc = temp.data.function->def->byteCode;
            } else if (temp.type == GST_CFUNCTION) {
                int status;
                vm->ret.type = GST_NIL;
                status = temp.data.cfunction(vm);
                if (status == GST_RETURN_OK) {
                    temp = vm->ret;
                    goto vm_return;
                } else {
                    goto vm_error;
                }
            } else {
                gst_error(vm, GST_EXPECTED_FUNCTION);
            }
            break;

        case GST_OP_ARR: /* Array literal */
            {
                uint32_t i;
                uint32_t arrayLen = pc[2];
                GstArray *array = gst_array(vm, arrayLen);
                array->count = arrayLen;
                for (i = 0; i < arrayLen; ++i)
                    array->data[i] = stack[pc[3 + i]];
                temp.type = GST_ARRAY;
                temp.data.array = array;
                stack[pc[1]] = temp;
                pc += 3 + arrayLen;
            }
            break;

        case GST_OP_DIC: /* Table literal */
            {
                uint32_t i = 3;
                uint32_t kvs = pc[2];
                GstTable *t = gst_table(vm, 2 * kvs);
                kvs = kvs + 3;
                while (i < kvs) {
                    v1 = stack[pc[i++]];
                    v2 = stack[pc[i++]];
                    gst_table_put(vm, t, v1, v2);
                }
                temp.type = GST_TABLE;
                temp.data.table = t;
                stack[pc[1]] = temp;
                pc += kvs;
            }
            break;

        case GST_OP_TUP: /* Tuple literal */
            {
                uint32_t i;
                uint32_t len = pc[2];
                GstValue *tuple = gst_tuple_begin(vm, len);
                for (i = 0; i < len; ++i)
                    tuple[i] = stack[pc[3 + i]];
                temp.type = GST_TUPLE;
                temp.data.tuple = gst_tuple_end(vm, tuple);
                stack[pc[1]] = temp;
                pc += 3 + len;
            }
            break;

        case GST_OP_TRN: /* Transfer */
            temp = stack[pc[2]]; /* The thread */
            v1 = stack[pc[3]]; /* The value to pass in */
            if (temp.type != GST_THREAD && temp.type != GST_NIL)
                gst_error(vm, "expected thread");
            if (temp.type == GST_NIL && vm->thread->parent) {
                temp = gst_wrap_thread(vm->thread->parent);
            }
            if (temp.type == GST_THREAD) {
                if (temp.data.thread->status == GST_THREAD_DEAD ||
                    temp.data.thread->status == GST_THREAD_ERROR)
                    gst_error(vm, "cannot enter dead thread");
            }
            gst_frame_ret(stack) = pc[1];
            vm->thread->status = GST_THREAD_PENDING;
            gst_frame_pc(stack) = pc + 4;
            if (temp.type == GST_NIL) {
                vm->ret = v1;
                return GST_RETURN_OK;
            }
            temp.data.thread->status = GST_THREAD_ALIVE;
            vm->thread = temp.data.thread;
            stack = gst_thread_stack(temp.data.thread);
            if (gst_frame_callee(stack).type != GST_FUNCTION)
                goto vm_return;
            stack[gst_frame_ret(stack)] = v1;
            pc = gst_frame_pc(stack);
            continue;

        /* Handle returning from stack frame. Expect return value in temp. */
        vm_return:
            stack = gst_thread_popframe(vm, vm->thread);
            while (vm->thread->count < GST_FRAME_SIZE ||
                vm->thread->status == GST_THREAD_DEAD ||
                vm->thread->status == GST_THREAD_ERROR) {
                vm->thread->status = GST_THREAD_DEAD;
                if (vm->thread->parent) {
                    vm->thread = vm->thread->parent;
                    stack = vm->thread->data + vm->thread->count;
                } else {
                    vm->ret = temp;
                    return GST_RETURN_OK;
                }
            }
            vm->thread->status = GST_THREAD_ALIVE;
            pc = gst_frame_pc(stack);
            stack[gst_frame_ret(stack)] = temp;
            continue;

        /* Handle errors from c functions and vm opcodes */
        vm_error:
            vm->thread->status = GST_THREAD_ERROR;
            while (vm->thread->count < GST_FRAME_SIZE ||
                vm->thread->status == GST_THREAD_DEAD ||
                vm->thread->status == GST_THREAD_ERROR) {
                if (vm->thread->errorParent == NULL)
                    return GST_RETURN_ERROR;
                vm->thread = vm->thread->errorParent;
            }
            vm->thread->status = GST_THREAD_ALIVE;
            stack = vm->thread->data + vm->thread->count;
            stack[gst_frame_ret(stack)] = vm->ret;
            pc = gst_frame_pc(stack);
            continue;

        } /* end switch */

        /* Check for collection every cycle. If the instruction definitely does
         * not allocate memory, it can use continue instead of break to
         * skip this check */
        gst_maybe_collect(vm);

    } /* end for */

}

/* Run the vm with a given function. This function is
 * called to start the vm. */
int gst_run(Gst *vm, GstValue callee) {
    int result;
    if (vm->thread &&
        (vm->thread->status == GST_THREAD_DEAD ||
         vm->thread->status == GST_THREAD_ALIVE)) {
        /* Reuse old thread */
        gst_thread_reset(vm, vm->thread, callee);
    } else {
        /* Create new thread */
        vm->thread = gst_thread(vm, callee, 64);
        if (vm->thread == NULL)
            return GST_RETURN_CRASH;
    }
    if (callee.type == GST_CFUNCTION) {
        vm->ret.type = GST_NIL;
        result = callee.data.cfunction(vm);
    } else if (callee.type == GST_FUNCTION) {
        result = gst_continue(vm);
    } else {
        return GST_RETURN_CRASH;
    }
    /* Handle yields */
    while (result == GST_RETURN_OK && vm->thread->status == GST_THREAD_PENDING) {
        /* Send back in the value yielded - TODO - do something useful with this */
        GstValue *stack = gst_thread_stack(vm->thread);
        stack[gst_frame_ret(stack)] = vm->ret;
        /* Resume */
        result = gst_continue(vm);
    }
    return result;
}

/* Get an argument from the stack */
GstValue gst_arg(Gst *vm, uint32_t index) {
    GstValue *stack = gst_thread_stack(vm->thread);
    uint32_t frameSize = gst_frame_size(stack);
    if (frameSize <= index) {
        GstValue ret;
        ret.type = GST_NIL;
        return ret;
    }
    return stack[index];
}

/* Put a value on the stack */
void gst_set_arg(Gst* vm, uint32_t index, GstValue x) {
    GstValue *stack = gst_thread_stack(vm->thread);
    uint32_t frameSize = gst_frame_size(stack);
    if (frameSize <= index) return;
    stack[index] = x;
}

/* Get the size of the VMStack */
uint32_t gst_count_args(Gst *vm) {
    GstValue *stack = gst_thread_stack(vm->thread);
    return gst_frame_size(stack);
}

/* Initialize the VM */
void gst_init(Gst *vm) {
    vm->ret.type = GST_NIL;
    vm->crash = NULL;
    /* Garbage collection */
    vm->blocks = NULL;
    vm->nextCollection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * horrible for performance, but helps ensure
     * there are no memory bugs during dev */
    vm->memoryInterval = 0;
    vm->black = 0;
    /* Add thread */
    vm->thread = NULL;
    /* Set up the cache */
    vm->cache = gst_raw_calloc(1, 128 * sizeof(GstValue));
    vm->cache_capacity = vm->cache == NULL ? 0 : 128;
    vm->cache_count = 0;
    vm->cache_deleted = 0;
    /* Set up global env */
    vm->modules = gst_table(vm, 10);
    vm->registry = gst_table(vm, 10);
    vm->env = gst_table(vm, 10);
}

/* Clear all memory associated with the VM */
void gst_deinit(Gst *vm) {
    gst_clear_memory(vm);
    vm->thread = NULL;
    vm->modules = NULL;
    vm->registry = NULL;
    vm->ret.type = GST_NIL;
    /* Deinit the cache */
    gst_raw_free(vm->cache);
    vm->cache = NULL;
    vm->cache_count = 0;
    vm->cache_capacity = 0;
    vm->cache_deleted = 0;
}

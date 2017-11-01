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
#include "wrap.h"
#include "gc.h"

/* Initialize a new thread */
DstThread *dst_thread(Dst *vm, DstValue callee, uint32_t capacity) {
    DstThread *thread = dst_alloc(vm, DST_MEMORY_THREAD, sizeof(DstThread));
    if (capacity < DST_FRAME_SIZE) capacity = DST_FRAME_SIZE;
    thread->capacity = capacity;
    DstValue *data = malloc(vm, sizeof(DstValue) * capacity);
    if (NULL == data) {
        DST_OUT_OF_MEMORY;
    }
    thread->data = data;
    return dst_thread_reset(vm, thread, callee);
}

/* Clear a thread (reset it) */
DstThread *dst_thread_reset(Dst *vm, DstThread *thread, DstValue callee) {
    DstValue *stack;
    thread->count = DST_FRAME_SIZE;
    thread->status = DST_THREAD_PENDING;
    stack = thread->data + DST_FRAME_SIZE;
    dst_frame_size(stack) = 0;
    dst_frame_prevsize(stack) = 0;
    dst_frame_ret(stack) = 0;
    dst_frame_args(stack) = 0;
    dst_frame_pc(stack) = NULL;
    dst_frame_env(stack) = NULL;
    dst_frame_callee(stack) = callee;
    dst_thread_endframe(vm, thread);
    thread->parent = NULL; /* Need to set parent manually */
    return thread;
}

/* Ensure that the thread has enough extra capacity */
void dst_thread_ensure_extra(Dst *vm, DstThread *thread, uint32_t extra) {
    DstValue *newData, *stack;
    uint32_t usedCapacity, neededCapacity, newCapacity;
    stack = thread->data + thread->count;
    usedCapacity = thread->count + dst_frame_size(stack) + DST_FRAME_SIZE;
    neededCapacity = usedCapacity + extra;
    if (thread->capacity >= neededCapacity) return;
    newCapacity = 2 * neededCapacity;

    newData = realloc(thread->data, sizeof(DstValue) * newCapacity);
    if (NULL == newData) {
        DST_OUT_OF_MEMORY;
    }

    thread->data = newData;
    thread->capacity = newCapacity;
}

/* Push a value on the current stack frame*/
void dst_thread_push(Dst *vm, DstThread *thread, DstValue x) {
    DstValue *stack;
    dst_thread_ensure_extra(vm, thread, 1);
    stack = thread->data + thread->count;
    stack[dst_frame_size(stack)++] = x;
}

/* Push n nils onto the stack */
void dst_thread_pushnil(Dst *vm, DstThread *thread, uint32_t n) {
    DstValue *stack, *current, *end;
    dst_thread_ensure_extra(vm, thread, n);
    stack = thread->data + thread->count;
    current = stack + dst_frame_size(stack);
    end = current + n;
    for (; current < end; ++current) {
        current->type = DST_NIL;
    }
    dst_frame_size(stack) += n;
}

/* Package up extra args after and including n into tuple at n. Used for
 * packing up varargs to variadic functions. */
void dst_thread_tuplepack(Dst *vm, DstThread *thread, uint32_t n) {
    DstValue *stack = thread->data + thread->count;
    uint32_t size = dst_frame_size(stack);
    if (n >= size) {
        /* Push one extra nil to ensure space for tuple */
        dst_thread_pushnil(vm, thread, n - size + 1);
        stack = thread->data + thread->count;
        stack[n].type = DST_TUPLE;
        stack[n].as.tuple = dst_tuple_end(vm, dst_tuple_begin(vm, 0));
        dst_frame_size(stack) = n + 1;
    } else {
        uint32_t i;
        DstValue *tuple = dst_tuple_begin(vm, size - n);
        for (i = n; i < size; ++i)
            tuple[i - n] = stack[i];
        stack[n].type = DST_TUPLE;
        stack[n].as.tuple = dst_tuple_end(vm, tuple);
    }
}

/* Push a stack frame to a thread, with space for arity arguments. Returns the new
 * stack. */
DstValue *dst_thread_beginframe(Dst *vm, DstThread *thread, DstValue callee, uint32_t arity) {
    uint32_t frameOffset;
    DstValue *oldStack, *newStack;

    /* Push the frame */
    dst_thread_ensure_extra(vm, thread, DST_FRAME_SIZE + arity + 4);
    oldStack = thread->data + thread->count;
    frameOffset = dst_frame_size(oldStack) + DST_FRAME_SIZE;
    newStack = oldStack + frameOffset;
    dst_frame_prevsize(newStack) = dst_frame_size(oldStack);
    dst_frame_env(newStack) = NULL;
    dst_frame_size(newStack) = 0;
    dst_frame_callee(newStack) = callee;
    thread->count += frameOffset;

    /* Ensure the extra space and initialize to nil */
    dst_thread_pushnil(vm, thread, arity);

    /* Return ok */
    return thread->data + thread->count;
}

/* After pushing arguments to a stack frame created with dst_thread_beginframe, call this
 * to finalize the frame before starting a function call. */
void dst_thread_endframe(Dst *vm, DstThread *thread) {
    DstValue *stack = thread->data + thread->count;
    DstValue callee = dst_frame_callee(stack);
    if (callee.type == DST_FUNCTION) {
        DstFunction *fn = callee.as.function;
        uint32_t locals = fn->def->locals;
        dst_frame_pc(stack) = fn->def->byteCode;
        if (fn->def->flags & DST_FUNCDEF_FLAG_VARARG) {
            uint32_t arity = fn->def->arity;
            dst_thread_tuplepack(vm, thread, arity);
        }
        if (dst_frame_size(stack) < locals) {
            dst_thread_pushnil(vm, thread, locals - dst_frame_size(stack));
        }
    }
}

/* Pop a stack frame from the thread. Returns the new stack frame, or
 * NULL if there are no more frames */
DstValue *dst_thread_popframe(Dst *vm, DstThread *thread) {
    DstValue *stack = thread->data + thread->count;
    uint32_t prevsize = dst_frame_prevsize(stack);
    DstValue *nextstack = stack - DST_FRAME_SIZE - prevsize;
    DstFuncEnv *env = dst_frame_env(stack);

    /* Check for closures */
    if (env != NULL) {
        uint32_t size = dst_frame_size(stack);
        env->thread = NULL;
        env->stackOffset = size;
        env->values = malloc(sizeof(DstValue) * size);
        memcpy(env->values, stack, sizeof(DstValue) * size);
    }

    /* Shrink stack */
    thread->count -= DST_FRAME_SIZE + prevsize;

    /* Check if the stack is empty, and if so, return null */
    if (thread->count)
        return nextstack;
    else
        return NULL;
}

/* Count the number of stack frames in a thread */
uint32_t dst_thread_countframes(DstThread *thread) {
    uint32_t count = 0;
    const DstValue *stack = thread->data + DST_FRAME_SIZE;
    const DstValue *laststack = thread->data + thread->count;
    while (stack <= laststack) {
        ++count;
        stack += dst_frame_size(stack) + DST_FRAME_SIZE;
    }
    return count;
}

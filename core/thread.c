#include <gst/gst.h>

/* Create a new thread */
GstThread *gst_thread(Gst *vm, GstValue callee, uint32_t capacity) {
    GstThread *thread = gst_alloc(vm, sizeof(GstThread));
    GstValue *data, *stack;
    if (capacity < GST_FRAME_SIZE) capacity = GST_FRAME_SIZE;
    data = gst_alloc(vm, sizeof(GstValue) * capacity);
    thread->capacity = capacity;
    thread->count = GST_FRAME_SIZE;
    thread->data = data;
    thread->status = GST_THREAD_PENDING;
    stack = data + GST_FRAME_SIZE;
    gst_frame_size(stack) = 0;
    gst_frame_prevsize(stack) = 0;
    gst_frame_ret(stack) = 0;
    gst_frame_errloc(stack) = 0;
    gst_frame_pc(stack) = NULL;
    gst_frame_env(stack) = NULL;
    gst_frame_errjmp(stack) = NULL;
    gst_frame_callee(stack) = callee;
    gst_thread_endframe(vm, thread);
    thread->parent = NULL;
    return thread;
}

/* Ensure that the thread has enough EXTRA capacity */
void gst_thread_ensure_extra(Gst *vm, GstThread *thread, uint32_t extra) {
    GstValue *newData, *stack;
    uint32_t usedCapacity, neededCapacity, newCapacity;
    stack = thread->data + thread->count;
    usedCapacity = thread->count + gst_frame_size(stack) + GST_FRAME_SIZE;
    neededCapacity = usedCapacity + extra;
    if (thread->capacity >= neededCapacity) return;
    newCapacity = 2 * neededCapacity;
    newData = gst_alloc(vm, sizeof(GstValue) * newCapacity);
    gst_memcpy(newData, thread->data, sizeof(GstValue) * usedCapacity);
    thread->data = newData;
    thread->capacity = newCapacity;
}

/* Push a value on the current stack frame*/
void gst_thread_push(Gst *vm, GstThread *thread, GstValue x) {
    GstValue *stack;
    gst_thread_ensure_extra(vm, thread, 1);
    stack = thread->data + thread->count;
    stack[gst_frame_size(stack)++] = x;
}

/* Push n nils onto the stack */
void gst_thread_pushnil(Gst *vm, GstThread *thread, uint32_t n) {
    GstValue *stack, *current, *end;
    gst_thread_ensure_extra(vm, thread, n);
    stack = thread->data + thread->count;
    current = stack + gst_frame_size(stack);
    end = current + n;
    for (; current < end; ++current) {
        current->type = GST_NIL;
    }
    gst_frame_size(stack) += n;
}

/* Package up extra args after and including n into tuple at n*/
void gst_thread_tuplepack(Gst *vm, GstThread *thread, uint32_t n) {
    GstValue *stack = thread->data + thread->count;
    uint32_t size = gst_frame_size(stack);
    if (n >= size) {
        gst_thread_pushnil(vm, thread, n - size + 1);
        stack = thread->data + thread->count;
        stack[n].type = GST_TUPLE;
        stack[n].data.tuple = gst_tuple(vm, 0);
    } else {
        uint32_t i;
        GstValue *tuple = gst_tuple(vm, size - n);
        for (i = n; i < size; ++i)
            tuple[i - n] = stack[i];
        stack[n].type = GST_TUPLE;
        stack[n].data.tuple = tuple;
        gst_frame_size(stack) = n + 1;
    }
}

/* Push a stack frame to a thread, with space for arity arguments. Returns the new
 * stack. */
GstValue *gst_thread_beginframe(Gst *vm, GstThread *thread, GstValue callee, uint32_t arity) {
    uint32_t frameOffset;
    GstValue *oldStack, *newStack;

    /* Push the frame */
    gst_thread_ensure_extra(vm, thread, GST_FRAME_SIZE + arity + 4);
    oldStack = thread->data + thread->count;
    frameOffset = gst_frame_size(oldStack) + GST_FRAME_SIZE;
    newStack = oldStack + frameOffset;
    gst_frame_prevsize(newStack) = gst_frame_size(oldStack);
    gst_frame_env(newStack) = NULL;
    gst_frame_errjmp(newStack) = NULL;
    gst_frame_size(newStack) = 0;
    gst_frame_callee(newStack) = callee;
    thread->count += frameOffset; 
    
    /* Ensure the extra space and initialize to nil */
    gst_thread_pushnil(vm, thread, arity);

    /* Return ok */
    return thread->data + thread->count;
}

/* After pushing arguments to a stack frame created with gst_thread_beginframe, call this
 * to finalize the frame before starting a function call. */
void gst_thread_endframe(Gst *vm, GstThread *thread) {
    GstValue *stack = thread->data + thread->count;
    GstValue callee = gst_frame_callee(stack);
    if (callee.type == GST_FUNCTION) {
        GstFunction *fn = callee.data.function;
        gst_frame_pc(stack) = fn->def->byteCode;
        if (fn->def->flags & GST_FUNCDEF_FLAG_VARARG) {
            uint32_t arity = fn->def->arity;
            gst_thread_tuplepack(vm, thread, arity);
        } else {
            uint32_t locals = fn->def->locals;
            if (gst_frame_size(stack) < locals) {
                gst_thread_pushnil(vm, thread, locals - gst_frame_size(stack));
            }
        }
    }
}

/* Pop a stack frame from the thread. Returns the new stack frame, or
 * NULL if there are no more frames */
GstValue *gst_thread_popframe(Gst *vm, GstThread *thread) {
    GstValue *stack = thread->data + thread->count;
    uint32_t prevsize = gst_frame_prevsize(stack);
    GstValue *nextstack = stack - GST_FRAME_SIZE - prevsize;
    GstFuncEnv *env = gst_frame_env(stack);

    /* Check for closures */
    if (env != NULL) {
        uint32_t size = gst_frame_size(stack);
        env->thread = NULL;
        env->stackOffset = size;
        env->values = gst_alloc(vm, sizeof(GstValue) * size);
        gst_memcpy(env->values, stack, sizeof(GstValue) * size);
    }

    /* Shrink stack */
    thread->count -= GST_FRAME_SIZE + prevsize;

    /* Check if the stack is empty, and if so, return null */
    if (thread->count)
        return nextstack;
    else
        return NULL;
}

/* Move the current stack frame over its parent stack frame, allowing
 * for primitive tail calls. */
GstValue *gst_thread_tail(Gst *vm, GstThread *thread) {
    GstFuncEnv *env;
    GstValue *stack = thread->data + thread->count;
    GstValue *nextStack = gst_thread_popframe(vm, thread);
    uint32_t i;

    if (nextStack == NULL) return NULL;
    env = gst_frame_env(nextStack);

    /* Check for old closures */
    if (env != NULL) {
        uint32_t size = gst_frame_size(stack);
        env->thread = NULL;
        env->stackOffset = size;
        env->values = gst_alloc(vm, sizeof(GstValue) * size);
        gst_memcpy(env->values, stack, sizeof(GstValue) * size);
    }

    /* Modify new closure */
    env = gst_frame_env(stack);
    if (env != NULL) {
        env->stackOffset = thread->count;
    }

    /* Copy over (some of) stack frame. Leave ret and prevsize untouched. */
    gst_frame_env(nextStack) = env;
    gst_frame_size(nextStack) = gst_frame_size(stack);
    gst_frame_pc(nextStack) = gst_frame_pc(stack);
    gst_frame_errjmp(nextStack) = gst_frame_errjmp(stack);
    gst_frame_errloc(nextStack) = gst_frame_errloc(stack);
    gst_frame_callee(nextStack) = gst_frame_callee(stack);

    /* Copy stack arguments */
    for (i = 0; i < gst_frame_size(nextStack); ++i)
        nextStack[i] = stack[i];

    return nextStack;
}

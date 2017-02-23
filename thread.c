#include "thread.h"
#include "vm.h"
#include <string.h>

/* Get the stack frame pointer for a thread */
GstStackFrame *gst_thread_frame(GstThread * thread) {
    return (GstStackFrame *)(thread->data + thread->count - GST_FRAME_SIZE);
}

/* Ensure that a thread has enough space in it */
void gst_thread_ensure(Gst *vm, GstThread *thread, uint32_t size) {
	if (size > thread->capacity) {
    	uint32_t newCap = size * 2;
		GstValue *newData = gst_alloc(vm, sizeof(GstValue) * newCap);
		memcpy(newData, thread->data, thread->capacity * sizeof(GstValue));
		thread->data = newData;
		thread->capacity = newCap;
	}
}

/* Push a stack frame onto a thread */
void gst_thread_push(Gst *vm, GstThread *thread, GstValue callee, uint32_t size) {
    uint16_t oldSize;
    uint32_t nextCount, i;
    GstStackFrame *frame;
    if (thread->count) {
        frame = gst_thread_frame(thread);
        oldSize = frame->size;
    } else {
        oldSize = 0;
    }
    nextCount = thread->count + oldSize + GST_FRAME_SIZE;
    gst_thread_ensure(vm, thread, nextCount + size);
    thread->count = nextCount;
    /* Ensure values start out as nil so as to not confuse
     * the garabage collector */
    for (i = nextCount; i < nextCount + size; ++i)
        thread->data[i].type = GST_NIL;
    vm->base = thread->data + thread->count;
    vm->frame = frame = (GstStackFrame *)(vm->base - GST_FRAME_SIZE);
    /* Set up the new stack frame */
    frame->prevSize = oldSize;
    frame->size = size;
    frame->env = NULL;
    frame->callee = callee;
    frame->errorJump = NULL;
}

/* Copy the current function stack to the current closure
   environment. Call when exiting function with closures. */
void gst_thread_split_env(Gst *vm) {
    GstStackFrame *frame = vm->frame;
    GstFuncEnv *env = frame->env;
    /* Check for closures */
    if (env) {
        GstThread *thread = vm->thread;
        uint32_t size = frame->size;
        env->thread = NULL;
        env->stackOffset = size;
        env->values = gst_alloc(vm, sizeof(GstValue) * size);
        memcpy(env->values, thread->data + thread->count, size * sizeof(GstValue));
    }
}

/* Pop the top-most stack frame from stack */
void gst_thread_pop(Gst *vm) {
    GstThread *thread = vm->thread;
    GstStackFrame *frame = vm->frame;
    uint32_t delta = GST_FRAME_SIZE + frame->prevSize;
    if (thread->count) {
        gst_thread_split_env(vm);
    } else {
        gst_crash(vm, "stack underflow");
    }
    thread->count -= delta;
    vm->base -= delta;
    vm->frame = (GstStackFrame *)(vm->base - GST_FRAME_SIZE);
}
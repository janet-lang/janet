#ifndef THREAD_H
#define THREAD_H

#include "datatypes.h"

/* The size of a StackFrame in units of Values. */
#define GST_FRAME_SIZE ((sizeof(GstStackFrame) + sizeof(GstValue) - 1) / sizeof(GstValue))

/* Get the stack frame pointer for a thread */
GstStackFrame *gst_thread_frame(GstThread * thread);

/* Ensure that a thread has enough space in it */
void gst_thread_ensure(Gst *vm, GstThread *thread, uint32_t size);

/* Push a stack frame onto a thread */
void gst_thread_push(Gst *vm, GstThread *thread, GstValue callee, uint32_t size);

/* Copy the current function stack to the current closure
   environment. Call when exiting function with closures. */
void gst_thread_split_env(Gst *vm);

/* Pop the top-most stack frame from stack */
void gst_thread_pop(Gst *vm);

#endif

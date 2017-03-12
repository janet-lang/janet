#ifndef thread_h_INCLUDED
#define thread_h_INCLUDED

#include "datatypes.h"

/* Get the current stack frame */
#define gst_thread_stack(t) ((t)->data + (t)->count)

/* Create a new thread */
GstThread *gst_thread(Gst *vm, GstValue callee, uint32_t capacity); 

/* Ensure that the thread has enough EXTRA capacity */
void gst_thread_ensure_extra(Gst *vm, GstThread *thread, uint32_t extra); 

/* Push a value on the current stack frame*/
void gst_thread_push(Gst *vm, GstThread *thread, GstValue x); 

/* Push n nils onto the stack */
void gst_thread_pushnil(Gst *vm, GstThread *thread, uint32_t n); 

/* Package up extra args after and including n into tuple at n*/
void gst_thread_tuplepack(Gst *vm, GstThread *thread, uint32_t n); 

/* Push a stack frame to a thread, with space for arity arguments. Returns the new
 * stack. */
GstValue *gst_thread_beginframe(Gst *vm, GstThread *thread, GstValue callee, uint32_t arity); 

/* After pushing arguments to a stack frame created with gst_thread_beginframe, call this
 * to finalize the frame before starting a function call. */
void gst_thread_endframe(Gst *vm, GstThread *thread);

/* Pop a stack frame from the thread. Returns the new stack frame, or
 * NULL if there are no more frames */
GstValue *gst_thread_popframe(Gst *vm, GstThread *thread); 

/* Move the current stack frame over its parent stack frame, allowing
 * for primitive tail calls. Return new stack. */
GstValue *gst_thread_tail(Gst *vm, GstThread *thread);

#endif // thread_h_INCLUDED


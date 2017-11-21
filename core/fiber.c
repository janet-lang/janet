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
#include "gc.h"

/* Initialize a new fiber */
DstFiber *dst_fiber(uint32_t capacity) {
    DstFiber *fiber = dst_alloc(DST_MEMORY_FIBER, sizeof(DstFiber));
    fiber->capacity = capacity;
    if (capacity) {
        DstValue *data = malloc(sizeof(DstValue) * capacity);
        if (NULL == data) {
            DST_OUT_OF_MEMORY;
        }
        fiber->data = data;
    } else {
        fiber->data = NULL;
    }
    return dst_fiber_reset(fiber);
}

/* Clear a fiber (reset it) */
DstFiber *dst_fiber_reset(DstFiber *fiber) {
    fiber->frame = 0;
    fiber->frametop = 0;
    fiber->stacktop = DST_FRAME_SIZE;
    fiber->status = DST_FIBER_DEAD;
    return fiber;
}

/* Ensure that the fiber has enough extra capacity */
void dst_fiber_setcapacity(DstFiber *fiber, uint32_t n) {
    DstValue *newData = realloc(fiber->data, sizeof(DstValue) * n);
    if (NULL == newData) {
        DST_OUT_OF_MEMORY;
    }
    fiber->data = newData;
    fiber->capacity = n;
}

/* Push a value on the next stack frame */
void dst_fiber_push(DstFiber *fiber, DstValue x) {
    if (fiber->stacktop >= fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * fiber->stacktop);
    }
    fiber->data[fiber->stacktop++] = x;
}

/* Push 2 values on the next stack frame */
void dst_fiber_push2(DstFiber *fiber, DstValue x, DstValue y) {
    uint32_t newtop = fiber->stacktop + 2;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->stacktop = newtop;
}

/* Push 3 values on the next stack frame */
void dst_fiber_push3(DstFiber *fiber, DstValue x, DstValue y, DstValue z) {
    uint32_t newtop = fiber->stacktop + 3;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->data[fiber->stacktop + 2] = z;
    fiber->stacktop = newtop;
}

/* Push an array on the next stack frame */
void dst_fiber_pushn(DstFiber *fiber, const DstValue *arr, uint32_t n) {
    uint32_t newtop = fiber->stacktop + n;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    memcpy(fiber->data + fiber->stacktop, arr, n * sizeof(DstValue));
    fiber->stacktop = newtop;
}

/* Pop a value off of the stack. Will not destroy a current stack frame.
 * If there is nothing to pop of of the stack, return nil. */
DstValue dst_fiber_popvalue(DstFiber *fiber) {
   uint32_t newstacktop = fiber->stacktop - 1;
   if (newstacktop < fiber->frametop + DST_FRAME_SIZE) {
       return dst_wrap_nil();
   }
   fiber->stacktop = newstacktop;
   return fiber->data[newstacktop];
}

/* Push a stack frame to a fiber */
void dst_fiber_funcframe(DstFiber *fiber, DstFunction *func) {
    DstStackFrame *newframe;

    uint32_t i;
    uint32_t oldframe = fiber->frame;
    uint32_t nextframe = fiber->frametop + DST_FRAME_SIZE;
    uint32_t nextframetop = nextframe + func->def->slotcount;
    uint32_t nextstacktop = nextframetop + DST_FRAME_SIZE;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Set the next frame */
    fiber->frame = nextframe;
    fiber->frametop = nextframetop;
    newframe = dst_fiber_frame(fiber);

    /* Set up the new frame */
    newframe->prevframe = oldframe;
    newframe->pc = func->def->bytecode;
    newframe->func = func;

    /* Nil unset locals (Needed for gc correctness) */
    for (i = fiber->stacktop; i < fiber->frametop; ++i) {
        fiber->data[i].type = DST_NIL;
    }

    /* Check varargs */
    if (func->def->flags & DST_FUNCDEF_FLAG_VARARG) {
        uint32_t tuplehead = fiber->frame + func->def->arity;
        if (tuplehead >= fiber->stacktop) {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(
                fiber->data + tuplehead,
                fiber->stacktop - tuplehead));
        }
    }

    /* Set stack top */
    fiber->stacktop = nextstacktop;
}

/* Create a tail frame for a function */
void dst_fiber_funcframe_tail(DstFiber *fiber, DstFunction *func) {
    uint32_t i;
    uint32_t nextframetop = fiber->frame + func->def->slotcount;
    uint32_t nextstacktop = nextframetop + DST_FRAME_SIZE;
    uint32_t size = (fiber->stacktop - fiber->frametop) - DST_FRAME_SIZE;
    uint32_t argtop = fiber->frame + size;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    DstValue *stack = fiber->data + fiber->frame;
    DstValue *args = fiber->data + fiber->frametop + DST_FRAME_SIZE;

    /* Detatch old function */
    if (NULL != dst_fiber_frame(fiber)->func)
        dst_function_detach(dst_fiber_frame(fiber)->func);

    memmove(stack, args, size * sizeof(DstValue));

    /* Set stack stuff */
    fiber->stacktop = nextstacktop;
    fiber->frametop = nextframetop;

    /* Nil unset locals (Needed for gc correctness) */
    for (i = fiber->frame + size; i < fiber->frametop; ++i) {
        fiber->data[i].type = DST_NIL;
    }

    /* Check varargs */
    if (func->def->flags & DST_FUNCDEF_FLAG_VARARG) {
        uint32_t tuplehead = fiber->frame + func->def->arity;
        if (tuplehead >= argtop) {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(
                fiber->data + tuplehead,
                argtop - tuplehead));
        }
    }

    /* Set frame stuff */
    dst_fiber_frame(fiber)->func = func;
    dst_fiber_frame(fiber)->pc = func->def->bytecode;
}

/* Push a stack frame to a fiber for a c function */
void dst_fiber_cframe(DstFiber *fiber) {
    DstStackFrame *newframe;

    uint32_t oldframe = fiber->frame;
    uint32_t nextframe = fiber->frametop + DST_FRAME_SIZE;
    uint32_t nextframetop = fiber->stacktop;
    uint32_t nextstacktop = nextframetop + DST_FRAME_SIZE;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Set the next frame */
    fiber->frame = nextframe;
    fiber->frametop = nextframetop;
    fiber->stacktop = nextstacktop;
    newframe = dst_fiber_frame(fiber);

    /* Set up the new frame */
    newframe->prevframe = oldframe;
    newframe->pc = NULL;
    newframe->func = NULL;
}

/* Create a cframe for a tail call */
void dst_fiber_cframe_tail(DstFiber *fiber) {
    uint32_t size = (fiber->stacktop - fiber->frametop) - DST_FRAME_SIZE;
    uint32_t nextframetop = fiber->frame + size;;
    uint32_t nextstacktop = nextframetop + DST_FRAME_SIZE;

    if (fiber->frame == 0) {
        return dst_fiber_cframe(fiber);
    }

    DstValue *stack = fiber->data + fiber->frame;
    DstValue *args = fiber->data + fiber->frametop + DST_FRAME_SIZE;

    /* Detach old function */
    if (NULL != dst_fiber_frame(fiber)->func)
        dst_function_detach(dst_fiber_frame(fiber)->func);

    /* Copy pushed args to frame */
    memmove(stack, args, size * sizeof(DstValue));

    /* Set the next frame */
    fiber->frametop = nextframetop;
    fiber->stacktop = nextstacktop;

    /* Set up the new frame */
    dst_fiber_frame(fiber)->func = NULL;
    dst_fiber_frame(fiber)->pc = NULL;
}

/* Pop a stack frame from the fiber. Returns the new stack frame, or
 * NULL if there are no more frames */
void dst_fiber_popframe(DstFiber *fiber) {
    DstStackFrame *frame = dst_fiber_frame(fiber);
    
    /* Clean up the frame (detach environments) */
    if (NULL != frame->func)
        dst_function_detach(frame->func);

    /* Shrink stack */
    fiber->stacktop = fiber->frame;
    fiber->frametop = fiber->frame - DST_FRAME_SIZE;
    fiber->frame = frame->prevframe;
}

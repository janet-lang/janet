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
#include "fiber.h"
#include "gc.h"

/* Initialize a new fiber */
DstFiber *dst_fiber(DstFunction *callee, int32_t capacity) {
    DstFiber *fiber = dst_gcalloc(DST_MEMORY_FIBER, sizeof(DstFiber));
    if (capacity < 16) {
        capacity = 16;
    }
    fiber->capacity = capacity;
    if (capacity) {
        Dst *data = malloc(sizeof(Dst) * capacity);
        if (NULL == data) {
            DST_OUT_OF_MEMORY;
        }
        fiber->data = data;
    }
    fiber->maxstack = DST_STACK_MAX;
    fiber->flags = DST_FIBER_MASK_DEBUG;
    return dst_fiber_reset(fiber, callee);
}

/* Clear a fiber (reset it) */
DstFiber *dst_fiber_reset(DstFiber *fiber, DstFunction *callee) {
    fiber->frame = 0;
    fiber->stackstart = DST_FRAME_SIZE;
    fiber->stacktop = DST_FRAME_SIZE;
    fiber->status = DST_FIBER_NEW;
    fiber->root = callee;
    fiber->child = NULL;
    fiber->flags |= DST_FIBER_FLAG_NEW;
    return fiber;
}

/* Ensure that the fiber has enough extra capacity */
void dst_fiber_setcapacity(DstFiber *fiber, int32_t n) {
    Dst *newData = realloc(fiber->data, sizeof(Dst) * n);
    if (NULL == newData) {
        DST_OUT_OF_MEMORY;
    }
    fiber->data = newData;
    fiber->capacity = n;
}

/* Push a value on the next stack frame */
void dst_fiber_push(DstFiber *fiber, Dst x) {
    if (fiber->stacktop >= fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * fiber->stacktop);
    }
    fiber->data[fiber->stacktop++] = x;
}

/* Push 2 values on the next stack frame */
void dst_fiber_push2(DstFiber *fiber, Dst x, Dst y) {
    int32_t newtop = fiber->stacktop + 2;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->stacktop = newtop;
}

/* Push 3 values on the next stack frame */
void dst_fiber_push3(DstFiber *fiber, Dst x, Dst y, Dst z) {
    int32_t newtop = fiber->stacktop + 3;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->data[fiber->stacktop + 2] = z;
    fiber->stacktop = newtop;
}

/* Push an array on the next stack frame */
void dst_fiber_pushn(DstFiber *fiber, const Dst *arr, int32_t n) {
    int32_t newtop = fiber->stacktop + n;
    if (newtop > fiber->capacity) {
        dst_fiber_setcapacity(fiber, 2 * newtop);
    }
    memcpy(fiber->data + fiber->stacktop, arr, n * sizeof(Dst));
    fiber->stacktop = newtop;
}

/* Push a stack frame to a fiber */
void dst_fiber_funcframe(DstFiber *fiber, DstFunction *func) {
    DstStackFrame *newframe;

    int32_t i;
    int32_t oldtop = fiber->stacktop;
    int32_t oldframe = fiber->frame;
    int32_t nextframe = fiber->stackstart;
    int32_t nextstacktop = nextframe + func->def->slotcount + DST_FRAME_SIZE;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Nil unset stack arguments (Needed for gc correctness) */
    for (i = fiber->stacktop; i < nextstacktop; ++i) {
        fiber->data[i] = dst_wrap_nil();
    }

    /* Set up the next frame */
    fiber->frame = nextframe;
    fiber->stacktop = fiber->stackstart = nextstacktop;
    newframe = dst_fiber_frame(fiber);
    newframe->prevframe = oldframe;
    newframe->pc = func->def->bytecode;
    newframe->func = func;
    newframe->env = NULL;
    newframe->flags = 0;

    /* Check varargs */
    if (func->def->flags & DST_FUNCDEF_FLAG_VARARG) {
        int32_t tuplehead = fiber->frame + func->def->arity;
        if (tuplehead >= oldtop) {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(
                fiber->data + tuplehead,
                oldtop - tuplehead));
        }
    }
}

/* If a frame has a closure environment, detach it from
 * the stack and have it keep its own values */
static void dst_env_detach(DstFuncEnv *env) {
    /* Check for closure environment */
    if (env) {
        size_t s = sizeof(Dst) * env->length;
        Dst *vmem = malloc(s);
        if (NULL == vmem) {
            DST_OUT_OF_MEMORY;
        }
        memcpy(vmem, env->as.fiber->data + env->offset, s);
        env->offset = 0;
        env->as.values = vmem;
    }
}

/* Create a tail frame for a function */
void dst_fiber_funcframe_tail(DstFiber *fiber, DstFunction *func) {
    int32_t i;
    int32_t nextframetop = fiber->frame + func->def->slotcount;
    int32_t nextstacktop = nextframetop + DST_FRAME_SIZE;
    int32_t stacksize;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    Dst *stack = fiber->data + fiber->frame;
    Dst *args = fiber->data + fiber->stackstart;

    /* Detatch old function */
    if (NULL != dst_fiber_frame(fiber)->func)
        dst_env_detach(dst_fiber_frame(fiber)->env);
    dst_fiber_frame(fiber)->env = NULL;

    /* Check varargs */
    if (func->def->flags & DST_FUNCDEF_FLAG_VARARG) {
        int32_t tuplehead = fiber->stackstart + func->def->arity;
        if (tuplehead >= fiber->stacktop) {
            if (tuplehead >= fiber->capacity) dst_fiber_setcapacity(fiber, 2 * (tuplehead + 1));
            for (i = fiber->stacktop; i < tuplehead; ++i) fiber->data[i] = dst_wrap_nil();
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = dst_wrap_tuple(dst_tuple_n(
                fiber->data + tuplehead,
                fiber->stacktop - tuplehead));
        }
        stacksize = tuplehead - fiber->stackstart + 1;
    } else {
        stacksize = fiber->stacktop - fiber->stackstart;
    }

    if (stacksize) memmove(stack, args, stacksize * sizeof(Dst));

    /* Nil unset locals (Needed for functional correctness) */
    for (i = fiber->frame + stacksize; i < nextframetop; ++i)
        fiber->data[i] = dst_wrap_nil();

    /* Set stack stuff */
    fiber->stacktop = fiber->stackstart = nextstacktop;

    /* Set frame stuff */
    dst_fiber_frame(fiber)->func = func;
    dst_fiber_frame(fiber)->pc = func->def->bytecode;
    dst_fiber_frame(fiber)->flags |= DST_STACKFRAME_TAILCALL;
}

/* Push a stack frame to a fiber for a c function */
void dst_fiber_cframe(DstFiber *fiber) {
    DstStackFrame *newframe;

    int32_t oldframe = fiber->frame;
    int32_t nextframe = fiber->stackstart;
    int32_t nextstacktop = fiber->stacktop + DST_FRAME_SIZE;

    if (fiber->capacity < nextstacktop) {
        dst_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Set the next frame */
    fiber->frame = nextframe;
    fiber->stacktop = fiber->stackstart = nextstacktop;
    newframe = dst_fiber_frame(fiber);

    /* Set up the new frame */
    newframe->prevframe = oldframe;
    newframe->pc = NULL;
    newframe->func = NULL;
    newframe->env = NULL;
    newframe->flags = 0;
}

/* Pop a stack frame from the fiber. Returns the new stack frame, or
 * NULL if there are no more frames */
void dst_fiber_popframe(DstFiber *fiber) {
    DstStackFrame *frame = dst_fiber_frame(fiber);
    if (fiber->frame == 0) return;
    
    /* Clean up the frame (detach environments) */
    if (NULL != frame->func)
        dst_env_detach(frame->env);

    /* Shrink stack */
    fiber->stacktop = fiber->stackstart = fiber->frame;
    fiber->frame = frame->prevframe;
}

/* CFuns */

static int cfun_fiber(DstArgs args) {
    DstFiber *fiber;
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_FUNCTION);
    fiber = dst_fiber(dst_unwrap_function(args.v[0]), 64);
    return dst_return(args, dst_wrap_fiber(fiber));
}

static int cfun_status(DstArgs args) {
    const char *status = "";
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_FIBER);
    switch(dst_unwrap_fiber(args.v[0])->status) {
        case DST_FIBER_PENDING:
            status = ":pending";
            break;
        case DST_FIBER_NEW:
            status = ":new";
            break;
        case DST_FIBER_ALIVE:
            status = ":alive";
            break;
        case DST_FIBER_DEAD:
            status = ":dead";
            break;
        case DST_FIBER_ERROR:
            status = ":error";
            break;
        case DST_FIBER_DEBUG:
            status = ":debug";
            break;
    }
    return dst_return(args, dst_csymbolv(status));
}

/* Extract info from one stack frame */
static Dst doframe(DstStackFrame *frame) {
    int32_t off;
    DstTable *t = dst_table(3);
    DstFuncDef *def = NULL;
    if (frame->func) {
        dst_table_put(t, dst_csymbolv(":function"), dst_wrap_function(frame->func));
        def = frame->func->def;
        if (def->name) {
            dst_table_put(t, dst_csymbolv(":name"), dst_wrap_string(def->name));
        }
    } else {
        dst_table_put(t, dst_csymbolv(":c"), dst_wrap_true());
    }
    if (frame->flags & DST_STACKFRAME_TAILCALL) {
        dst_table_put(t, dst_csymbolv(":tail"), dst_wrap_true());
    }
    if (frame->pc) {
        off = frame->pc - def->bytecode;
        dst_table_put(t, dst_csymbolv(":pc"), dst_wrap_integer(off));
        if (def->sourcemap) {
            DstSourceMapping mapping = def->sourcemap[off];
            dst_table_put(t, dst_csymbolv(":source-start"), dst_wrap_integer(mapping.start));
            dst_table_put(t, dst_csymbolv(":source-end"), dst_wrap_integer(mapping.end));
        }
        if (def->source) {
            dst_table_put(t, dst_csymbolv(":source"), dst_wrap_string(def->source));
        } else if (def->sourcepath) {
            dst_table_put(t, dst_csymbolv(":sourcepath"), dst_wrap_string(def->sourcepath));
        }
    }
    return dst_wrap_table(t);
}

static int cfun_stack(DstArgs args) {
    DstFiber *fiber;
    DstArray *array;
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_FIBER);
    fiber = dst_unwrap_fiber(args.v[0]);
    array = dst_array(0);
    {
        int32_t i = fiber->frame;
        DstStackFrame *frame;
        while (i > 0) {
            frame = (DstStackFrame *)(fiber->data + i - DST_FRAME_SIZE);
            dst_array_push(array, doframe(frame));
            i = frame->prevframe;
        }
    }
    return dst_return(args, dst_wrap_array(array));
}

static const DstReg cfuns[] = {
    {"fiber", cfun_fiber},
    {"fiber-status", cfun_status},
    {"fiber-stack", cfun_stack},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_fiber(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

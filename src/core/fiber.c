/*
* Copyright (c) 2018 Calvin Rose
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

#include <janet/janet.h>
#include "fiber.h"
#include "state.h"
#include "gc.h"

static JanetFiber *make_fiber(int32_t capacity) {
    Janet *data;
    JanetFiber *fiber = janet_gcalloc(JANET_MEMORY_FIBER, sizeof(JanetFiber));
    if (capacity < 16) {
        capacity = 16;
    }
    fiber->capacity = capacity;
    data = malloc(sizeof(Janet) * capacity);
    if (NULL == data) {
        JANET_OUT_OF_MEMORY;
    }
    fiber->data = data;
    fiber->maxstack = JANET_STACK_MAX;
    fiber->frame = 0;
    fiber->stackstart = JANET_FRAME_SIZE;
    fiber->stacktop = JANET_FRAME_SIZE;
    fiber->child = NULL;
    fiber->flags = JANET_FIBER_MASK_YIELD;
    janet_fiber_set_status(fiber, JANET_STATUS_NEW);
    return fiber;
}

/* Initialize a new fiber */
JanetFiber *janet_fiber(JanetFunction *callee, int32_t capacity) {
    JanetFiber *fiber = make_fiber(capacity);
    if (janet_fiber_funcframe(fiber, callee))
        janet_fiber_set_status(fiber, JANET_STATUS_ERROR);
    return fiber;
}

/* Clear a fiber (reset it) with argn values on the stack. */
JanetFiber *janet_fiber_n(JanetFunction *callee, int32_t capacity, const Janet *argv, int32_t argn) {
    int32_t newstacktop;
    JanetFiber *fiber = make_fiber(capacity);
    newstacktop = fiber->stacktop + argn;
    if (newstacktop >= fiber->capacity) {
        janet_fiber_setcapacity(fiber, 2 * newstacktop);
    }
    memcpy(fiber->data + fiber->stacktop, argv, argn * sizeof(Janet));
    fiber->stacktop = newstacktop;
    if (janet_fiber_funcframe(fiber, callee))
        janet_fiber_set_status(fiber, JANET_STATUS_ERROR);
    return fiber;
}

/* Ensure that the fiber has enough extra capacity */
void janet_fiber_setcapacity(JanetFiber *fiber, int32_t n) {
    Janet *newData = realloc(fiber->data, sizeof(Janet) * n);
    if (NULL == newData) {
        JANET_OUT_OF_MEMORY;
    }
    fiber->data = newData;
    fiber->capacity = n;
}

/* Push a value on the next stack frame */
void janet_fiber_push(JanetFiber *fiber, Janet x) {
    if (fiber->stacktop >= fiber->capacity) {
        janet_fiber_setcapacity(fiber, 2 * fiber->stacktop);
    }
    fiber->data[fiber->stacktop++] = x;
}

/* Push 2 values on the next stack frame */
void janet_fiber_push2(JanetFiber *fiber, Janet x, Janet y) {
    int32_t newtop = fiber->stacktop + 2;
    if (newtop > fiber->capacity) {
        janet_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->stacktop = newtop;
}

/* Push 3 values on the next stack frame */
void janet_fiber_push3(JanetFiber *fiber, Janet x, Janet y, Janet z) {
    int32_t newtop = fiber->stacktop + 3;
    if (newtop > fiber->capacity) {
        janet_fiber_setcapacity(fiber, 2 * newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->data[fiber->stacktop + 2] = z;
    fiber->stacktop = newtop;
}

/* Push an array on the next stack frame */
void janet_fiber_pushn(JanetFiber *fiber, const Janet *arr, int32_t n) {
    int32_t newtop = fiber->stacktop + n;
    if (newtop > fiber->capacity) {
        janet_fiber_setcapacity(fiber, 2 * newtop);
    }
    memcpy(fiber->data + fiber->stacktop, arr, n * sizeof(Janet));
    fiber->stacktop = newtop;
}

/* Push a stack frame to a fiber */
int janet_fiber_funcframe(JanetFiber *fiber, JanetFunction *func) {
    JanetStackFrame *newframe;

    int32_t i;
    int32_t oldtop = fiber->stacktop;
    int32_t oldframe = fiber->frame;
    int32_t nextframe = fiber->stackstart;
    int32_t nextstacktop = nextframe + func->def->slotcount + JANET_FRAME_SIZE;
    int32_t next_arity = fiber->stacktop - fiber->stackstart;

    if (fiber->capacity < nextstacktop) {
        janet_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Nil unset stack arguments (Needed for gc correctness) */
    for (i = fiber->stacktop; i < nextstacktop; ++i) {
        fiber->data[i] = janet_wrap_nil();
    }

    /* Set up the next frame */
    fiber->frame = nextframe;
    fiber->stacktop = fiber->stackstart = nextstacktop;
    newframe = janet_fiber_frame(fiber);
    newframe->prevframe = oldframe;
    newframe->pc = func->def->bytecode;
    newframe->func = func;
    newframe->env = NULL;
    newframe->flags = 0;

    /* Check varargs */
    if (func->def->flags & JANET_FUNCDEF_FLAG_VARARG) {
        int32_t tuplehead = fiber->frame + func->def->arity;
        if (tuplehead >= oldtop) {
            fiber->data[tuplehead] = janet_wrap_tuple(janet_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = janet_wrap_tuple(janet_tuple_n(
                fiber->data + tuplehead,
                oldtop - tuplehead));
        }
    }

    /* Check strict arity AFTER getting fiber to valid state. */
    if (func->def->flags & JANET_FUNCDEF_FLAG_FIXARITY) {
        if (func->def->arity != next_arity) {
            return 1;
        }
    }

    /* Good return */
    return 0;
}

/* If a frame has a closure environment, detach it from
 * the stack and have it keep its own values */
static void janet_env_detach(JanetFuncEnv *env) {
    /* Check for closure environment */
    if (env) {
        size_t s = sizeof(Janet) * env->length;
        Janet *vmem = malloc(s);
        if (NULL == vmem) {
            JANET_OUT_OF_MEMORY;
        }
        memcpy(vmem, env->as.fiber->data + env->offset, s);
        env->offset = 0;
        env->as.values = vmem;
    }
}

/* Create a tail frame for a function */
int janet_fiber_funcframe_tail(JanetFiber *fiber, JanetFunction *func) {
    int32_t i;
    int32_t nextframetop = fiber->frame + func->def->slotcount;
    int32_t nextstacktop = nextframetop + JANET_FRAME_SIZE;
    int32_t next_arity = fiber->stacktop - fiber->stackstart;
    int32_t stacksize;

    if (fiber->capacity < nextstacktop) {
        janet_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    Janet *stack = fiber->data + fiber->frame;
    Janet *args = fiber->data + fiber->stackstart;

    /* Detatch old function */
    if (NULL != janet_fiber_frame(fiber)->func)
        janet_env_detach(janet_fiber_frame(fiber)->env);
    janet_fiber_frame(fiber)->env = NULL;

    /* Check varargs */
    if (func->def->flags & JANET_FUNCDEF_FLAG_VARARG) {
        int32_t tuplehead = fiber->stackstart + func->def->arity;
        if (tuplehead >= fiber->stacktop) {
            if (tuplehead >= fiber->capacity) janet_fiber_setcapacity(fiber, 2 * (tuplehead + 1));
            for (i = fiber->stacktop; i < tuplehead; ++i) fiber->data[i] = janet_wrap_nil();
            fiber->data[tuplehead] = janet_wrap_tuple(janet_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = janet_wrap_tuple(janet_tuple_n(
                fiber->data + tuplehead,
                fiber->stacktop - tuplehead));
        }
        stacksize = tuplehead - fiber->stackstart + 1;
    } else {
        stacksize = fiber->stacktop - fiber->stackstart;
    }

    if (stacksize) memmove(stack, args, stacksize * sizeof(Janet));

    /* Nil unset locals (Needed for functional correctness) */
    for (i = fiber->frame + stacksize; i < nextframetop; ++i)
        fiber->data[i] = janet_wrap_nil();

    /* Set stack stuff */
    fiber->stacktop = fiber->stackstart = nextstacktop;

    /* Set frame stuff */
    janet_fiber_frame(fiber)->func = func;
    janet_fiber_frame(fiber)->pc = func->def->bytecode;
    janet_fiber_frame(fiber)->flags |= JANET_STACKFRAME_TAILCALL;

    /* Check strict arity AFTER getting fiber to valid state. */
    if (func->def->flags & JANET_FUNCDEF_FLAG_FIXARITY) {
        if (func->def->arity != next_arity) {
            return 1;
        }
    }

    /* Good return */
    return 0;
}

/* Push a stack frame to a fiber for a c function */
void janet_fiber_cframe(JanetFiber *fiber, JanetCFunction cfun) {
    JanetStackFrame *newframe;

    int32_t oldframe = fiber->frame;
    int32_t nextframe = fiber->stackstart;
    int32_t nextstacktop = fiber->stacktop + JANET_FRAME_SIZE;

    if (fiber->capacity < nextstacktop) {
        janet_fiber_setcapacity(fiber, 2 * nextstacktop);
    }

    /* Set the next frame */
    fiber->frame = nextframe;
    fiber->stacktop = fiber->stackstart = nextstacktop;
    newframe = janet_fiber_frame(fiber);

    /* Set up the new frame */
    newframe->prevframe = oldframe;
    newframe->pc = (uint32_t *) cfun;
    newframe->func = NULL;
    newframe->env = NULL;
    newframe->flags = 0;
}

/* Pop a stack frame from the fiber. Returns the new stack frame, or
 * NULL if there are no more frames */
void janet_fiber_popframe(JanetFiber *fiber) {
    JanetStackFrame *frame = janet_fiber_frame(fiber);
    if (fiber->frame == 0) return;

    /* Clean up the frame (detach environments) */
    if (NULL != frame->func)
        janet_env_detach(frame->env);

    /* Shrink stack */
    fiber->stacktop = fiber->stackstart = fiber->frame;
    fiber->frame = frame->prevframe;
}

/* CFuns */

static int cfun_new(JanetArgs args) {
    JanetFiber *fiber;
    JanetFunction *func;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_FUNCTION(func, args, 0);
    if (func->def->flags & JANET_FUNCDEF_FLAG_FIXARITY) {
        if (func->def->arity != 0) {
            JANET_THROW(args, "expected nullary function in fiber constructor");
        }
    }
    fiber = janet_fiber(func, 64);
    if (args.n == 2) {
        const uint8_t *flags;
        int32_t len, i;
        JANET_ARG_BYTES(flags, len, args, 1);
        fiber->flags = 0;
        janet_fiber_set_status(fiber, JANET_STATUS_NEW);
        for (i = 0; i < len; i++) {
            if (flags[i] >= '0' && flags[i] <= '9') {
                fiber->flags |= JANET_FIBER_MASK_USERN(flags[i] - '0');
            } else {
                switch (flags[i]) {
                    default:
                        JANET_THROW(args, "invalid flag, expected a, d, e, u, or y");
                    case ':':
                        break;
                    case 'a':
                        fiber->flags |=
                            JANET_FIBER_MASK_DEBUG |
                            JANET_FIBER_MASK_ERROR |
                            JANET_FIBER_MASK_USER |
                            JANET_FIBER_MASK_YIELD;
                        break;
                    case 'd':
                        fiber->flags |= JANET_FIBER_MASK_DEBUG;
                        break;
                    case 'e':
                        fiber->flags |= JANET_FIBER_MASK_ERROR;
                        break;
                    case 'u':
                        fiber->flags |= JANET_FIBER_MASK_USER;
                        break;
                    case 'y':
                        fiber->flags |= JANET_FIBER_MASK_YIELD;
                        break;
                }
            }
        }
    }
    JANET_RETURN_FIBER(args, fiber);
}

static int cfun_status(JanetArgs args) {
    JanetFiber *fiber;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    uint32_t s = (fiber->flags & JANET_FIBER_STATUS_MASK) >>
        JANET_FIBER_STATUS_OFFSET;
    JANET_RETURN_CSYMBOL(args, janet_status_names[s]);
}

static int cfun_current(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    JANET_RETURN_FIBER(args, janet_vm_fiber);
}

static int cfun_maxstack(JanetArgs args) {
    JanetFiber *fiber;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    JANET_RETURN_INTEGER(args, fiber->maxstack);
}

static int cfun_setmaxstack(JanetArgs args) {
    JanetFiber *fiber;
    int32_t maxs;
    JANET_FIXARITY(args, 2);
    JANET_ARG_FIBER(fiber, args, 0);
    JANET_ARG_INTEGER(maxs, args, 1);
    if (maxs < 0) {
        JANET_THROW(args, "expected positive integer");
    }
    fiber->maxstack = maxs;
    JANET_RETURN_FIBER(args, fiber);
}

static const JanetReg cfuns[] = {
    {"fiber/new", cfun_new,
        "(fiber/new func [,sigmask])\n\n"
        "Create a new fiber with function body func. Can optionally "
        "take a set of signals to block from the current parent fiber "
        "when called. The mask is specified as symbol where each character "
        "is used to indicate a signal to block. The default sigmask is :y. "
        "For example, \n\n"
        "\t(fiber/new myfun :e123)\n\n"
        "blocks error signals and user signals 1, 2 and 3. The signals are "
        "as follows: \n\n"
        "\ta - block all signals\n"
        "\td - block debug signals\n"
        "\te - block error signals\n"
        "\tu - block user signals\n"
        "\ty - block yield signals\n"
        "\t0-9 - block a specific user signal"
    },
    {"fiber/status", cfun_status,
        "(fiber/status fib)\n\n"
        "Get the status of a fiber. The status will be one of:\n\n"
        "\t:dead - the fiber has finished\n"
        "\t:error - the fiber has errored out\n"
        "\t:debug - the fiber is suspended in debug mode\n"
        "\t:pending - the fiber has been yielded\n"
        "\t:user(0-9) - the fiber is suspended by a user signal\n"
        "\t:alive - the fiber is currently running and cannot be resumed\n"
        "\t:new - the fiber has just been created and not yet run"
    },
    {"fiber/current", cfun_current,
        "(fiber/current)\n\n"
        "Returns the currently running fiber."
    },
    {"fiber/maxstack", cfun_maxstack,
        "(fiber/maxstack fib)\n\n"
        "Gets the maximum stack size in janet values allowed for a fiber. While memory for "
        "the fiber's stack is not allocated up front, the fiber will not allocated more "
        "than this amount and will throw a stackoverflow error if more memory is needed. "
    },
    {"fiber/setmaxstack", cfun_setmaxstack,
        "(fiber/setmaxstack fib maxstack)\n\n"
        "Sets the maximum stack size in janet values for a fiber. By default, the "
        "maximum stacksize is usually 8192."
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
int janet_lib_fiber(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}

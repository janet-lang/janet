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

/* Initialize a new fiber */
JanetFiber *janet_fiber(JanetFunction *callee, int32_t capacity) {
    JanetFiber *fiber = janet_gcalloc(JANET_MEMORY_FIBER, sizeof(JanetFiber));
    if (capacity < 16) {
        capacity = 16;
    }
    fiber->capacity = capacity;
    if (capacity) {
        Janet *data = malloc(sizeof(Janet) * capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
        }
        fiber->data = data;
    }
    fiber->maxstack = JANET_STACK_MAX;
    return janet_fiber_reset(fiber, callee);
}

/* Clear a fiber (reset it) */
JanetFiber *janet_fiber_reset(JanetFiber *fiber, JanetFunction *callee) {
    fiber->frame = 0;
    fiber->stackstart = JANET_FRAME_SIZE;
    fiber->stacktop = JANET_FRAME_SIZE;
    fiber->root = callee;
    fiber->child = NULL;
    fiber->flags = JANET_FIBER_MASK_YIELD;
    janet_fiber_set_status(fiber, JANET_STATUS_NEW);
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

    /* Check strict arity */
    if (func->def->flags & JANET_FUNCDEF_FLAG_FIXARITY) {
        if (func->def->arity != (fiber->stacktop - fiber->stackstart)) {
            return 1;
        }
    }

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
    int32_t stacksize;

    /* Check strict arity */
    if (func->def->flags & JANET_FUNCDEF_FLAG_FIXARITY) {
        if (func->def->arity != (fiber->stacktop - fiber->stackstart)) {
            return 1;
        }
    }

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
        if (func->def->arity != 1) {
            JANET_THROW(args, "expected unit arity function in fiber constructor");
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
    const char *status = "";
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    uint32_t s = (fiber->flags & JANET_FIBER_STATUS_MASK) >>
        JANET_FIBER_STATUS_OFFSET;
    switch (s) {
        case JANET_STATUS_DEAD: status = ":dead"; break;
        case JANET_STATUS_ERROR: status = ":error"; break;
        case JANET_STATUS_DEBUG: status = ":debug"; break;
        case JANET_STATUS_PENDING: status = ":pending"; break;
        case JANET_STATUS_USER0: status = ":user0"; break;
        case JANET_STATUS_USER1: status = ":user1"; break;
        case JANET_STATUS_USER2: status = ":user2"; break;
        case JANET_STATUS_USER3: status = ":user3"; break;
        case JANET_STATUS_USER4: status = ":user4"; break;
        case JANET_STATUS_USER5: status = ":user5"; break;
        case JANET_STATUS_USER6: status = ":user6"; break;
        case JANET_STATUS_USER7: status = ":user7"; break;
        case JANET_STATUS_USER8: status = ":user8"; break;
        case JANET_STATUS_USER9: status = ":user9"; break;
        case JANET_STATUS_NEW: status = ":new"; break;
        default:
        case JANET_STATUS_ALIVE: status = ":alive"; break;
    }
    JANET_RETURN_CSYMBOL(args, status);
}

/* Extract info from one stack frame */
static Janet doframe(JanetStackFrame *frame) {
    int32_t off;
    JanetTable *t = janet_table(3);
    JanetFuncDef *def = NULL;
    if (frame->func) {
        janet_table_put(t, janet_csymbolv(":function"), janet_wrap_function(frame->func));
        def = frame->func->def;
        if (def->name) {
            janet_table_put(t, janet_csymbolv(":name"), janet_wrap_string(def->name));
        }
    } else {
        JanetCFunction cfun = (JanetCFunction)(frame->pc);
        if (cfun) {
            Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
            if (!janet_checktype(name, JANET_NIL)) {
                janet_table_put(t, janet_csymbolv(":name"), name);
            }
        }
        janet_table_put(t, janet_csymbolv(":c"), janet_wrap_true());
    }
    if (frame->flags & JANET_STACKFRAME_TAILCALL) {
        janet_table_put(t, janet_csymbolv(":tail"), janet_wrap_true());
    }
    if (frame->func && frame->pc) {
        off = (int32_t) (frame->pc - def->bytecode);
        janet_table_put(t, janet_csymbolv(":pc"), janet_wrap_integer(off));
        if (def->sourcemap) {
            JanetSourceMapping mapping = def->sourcemap[off];
            janet_table_put(t, janet_csymbolv(":line"), janet_wrap_integer(mapping.line));
            janet_table_put(t, janet_csymbolv(":column"), janet_wrap_integer(mapping.column));
        }
        if (def->source) {
            janet_table_put(t, janet_csymbolv(":source"), janet_wrap_string(def->source));
        }
    }
    return janet_wrap_table(t);
}

static int cfun_stack(JanetArgs args) {
    JanetFiber *fiber;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    array = janet_array(0);
    {
        int32_t i = fiber->frame;
        JanetStackFrame *frame;
        while (i > 0) {
            frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            janet_array_push(array, doframe(frame));
            i = frame->prevframe;
        }
    }
    JANET_RETURN_ARRAY(args, array);
}

static int cfun_current(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    JANET_RETURN_FIBER(args, janet_vm_fiber);
}

static int cfun_lineage(JanetArgs args) {
    JanetFiber *fiber;
    JanetArray *array;
    JANET_FIXARITY(args, 1);
    JANET_ARG_FIBER(fiber, args, 0);
    array = janet_array(0);
    while (fiber) {
        janet_array_push(array, janet_wrap_fiber(fiber));
        fiber = fiber->child;
    }
    JANET_RETURN_ARRAY(args, array);
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
    {"fiber.new", cfun_new, NULL},
    {"fiber.status", cfun_status, NULL},
    {"fiber.stack", cfun_stack, NULL},
    {"fiber.current", cfun_current, NULL},
    {"fiber.lineage", cfun_lineage, NULL},
    {"fiber.maxstack", cfun_maxstack, NULL},
    {"fiber.setmaxstack", cfun_setmaxstack, NULL},
    {NULL, NULL, NULL}
};

/* Module entry point */
int janet_lib_fiber(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}

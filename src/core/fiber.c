/*
* Copyright (c) 2024 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "fiber.h"
#include "state.h"
#include "gc.h"
#include "util.h"
#endif

static void fiber_reset(JanetFiber *fiber) {
    fiber->maxstack = JANET_STACK_MAX;
    fiber->frame = 0;
    fiber->stackstart = JANET_FRAME_SIZE;
    fiber->stacktop = JANET_FRAME_SIZE;
    fiber->child = NULL;
    fiber->flags = JANET_FIBER_MASK_YIELD | JANET_FIBER_RESUME_NO_USEVAL | JANET_FIBER_RESUME_NO_SKIP;
    fiber->env = NULL;
    fiber->last_value = janet_wrap_nil();
#ifdef JANET_EV
    fiber->sched_id = 0;
    fiber->ev_callback = NULL;
    fiber->ev_state = NULL;
    fiber->ev_stream = NULL;
    fiber->supervisor_channel = NULL;
#endif
    janet_fiber_set_status(fiber, JANET_STATUS_NEW);
}

static JanetFiber *fiber_alloc(int32_t capacity) {
    Janet *data;
    JanetFiber *fiber = janet_gcalloc(JANET_MEMORY_FIBER, sizeof(JanetFiber));
    if (capacity < 32) {
        capacity = 32;
    }
    fiber->capacity = capacity;
    data = janet_malloc(sizeof(Janet) * (size_t) capacity);
    if (NULL == data) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm.next_collection += sizeof(Janet) * capacity;
    fiber->data = data;
    return fiber;
}

/* Create a new fiber with argn values on the stack by reusing a fiber. */
JanetFiber *janet_fiber_reset(JanetFiber *fiber, JanetFunction *callee, int32_t argc, const Janet *argv) {
    int32_t newstacktop;
    fiber_reset(fiber);
    if (argc) {
        newstacktop = fiber->stacktop + argc;
        if (newstacktop >= fiber->capacity) {
            janet_fiber_setcapacity(fiber, 2 * newstacktop);
        }
        if (argv) {
            memcpy(fiber->data + fiber->stacktop, argv, argc * sizeof(Janet));
        } else {
            /* If argv not given, fill with nil */
            for (int32_t i = 0; i < argc; i++) {
                fiber->data[fiber->stacktop + i] = janet_wrap_nil();
            }
        }
        fiber->stacktop = newstacktop;
    }
    /* Don't panic on failure since we use this to implement janet_pcall */
    if (janet_fiber_funcframe(fiber, callee)) return NULL;
    janet_fiber_frame(fiber)->flags |= JANET_STACKFRAME_ENTRANCE;
#ifdef JANET_EV
    fiber->supervisor_channel = NULL;
#endif
    return fiber;
}

/* Create a new fiber with argn values on the stack. */
JanetFiber *janet_fiber(JanetFunction *callee, int32_t capacity, int32_t argc, const Janet *argv) {
    return janet_fiber_reset(fiber_alloc(capacity), callee, argc, argv);
}

#ifdef JANET_DEBUG
/* Test for memory issues by reallocating fiber every time we push a stack frame */
static void janet_fiber_refresh_memory(JanetFiber *fiber) {
    int32_t n = fiber->capacity;
    if (n) {
        Janet *newData = janet_malloc(sizeof(Janet) * n);
        if (NULL == newData) {
            JANET_OUT_OF_MEMORY;
        }
        memcpy(newData, fiber->data, fiber->capacity * sizeof(Janet));
        janet_free(fiber->data);
        fiber->data = newData;
    }
}
#endif

/* Ensure that the fiber has enough extra capacity */
void janet_fiber_setcapacity(JanetFiber *fiber, int32_t n) {
    int32_t old_size = fiber->capacity;
    int32_t diff = n - old_size;
    Janet *newData = janet_realloc(fiber->data, sizeof(Janet) * n);
    if (NULL == newData) {
        JANET_OUT_OF_MEMORY;
    }
    fiber->data = newData;
    fiber->capacity = n;
    janet_vm.next_collection += sizeof(Janet) * diff;
}

/* Grow fiber if needed */
static void janet_fiber_grow(JanetFiber *fiber, int32_t needed) {
    int32_t cap = needed > (INT32_MAX / 2) ? INT32_MAX : 2 * needed;
    janet_fiber_setcapacity(fiber, cap);
}

/* Push a value on the next stack frame */
void janet_fiber_push(JanetFiber *fiber, Janet x) {
    if (fiber->stacktop == INT32_MAX) janet_panic("stack overflow");
    if (fiber->stacktop >= fiber->capacity) {
        janet_fiber_grow(fiber, fiber->stacktop);
    }
    fiber->data[fiber->stacktop++] = x;
}

/* Push 2 values on the next stack frame */
void janet_fiber_push2(JanetFiber *fiber, Janet x, Janet y) {
    if (fiber->stacktop >= INT32_MAX - 1) janet_panic("stack overflow");
    int32_t newtop = fiber->stacktop + 2;
    if (newtop > fiber->capacity) {
        janet_fiber_grow(fiber, newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->stacktop = newtop;
}

/* Push 3 values on the next stack frame */
void janet_fiber_push3(JanetFiber *fiber, Janet x, Janet y, Janet z) {
    if (fiber->stacktop >= INT32_MAX - 2) janet_panic("stack overflow");
    int32_t newtop = fiber->stacktop + 3;
    if (newtop > fiber->capacity) {
        janet_fiber_grow(fiber, newtop);
    }
    fiber->data[fiber->stacktop] = x;
    fiber->data[fiber->stacktop + 1] = y;
    fiber->data[fiber->stacktop + 2] = z;
    fiber->stacktop = newtop;
}

/* Push an array on the next stack frame */
void janet_fiber_pushn(JanetFiber *fiber, const Janet *arr, int32_t n) {
    if (fiber->stacktop > INT32_MAX - n) janet_panic("stack overflow");
    int32_t newtop = fiber->stacktop + n;
    if (newtop > fiber->capacity) {
        janet_fiber_grow(fiber, newtop);
    }
    safe_memcpy(fiber->data + fiber->stacktop, arr, n * sizeof(Janet));
    fiber->stacktop = newtop;
}

/* Create a struct with n values. If n is odd, the last value is ignored. */
static Janet make_struct_n(const Janet *args, int32_t n) {
    int32_t i = 0;
    JanetKV *st = janet_struct_begin(n & (~1));
    for (; i < n; i += 2) {
        janet_struct_put(st, args[i], args[i + 1]);
    }
    return janet_wrap_struct(janet_struct_end(st));
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

    /* Check strict arity before messing with state */
    if (next_arity < func->def->min_arity) return 1;
    if (next_arity > func->def->max_arity) return 1;

    if (fiber->capacity < nextstacktop) {
        janet_fiber_setcapacity(fiber, 2 * nextstacktop);
#ifdef JANET_DEBUG
    } else {
        janet_fiber_refresh_memory(fiber);
#endif
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
        int st = func->def->flags & JANET_FUNCDEF_FLAG_STRUCTARG;
        if (tuplehead >= oldtop) {
            fiber->data[tuplehead] = st
                                     ? make_struct_n(NULL, 0)
                                     : janet_wrap_tuple(janet_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = st
                                     ? make_struct_n(
                                         fiber->data + tuplehead,
                                         oldtop - tuplehead)
                                     : janet_wrap_tuple(janet_tuple_n(
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
        janet_env_valid(env);
        int32_t len = env->length;
        size_t s = sizeof(Janet) * (size_t) len;
        Janet *vmem = janet_malloc(s);
        janet_vm.next_collection += (uint32_t) s;
        if (NULL == vmem) {
            JANET_OUT_OF_MEMORY;
        }
        Janet *values = env->as.fiber->data + env->offset;
        safe_memcpy(vmem, values, s);
        uint32_t *bitset = janet_stack_frame(values)->func->def->closure_bitset;
        if (bitset) {
            /* Clear unneeded references in closure environment */
            for (int32_t i = 0; i < len; i += 32) {
                uint32_t mask = ~(bitset[i >> 5]);
                int32_t maxj = i + 32 > len ? len : i + 32;
                for (int32_t j = i; j < maxj; j++) {
                    if (mask & 1) vmem[j] = janet_wrap_nil();
                    mask >>= 1;
                }
            }
        }
        env->offset = 0;
        env->as.values = vmem;
    }
}

/* Validate potentially untrusted func env (unmarshalled envs are difficult to verify) */
int janet_env_valid(JanetFuncEnv *env) {
    if (env->offset < 0) {
        int32_t real_offset = -(env->offset);
        JanetFiber *fiber = env->as.fiber;
        int32_t i = fiber->frame;
        while (i > 0) {
            JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            if (real_offset == i &&
                    frame->env == env &&
                    frame->func &&
                    frame->func->def->slotcount == env->length) {
                env->offset = real_offset;
                return 1;
            }
            i = frame->prevframe;
        }
        /* Invalid, set to empty off-stack variant. */
        env->offset = 0;
        env->length = 0;
        env->as.values = NULL;
        return 0;
    } else {
        return 1;
    }
}

/* Detach a fiber from the env if the target fiber has stopped mutating */
void janet_env_maybe_detach(JanetFuncEnv *env) {
    /* Check for detachable closure envs */
    janet_env_valid(env);
    if (env->offset > 0) {
        JanetFiberStatus s = janet_fiber_status(env->as.fiber);
        int isFinished = s == JANET_STATUS_DEAD ||
                         s == JANET_STATUS_ERROR ||
                         s == JANET_STATUS_USER0 ||
                         s == JANET_STATUS_USER1 ||
                         s == JANET_STATUS_USER2 ||
                         s == JANET_STATUS_USER3 ||
                         s == JANET_STATUS_USER4;
        if (isFinished) {
            janet_env_detach(env);
        }
    }
}

/* Create a tail frame for a function */
int janet_fiber_funcframe_tail(JanetFiber *fiber, JanetFunction *func) {
    int32_t i;
    int32_t nextframetop = fiber->frame + func->def->slotcount;
    int32_t nextstacktop = nextframetop + JANET_FRAME_SIZE;
    int32_t next_arity = fiber->stacktop - fiber->stackstart;
    int32_t stacksize;

    /* Check strict arity before messing with state */
    if (next_arity < func->def->min_arity) return 1;
    if (next_arity > func->def->max_arity) return 1;

    if (fiber->capacity < nextstacktop) {
        janet_fiber_setcapacity(fiber, 2 * nextstacktop);
#ifdef JANET_DEBUG
    } else {
        janet_fiber_refresh_memory(fiber);
#endif
    }

    Janet *stack = fiber->data + fiber->frame;
    Janet *args = fiber->data + fiber->stackstart;

    /* Detach old function */
    if (NULL != janet_fiber_frame(fiber)->func)
        janet_env_detach(janet_fiber_frame(fiber)->env);
    janet_fiber_frame(fiber)->env = NULL;

    /* Check varargs */
    if (func->def->flags & JANET_FUNCDEF_FLAG_VARARG) {
        int32_t tuplehead = fiber->stackstart + func->def->arity;
        int st = func->def->flags & JANET_FUNCDEF_FLAG_STRUCTARG;
        if (tuplehead >= fiber->stacktop) {
            if (tuplehead >= fiber->capacity) janet_fiber_setcapacity(fiber, 2 * (tuplehead + 1));
            for (i = fiber->stacktop; i < tuplehead; ++i) fiber->data[i] = janet_wrap_nil();
            fiber->data[tuplehead] = st
                                     ? make_struct_n(NULL, 0)
                                     : janet_wrap_tuple(janet_tuple_n(NULL, 0));
        } else {
            fiber->data[tuplehead] = st
                                     ? make_struct_n(
                                         fiber->data + tuplehead,
                                         fiber->stacktop - tuplehead)
                                     : janet_wrap_tuple(janet_tuple_n(
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
#ifdef JANET_DEBUG
    } else {
        janet_fiber_refresh_memory(fiber);
#endif
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

/* Pop a stack frame from the fiber. */
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

JanetFiberStatus janet_fiber_status(JanetFiber *f) {
    return ((f)->flags & JANET_FIBER_STATUS_MASK) >> JANET_FIBER_STATUS_OFFSET;
}

JanetFiber *janet_current_fiber(void) {
    return janet_vm.fiber;
}

JanetFiber *janet_root_fiber(void) {
    return janet_vm.root_fiber;
}

/* CFuns */

JANET_CORE_FN(cfun_fiber_getenv,
              "(fiber/getenv fiber)",
              "Gets the environment for a fiber. Returns nil if no such table is "
              "set yet.") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    return fiber->env ?
           janet_wrap_table(fiber->env) :
           janet_wrap_nil();
}

JANET_CORE_FN(cfun_fiber_setenv,
              "(fiber/setenv fiber table)",
              "Sets the environment table for a fiber. Set to nil to remove the current "
              "environment.") {
    janet_fixarity(argc, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    if (janet_checktype(argv[1], JANET_NIL)) {
        fiber->env = NULL;
    } else {
        fiber->env = janet_gettable(argv, 1);
    }
    return argv[0];
}

JANET_CORE_FN(cfun_fiber_new,
              "(fiber/new func &opt sigmask env)",
              "Create a new fiber with function body func. Can optionally "
              "take a set of signals `sigmask` to capture from child fibers, "
              "and an environment table `env`. The mask is specified as a keyword where each character "
              "is used to indicate a signal to block. If the ev module is enabled, and "
              "this fiber is used as an argument to `ev/go`, these \"blocked\" signals "
              "will result in messages being sent to the supervisor channel. "
              "The default sigmask is :y. "
              "For example,\n\n"
              "    (fiber/new myfun :e123)\n\n"
              "blocks error signals and user signals 1, 2 and 3. The signals are "
              "as follows:\n\n"
              "* :a - block all signals\n"
              "* :d - block debug signals\n"
              "* :e - block error signals\n"
              "* :t - block termination signals: error + user[0-4]\n"
              "* :u - block user signals\n"
              "* :y - block yield signals\n"
              "* :w - block await signals (user9)\n"
              "* :r - block interrupt signals (user8)\n"
              "* :0-9 - block a specific user signal\n\n"
              "The sigmask argument also can take environment flags. If any mutually "
              "exclusive flags are present, the last flag takes precedence.\n\n"
              "* :i - inherit the environment from the current fiber\n"
              "* :p - the environment table's prototype is the current environment table") {
    janet_arity(argc, 1, 3);
    JanetFunction *func = janet_getfunction(argv, 0);
    JanetFiber *fiber;
    if (func->def->min_arity > 1) {
        janet_panicf("fiber function must accept 0 or 1 arguments");
    }
    fiber = janet_fiber(func, 64, func->def->min_arity, NULL);
    janet_assert(fiber != NULL, "bad fiber arity check");
    if (argc == 3 && !janet_checktype(argv[2], JANET_NIL)) {
        fiber->env = janet_gettable(argv, 2);
    }
    if (argc >= 2) {
        int32_t i;
        JanetByteView view = janet_getbytes(argv, 1);
        fiber->flags = JANET_FIBER_RESUME_NO_USEVAL | JANET_FIBER_RESUME_NO_SKIP;
        janet_fiber_set_status(fiber, JANET_STATUS_NEW);
        for (i = 0; i < view.len; i++) {
            if (view.bytes[i] >= '0' && view.bytes[i] <= '9') {
                fiber->flags |= JANET_FIBER_MASK_USERN(view.bytes[i] - '0');
            } else {
                switch (view.bytes[i]) {
                    default:
                        janet_panicf("invalid flag %c, expected a, t, d, e, u, y, w, r, i, or p", view.bytes[i]);
                        break;
                    case 'a':
                        fiber->flags |=
                            JANET_FIBER_MASK_DEBUG |
                            JANET_FIBER_MASK_ERROR |
                            JANET_FIBER_MASK_USER |
                            JANET_FIBER_MASK_YIELD;
                        break;
                    case 't':
                        fiber->flags |=
                            JANET_FIBER_MASK_ERROR |
                            JANET_FIBER_MASK_USER0 |
                            JANET_FIBER_MASK_USER1 |
                            JANET_FIBER_MASK_USER2 |
                            JANET_FIBER_MASK_USER3 |
                            JANET_FIBER_MASK_USER4;
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
                    case 'w':
                        fiber->flags |= JANET_FIBER_MASK_USER9;
                        break;
                    case 'r':
                        fiber->flags |= JANET_FIBER_MASK_USER8;
                        break;
                    case 'i':
                        if (!janet_vm.fiber->env) {
                            janet_vm.fiber->env = janet_table(0);
                        }
                        fiber->env = janet_vm.fiber->env;
                        break;
                    case 'p':
                        if (!janet_vm.fiber->env) {
                            janet_vm.fiber->env = janet_table(0);
                        }
                        fiber->env = janet_table(0);
                        fiber->env->proto = janet_vm.fiber->env;
                        break;
                }
            }
        }
    }
    return janet_wrap_fiber(fiber);
}

JANET_CORE_FN(cfun_fiber_status,
              "(fiber/status fib)",
              "Get the status of a fiber. The status will be one of:\n\n"
              "* :dead - the fiber has finished\n"
              "* :error - the fiber has errored out\n"
              "* :debug - the fiber is suspended in debug mode\n"
              "* :pending - the fiber has been yielded\n"
              "* :user(0-7) - the fiber is suspended by a user signal\n"
              "* :interrupted - the fiber was interrupted\n"
              "* :suspended - the fiber is waiting to be resumed by the scheduler\n"
              "* :alive - the fiber is currently running and cannot be resumed\n"
              "* :new - the fiber has just been created and not yet run") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    uint32_t s = janet_fiber_status(fiber);
    return janet_ckeywordv(janet_status_names[s]);
}

JANET_CORE_FN(cfun_fiber_current,
              "(fiber/current)",
              "Returns the currently running fiber.") {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_fiber(janet_vm.fiber);
}

JANET_CORE_FN(cfun_fiber_root,
              "(fiber/root)",
              "Returns the current root fiber. The root fiber is the oldest ancestor "
              "that does not have a parent.") {
    (void) argv;
    janet_fixarity(argc, 0);
    return janet_wrap_fiber(janet_vm.root_fiber);
}

JANET_CORE_FN(cfun_fiber_maxstack,
              "(fiber/maxstack fib)",
              "Gets the maximum stack size in janet values allowed for a fiber. While memory for "
              "the fiber's stack is not allocated up front, the fiber will not allocated more "
              "than this amount and will throw a stack-overflow error if more memory is needed. ") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    return janet_wrap_integer(fiber->maxstack);
}

JANET_CORE_FN(cfun_fiber_setmaxstack,
              "(fiber/setmaxstack fib maxstack)",
              "Sets the maximum stack size in janet values for a fiber. By default, the "
              "maximum stack size is usually 8192.") {
    janet_fixarity(argc, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    int32_t maxs = janet_getinteger(argv, 1);
    if (maxs < 0) {
        janet_panic("expected positive integer");
    }
    fiber->maxstack = maxs;
    return argv[0];
}

int janet_fiber_can_resume(JanetFiber *fiber) {
    JanetFiberStatus s = janet_fiber_status(fiber);
    int isFinished = s == JANET_STATUS_DEAD ||
                     s == JANET_STATUS_ERROR ||
                     s == JANET_STATUS_USER0 ||
                     s == JANET_STATUS_USER1 ||
                     s == JANET_STATUS_USER2 ||
                     s == JANET_STATUS_USER3 ||
                     s == JANET_STATUS_USER4;
    return !isFinished;
}

JANET_CORE_FN(cfun_fiber_can_resume,
              "(fiber/can-resume? fiber)",
              "Check if a fiber is finished and cannot be resumed.") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    return janet_wrap_boolean(janet_fiber_can_resume(fiber));
}

JANET_CORE_FN(cfun_fiber_last_value,
              "(fiber/last-value fiber)",
              "Get the last value returned or signaled from the fiber.") {
    janet_fixarity(argc, 1);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    return fiber->last_value;
}

/* Module entry point */
void janet_lib_fiber(JanetTable *env) {
    JanetRegExt fiber_cfuns[] = {
        JANET_CORE_REG("fiber/new", cfun_fiber_new),
        JANET_CORE_REG("fiber/status", cfun_fiber_status),
        JANET_CORE_REG("fiber/root", cfun_fiber_root),
        JANET_CORE_REG("fiber/current", cfun_fiber_current),
        JANET_CORE_REG("fiber/maxstack", cfun_fiber_maxstack),
        JANET_CORE_REG("fiber/setmaxstack", cfun_fiber_setmaxstack),
        JANET_CORE_REG("fiber/getenv", cfun_fiber_getenv),
        JANET_CORE_REG("fiber/setenv", cfun_fiber_setenv),
        JANET_CORE_REG("fiber/can-resume?", cfun_fiber_can_resume),
        JANET_CORE_REG("fiber/last-value", cfun_fiber_last_value),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, fiber_cfuns);
}

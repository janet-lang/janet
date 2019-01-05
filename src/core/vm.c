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
#include "state.h"
#include "fiber.h"
#include "gc.h"
#include "symcache.h"
#include "util.h"

/* VM state */
JANET_THREAD_LOCAL JanetTable *janet_vm_registry;
JANET_THREAD_LOCAL int janet_vm_stackn = 0;
JANET_THREAD_LOCAL JanetFiber *janet_vm_fiber = NULL;

/* Virtual regsiters
 * 
 * One instruction word
 * CC | BB | AA | OP
 * DD | DD | DD | OP
 * EE | EE | AA | OP
 */
#define A ((*pc >> 8)  & 0xFF)
#define B ((*pc >> 16) & 0xFF)
#define C (*pc >> 24)
#define D (*pc >> 8)
#define E (*pc >> 16)

/* Signed interpretations of registers */
#define CS (*((int32_t *)pc) >> 24)
#define DS (*((int32_t *)pc) >> 8)
#define ES (*((int32_t *)pc) >> 16)

/* How we dispatch instructions. By default, we use
 * a switch inside an infinite loop. For GCC/clang, we use
 * computed gotos. */
#ifdef ____GNUC__
#define VM_START() { goto *op_lookup[first_opcode];
#define VM_END() }
#define VM_OP(op) label_##op :
#define VM_DEFAULT() label_unknown_op:
#define vm_next() goto *op_lookup[*pc & 0xFF];
static void *op_lookup[255] = {
    &&label_JOP_NOOP,
    &&label_JOP_ERROR,
    &&label_JOP_TYPECHECK,
    &&label_JOP_RETURN,
    &&label_JOP_RETURN_NIL,
    &&label_JOP_ADD_IMMEDIATE,
    &&label_JOP_ADD,
    &&label_JOP_SUBTRACT,
    &&label_JOP_MULTIPLY_IMMEDIATE,
    &&label_JOP_MULTIPLY,
    &&label_JOP_DIVIDE_IMMEDIATE,
    &&label_JOP_DIVIDE,
    &&label_JOP_BAND,
    &&label_JOP_BOR,
    &&label_JOP_BXOR,
    &&label_JOP_BNOT,
    &&label_JOP_SHIFT_LEFT,
    &&label_JOP_SHIFT_LEFT_IMMEDIATE,
    &&label_JOP_SHIFT_RIGHT,
    &&label_JOP_SHIFT_RIGHT_IMMEDIATE,
    &&label_JOP_SHIFT_RIGHT_UNSIGNED,
    &&label_JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE,
    &&label_JOP_MOVE_FAR,
    &&label_JOP_MOVE_NEAR,
    &&label_JOP_JUMP,
    &&label_JOP_JUMP_IF,
    &&label_JOP_JUMP_IF_NOT,
    &&label_JOP_GREATER_THAN,
    &&label_JOP_GREATER_THAN_IMMEDIATE,
    &&label_JOP_LESS_THAN,
    &&label_JOP_LESS_THAN_IMMEDIATE,
    &&label_JOP_EQUALS,
    &&label_JOP_EQUALS_IMMEDIATE,
    &&label_JOP_COMPARE,
    &&label_JOP_LOAD_NIL,
    &&label_JOP_LOAD_TRUE,
    &&label_JOP_LOAD_FALSE,
    &&label_JOP_LOAD_INTEGER,
    &&label_JOP_LOAD_CONSTANT,
    &&label_JOP_LOAD_UPVALUE,
    &&label_JOP_LOAD_SELF,
    &&label_JOP_SET_UPVALUE,
    &&label_JOP_CLOSURE,
    &&label_JOP_PUSH,
    &&label_JOP_PUSH_2,
    &&label_JOP_PUSH_3,
    &&label_JOP_PUSH_ARRAY,
    &&label_JOP_CALL,
    &&label_JOP_TAILCALL,
    &&label_JOP_RESUME,
    &&label_JOP_SIGNAL,
    &&label_JOP_GET,
    &&label_JOP_PUT,
    &&label_JOP_GET_INDEX,
    &&label_JOP_PUT_INDEX,
    &&label_JOP_LENGTH,
    &&label_JOP_MAKE_ARRAY,
    &&label_JOP_MAKE_BUFFER,
    &&label_JOP_MAKE_STRING,
    &&label_JOP_MAKE_STRUCT,
    &&label_JOP_MAKE_TABLE,
    &&label_JOP_MAKE_TUPLE,
    &&label_JOP_NUMERIC_LESS_THAN,
    &&label_JOP_NUMERIC_LESS_THAN_EQUAL,
    &&label_JOP_NUMERIC_GREATER_THAN,
    &&label_JOP_NUMERIC_GREATER_THAN_EQUAL,
    &&label_JOP_NUMERIC_EQUAL,
    &&label_unknown_op
};
#else
#define VM_START() uint8_t opcode = first_opcode; for (;;) {switch(opcode) {
#define VM_END() }}
#define VM_OP(op) case op :
#define VM_DEFAULT() default:
#define vm_next() opcode = *pc & 0xFF; continue
#endif

/* Commit and restore VM state before possible longjmp */
#define vm_commit() do { janet_stack_frame(stack)->pc = pc; } while (0)
#define vm_restore() do { \
    stack = fiber->data + fiber->frame; \
    pc = janet_stack_frame(stack)->pc; \
    func = janet_stack_frame(stack)->func; \
} while (0)

/* Next instruction variations */
#define maybe_collect() do {\
    if (janet_vm_next_collection >= janet_vm_gc_interval) janet_collect(); } while (0)
#define vm_checkgc_next() maybe_collect(); vm_next()
#define vm_pcnext() pc++; vm_next()
#define vm_checkgc_pcnext() maybe_collect(); vm_pcnext()

/* Handle certain errors in main vm loop */
#define vm_throw(e) do { retreg = janet_cstringv(e); goto vm_error; } while (0)
#define vm_assert(cond, e) do {if (!(cond)) vm_throw((e)); } while (0)
#define vm_assert_type(X, T) do { \
    if (!(janet_checktype((X), (T)))) { \
        expected_types = 1 << (T); \
        retreg = (X); \
        goto vm_type_error; \
    } \
} while (0)
#define vm_assert_types(X, TS) do { \
    if (!(janet_checktypes((X), (TS)))) { \
        expected_types = (TS); \
        retreg = (X); \
        goto vm_type_error; \
    } \
} while (0)

/* Templates for certain patterns in opcodes */
#define vm_binop_immediate(op)\
    {\
        Janet op1 = stack[B];\
        vm_assert_type(op1, JANET_NUMBER);\
        double x1 = janet_unwrap_number(op1);\
        stack[A] = janet_wrap_number(x1 op CS);\
        vm_pcnext();\
    }
#define _vm_bitop_immediate(op, type1)\
    {\
        Janet op1 = stack[B];\
        vm_assert_type(op1, JANET_NUMBER);\
        type1 x1 = (type1) janet_unwrap_integer(op1);\
        stack[A] = janet_wrap_integer(x1 op CS);\
        vm_pcnext();\
    }
#define vm_bitop_immediate(op) _vm_bitop_immediate(op, int32_t);
#define vm_bitopu_immediate(op) _vm_bitop_immediate(op, uint32_t);
#define _vm_binop(op, wrap)\
    {\
        Janet op1 = stack[B];\
        Janet op2 = stack[C];\
        vm_assert_type(op1, JANET_NUMBER);\
        vm_assert_type(op2, JANET_NUMBER);\
        double x1 = janet_unwrap_number(op1);\
        double x2 = janet_unwrap_number(op2);\
        stack[A] = wrap(x1 op x2);\
        vm_pcnext();\
    }
#define vm_binop(op) _vm_binop(op, janet_wrap_number)
#define vm_numcomp(op) _vm_binop(op, janet_wrap_boolean)
#define _vm_bitop(op, type1)\
    {\
        Janet op1 = stack[B];\
        Janet op2 = stack[C];\
        vm_assert_type(op1, JANET_NUMBER);\
        vm_assert_type(op2, JANET_NUMBER);\
        type1 x1 = (type1) janet_unwrap_integer(op1);\
        int32_t x2 = janet_unwrap_integer(op2);\
        stack[A] = janet_wrap_integer(x1 op x2);\
        vm_pcnext();\
    }
#define vm_bitop(op) _vm_bitop(op, int32_t)
#define vm_bitopu(op) _vm_bitop(op, uint32_t)

/* Interpreter main loop */
static JanetSignal run_vm(JanetFiber *fiber, Janet in) {

    /* Interpreter state */
    register Janet *stack;
    register uint32_t *pc;
    register JanetFunction *func;
    vm_restore();

    /* Keep in mind the garbage collector cannot see this value.
     * Values stored here should be used immediately */
    Janet retreg;

    /* Expected types on type error */
    uint16_t expected_types;

    /* Signal to return when done */
    JanetSignal signal = JANET_SIGNAL_OK;
    
    /* Only should be hit if the fiber is either waiting for a child, or
     * waiting to be resumed. In those cases, use input and increment pc. We
     * DO NOT use input when resuming a fiber that has been interrupted at a 
     * breakpoint. */
    if (janet_fiber_status(fiber) != JANET_STATUS_NEW && 
            ((*pc & 0xFF) == JOP_SIGNAL || (*pc & 0xFF) == JOP_RESUME)) {
        stack[A] = in;
        pc++;
    }

    /* The first opcode to execute. If the first opcode has
     * the breakpoint bit set and we were in the debug state, skip
     * that first breakpoint. */
    uint8_t first_opcode = (janet_fiber_status(fiber) == JANET_STATUS_DEBUG)
        ? (*pc & 0x7F)
        : (*pc & 0xFF);

    /* Main interpreter loop. Semantically is a switch on
     * (*pc & 0xFF) inside of an infinte loop. */
    VM_START();

    VM_DEFAULT();
    signal = JANET_SIGNAL_DEBUG;
    retreg = janet_wrap_nil();
    goto vm_exit;

    VM_OP(JOP_NOOP)
    vm_pcnext();

    VM_OP(JOP_ERROR)
    retreg = stack[A];
    goto vm_error;

    VM_OP(JOP_TYPECHECK)
    if (!janet_checktypes(stack[A], E)) {
        JanetArgs tempargs;
        tempargs.n = A + 1;
        tempargs.v = stack;
        janet_typemany_err(tempargs, A, E);
        goto vm_error;
    }
    vm_pcnext();

    VM_OP(JOP_RETURN)
    retreg = stack[D];
    goto vm_return;

    VM_OP(JOP_RETURN_NIL)
    retreg = janet_wrap_nil();
    goto vm_return;

    VM_OP(JOP_ADD_IMMEDIATE)
    vm_binop_immediate(+);

    VM_OP(JOP_ADD)
    vm_binop(+);

    VM_OP(JOP_SUBTRACT)
    vm_binop(-);

    VM_OP(JOP_MULTIPLY_IMMEDIATE)
    vm_binop_immediate(*);

    VM_OP(JOP_MULTIPLY)
    vm_binop(*);

    VM_OP(JOP_NUMERIC_LESS_THAN)
    vm_numcomp(<);

    VM_OP(JOP_NUMERIC_LESS_THAN_EQUAL)
    vm_numcomp(<=);

    VM_OP(JOP_NUMERIC_GREATER_THAN)
    vm_numcomp(>);

    VM_OP(JOP_NUMERIC_GREATER_THAN_EQUAL)
    vm_numcomp(>=);

    VM_OP(JOP_NUMERIC_EQUAL)
    vm_numcomp(==);

    VM_OP(JOP_DIVIDE_IMMEDIATE)
    vm_binop_immediate(/);

    VM_OP(JOP_DIVIDE)
    vm_binop(/);

    VM_OP(JOP_BAND)
    vm_bitop(&);

    VM_OP(JOP_BOR)
    vm_bitop(|);

    VM_OP(JOP_BXOR)
    vm_bitop(^);

    VM_OP(JOP_BNOT)
    {
        Janet op = stack[E];
        vm_assert_type(op, JANET_NUMBER);
        stack[A] = janet_wrap_integer(~janet_unwrap_integer(op));
        vm_pcnext();
    }

    VM_OP(JOP_SHIFT_RIGHT_UNSIGNED)
    vm_bitopu(>>);

    VM_OP(JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE)
    vm_bitopu_immediate(>>);

    VM_OP(JOP_SHIFT_RIGHT)
    vm_bitop(>>);

    VM_OP(JOP_SHIFT_RIGHT_IMMEDIATE)
    vm_bitop_immediate(>>);

    VM_OP(JOP_SHIFT_LEFT)
    vm_bitop(<<);

    VM_OP(JOP_SHIFT_LEFT_IMMEDIATE)
    vm_bitop_immediate(<<);

    VM_OP(JOP_MOVE_NEAR)
    stack[A] = stack[E];
    vm_pcnext();

    VM_OP(JOP_MOVE_FAR)
    stack[E] = stack[A];
    vm_pcnext();

    VM_OP(JOP_JUMP)
    pc += DS;
    vm_next();

    VM_OP(JOP_JUMP_IF)
    if (janet_truthy(stack[A])) {
        pc += ES;
    } else {
        pc++;
    }
    vm_next();

    VM_OP(JOP_JUMP_IF_NOT)
    if (janet_truthy(stack[A])) {
        pc++;
    } else {
        pc += ES;
    }
    vm_next();

    VM_OP(JOP_LESS_THAN)
    stack[A] = janet_wrap_boolean(janet_compare(stack[B], stack[C]) < 0);
    vm_pcnext();

    VM_OP(JOP_LESS_THAN_IMMEDIATE)
    stack[A] = janet_wrap_boolean(janet_unwrap_integer(stack[B]) < CS);
    vm_pcnext();

    VM_OP(JOP_GREATER_THAN)
    stack[A] = janet_wrap_boolean(janet_compare(stack[B], stack[C]) > 0);
    vm_pcnext();

    VM_OP(JOP_GREATER_THAN_IMMEDIATE)
    stack[A] = janet_wrap_boolean(janet_unwrap_integer(stack[B]) > CS);
    vm_pcnext();

    VM_OP(JOP_EQUALS)
    stack[A] = janet_wrap_boolean(janet_equals(stack[B], stack[C]));
    vm_pcnext();

    VM_OP(JOP_EQUALS_IMMEDIATE)
    stack[A] = janet_wrap_boolean(janet_unwrap_integer(stack[B]) == CS);
    vm_pcnext();

    VM_OP(JOP_COMPARE)
    stack[A] = janet_wrap_integer(janet_compare(stack[B], stack[C]));
    vm_pcnext();

    VM_OP(JOP_LOAD_NIL)
    stack[D] = janet_wrap_nil();
    vm_pcnext();

    VM_OP(JOP_LOAD_TRUE)
    stack[D] = janet_wrap_true();
    vm_pcnext();

    VM_OP(JOP_LOAD_FALSE)
    stack[D] = janet_wrap_false();
    vm_pcnext();

    VM_OP(JOP_LOAD_INTEGER)
    stack[A] = janet_wrap_integer(ES);
    vm_pcnext();

    VM_OP(JOP_LOAD_CONSTANT)
    {
        int32_t cindex = (int32_t)E;
        vm_assert(cindex < func->def->constants_length, "invalid constant");
        stack[A] = func->def->constants[cindex];
        vm_pcnext();
    }

    VM_OP(JOP_LOAD_SELF)
    stack[D] = janet_wrap_function(func);
    vm_pcnext();

    VM_OP(JOP_LOAD_UPVALUE)
    {
        int32_t eindex = B;
        int32_t vindex = C;
        JanetFuncEnv *env;
        vm_assert(func->def->environments_length > eindex, "invalid upvalue environment");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue index");
        if (env->offset) {
            /* On stack */
            stack[A] = env->as.fiber->data[env->offset + vindex];
        } else {
            /* Off stack */
            stack[A] = env->as.values[vindex];
        }
        vm_pcnext();
    }

    VM_OP(JOP_SET_UPVALUE)
    {
        int32_t eindex = B;
        int32_t vindex = C;
        JanetFuncEnv *env;
        vm_assert(func->def->environments_length > eindex, "invalid upvalue environment");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue index");
        if (env->offset) {
            env->as.fiber->data[env->offset + vindex] = stack[A];
        } else {
            env->as.values[vindex] = stack[A];
        }
        vm_pcnext();
    }

    VM_OP(JOP_CLOSURE)
    {
        JanetFuncDef *fd;
        JanetFunction *fn;
        int32_t elen;
        int32_t defindex = (int32_t)E;
        vm_assert(defindex < func->def->defs_length, "invalid funcdef");
        fd = func->def->defs[defindex];
        elen = fd->environments_length;
        fn = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction) + (elen * sizeof(JanetFuncEnv *)));
        fn->def = fd;
        {
            int32_t i;
            for (i = 0; i < elen; ++i) {
                int32_t inherit = fd->environments[i];
                if (inherit == -1) {
                    JanetStackFrame *frame = janet_stack_frame(stack);
                    if (!frame->env) {
                        /* Lazy capture of current stack frame */
                        JanetFuncEnv *env = janet_gcalloc(JANET_MEMORY_FUNCENV, sizeof(JanetFuncEnv));
                        env->offset = fiber->frame;
                        env->as.fiber = fiber;
                        env->length = func->def->slotcount;
                        frame->env = env;
                    }
                    fn->envs[i] = frame->env;
                } else {
                    fn->envs[i] = func->envs[inherit];
                }
            }
        }
        stack[A] = janet_wrap_function(fn);
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_PUSH)
    janet_fiber_push(fiber, stack[D]);
    stack = fiber->data + fiber->frame;
    vm_checkgc_pcnext();

    VM_OP(JOP_PUSH_2)
    janet_fiber_push2(fiber, stack[A], stack[E]);
    stack = fiber->data + fiber->frame;
    vm_checkgc_pcnext();

    VM_OP(JOP_PUSH_3)
    janet_fiber_push3(fiber, stack[A], stack[B], stack[C]);
    stack = fiber->data + fiber->frame;
    vm_checkgc_pcnext();

    VM_OP(JOP_PUSH_ARRAY)
    {
        const Janet *vals;
        int32_t len;
        if (janet_indexed_view(stack[D], &vals, &len)) {
            janet_fiber_pushn(fiber, vals, len);
        } else {
            retreg = stack[D];
            expected_types = JANET_TFLAG_INDEXED;
            goto vm_type_error;
        }
    }
    stack = fiber->data + fiber->frame;
    vm_checkgc_pcnext();

    VM_OP(JOP_CALL)
    {
        Janet callee = stack[E];
        if (fiber->maxstack &&
                fiber->stacktop > fiber->maxstack) {
            vm_throw("stack overflow");
        }
        if (janet_checktype(callee, JANET_FUNCTION)) {
            func = janet_unwrap_function(callee);
            janet_stack_frame(stack)->pc = pc;
            if (janet_fiber_funcframe(fiber, func))
                goto vm_arity_error;
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (janet_checktype(callee, JANET_CFUNCTION)) {
            JanetArgs args;
            args.n = fiber->stacktop - fiber->stackstart;
            janet_fiber_cframe(fiber, janet_unwrap_cfunction(callee));
            retreg = janet_wrap_nil();
            args.v = fiber->data + fiber->frame;
            args.ret = &retreg;
            if (janet_unwrap_cfunction(callee)(args)) {
                signal = JANET_SIGNAL_ERROR;
                goto vm_exit;
            }
            janet_fiber_popframe(fiber);
            if (fiber->frame == 0) goto vm_exit;
            stack = fiber->data + fiber->frame;
            stack[A] = retreg;
            vm_checkgc_pcnext();
        } else {
            int status;
            int32_t argn = fiber->stacktop - fiber->stackstart;
            Janet ds, key;
            if (janet_checktypes(callee, JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY)) {
                if (argn != 1) {
                    retreg = callee;
                    goto vm_arity_error_2;
                }
                ds = callee;
                key = fiber->data[fiber->stackstart];
            } else if (janet_checktypes(callee, JANET_TFLAG_SYMBOL | JANET_TFLAG_KEYWORD)) {
                if (argn != 1) {
                    retreg = callee;
                    goto vm_arity_error_2;
                }
                ds = fiber->data[fiber->stackstart];
                key = callee;
            } else {
                expected_types = JANET_TFLAG_CALLABLE;
                retreg = callee;
                goto vm_type_error;
            }
            fiber->stacktop = fiber->stackstart;
            status = janet_get(ds, key, stack + A);
            if (status == -2) {
                vm_throw("expected integer key");
            } else if (status == -1) {
                vm_throw("expected table or struct");
            }
            vm_pcnext();
        }
    }

    VM_OP(JOP_TAILCALL)
    {
        Janet callee = stack[D];
        if (janet_checktype(callee, JANET_FUNCTION)) {
            func = janet_unwrap_function(callee);
            if (janet_fiber_funcframe_tail(fiber, func))
                goto vm_arity_error;
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (janet_checktype(callee, JANET_CFUNCTION)) {
            JanetArgs args;
            args.n = fiber->stacktop - fiber->stackstart;
            janet_fiber_cframe(fiber, janet_unwrap_cfunction(callee));
            retreg = janet_wrap_nil();
            args.v = fiber->data + fiber->frame;
            args.ret = &retreg;
            if (janet_unwrap_cfunction(callee)(args)) {
                signal = JANET_SIGNAL_ERROR;
                goto vm_exit;
            }
            janet_fiber_popframe(fiber);
            janet_fiber_popframe(fiber);
            if (fiber->frame == 0) goto vm_exit;
            goto vm_reset;
        } else {
            int status;
            int32_t argn = fiber->stacktop - fiber->stackstart;
            Janet ds, key;
            if (janet_checktypes(callee, JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY)) {
                if (argn != 1) {
                    retreg = callee;
                    goto vm_arity_error_2;
                }
                ds = callee;
                key = fiber->data[fiber->stackstart];
            } else if (janet_checktypes(callee, JANET_TFLAG_SYMBOL | JANET_TFLAG_KEYWORD)) {
                if (argn != 1) {
                    retreg = callee;
                    goto vm_arity_error_2;
                }
                ds = fiber->data[fiber->stackstart];
                key = callee;
            } else {
                expected_types = JANET_TFLAG_CALLABLE;
                retreg = callee;
                goto vm_type_error;
            }
            fiber->stacktop = fiber->stackstart;
            status = janet_get(ds, key, &retreg);
            if (status == -2) {
                vm_throw("expected integer key");
            } else if (status == -1) {
                vm_throw("expected table or struct");
            }
            janet_fiber_popframe(fiber);
            if (fiber->frame == 0) goto vm_exit;
            goto vm_reset;
        }
    }

    VM_OP(JOP_RESUME)
    {
        vm_assert_type(stack[B], JANET_FIBER);
        JanetFiber *child = janet_unwrap_fiber(stack[B]);
        fiber->child = child;
        JanetSignal sig = janet_continue(child, stack[C], &retreg);
        if (sig != JANET_SIGNAL_OK && !(child->flags & (1 << sig))) {
            signal = sig;
            goto vm_exit;
        }
        fiber->child = NULL;
        stack[A] = retreg;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_SIGNAL)
    {
        int32_t s = C;
        if (s > JANET_SIGNAL_USER9) s = JANET_SIGNAL_USER9;
        if (s < 0) s = 0;
        signal = s;
        retreg = stack[B];
        goto vm_exit;
    }

    VM_OP(JOP_PUT)
    {
        Janet ds = stack[A];
        Janet key = stack[B];
        Janet value = stack[C];
        int status = janet_put(ds, key, value);
        if (status == -1) {
            expected_types = JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER | JANET_TFLAG_TABLE;
            retreg = ds;
            goto vm_type_error;
        } else if (status == -2) {
            vm_throw("expected integer key for data structure");
        } else if (status == -3) {
            vm_throw("expected integer value for data structure");
        }
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_PUT_INDEX)
    {
        Janet ds = stack[A];
        Janet value = stack[B];
        int32_t index = (int32_t)C;
        int status = janet_putindex(ds, index, value);
        if (status == -1) {
            expected_types = JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER | JANET_TFLAG_TABLE;
            retreg = ds;
            goto vm_type_error;
        } else if (status == -3) {
            vm_throw("expected integer value for data structure");
        }
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_GET)
    {
        Janet ds = stack[B];
        Janet key = stack[C];
        int status = janet_get(ds, key, stack + A);
        if (status == -1) {
            expected_types = JANET_TFLAG_LENGTHABLE;
            retreg = ds;
            goto vm_type_error;
        } else if (status == -2) {
            vm_throw("expected integer key for data structure");
        }
        vm_pcnext();
    }

    VM_OP(JOP_GET_INDEX)
    {
        Janet ds = stack[B];
        int32_t index = C;
        if (janet_getindex(ds, index, stack + A)) {
            expected_types = JANET_TFLAG_LENGTHABLE;
            retreg = ds;
            goto vm_type_error;
        }
        vm_pcnext();
    }

    VM_OP(JOP_LENGTH)
    {
        Janet x = stack[E];
        int32_t len;
        if (janet_length(x, &len)) {
            expected_types = JANET_TFLAG_LENGTHABLE;
            retreg = x;
            goto vm_type_error;
        }
        stack[A] = janet_wrap_integer(len);
        vm_pcnext();
    }

    VM_OP(JOP_MAKE_ARRAY)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        stack[D] = janet_wrap_array(janet_array_n(mem, count));
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_MAKE_TUPLE)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        stack[D] = janet_wrap_tuple(janet_tuple_n(mem, count));
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_MAKE_TABLE)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        if (count & 1)
            vm_throw("expected even number of arguments to table constructor");
        JanetTable *table = janet_table(count / 2);
        for (int32_t i = 0; i < count; i += 2)
            janet_table_put(table, mem[i], mem[i + 1]);
        stack[D] = janet_wrap_table(table);
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_MAKE_STRUCT)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        if (count & 1)
            vm_throw("expected even number of arguments to struct constructor");
        JanetKV *st = janet_struct_begin(count / 2);
        for (int32_t i = 0; i < count; i += 2)
            janet_struct_put(st, mem[i], mem[i + 1]);
        stack[D] = janet_wrap_struct(janet_struct_end(st));
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_MAKE_STRING)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        JanetBuffer buffer;
        janet_buffer_init(&buffer, 10 * count);
        for (int32_t i = 0; i < count; i++)
            janet_to_string_b(&buffer, mem[i]);
        stack[D] = janet_stringv(buffer.data, buffer.count);
        janet_buffer_deinit(&buffer);
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_MAKE_BUFFER)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        JanetBuffer *buffer = janet_buffer(10 * count);
        for (int32_t i = 0; i < count; i++)
            janet_to_string_b(buffer, mem[i]);
        stack[D] = janet_wrap_buffer(buffer);
        fiber->stacktop = fiber->stackstart;
        vm_checkgc_pcnext();
    }

    /* Handle returning from stack frame. Expect return value in retreg */
    vm_return:
    {
        janet_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        goto vm_reset;
    }

    /* Handle function calls with bad arity */
    vm_arity_error:
    {
        int32_t nargs = fiber->stacktop - fiber->stackstart;
        retreg = janet_wrap_string(janet_formatc("%v called with %d argument%s, expected %d",
                    janet_wrap_function(func),
                    nargs,
                    nargs == 1 ? "" : "s",
                    func->def->arity));
        signal = JANET_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Handle calling a data structure, keyword, or symbol with bad arity */
    vm_arity_error_2:
    {
        int32_t nargs = fiber->stacktop - fiber->stackstart;
        retreg = janet_wrap_string(janet_formatc("%v called with %d argument%s, expected 1",
                    retreg,
                    nargs,
                    nargs == 1 ? "" : "s"));
        signal = JANET_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Handle type errors. The found type is the type of retreg,
     * the expected types are in the expected_types field. */
    vm_type_error:
    {
        JanetBuffer errbuf;
        janet_buffer_init(&errbuf, 10);
        janet_buffer_push_cstring(&errbuf, "expected ");
        janet_buffer_push_types(&errbuf, expected_types);
        janet_buffer_push_cstring(&errbuf, ", got ");
        janet_buffer_push_cstring(&errbuf, janet_type_names[janet_type(retreg)]);
        retreg = janet_stringv(errbuf.data, errbuf.count);
        janet_buffer_deinit(&errbuf);
        signal = JANET_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Handle errors from c functions and vm opcodes */
    vm_error:
    {
        signal = JANET_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Exit from vm loop. If signal is not set explicitely, does
     * a successful return (JANET_SIGNAL_OK). */
    vm_exit:
    {
        janet_stack_frame(stack)->pc = pc;
        janet_fiber_push(fiber, retreg);
        return signal;
    }

    /* Reset state of machine */
    vm_reset:
    {
        vm_restore();
        stack[A] = retreg;
        vm_checkgc_pcnext();
    }

    VM_END()
}

/* Enter the main vm loop */
JanetSignal janet_continue(JanetFiber *fiber, Janet in, Janet *out) {

    /* Check conditions */
    if (janet_vm_stackn >= JANET_RECURSION_GUARD) {
        janet_fiber_set_status(fiber, JANET_STATUS_ERROR);
        *out = janet_cstringv("C stack recursed too deeply");
        return JANET_SIGNAL_ERROR;
    }
    JanetFiberStatus startstatus = janet_fiber_status(fiber);
    if (startstatus == JANET_STATUS_ALIVE ||
            startstatus == JANET_STATUS_DEAD ||
            startstatus == JANET_STATUS_ERROR) {
        *out = janet_cstringv("cannot resume alive, dead, or errored fiber");
        return JANET_SIGNAL_ERROR;
    }

    /* Continue child fiber if it exists */
    if (fiber->child) {
        JanetFiber *child = fiber->child;
        janet_vm_stackn++;
        JanetSignal sig = janet_continue(child, in, &in);
        janet_vm_stackn--;
        if (sig != JANET_SIGNAL_OK && !(child->flags & (1 << sig))) {
            *out = in;
            return sig;
        }
        fiber->child = NULL;
    }

    /* Prepare state */
    janet_vm_stackn++;
    janet_gcroot(janet_wrap_fiber(fiber));
    JanetFiber *old_vm_fiber = janet_vm_fiber;
    janet_vm_fiber = fiber;
    janet_fiber_set_status(fiber, JANET_STATUS_ALIVE);

    /* Run loop */
    JanetSignal signal = run_vm(fiber, in);

    /* Tear down */
    janet_fiber_set_status(fiber, signal);
    janet_vm_fiber = old_vm_fiber;
    janet_vm_stackn--;
    janet_gcunroot(janet_wrap_fiber(fiber));

    /* Pop error or return value from fiber stack */
    *out = fiber->data[--fiber->stacktop];

    return signal;
}

JanetSignal janet_call(
        JanetFunction *fun,
        int32_t argn,
        const Janet *argv,
        Janet *out,
        JanetFiber **f) {
    JanetFiber *fiber = janet_fiber_n(fun, 64, argv, argn);
    if (f) *f = fiber;
    if (!fiber) {
        *out = janet_cstringv("arity mismatch");
        return JANET_SIGNAL_ERROR;
    }
    return janet_continue(fiber, janet_wrap_nil(), out);
}

/* Setup VM */
int janet_init(void) {
    /* Garbage collection */
    janet_vm_blocks = NULL;
    janet_vm_next_collection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * incredibly horrible for performance, but can help ensure
     * there are no memory bugs during development */
    janet_vm_gc_interval = 0x10000;
    janet_symcache_init();
    /* Initialize gc roots */
    janet_vm_roots = NULL;
    janet_vm_root_count = 0;
    janet_vm_root_capacity = 0;
    /* Initialize registry */
    janet_vm_registry = janet_table(0);
    janet_gcroot(janet_wrap_table(janet_vm_registry));
    return 0;
}

/* Clear all memory associated with the VM */
void janet_deinit(void) {
    janet_clear_memory();
    janet_symcache_deinit();
    free(janet_vm_roots);
    janet_vm_roots = NULL;
    janet_vm_root_count = 0;
    janet_vm_root_capacity = 0;
    janet_vm_registry = NULL;
}

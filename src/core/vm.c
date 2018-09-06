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

/* Maybe collect garbage */
#define janet_maybe_collect() do {\
    if (janet_vm_next_collection >= janet_vm_gc_interval) janet_collect(); } while (0)

/* Start running the VM from where it left off. */
JanetSignal janet_continue(JanetFiber *fiber, Janet in, Janet *out) {

    /* Save old fiber to reset */
    JanetFiber *old_vm_fiber = janet_vm_fiber;

    /* interpreter state */
    register Janet *stack;
    register uint32_t *pc;
    register JanetFunction *func;

    /* Keep in mind the garbage collector cannot see this value.
     * Values stored here should be used immediately */
    Janet retreg;

    /* Expected types on type error */
    uint16_t expected_types;

    /* Signal to return when done */
    JanetSignal signal = JANET_SIGNAL_OK;

    /* Ensure fiber is not alive, dead, or error */
    JanetFiberStatus startstatus = janet_fiber_status(fiber);
    if (startstatus == JANET_STATUS_ALIVE ||
            startstatus == JANET_STATUS_DEAD ||
            startstatus == JANET_STATUS_ERROR) {
        *out = janet_cstringv("cannot resume alive, dead, or errored fiber");
        return JANET_SIGNAL_ERROR;
    }

    /* Increment the stackn */
    if (janet_vm_stackn >= JANET_RECURSION_GUARD) {
        janet_fiber_set_status(fiber, JANET_STATUS_ERROR);
        *out = janet_cstringv("C stack recursed too deeply");
        return JANET_SIGNAL_ERROR;
    }
    janet_vm_stackn++;

    /* Setup fiber state */
    janet_vm_fiber = fiber;
    janet_gcroot(janet_wrap_fiber(fiber));
    janet_gcroot(in);
    if (startstatus == JANET_STATUS_NEW) {
        janet_fiber_push(fiber, in);
        if (janet_fiber_funcframe(fiber, fiber->root)) {
            janet_gcunroot(janet_wrap_fiber(fiber));
            janet_gcunroot(in);
            *out = janet_wrap_string(janet_formatc(
                        "Could not start fiber with function of arity %d",
                        fiber->root->def->arity));
            return JANET_SIGNAL_ERROR;
        }
    }
    janet_fiber_set_status(fiber, JANET_STATUS_ALIVE);
    stack = fiber->data + fiber->frame;
    pc = janet_stack_frame(stack)->pc;
    func = janet_stack_frame(stack)->func;

    /* Used to extract bits from the opcode that correspond to arguments.
     * Pulls out unsigned integers */
#define oparg(shift, mask) (((*pc) >> ((shift) << 3)) & (mask))

    /* Check for child fiber. If there is a child, run child before self.
     * This should only be hit when the current fiber is pending on a RESUME
     * instruction. */
    if (fiber->child) {
        retreg = in;
        goto vm_resume_child;
    } else if (fiber->flags & JANET_FIBER_FLAG_SIGNAL_WAITING) {
        /* If waiting for response to signal, use input and increment pc */
        stack[oparg(1, 0xFF)] = in;
        pc++;
        fiber->flags &= ~JANET_FIBER_FLAG_SIGNAL_WAITING;
    }

/* Use computed gotos for GCC and clang, otherwise use switch */
#ifdef __GNUC__
#define VM_START() {vm_next();
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
    &&label_JOP_ADD_INTEGER,
    &&label_JOP_ADD_IMMEDIATE,
    &&label_JOP_ADD_REAL,
    &&label_JOP_ADD,
    &&label_JOP_SUBTRACT_INTEGER,
    &&label_JOP_SUBTRACT_REAL,
    &&label_JOP_SUBTRACT,
    &&label_JOP_MULTIPLY_INTEGER,
    &&label_JOP_MULTIPLY_IMMEDIATE,
    &&label_JOP_MULTIPLY_REAL,
    &&label_JOP_MULTIPLY,
    &&label_JOP_DIVIDE_INTEGER,
    &&label_JOP_DIVIDE_IMMEDIATE,
    &&label_JOP_DIVIDE_REAL,
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
    &&label_JOP_GREATER_THAN_INTEGER,
    &&label_JOP_GREATER_THAN_IMMEDIATE,
    &&label_JOP_GREATER_THAN_REAL,
    &&label_JOP_GREATER_THAN_EQUAL_REAL,
    &&label_JOP_LESS_THAN,
    &&label_JOP_LESS_THAN_INTEGER,
    &&label_JOP_LESS_THAN_IMMEDIATE,
    &&label_JOP_LESS_THAN_REAL,
    &&label_JOP_LESS_THAN_EQUAL_REAL,
    &&label_JOP_EQUALS,
    &&label_JOP_EQUALS_INTEGER,
    &&label_JOP_EQUALS_IMMEDIATE,
    &&label_JOP_EQUALS_REAL,
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
#define VM_START() for(;;){switch(*pc & 0xFF){
#define VM_END() }}
#define VM_OP(op) case op :
#define VM_DEFAULT() default:
#define vm_next() continue
#endif

#define vm_checkgc_next() janet_maybe_collect(); vm_next()

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
    if (!((1 << janet_type(X)) & (TS))) { \
        expected_types = (TS); \
        retreg = (X); \
        goto vm_type_error; \
    } \
} while (0)

#define vm_binop_integer(op) \
    stack[oparg(1, 0xFF)] = janet_wrap_integer(\
        janet_unwrap_integer(stack[oparg(2, 0xFF)]) op janet_unwrap_integer(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_real(op)\
    stack[oparg(1, 0xFF)] = janet_wrap_real(\
        janet_unwrap_real(stack[oparg(2, 0xFF)]) op janet_unwrap_real(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_immediate(op)\
    stack[oparg(1, 0xFF)] = janet_wrap_integer(\
        janet_unwrap_integer(stack[oparg(2, 0xFF)]) op (*((int32_t *)pc) >> 24)\
    );\
    pc++;\
    vm_next();

#define vm_binop(op)\
    {\
        Janet op1 = stack[oparg(2, 0xFF)];\
        Janet op2 = stack[oparg(3, 0xFF)];\
        vm_assert_types(op1, JANET_TFLAG_NUMBER);\
        vm_assert_types(op2, JANET_TFLAG_NUMBER);\
        stack[oparg(1, 0xFF)] = janet_checktype(op1, JANET_INTEGER)\
            ? (janet_checktype(op2, JANET_INTEGER)\
                ? janet_wrap_integer(janet_unwrap_integer(op1) op janet_unwrap_integer(op2))\
                : janet_wrap_real((double)janet_unwrap_integer(op1) op janet_unwrap_real(op2)))\
            : (janet_checktype(op2, JANET_INTEGER)\
                ? janet_wrap_real(janet_unwrap_real(op1) op (double)janet_unwrap_integer(op2))\
                : janet_wrap_real(janet_unwrap_real(op1) op janet_unwrap_real(op2)));\
        pc++;\
        vm_next();\
    }

#define vm_numcomp(op)\
    {\
        Janet op1 = stack[oparg(2, 0xFF)];\
        Janet op2 = stack[oparg(3, 0xFF)];\
        vm_assert_types(op1, JANET_TFLAG_NUMBER);\
        vm_assert_types(op2, JANET_TFLAG_NUMBER);\
        stack[oparg(1, 0xFF)] = janet_wrap_boolean(janet_checktype(op1, JANET_INTEGER)\
            ? (janet_checktype(op2, JANET_INTEGER)\
                ? janet_unwrap_integer(op1) op janet_unwrap_integer(op2)\
                : (double)janet_unwrap_integer(op1) op janet_unwrap_real(op2))\
            : (janet_checktype(op2, JANET_INTEGER)\
                ? janet_unwrap_real(op1) op (double)janet_unwrap_integer(op2)\
                : janet_unwrap_real(op1) op janet_unwrap_real(op2)));\
        pc++;\
        vm_next();\
    }

    /* Main interpreter loop. Semantically is a switch on
     * (*pc & 0xFF) inside of an infinte loop. */
    VM_START();

    VM_DEFAULT();
    retreg = janet_wrap_nil();
    goto vm_exit;

    VM_OP(JOP_NOOP)
    pc++;
    vm_next();

    VM_OP(JOP_ERROR)
    retreg = stack[oparg(1, 0xFF)];
    goto vm_error;

    VM_OP(JOP_TYPECHECK)
    if (!((1 << janet_type(stack[oparg(1, 0xFF)])) & oparg(2, 0xFFFF))) {
        JanetArgs tempargs;
        tempargs.n = oparg(1, 0xFF) + 1;
        tempargs.v = stack;
        janet_typemany_err(tempargs, oparg(1, 0xFF), oparg(2, 0xFFFF));
        goto vm_error;
    }
    pc++;
    vm_next();

    VM_OP(JOP_RETURN)
    retreg = stack[oparg(1, 0xFFFFFF)];
    goto vm_return;

    VM_OP(JOP_RETURN_NIL)
    retreg = janet_wrap_nil();
    goto vm_return;

    VM_OP(JOP_ADD_INTEGER)
    vm_binop_integer(+);

    VM_OP(JOP_ADD_IMMEDIATE)
    vm_binop_immediate(+);

    VM_OP(JOP_ADD_REAL)
    vm_binop_real(+);

    VM_OP(JOP_ADD)
    vm_binop(+);

    VM_OP(JOP_SUBTRACT_INTEGER)
    vm_binop_integer(-);

    VM_OP(JOP_SUBTRACT_REAL)
    vm_binop_real(-);

    VM_OP(JOP_SUBTRACT)
    vm_binop(-);

    VM_OP(JOP_MULTIPLY_INTEGER)
    vm_binop_integer(*);

    VM_OP(JOP_MULTIPLY_IMMEDIATE)
    vm_binop_immediate(*);

    VM_OP(JOP_MULTIPLY_REAL)
    vm_binop_real(*);

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

    VM_OP(JOP_DIVIDE_INTEGER)
    vm_assert(janet_unwrap_integer(stack[oparg(3, 0xFF)]) != 0, "integer divide error");
    vm_assert(!(janet_unwrap_integer(stack[oparg(3, 0xFF)]) == -1 &&
                janet_unwrap_integer(stack[oparg(2, 0xFF)]) == INT32_MIN),
            "integer divide error");
    vm_binop_integer(/);

    VM_OP(JOP_DIVIDE_IMMEDIATE)
    {
        int32_t op1 = janet_unwrap_integer(stack[oparg(2, 0xFF)]);
        int32_t op2 = *((int32_t *)pc) >> 24;
        /* Check for degenerate integer division (divide by zero, and dividing
         * min value by -1). These checks could be omitted if the arg is not
         * 0 or -1. */
        if (op2 == 0)
            vm_throw("integer divide error");
        if (op2 == -1 && op1 == INT32_MIN)
            vm_throw("integer divide error");
        else
            stack[oparg(1, 0xFF)] = janet_wrap_integer(op1 / op2);
        pc++;
        vm_next();
    }

    VM_OP(JOP_DIVIDE_REAL)
    vm_binop_real(/);

    VM_OP(JOP_DIVIDE)
    {
        Janet op1 = stack[oparg(2, 0xFF)];
        Janet op2 = stack[oparg(3, 0xFF)];
        vm_assert_types(op1, JANET_TFLAG_NUMBER);
        vm_assert_types(op2, JANET_TFLAG_NUMBER);
        if (janet_checktype(op2, JANET_INTEGER) && janet_unwrap_integer(op2) == 0)
            vm_throw("integer divide by zero");
        if (janet_checktype(op2, JANET_INTEGER) && janet_unwrap_integer(op2) == -1 &&
            janet_checktype(op1, JANET_INTEGER) && janet_unwrap_integer(op1) == INT32_MIN)
            vm_throw("integer divide out of range");
        stack[oparg(1, 0xFF)] = janet_checktype(op1, JANET_INTEGER)
            ? (janet_checktype(op2, JANET_INTEGER)
                ? janet_wrap_integer(janet_unwrap_integer(op1) / janet_unwrap_integer(op2))
                : janet_wrap_real((double)janet_unwrap_integer(op1) / janet_unwrap_real(op2)))
            : (janet_checktype(op2, JANET_INTEGER)
                ? janet_wrap_real(janet_unwrap_real(op1) / (double)janet_unwrap_integer(op2))
                : janet_wrap_real(janet_unwrap_real(op1) / janet_unwrap_real(op2)));
        pc++;
        vm_next();
    }

    VM_OP(JOP_BAND)
    vm_binop_integer(&);

    VM_OP(JOP_BOR)
    vm_binop_integer(|);

    VM_OP(JOP_BXOR)
    vm_binop_integer(^);

    VM_OP(JOP_BNOT)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(~janet_unwrap_integer(stack[oparg(2, 0xFFFF)]));
    ++pc;
    vm_next();

    VM_OP(JOP_SHIFT_RIGHT_UNSIGNED)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(
        (int32_t)(((uint32_t)janet_unwrap_integer(stack[oparg(2, 0xFF)]))
        >>
        janet_unwrap_integer(stack[oparg(3, 0xFF)]))
    );
    pc++;
    vm_next();

    VM_OP(JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(
        (int32_t) (((uint32_t)janet_unwrap_integer(stack[oparg(2, 0xFF)])) >> oparg(3, 0xFF))
    );
    pc++;
    vm_next();

    VM_OP(JOP_SHIFT_RIGHT)
    vm_binop_integer(>>);

    VM_OP(JOP_SHIFT_RIGHT_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(
        (int32_t)(janet_unwrap_integer(stack[oparg(2, 0xFF)]) >> oparg(3, 0xFF))
    );
    pc++;
    vm_next();

    VM_OP(JOP_SHIFT_LEFT)
    vm_binop_integer(<<);

    VM_OP(JOP_SHIFT_LEFT_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(
        janet_unwrap_integer(stack[oparg(2, 0xFF)]) << oparg(3, 0xFF)
    );
    pc++;
    vm_next();

    VM_OP(JOP_MOVE_NEAR)
    stack[oparg(1, 0xFF)] = stack[oparg(2, 0xFFFF)];
    pc++;
    vm_next();

    VM_OP(JOP_MOVE_FAR)
    stack[oparg(2, 0xFFFF)] = stack[oparg(1, 0xFF)];
    pc++;
    vm_next();

    VM_OP(JOP_JUMP)
    pc += (*(int32_t *)pc) >> 8;
    vm_next();

    VM_OP(JOP_JUMP_IF)
    if (janet_truthy(stack[oparg(1, 0xFF)])) {
        pc += (*(int32_t *)pc) >> 16;
    } else {
        pc++;
    }
    vm_next();

    VM_OP(JOP_JUMP_IF_NOT)
    if (janet_truthy(stack[oparg(1, 0xFF)])) {
        pc++;
    } else {
        pc += (*(int32_t *)pc) >> 16;
    }
    vm_next();

    VM_OP(JOP_LESS_THAN)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(janet_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ) < 0);
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_LESS_THAN_INTEGER)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) <
            janet_unwrap_integer(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_LESS_THAN_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) < ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_LESS_THAN_REAL)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_real(stack[oparg(2, 0xFF)]) <
            janet_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_LESS_THAN_EQUAL_REAL)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_real(stack[oparg(2, 0xFF)]) <=
            janet_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();


    VM_OP(JOP_GREATER_THAN)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(janet_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ) > 0);
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_GREATER_THAN_INTEGER)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) >
            janet_unwrap_integer(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_GREATER_THAN_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) > ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_GREATER_THAN_REAL)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_real(stack[oparg(2, 0xFF)]) >
            janet_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_GREATER_THAN_EQUAL_REAL)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_real(stack[oparg(2, 0xFF)]) >=
            janet_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    VM_OP(JOP_EQUALS)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(janet_equals(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_EQUALS_INTEGER)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) ==
            janet_unwrap_integer(stack[oparg(3, 0xFF)])
        );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_EQUALS_REAL)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_real(stack[oparg(2, 0xFF)]) ==
            janet_unwrap_real(stack[oparg(3, 0xFF)])
        );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(JOP_EQUALS_IMMEDIATE)
    stack[oparg(1, 0xFF)] = janet_wrap_boolean(
            janet_unwrap_integer(stack[oparg(2, 0xFF)]) == ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    VM_OP(JOP_COMPARE)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(janet_compare(
        stack[oparg(2, 0xFF)],
        stack[oparg(3, 0xFF)]
    ));
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_NIL)
    stack[oparg(1, 0xFFFFFF)] = janet_wrap_nil();
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_TRUE)
    stack[oparg(1, 0xFFFFFF)] = janet_wrap_boolean(1);
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_FALSE)
    stack[oparg(1, 0xFFFFFF)] = janet_wrap_boolean(0);
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_INTEGER)
    stack[oparg(1, 0xFF)] = janet_wrap_integer(*((int32_t *)pc) >> 16);
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_CONSTANT)
    {
        int32_t index = oparg(2, 0xFFFF);
        vm_assert(index < func->def->constants_length, "invalid constant");
        stack[oparg(1, 0xFF)] = func->def->constants[index];
        pc++;
        vm_next();
    }

    VM_OP(JOP_LOAD_SELF)
    stack[oparg(1, 0xFFFFFF)] = janet_wrap_function(func);
    pc++;
    vm_next();

    VM_OP(JOP_LOAD_UPVALUE)
    {
        int32_t eindex = oparg(2, 0xFF);
        int32_t vindex = oparg(3, 0xFF);
        JanetFuncEnv *env;
        vm_assert(func->def->environments_length > eindex, "invalid upvalue environment");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue index");
        if (env->offset) {
            /* On stack */
            stack[oparg(1, 0xFF)] = env->as.fiber->data[env->offset + vindex];
        } else {
            /* Off stack */
            stack[oparg(1, 0xFF)] = env->as.values[vindex];
        }
        pc++;
        vm_next();
    }

    VM_OP(JOP_SET_UPVALUE)
    {
        int32_t eindex = oparg(2, 0xFF);
        int32_t vindex = oparg(3, 0xFF);
        JanetFuncEnv *env;
        vm_assert(func->def->environments_length > eindex, "invalid upvalue environment");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue index");
        if (env->offset) {
            env->as.fiber->data[env->offset + vindex] = stack[oparg(1, 0xFF)];
        } else {
            env->as.values[vindex] = stack[oparg(1, 0xFF)];
        }
        pc++;
        vm_next();
    }

    VM_OP(JOP_CLOSURE)
    {
        JanetFuncDef *fd;
        JanetFunction *fn;
        int32_t elen;
        vm_assert((int32_t)oparg(2, 0xFFFF) < func->def->defs_length, "invalid funcdef");
        fd = func->def->defs[(int32_t)oparg(2, 0xFFFF)];
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
        stack[oparg(1, 0xFF)] = janet_wrap_function(fn);
        pc++;
        vm_checkgc_next();
    }

    VM_OP(JOP_PUSH)
        janet_fiber_push(fiber, stack[oparg(1, 0xFFFFFF)]);
        pc++;
        stack = fiber->data + fiber->frame;
        vm_checkgc_next();

    VM_OP(JOP_PUSH_2)
        janet_fiber_push2(fiber,
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFFFF)]);
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(JOP_PUSH_3)
        janet_fiber_push3(fiber,
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(JOP_PUSH_ARRAY)
    {
        const Janet *vals;
        int32_t len;
        if (janet_indexed_view(stack[oparg(1, 0xFFFFFF)], &vals, &len)) {
            janet_fiber_pushn(fiber, vals, len);
        } else {
            retreg = stack[oparg(1, 0xFFFFFF)];
            expected_types = JANET_TFLAG_INDEXED;
            goto vm_type_error;
        }
    }
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(JOP_CALL)
    {
        Janet callee = stack[oparg(2, 0xFFFF)];
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
            if ((signal = janet_unwrap_cfunction(callee)(args))) {
                goto vm_exit;
            }
            goto vm_return_cfunc;
        }
        expected_types = JANET_TFLAG_CALLABLE;
        retreg = callee;
        goto vm_type_error;
    }

    VM_OP(JOP_TAILCALL)
    {
        Janet callee = stack[oparg(1, 0xFFFFFF)];
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
            if ((signal = janet_unwrap_cfunction(callee)(args))) {
                goto vm_exit;
            }
            goto vm_return_cfunc_tail;
        }
        expected_types = JANET_TFLAG_CALLABLE;
        retreg = callee;
        goto vm_type_error;
    }

    VM_OP(JOP_RESUME)
    {
        Janet fiberval = stack[oparg(2, 0xFF)];
        vm_assert_type(fiberval, JANET_FIBER);
        retreg = stack[oparg(3, 0xFF)];
        fiber->child = janet_unwrap_fiber(fiberval);
        goto vm_resume_child;
    }

    VM_OP(JOP_SIGNAL)
    {
        int32_t s = oparg(3, 0xFF);
        if (s > JANET_SIGNAL_USER9) s = JANET_SIGNAL_USER9;
        if (s < 0) s = 0;
        signal = s;
        retreg = stack[oparg(2, 0xFF)];
        fiber->flags |= JANET_FIBER_FLAG_SIGNAL_WAITING;
        goto vm_exit;
    }

    VM_OP(JOP_PUT)
    {
        Janet ds = stack[oparg(1, 0xFF)];
        Janet key = stack[oparg(2, 0xFF)];
        Janet value = stack[oparg(3, 0xFF)];
        switch (janet_type(ds)) {
            default:
                expected_types = JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER | JANET_TFLAG_TABLE;
                retreg = ds;
                goto vm_type_error;
            case JANET_ARRAY:
            {
                int32_t index;
                JanetArray *array = janet_unwrap_array(ds);
                vm_assert_type(key, JANET_INTEGER);
                if (janet_unwrap_integer(key) < 0)
                    vm_throw("expected non-negative integer key");
                index = janet_unwrap_integer(key);
                if (index == INT32_MAX)
                    vm_throw("key too large");
                if (index >= array->count) {
                    janet_array_setcount(array, index + 1);
                }
                array->data[index] = value;
                break;
            }
            case JANET_BUFFER:
            {
                int32_t index;
                JanetBuffer *buffer = janet_unwrap_buffer(ds);
                vm_assert_type(key, JANET_INTEGER);
                if (janet_unwrap_integer(key) < 0)
                    vm_throw("expected non-negative integer key");
                index = janet_unwrap_integer(key);
                if (index == INT32_MAX)
                    vm_throw("key too large");
                vm_assert_type(value, JANET_INTEGER);
                if (index >= buffer->count) {
                    janet_buffer_setcount(buffer, index + 1);
                }
                buffer->data[index] = (uint8_t) (janet_unwrap_integer(value) & 0xFF);
                break;
            }
            case JANET_TABLE:
                janet_table_put(janet_unwrap_table(ds), key, value);
                break;
        }
        ++pc;
        vm_checkgc_next();
    }

    VM_OP(JOP_PUT_INDEX)
    {
        Janet ds = stack[oparg(1, 0xFF)];
        Janet value = stack[oparg(2, 0xFF)];
        int32_t index = oparg(3, 0xFF);
        switch (janet_type(ds)) {
            default:
                expected_types = JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER;
                retreg = ds;
                goto vm_type_error;
            case JANET_ARRAY:
                if (index >= janet_unwrap_array(ds)->count) {
                    janet_array_ensure(janet_unwrap_array(ds), 2 * index);
                    janet_unwrap_array(ds)->count = index + 1;
                }
                janet_unwrap_array(ds)->data[index] = value;
                break;
            case JANET_BUFFER:
                vm_assert_type(value, JANET_INTEGER);
                if (index >= janet_unwrap_buffer(ds)->count) {
                    janet_buffer_ensure(janet_unwrap_buffer(ds), 2 * index);
                    janet_unwrap_buffer(ds)->count = index + 1;
                }
                janet_unwrap_buffer(ds)->data[index] = janet_unwrap_integer(value);
                break;
        }
        ++pc;
        vm_checkgc_next();
    }

    VM_OP(JOP_GET)
    {
        Janet ds = stack[oparg(2, 0xFF)];
        Janet key = stack[oparg(3, 0xFF)];
        Janet value;
        switch (janet_type(ds)) {
            default:
                expected_types = JANET_TFLAG_LENGTHABLE;
                retreg = ds;
                goto vm_type_error;
            case JANET_STRUCT:
                value = janet_struct_get(janet_unwrap_struct(ds), key);
                break;
            case JANET_TABLE:
                value = janet_table_get(janet_unwrap_table(ds), key);
                break;
            case JANET_ARRAY:
                {
                    JanetArray *array = janet_unwrap_array(ds);
                    int32_t index;
                    vm_assert_type(key, JANET_INTEGER);
                    index = janet_unwrap_integer(key);
                    if (index < 0 || index >= array->count) {
                        /*vm_throw("index out of bounds");*/
                        value = janet_wrap_nil();
                    } else {
                        value = array->data[index];
                    }
                    break;
                }
            case JANET_TUPLE:
                {
                    const Janet *tuple = janet_unwrap_tuple(ds);
                    int32_t index;
                    vm_assert_type(key, JANET_INTEGER);
                    index = janet_unwrap_integer(key);
                    if (index < 0 || index >= janet_tuple_length(tuple)) {
                        /*vm_throw("index out of bounds");*/
                        value = janet_wrap_nil();
                    } else {
                        value = tuple[index];
                    }
                    break;
                }
            case JANET_BUFFER:
                {
                    JanetBuffer *buffer = janet_unwrap_buffer(ds);
                    int32_t index;
                    vm_assert_type(key, JANET_INTEGER);
                    index = janet_unwrap_integer(key);
                    if (index < 0 || index >= buffer->count) {
                        /*vm_throw("index out of bounds");*/
                        value = janet_wrap_nil();
                    } else {
                        value = janet_wrap_integer(buffer->data[index]);
                    }
                    break;
                }
            case JANET_STRING:
            case JANET_SYMBOL:
                {
                    const uint8_t *str = janet_unwrap_string(ds);
                    int32_t index;
                    vm_assert_type(key, JANET_INTEGER);
                    index = janet_unwrap_integer(key);
                    if (index < 0 || index >= janet_string_length(str)) {
                        /*vm_throw("index out of bounds");*/
                        value = janet_wrap_nil();
                    } else {
                        value = janet_wrap_integer(str[index]);
                    }
                    break;
                }
        }
        stack[oparg(1, 0xFF)] = value;
        ++pc;
        vm_next();
    }

    VM_OP(JOP_GET_INDEX)
    {
        Janet ds = stack[oparg(2, 0xFF)];
        int32_t index = oparg(3, 0xFF);
        Janet value;
        switch (janet_type(ds)) {
            default:
                expected_types = JANET_TFLAG_LENGTHABLE;
                retreg = ds;
                goto vm_type_error;
            case JANET_STRING:
            case JANET_SYMBOL:
                if (index >= janet_string_length(janet_unwrap_string(ds))) {
                    /*vm_throw("index out of bounds");*/
                    value = janet_wrap_nil();
                } else {
                    value = janet_wrap_integer(janet_unwrap_string(ds)[index]);
                }
                break;
            case JANET_ARRAY:
                if (index >= janet_unwrap_array(ds)->count) {
                    /*vm_throw("index out of bounds");*/
                    value = janet_wrap_nil();
                } else {
                    value = janet_unwrap_array(ds)->data[index];
                }
                break;
            case JANET_BUFFER:
                if (index >= janet_unwrap_buffer(ds)->count) {
                    /*vm_throw("index out of bounds");*/
                    value = janet_wrap_nil();
                } else {
                    value = janet_wrap_integer(janet_unwrap_buffer(ds)->data[index]);
                }
                break;
            case JANET_TUPLE:
                if (index >= janet_tuple_length(janet_unwrap_tuple(ds))) {
                    /*vm_throw("index out of bounds");*/
                    value = janet_wrap_nil();
                } else {
                    value = janet_unwrap_tuple(ds)[index];
                }
                break;
            case JANET_TABLE:
                value = janet_table_get(janet_unwrap_table(ds), janet_wrap_integer(index));
                break;
            case JANET_STRUCT:
                value = janet_struct_get(janet_unwrap_struct(ds), janet_wrap_integer(index));
                break;
        }
        stack[oparg(1, 0xFF)] = value;
        ++pc;
        vm_next();
    }

    VM_OP(JOP_LENGTH)
    {
        Janet x = stack[oparg(2, 0xFFFF)];
        int32_t len;
        switch (janet_type(x)) {
            default:
                expected_types = JANET_TFLAG_LENGTHABLE;
                retreg = x;
                goto vm_type_error;
            case JANET_STRING:
            case JANET_SYMBOL:
                len = janet_string_length(janet_unwrap_string(x));
                break;
            case JANET_ARRAY:
                len = janet_unwrap_array(x)->count;
                break;
            case JANET_BUFFER:
                len = janet_unwrap_buffer(x)->count;
                break;
            case JANET_TUPLE:
                len = janet_tuple_length(janet_unwrap_tuple(x));
                break;
            case JANET_STRUCT:
                len = janet_struct_length(janet_unwrap_struct(x));
                break;
            case JANET_TABLE:
                len = janet_unwrap_table(x)->count;
                break;
        }
        stack[oparg(1, 0xFF)] = janet_wrap_integer(len);
        ++pc;
        vm_next();
    }

    VM_OP(JOP_MAKE_ARRAY)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        stack[oparg(1, 0xFFFFFF)] = janet_wrap_array(janet_array_n(mem, count));
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
    }

    VM_OP(JOP_MAKE_TUPLE)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        stack[oparg(1, 0xFFFFFF)] = janet_wrap_tuple(janet_tuple_n(mem, count));
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
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
        stack[oparg(1, 0xFFFFFF)] = janet_wrap_table(table);
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
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
        stack[oparg(1, 0xFFFFFF)] = janet_wrap_struct(janet_struct_end(st));
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
    }

    VM_OP(JOP_MAKE_STRING)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        JanetBuffer buffer;
        janet_buffer_init(&buffer, 10 * count);
        for (int32_t i = 0; i < count; i++)
            janet_to_string_b(&buffer, mem[i]);
        stack[oparg(1, 0xFFFFFF)] = janet_stringv(buffer.data, buffer.count);
        janet_buffer_deinit(&buffer);
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
    }

    VM_OP(JOP_MAKE_BUFFER)
    {
        int32_t count = fiber->stacktop - fiber->stackstart;
        Janet *mem = fiber->data + fiber->stackstart;
        JanetBuffer *buffer = janet_buffer(10 * count);
        for (int32_t i = 0; i < count; i++)
            janet_to_string_b(buffer, mem[i]);
        stack[oparg(1, 0xFFFFFF)] = janet_wrap_buffer(buffer);
        fiber->stacktop = fiber->stackstart;
        ++pc;
        vm_checkgc_next();
    }

    /* Return from c function. Simpler than returning from janet function */
    vm_return_cfunc:
    {
        janet_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        stack = fiber->data + fiber->frame;
        stack[oparg(1, 0xFF)] = retreg;
        ++pc;
        vm_checkgc_next();
    }

    /* Return from a cfunction that is in tail position (pop 2 stack frames) */
    vm_return_cfunc_tail:
    {
        janet_fiber_popframe(fiber);
        janet_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        goto vm_reset;
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
        retreg = janet_wrap_string(janet_formatc("%V called with %d argument%s, expected %d",
                    janet_wrap_function(func),
                    nargs,
                    nargs == 1 ? "" : "s",
                    func->def->arity));
        signal = JANET_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Resume a child fiber */
    vm_resume_child:
    {
        JanetFiber *child = fiber->child;
        JanetFiberStatus status = janet_fiber_status(child);
        if (status == JANET_STATUS_ALIVE ||
                status == JANET_STATUS_DEAD ||
                status == JANET_STATUS_ERROR) {
            vm_throw("cannot resume alive, dead, or errored fiber");
        }
        signal = janet_continue(child, retreg, &retreg);
        if (signal != JANET_SIGNAL_OK) {
            if (child->flags & (1 << signal)) {
                /* Intercept signal */
                signal = JANET_SIGNAL_OK;
                fiber->child = NULL;
            } else {
                /* Propogate signal */
                goto vm_exit;
            }
        }
        stack[oparg(1, 0xFF)] = retreg;
        pc++;
        vm_checkgc_next();
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
        janet_buffer_push_cstring(&errbuf, janet_type_names[janet_type(retreg)] + 1);
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
        janet_vm_stackn--;
        janet_gcunroot(in);
        janet_gcunroot(janet_wrap_fiber(fiber));
        janet_vm_fiber = old_vm_fiber;
        *out = retreg;
        /* All statuses correspond to signals except new and alive,
         * which cannot be entered when exiting the vm loop.
         * JANET_SIGNAL_OK -> JANET_STATUS_DEAD
         * JANET_SIGNAL_YIELD -> JANET_STATUS_PENDING */
        janet_fiber_set_status(fiber, signal);
        return signal;
    }

    /* Reset state of machine */
    vm_reset:
    {
        stack = fiber->data + fiber->frame;
        func = janet_stack_frame(stack)->func;
        pc = janet_stack_frame(stack)->pc;
        stack[oparg(1, 0xFF)] = retreg;
        pc++;
        vm_checkgc_next();
    }

    VM_END()

#undef oparg

#undef vm_error
#undef vm_assert
#undef vm_binop
#undef vm_binop_real
#undef vm_binop_integer
#undef vm_binop_immediate

}

JanetSignal janet_call(
        JanetFunction *fun,
        int32_t argn,
        const Janet *argv,
        Janet *out,
        JanetFiber **f) {
    int32_t i;
    JanetFiber *fiber = janet_fiber(fun, 64);
    if (f)
        *f = fiber;
    for (i = 0; i < argn; i++)
        janet_fiber_push(fiber, argv[i]);
    if (janet_fiber_funcframe(fiber, fiber->root)) {
        *out = janet_cstringv("arity mismatch");
        return JANET_SIGNAL_ERROR;
    }
    /* Prevent push an extra value on the stack */
    janet_fiber_set_status(fiber, JANET_STATUS_PENDING);
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
    janet_vm_gc_interval = 0x1000000;
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

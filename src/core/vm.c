/*
* Copyright (c) 2019 Calvin Rose
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

/* Virtual registers
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
#define vm_return(sig, val) do { \
    vm_commit(); \
    janet_fiber_push(fiber, (val)); \
    return (sig); \
} while (0)

/* Next instruction variations */
#define maybe_collect() do {\
    if (janet_vm_next_collection >= janet_vm_gc_interval) janet_collect(); } while (0)
#define vm_checkgc_next() maybe_collect(); vm_next()
#define vm_pcnext() pc++; vm_next()
#define vm_checkgc_pcnext() maybe_collect(); vm_pcnext()

/* Handle certain errors in main vm loop */
#define vm_throw(e) do { vm_commit(); janet_panic(e); } while (0)
#define vm_assert(cond, e) do {if (!(cond)) vm_throw((e)); } while (0)
#define vm_assert_type(X, T) do { \
    if (!(janet_checktype((X), (T)))) { \
        vm_commit(); \
        janet_panicf("expected %T, got %t", (1 << (T)), (X)); \
    } \
} while (0)
#define vm_assert_types(X, TS) do { \
    if (!(janet_checktypes((X), (TS)))) { \
        vm_commit(); \
        janet_panicf("expected %T, got %t", (TS), (X)); \
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

/* Call a non function type */
static Janet call_nonfn(JanetFiber *fiber, Janet callee) {
    int32_t argn = fiber->stacktop - fiber->stackstart;
    Janet ds, key;
    if (argn != 1) janet_panicf("%v called with arity %d, expected 1", callee, argn);
    if (janet_checktypes(callee, JANET_TFLAG_INDEXED | JANET_TFLAG_DICTIONARY)) {
        ds = callee;
        key = fiber->data[fiber->stackstart];
    } else {
        ds = fiber->data[fiber->stackstart];
        key = callee;
    }
    fiber->stacktop = fiber->stackstart;
    return janet_get(ds, key);
}

/* Interpreter main loop */
static JanetSignal run_vm(JanetFiber *fiber, Janet in) {

    /* Interpreter state */
    register Janet *stack;
    register uint32_t *pc;
    register JanetFunction *func;
    vm_restore();

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
     * (*pc & 0xFF) inside of an infinite loop. */
    VM_START();

    VM_DEFAULT();
    vm_return(JANET_SIGNAL_DEBUG, janet_wrap_nil());

    VM_OP(JOP_NOOP)
    vm_pcnext();

    VM_OP(JOP_ERROR)
    vm_return(JANET_SIGNAL_ERROR, stack[A]);

    VM_OP(JOP_TYPECHECK)
    vm_assert_types(stack[A], E);
    vm_pcnext();

    VM_OP(JOP_RETURN)
    {
        Janet retval = stack[D];
        janet_fiber_popframe(fiber);
        if (fiber->frame == 0) vm_return(JANET_SIGNAL_OK, retval);
        vm_restore();
        stack[A] = retval;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_RETURN_NIL)
    {
        Janet retval = janet_wrap_nil();
        janet_fiber_popframe(fiber);
        if (fiber->frame == 0) vm_return(JANET_SIGNAL_OK, retval);
        vm_restore();
        stack[A] = retval;
        vm_checkgc_pcnext();
    }

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
            janet_panicf("expected %T, got %t", JANET_TFLAG_INDEXED, stack[D]);
        }
    }
    stack = fiber->data + fiber->frame;
    vm_checkgc_pcnext();

    VM_OP(JOP_CALL)
    {
        Janet callee = stack[E];
        if (fiber->stacktop > fiber->maxstack) {
            vm_throw("stack overflow");
        }
        if (janet_checktype(callee, JANET_FUNCTION)) {
            func = janet_unwrap_function(callee);
            janet_stack_frame(stack)->pc = pc;
            if (janet_fiber_funcframe(fiber, func)) {
                int32_t n = fiber->stacktop - fiber->stackstart;
                janet_panicf("%v called with %d argument%s, expected %d",
                        callee, n, n == 1 ? "s" : "", func->def->arity);
            }
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (janet_checktype(callee, JANET_CFUNCTION)) {
            vm_commit();
            int32_t argc = fiber->stacktop - fiber->stackstart;
            janet_fiber_cframe(fiber, janet_unwrap_cfunction(callee));
            Janet ret = janet_unwrap_cfunction(callee)(argc, fiber->data + fiber->frame);
            janet_fiber_popframe(fiber);
            if (fiber->frame == 0) vm_return(JANET_SIGNAL_OK, ret);
            stack = fiber->data + fiber->frame;
            stack[A] = ret;
            vm_checkgc_pcnext();
        } else {
            vm_commit();
            stack[A] = call_nonfn(fiber, callee);
            vm_pcnext();
        }
    }

    VM_OP(JOP_TAILCALL)
    {
        Janet callee = stack[D];
        if (janet_checktype(callee, JANET_FUNCTION)) {
            func = janet_unwrap_function(callee);
            if (janet_fiber_funcframe_tail(fiber, func)) {
                janet_stack_frame(fiber->data + fiber->frame)->pc = pc;
                int32_t n = fiber->stacktop - fiber->stackstart;
                janet_panicf("%v called with %d argument%s, expected %d",
                        callee, n, n == 1 ? "s" : "", func->def->arity);
            }
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else {
            Janet retreg;
            vm_commit();
            if (janet_checktype(callee, JANET_CFUNCTION)) {
                int32_t argc = fiber->stacktop - fiber->stackstart;
                janet_fiber_cframe(fiber, janet_unwrap_cfunction(callee));
                retreg = janet_unwrap_cfunction(callee)(argc, fiber->data + fiber->frame);
                janet_fiber_popframe(fiber);
            } else {
                retreg = call_nonfn(fiber, callee);
            }
            janet_fiber_popframe(fiber);
            if (fiber->frame == 0)
                vm_return(JANET_SIGNAL_OK, retreg);
            vm_restore();
            stack[A] = retreg;
            vm_checkgc_pcnext();
        }
    }

    VM_OP(JOP_RESUME)
    {
        Janet retreg;
        vm_assert_type(stack[B], JANET_FIBER);
        JanetFiber *child = janet_unwrap_fiber(stack[B]);
        fiber->child = child;
        JanetSignal sig = janet_continue(child, stack[C], &retreg);
        if (sig != JANET_SIGNAL_OK && !(child->flags & (1 << sig)))
            vm_return(sig, retreg);
        fiber->child = NULL;
        stack[A] = retreg;
        vm_checkgc_pcnext();
    }

    VM_OP(JOP_SIGNAL)
    {
        int32_t s = C;
        if (s > JANET_SIGNAL_USER9) s = JANET_SIGNAL_USER9;
        if (s < 0) s = 0;
        vm_return(s, stack[B]);
    }

    VM_OP(JOP_PUT)
    vm_commit();
    janet_put(stack[A], stack[B], stack[C]);
    vm_checkgc_pcnext();

    VM_OP(JOP_PUT_INDEX)
    vm_commit();
    janet_putindex(stack[A], C, stack[B]);
    vm_checkgc_pcnext();

    VM_OP(JOP_GET)
    vm_commit();
    stack[A] = janet_get(stack[B], stack[C]);
    vm_pcnext();

    VM_OP(JOP_GET_INDEX)
    vm_commit();
    stack[A] = janet_getindex(stack[B], C);
    vm_pcnext();

    VM_OP(JOP_LENGTH)
    vm_commit();
    stack[A] = janet_wrap_integer(janet_length(stack[E]));
    vm_pcnext();

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
    JanetSignal signal;
    if (setjmp(fiber->buf)) {
        signal = JANET_SIGNAL_ERROR;
    } else {
        signal = run_vm(fiber, in);
    }

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

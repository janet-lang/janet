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

#include <dst/dst.h>
#include <dst/dstopcodes.h>
#include "state.h"
#include "fiber.h"
#include "gc.h"
#include "symcache.h"

/* VM state */
DST_THREAD_LOCAL int dst_vm_stackn = 0;
DST_THREAD_LOCAL DstFiber *dst_vm_fiber = NULL;

/* Maybe collect garbage */
#define dst_maybe_collect() do {\
    if (dst_vm_next_collection >= dst_vm_gc_interval) dst_collect(); } while (0)

/* Start running the VM from where it left off. */
DstSignal dst_continue(DstFiber *fiber, Dst in, Dst *out) {

    /* Save old fiber to reset */
    DstFiber *old_vm_fiber = dst_vm_fiber;

    /* interpreter state */
    register Dst *stack;
    register uint32_t *pc;
    register DstFunction *func;

    /* Keep in mind the garbage collector cannot see this value.
     * Values stored here should be used immediately */
    Dst retreg;

    /* Signal to return when done */
    DstSignal signal = DST_SIGNAL_OK;

    /* Ensure fiber is not alive, dead, or error */
    DstFiberStatus startstatus = dst_fiber_status(fiber);
    if (startstatus == DST_STATUS_ALIVE ||
            startstatus == DST_STATUS_DEAD ||
            startstatus == DST_STATUS_ERROR) {
        *out = dst_cstringv("cannot resume alive, dead, or errored fiber");
        return DST_SIGNAL_ERROR;
    }

    /* Increment the stackn */
    if (dst_vm_stackn >= DST_RECURSION_GUARD) {
        dst_fiber_set_status(fiber, DST_STATUS_ERROR);
        *out = dst_cstringv("C stack recursed too deeply");
        return DST_SIGNAL_ERROR;
    }
    dst_vm_stackn++;

    /* Setup fiber state */
    dst_vm_fiber = fiber;
    dst_gcroot(dst_wrap_fiber(fiber));
    dst_gcroot(in);
    if (startstatus == DST_STATUS_NEW) {
        dst_fiber_push(fiber, in);
        dst_fiber_funcframe(fiber, fiber->root);
    }
    dst_fiber_set_status(fiber, DST_STATUS_ALIVE);
    stack = fiber->data + fiber->frame;
    pc = dst_stack_frame(stack)->pc;
    func = dst_stack_frame(stack)->func;

    /* Used to extract bits from the opcode that correspond to arguments.
     * Pulls out unsigned integers */
#define oparg(shift, mask) (((*pc) >> ((shift) << 3)) & (mask))

    /* Check for child fiber. If there is a child, run child before self.
     * This should only be hit when the current fiber is pending on a RESUME
     * instruction. */
    if (fiber->child) {
        retreg = in;
        goto vm_resume_child;
    } else if (fiber->flags & DST_FIBER_FLAG_SIGNAL_WAITING) {
        /* If waiting for response to signal, use input and increment pc */
        stack[oparg(1, 0xFF)] = in;
        pc++;
        fiber->flags &= ~DST_FIBER_FLAG_SIGNAL_WAITING;
    }

/* Use computed gotos for GCC and clang, otherwise use switch */
#ifdef __GNUC__
#define VM_START() {vm_next();
#define VM_END() }
#define VM_OP(op) label_##op :
#define VM_DEFAULT() label_unknown_op:
#define vm_next() goto *op_lookup[*pc & 0xFF];
static void *op_lookup[255] = {
    &&label_DOP_NOOP,
    &&label_DOP_ERROR,
    &&label_DOP_TYPECHECK,
    &&label_DOP_RETURN,
    &&label_DOP_RETURN_NIL,
    &&label_DOP_ADD_INTEGER,
    &&label_DOP_ADD_IMMEDIATE,
    &&label_DOP_ADD_REAL,
    &&label_DOP_ADD,
    &&label_DOP_SUBTRACT_INTEGER,
    &&label_DOP_SUBTRACT_REAL,
    &&label_DOP_SUBTRACT,
    &&label_DOP_MULTIPLY_INTEGER,
    &&label_DOP_MULTIPLY_IMMEDIATE,
    &&label_DOP_MULTIPLY_REAL,
    &&label_DOP_MULTIPLY,
    &&label_DOP_DIVIDE_INTEGER,
    &&label_DOP_DIVIDE_IMMEDIATE,
    &&label_DOP_DIVIDE_REAL,
    &&label_DOP_DIVIDE,
    &&label_DOP_BAND,
    &&label_DOP_BOR,
    &&label_DOP_BXOR,
    &&label_DOP_BNOT,
    &&label_DOP_SHIFT_LEFT,
    &&label_DOP_SHIFT_LEFT_IMMEDIATE,
    &&label_DOP_SHIFT_RIGHT,
    &&label_DOP_SHIFT_RIGHT_IMMEDIATE,
    &&label_DOP_SHIFT_RIGHT_UNSIGNED,
    &&label_DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE,
    &&label_DOP_MOVE_FAR,
    &&label_DOP_MOVE_NEAR,
    &&label_DOP_JUMP,
    &&label_DOP_JUMP_IF,
    &&label_DOP_JUMP_IF_NOT,
    &&label_DOP_GREATER_THAN,
    &&label_DOP_GREATER_THAN_INTEGER,
    &&label_DOP_GREATER_THAN_IMMEDIATE,
    &&label_DOP_GREATER_THAN_REAL,
    &&label_DOP_GREATER_THAN_EQUAL_REAL,
    &&label_DOP_LESS_THAN,
    &&label_DOP_LESS_THAN_INTEGER,
    &&label_DOP_LESS_THAN_IMMEDIATE,
    &&label_DOP_LESS_THAN_REAL,
    &&label_DOP_LESS_THAN_EQUAL_REAL,
    &&label_DOP_EQUALS,
    &&label_DOP_EQUALS_INTEGER,
    &&label_DOP_EQUALS_IMMEDIATE,
    &&label_DOP_EQUALS_REAL,
    &&label_DOP_COMPARE,
    &&label_DOP_LOAD_NIL,
    &&label_DOP_LOAD_TRUE,
    &&label_DOP_LOAD_FALSE,
    &&label_DOP_LOAD_INTEGER,
    &&label_DOP_LOAD_CONSTANT,
    &&label_DOP_LOAD_UPVALUE,
    &&label_DOP_LOAD_SELF,
    &&label_DOP_SET_UPVALUE,
    &&label_DOP_CLOSURE,
    &&label_DOP_PUSH,
    &&label_DOP_PUSH_2,
    &&label_DOP_PUSH_3,
    &&label_DOP_PUSH_ARRAY,
    &&label_DOP_CALL,
    &&label_DOP_TAILCALL,
    &&label_DOP_RESUME,
    &&label_DOP_SIGNAL,
    &&label_DOP_GET,
    &&label_DOP_PUT,
    &&label_DOP_GET_INDEX,
    &&label_DOP_PUT_INDEX,
    &&label_DOP_LENGTH,
    &&label_unknown_op
};
#else
#define VM_START() for(;;){switch(*pc & 0xFF){
#define VM_END() }}
#define VM_OP(op) case op :
#define VM_DEFAULT() default:
#define vm_next() continue
#endif

#define vm_checkgc_next() dst_maybe_collect(); vm_next()

#define vm_throw(e) do { retreg = dst_cstringv(e); goto vm_error; } while (0)
#define vm_assert(cond, e) do {if (!(cond)) vm_throw((e)); } while (0)

#define vm_binop_integer(op) \
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        dst_unwrap_integer(stack[oparg(2, 0xFF)]) op dst_unwrap_integer(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_real(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_real(\
        dst_unwrap_real(stack[oparg(2, 0xFF)]) op dst_unwrap_real(stack[oparg(3, 0xFF)])\
    );\
    pc++;\
    vm_next();

#define vm_binop_immediate(op)\
    stack[oparg(1, 0xFF)] = dst_wrap_integer(\
        dst_unwrap_integer(stack[oparg(2, 0xFF)]) op (*((int32_t *)pc) >> 24)\
    );\
    pc++;\
    vm_next();

#define vm_binop(op)\
    {\
        Dst op1 = stack[oparg(2, 0xFF)];\
        Dst op2 = stack[oparg(3, 0xFF)];\
        vm_assert(dst_checktype(op1, DST_INTEGER) || dst_checktype(op1, DST_REAL), "expected number");\
        vm_assert(dst_checktype(op2, DST_INTEGER) || dst_checktype(op2, DST_REAL), "expected number");\
        stack[oparg(1, 0xFF)] = dst_checktype(op1, DST_INTEGER)\
            ? (dst_checktype(op2, DST_INTEGER)\
                ? dst_wrap_integer(dst_unwrap_integer(op1) op dst_unwrap_integer(op2))\
                : dst_wrap_real((double)dst_unwrap_integer(op1) op dst_unwrap_real(op2)))\
            : (dst_checktype(op2, DST_INTEGER)\
                ? dst_wrap_real(dst_unwrap_real(op1) op (double)dst_unwrap_integer(op2))\
                : dst_wrap_real(dst_unwrap_real(op1) op dst_unwrap_real(op2)));\
        pc++;\
        vm_next();\
    }

    /* Main interpreter loop. Semantically is a switch on
     * (*pc & 0xFF) inside of an infinte loop. */
    VM_START();

    VM_DEFAULT();
    retreg = dst_wrap_nil();
    goto vm_exit;

    VM_OP(DOP_NOOP)
    pc++;
    vm_next();

    VM_OP(DOP_ERROR)
    retreg = stack[oparg(1, 0xFF)];
    goto vm_error;

    VM_OP(DOP_TYPECHECK)
    if (!((1 << dst_type(stack[oparg(1, 0xFF)])) & oparg(2, 0xFFFF))) {
        DstArgs tempargs;
        tempargs.n = oparg(1, 0xFF) + 1;
        tempargs.v = stack;
        dst_typemany_err(tempargs, oparg(1, 0xFF), oparg(2, 0xFFFF));
        goto vm_error;
    }
    pc++;
    vm_next();

    VM_OP(DOP_RETURN)
    retreg = stack[oparg(1, 0xFFFFFF)];
    goto vm_return;

    VM_OP(DOP_RETURN_NIL)
    retreg = dst_wrap_nil();
    goto vm_return;

    VM_OP(DOP_ADD_INTEGER)
    vm_binop_integer(+);

    VM_OP(DOP_ADD_IMMEDIATE)
    vm_binop_immediate(+);

    VM_OP(DOP_ADD_REAL)
    vm_binop_real(+);

    VM_OP(DOP_ADD)
    vm_binop(+);

    VM_OP(DOP_SUBTRACT_INTEGER)
    vm_binop_integer(-);

    VM_OP(DOP_SUBTRACT_REAL)
    vm_binop_real(-);

    VM_OP(DOP_SUBTRACT)
    vm_binop(-);
        
    VM_OP(DOP_MULTIPLY_INTEGER)
    vm_binop_integer(*);

    VM_OP(DOP_MULTIPLY_IMMEDIATE)
    vm_binop_immediate(*);

    VM_OP(DOP_MULTIPLY_REAL)
    vm_binop_real(*);

    VM_OP(DOP_MULTIPLY)
    vm_binop(*);

    VM_OP(DOP_DIVIDE_INTEGER)
    vm_assert(dst_unwrap_integer(stack[oparg(3, 0xFF)]) != 0, "integer divide error");
    vm_assert(!(dst_unwrap_integer(stack[oparg(3, 0xFF)]) == -1 && 
                dst_unwrap_integer(stack[oparg(2, 0xFF)]) == INT32_MIN),
            "integer divide error");
    vm_binop_integer(/);
    
    VM_OP(DOP_DIVIDE_IMMEDIATE)
    {
        int32_t op1 = dst_unwrap_integer(stack[oparg(2, 0xFF)]);
        int32_t op2 = *((int32_t *)pc) >> 24;
        /* Check for degenerate integer division (divide by zero, and dividing
         * min value by -1). These checks could be omitted if the arg is not 
         * 0 or -1. */
        if (op2 == 0)
            vm_throw("integer divide error");
        if (op2 == -1 && op1 == INT32_MIN)
            vm_throw("integer divide error");
        else
            stack[oparg(1, 0xFF)] = dst_wrap_integer(op1 / op2);
        pc++;
        vm_next();
    }

    VM_OP(DOP_DIVIDE_REAL)
    vm_binop_real(/);

    VM_OP(DOP_DIVIDE)
    {
        Dst op1 = stack[oparg(2, 0xFF)];
        Dst op2 = stack[oparg(3, 0xFF)];
        vm_assert(dst_checktype(op1, DST_INTEGER) || dst_checktype(op1, DST_REAL), "expected number");
        vm_assert(dst_checktype(op2, DST_INTEGER) || dst_checktype(op2, DST_REAL), "expected number");
        if (dst_checktype(op2, DST_INTEGER) && dst_unwrap_integer(op2) == 0)
            vm_throw("integer divide error");
        if (dst_checktype(op2, DST_INTEGER) && dst_unwrap_integer(op2) == -1 &&
            dst_checktype(op1, DST_INTEGER) && dst_unwrap_integer(op1) == INT32_MIN)
            vm_throw("integer divide error");
        stack[oparg(1, 0xFF)] = dst_checktype(op1, DST_INTEGER)
            ? (dst_checktype(op2, DST_INTEGER)
                ? dst_wrap_integer(dst_unwrap_integer(op1) / dst_unwrap_integer(op2))
                : dst_wrap_real((double)dst_unwrap_integer(op1) / dst_unwrap_real(op2)))
            : (dst_checktype(op2, DST_INTEGER)
                ? dst_wrap_real(dst_unwrap_real(op1) / (double)dst_unwrap_integer(op2))
                : dst_wrap_real(dst_unwrap_real(op1) / dst_unwrap_real(op2)));
        pc++;
        vm_next();
    }

    VM_OP(DOP_BAND)
    vm_binop_integer(&);

    VM_OP(DOP_BOR)
    vm_binop_integer(|);

    VM_OP(DOP_BXOR)
    vm_binop_integer(^);

    VM_OP(DOP_BNOT)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(~dst_unwrap_integer(stack[oparg(2, 0xFFFF)]));
    vm_next();
        
    VM_OP(DOP_SHIFT_RIGHT_UNSIGNED)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(
        (int32_t)(((uint32_t)dst_unwrap_integer(stack[oparg(2, 0xFF)]))
        >>
        dst_unwrap_integer(stack[oparg(3, 0xFF)]))
    );
    pc++;
    vm_next();

    VM_OP(DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(
        (int32_t) (((uint32_t)dst_unwrap_integer(stack[oparg(2, 0xFF)])) >> oparg(3, 0xFF))
    );
    pc++;
    vm_next();

    VM_OP(DOP_SHIFT_RIGHT)
    vm_binop_integer(>>);

    VM_OP(DOP_SHIFT_RIGHT_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(
        (int32_t)(dst_unwrap_integer(stack[oparg(2, 0xFF)]) >> oparg(3, 0xFF))
    );
    pc++;
    vm_next();

    VM_OP(DOP_SHIFT_LEFT)
    vm_binop_integer(<<);

    VM_OP(DOP_SHIFT_LEFT_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(
        dst_unwrap_integer(stack[oparg(2, 0xFF)]) << oparg(3, 0xFF)
    );
    pc++;
    vm_next();

    VM_OP(DOP_MOVE_NEAR)
    stack[oparg(1, 0xFF)] = stack[oparg(2, 0xFFFF)];
    pc++;
    vm_next();

    VM_OP(DOP_MOVE_FAR)
    stack[oparg(2, 0xFFFF)] = stack[oparg(1, 0xFF)];
    pc++;
    vm_next();

    VM_OP(DOP_JUMP)
    pc += (*(int32_t *)pc) >> 8;
    vm_next();

    VM_OP(DOP_JUMP_IF)
    if (dst_truthy(stack[oparg(1, 0xFF)])) {
        pc += (*(int32_t *)pc) >> 16;
    } else {
        pc++;
    }
    vm_next();

    VM_OP(DOP_JUMP_IF_NOT)
    if (dst_truthy(stack[oparg(1, 0xFF)])) {
        pc++;
    } else {
        pc += (*(int32_t *)pc) >> 16;
    }
    vm_next();

    VM_OP(DOP_LESS_THAN)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ) < 0);
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_LESS_THAN_INTEGER)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) <
            dst_unwrap_integer(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_LESS_THAN_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) < ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_LESS_THAN_REAL)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_real(stack[oparg(2, 0xFF)]) <
            dst_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_LESS_THAN_EQUAL_REAL)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_real(stack[oparg(2, 0xFF)]) <=
            dst_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();


    VM_OP(DOP_GREATER_THAN)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ) > 0);
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_GREATER_THAN_INTEGER)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) >
            dst_unwrap_integer(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_GREATER_THAN_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) > ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_GREATER_THAN_REAL)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_real(stack[oparg(2, 0xFF)]) >
            dst_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_GREATER_THAN_EQUAL_REAL)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_real(stack[oparg(2, 0xFF)]) >=
            dst_unwrap_real(stack[oparg(3, 0xFF)]));
    pc++;
    vm_next();

    VM_OP(DOP_EQUALS)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_equals(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ));
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_EQUALS_INTEGER)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) ==
            dst_unwrap_integer(stack[oparg(3, 0xFF)])
        );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_EQUALS_REAL)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_real(stack[oparg(2, 0xFF)]) ==
            dst_unwrap_real(stack[oparg(3, 0xFF)])
        );
    pc++;
    vm_next();

    /* Candidate */
    VM_OP(DOP_EQUALS_IMMEDIATE)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(
            dst_unwrap_integer(stack[oparg(2, 0xFF)]) == ((*(int32_t *)pc) >> 24)
    );
    pc++;
    vm_next();

    VM_OP(DOP_COMPARE)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(dst_compare(
        stack[oparg(2, 0xFF)],
        stack[oparg(3, 0xFF)]
    ));
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_NIL)
    stack[oparg(1, 0xFFFFFF)] = dst_wrap_nil();
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_TRUE)
    stack[oparg(1, 0xFFFFFF)] = dst_wrap_boolean(1);
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_FALSE)
    stack[oparg(1, 0xFFFFFF)] = dst_wrap_boolean(0);
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_INTEGER)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(*((int32_t *)pc) >> 16);
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_CONSTANT)
    {
        int32_t index = oparg(2, 0xFFFF);
        vm_assert(index < func->def->constants_length, "invalid constant");
        stack[oparg(1, 0xFF)] = func->def->constants[index];
        pc++;
        vm_next();
    }

    VM_OP(DOP_LOAD_SELF)
    stack[oparg(1, 0xFFFFFF)] = dst_wrap_function(func);
    pc++;
    vm_next();

    VM_OP(DOP_LOAD_UPVALUE)
    {
        int32_t eindex = oparg(2, 0xFF);
        int32_t vindex = oparg(3, 0xFF);
        DstFuncEnv *env;
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

    VM_OP(DOP_SET_UPVALUE)
    {
        int32_t eindex = oparg(2, 0xFF);
        int32_t vindex = oparg(3, 0xFF);
        DstFuncEnv *env;
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

    VM_OP(DOP_CLOSURE)
    {
        DstFuncDef *fd;
        DstFunction *fn;
        int32_t elen;
        vm_assert((int32_t)oparg(2, 0xFFFF) < func->def->defs_length, "invalid funcdef");
        fd = func->def->defs[(int32_t)oparg(2, 0xFFFF)];
        elen = fd->environments_length;
        fn = dst_gcalloc(DST_MEMORY_FUNCTION, sizeof(DstFunction) + (elen * sizeof(DstFuncEnv *)));
        fn->def = fd;
        {
            int32_t i;
            for (i = 0; i < elen; ++i) {
                int32_t inherit = fd->environments[i];
                if (inherit == -1) {
                    DstStackFrame *frame = dst_stack_frame(stack);
                    if (!frame->env) {
                        /* Lazy capture of current stack frame */
                        DstFuncEnv *env = dst_gcalloc(DST_MEMORY_FUNCENV, sizeof(DstFuncEnv));
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
        stack[oparg(1, 0xFF)] = dst_wrap_function(fn);
        pc++;
        vm_checkgc_next();
    } 

    VM_OP(DOP_PUSH)
        dst_fiber_push(fiber, stack[oparg(1, 0xFFFFFF)]);
        pc++;
        stack = fiber->data + fiber->frame;
        vm_checkgc_next();

    VM_OP(DOP_PUSH_2)
        dst_fiber_push2(fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFFFF)]);
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_PUSH_3)
        dst_fiber_push3(fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_PUSH_ARRAY)
    {
        const Dst *vals;
        int32_t len;
        if (dst_seq_view(stack[oparg(1, 0xFFFFFF)], &vals, &len)) {
            dst_fiber_pushn(fiber, vals, len);        
        } else {
            vm_throw("expected array/tuple");
        }
    }
    pc++;
    stack = fiber->data + fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_CALL)
    {
        Dst callee = stack[oparg(2, 0xFFFF)];
        if (fiber->maxstack &&
                fiber->stacktop > fiber->maxstack) {
            vm_throw("stack overflow");
        }
        if (dst_checktype(callee, DST_FUNCTION)) {
            func = dst_unwrap_function(callee);
            dst_stack_frame(stack)->pc = pc;
            dst_fiber_funcframe(fiber, func);
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (dst_checktype(callee, DST_CFUNCTION)) {
            DstArgs args;
            args.n = fiber->stacktop - fiber->stackstart;
            dst_fiber_cframe(fiber);
            retreg = dst_wrap_nil();
            args.v = fiber->data + fiber->frame;
            args.ret = &retreg;
            if ((signal = dst_unwrap_cfunction(callee)(args))) {
                goto vm_exit;
            }
            goto vm_return_cfunc;
        }
        vm_throw("expected function");
    }

    VM_OP(DOP_TAILCALL)
    {
        Dst callee = stack[oparg(1, 0xFFFFFF)];
        if (dst_checktype(callee, DST_FUNCTION)) {
            func = dst_unwrap_function(callee);
            dst_fiber_funcframe_tail(fiber, func);
            stack = fiber->data + fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (dst_checktype(callee, DST_CFUNCTION)) {
            DstArgs args;
            args.n = fiber->stacktop - fiber->stackstart;
            dst_fiber_cframe(fiber);
            retreg = dst_wrap_nil();
            args.v = fiber->data + fiber->frame;
            args.ret = &retreg;
            if ((signal = dst_unwrap_cfunction(callee)(args))) {
                goto vm_exit;
            }
            goto vm_return_cfunc_tail;
        }
        vm_throw("expected function");
    }

    VM_OP(DOP_RESUME)
    {
        Dst fiberval = stack[oparg(2, 0xFF)];
        vm_assert(dst_checktype(fiberval, DST_FIBER), "expected fiber");
        retreg = stack[oparg(3, 0xFF)];
        fiber->child = dst_unwrap_fiber(fiberval);
        goto vm_resume_child;
    }

    VM_OP(DOP_SIGNAL)
    {
        int32_t s = oparg(3, 0xFF);
        if (s > DST_SIGNAL_USER9) s = DST_SIGNAL_USER9;
        if (s < 0) s = 0;
        signal = s;
        retreg = stack[oparg(2, 0xFF)];
        fiber->flags |= DST_FIBER_FLAG_SIGNAL_WAITING;
        goto vm_exit;
    }

    VM_OP(DOP_PUT)
    dst_put(stack[oparg(1, 0xFF)],
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]);
    ++pc;
    vm_checkgc_next();

    VM_OP(DOP_PUT_INDEX)
    dst_setindex(stack[oparg(1, 0xFF)],
            stack[oparg(2, 0xFF)],
            oparg(3, 0xFF));
    ++pc;
    vm_checkgc_next();

    VM_OP(DOP_GET)
    stack[oparg(1, 0xFF)] = dst_get(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]);
    ++pc;
    vm_next();

    VM_OP(DOP_GET_INDEX)
    stack[oparg(1, 0xFF)] = dst_getindex(
            stack[oparg(2, 0xFF)],
            oparg(3, 0xFF));
    ++pc;
    vm_next();

    VM_OP(DOP_LENGTH)
    stack[oparg(1, 0xFF)] = dst_wrap_integer(dst_length(stack[oparg(2, 0xFFFF)]));
    ++pc;
    vm_next();

    /* Return from c function. Simpler than returning from dst function */
    vm_return_cfunc:
    {
        dst_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        stack = fiber->data + fiber->frame;
        stack[oparg(1, 0xFF)] = retreg;
        pc++;
        vm_checkgc_next();
    }

    /* Return from a cfunction that is in tail position (pop 2 stack frames) */
    vm_return_cfunc_tail:
    {
        dst_fiber_popframe(fiber);
        dst_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        goto vm_reset;
    }

    /* Handle returning from stack frame. Expect return value in retreg */
    vm_return:
    {
        dst_fiber_popframe(fiber);
        if (fiber->frame == 0) goto vm_exit;
        goto vm_reset;
    }

    /* Resume a child fiber */
    vm_resume_child:
    {
        DstFiber *child = fiber->child;
        DstFiberStatus status = dst_fiber_status(child);
        if (status == DST_STATUS_ALIVE ||
                status == DST_STATUS_DEAD ||
                status == DST_STATUS_ERROR) {
            vm_throw("cannot resume alive, dead, or errored fiber");
        }
        signal = dst_continue(child, retreg, &retreg);
        if (signal != DST_SIGNAL_OK) {
            if (child->flags & (1 << signal)) {
                /* Intercept signal */
                signal = DST_SIGNAL_OK;
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

    /* Handle errors from c functions and vm opcodes */
    vm_error:
    {
        signal = DST_SIGNAL_ERROR;
        goto vm_exit;
    }

    /* Exit from vm loop. If signal is not set explicitely, does
     * a successful return (DST_SIGNAL_OK). */
    vm_exit:
    {
        dst_stack_frame(stack)->pc = pc;
        dst_vm_stackn--;
        dst_gcunroot(in);
        dst_gcunroot(dst_wrap_fiber(fiber));
        dst_vm_fiber = old_vm_fiber;
        *out = retreg;
        /* All statuses correspond to signals except new and alive, 
         * which cannot be entered when exiting the vm loop.
         * DST_SIGNAL_OK -> DST_STATUS_DEAD
         * DST_SIGNAL_YIELD -> DST_STATUS_PENDING */
        dst_fiber_set_status(fiber, signal);
        return signal;
    }

    /* Reset state of machine */
    vm_reset:
    {
        stack = fiber->data + fiber->frame;
        func = dst_stack_frame(stack)->func;
        pc = dst_stack_frame(stack)->pc;
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

DstSignal dst_call(DstFunction *fun, int32_t argn, const Dst *argv, Dst *out) {
    int32_t i;
    DstFiber *fiber = dst_fiber(fun, 64);
    for (i = 0; i < argn; i++) {
        dst_fiber_push(fiber, argv[i]);
    }
    dst_fiber_funcframe(fiber, fiber->root);
    /* Prevent push an extra value on the stack */
    dst_fiber_set_status(fiber, DST_STATUS_PENDING);
    return dst_continue(fiber, dst_wrap_nil(), out);
}

/* Setup VM */
int dst_init() {
    /* Garbage collection */
    dst_vm_blocks = NULL;
    dst_vm_next_collection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * horrible for performance, but helps ensure
     * there are no memory bugs during dev */
    dst_vm_gc_interval = 0x10000;
    dst_symcache_init();
    /* Initialize gc roots */
    dst_vm_roots = NULL;
    dst_vm_root_count = 0;
    dst_vm_root_capacity = 0;
    return 0;
}

/* Clear all memory associated with the VM */
void dst_deinit() {
    dst_clear_memory();
    dst_symcache_deinit();
    free(dst_vm_roots);
    dst_vm_roots = NULL;
    dst_vm_root_count = 0;
    dst_vm_root_capacity = 0;
}

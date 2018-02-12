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
#include <dst/dstopcodes.h>
#include "symcache.h"
#include "gc.h"

/* VM State */
DstFiber *dst_vm_fiber = NULL;

/* Helper to ensure proper fiber is activated after returning */
static int dst_update_fiber() {
    if (dst_vm_fiber->frame == 0) {
        dst_vm_fiber->status = DST_FIBER_DEAD;
    }
    while (dst_vm_fiber->status == DST_FIBER_DEAD ||
            dst_vm_fiber->status == DST_FIBER_ERROR) {
        if (NULL != dst_vm_fiber->parent) {
            dst_vm_fiber = dst_vm_fiber->parent;
            if (dst_vm_fiber->status == DST_FIBER_ALIVE) {
                /* If the parent thread is still alive,
                   we are inside a cfunction */
                return 1;
            }
        } else {
            /* The root thread has terminated */
            return 1;
        }
    }
    dst_vm_fiber->status = DST_FIBER_ALIVE;
    return 0;
}

/* Start running the VM from where it left off. */
static int dst_continue(Dst *returnreg) {

    /* VM state */
    register Dst *stack;
    register uint32_t *pc;
    register DstFunction *func;

    /* Keep in mind the garbage collector cannot see this value.
     * Values stored here should be used immediately */
    Dst retreg;

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
    &&label_DOP_LESS_THAN,
    &&label_DOP_EQUALS,
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
    &&label_DOP_TRANSFER,
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

    /* Used to extract bits from the opcode that correspond to arguments.
     * Pulls out unsigned integers */
#define oparg(shift, mask) (((*pc) >> ((shift) << 3)) & (mask))

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

#define vm_init_fiber_state() \
    dst_vm_fiber->status = DST_FIBER_ALIVE;\
    stack = dst_vm_fiber->data + dst_vm_fiber->frame;\
    pc = dst_stack_frame(stack)->pc;\
    func = dst_stack_frame(stack)->func;

    vm_init_fiber_state();

    /* Main interpreter loop. It is large, but it is
     * is maintainable. Adding new opcodes is mostly just adding newcases
     * to this loop, adding the opcode to opcodes.h, and adding it to the assembler.
     * Some opcodes, especially ones that do arithmetic, are almost entirely
     * templated by the above macros. */
    VM_START();

    VM_DEFAULT();
    retreg = dst_wrap_string(dst_formatc("unknown opcode %d", *pc & 0xFF));
    goto vm_error;

    VM_OP(DOP_NOOP)
    pc++;
    vm_next();

    VM_OP(DOP_ERROR)
    retreg = stack[oparg(1, 0xFF)];
    goto vm_error;

    VM_OP(DOP_TYPECHECK)
    vm_assert((1 << dst_type(stack[oparg(1, 0xFF)])) & oparg(2, 0xFFFF), "typecheck failed");
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

    VM_OP(DOP_GREATER_THAN)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_compare(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ) > 0);
    pc++;
    vm_next();

    VM_OP(DOP_EQUALS)
    stack[oparg(1, 0xFF)] = dst_wrap_boolean(dst_equals(
            stack[oparg(2, 0xFF)],
            stack[oparg(3, 0xFF)]
        ));
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
        vm_assert(func->def->environments_length > eindex, "invalid upvalue");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue");
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
        vm_assert(func->def->environments_length > eindex, "invalid upvalue");
        env = func->envs[eindex];
        vm_assert(env->length > vindex, "invalid upvalue");
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
        vm_assert((int32_t)oparg(2, 0xFFFF) < func->def->defs_length, "invalid funcdef");
        fd = func->def->defs[(int32_t)oparg(2, 0xFFFF)];
        fn = dst_thunk(fd);
        {
            int32_t elen = fd->environments_length;
            if (elen) {
                int32_t i;
                fn->envs = malloc(sizeof(DstFuncEnv *) * elen);
                if (NULL == fn->envs) {
                    DST_OUT_OF_MEMORY;
                }
                for (i = 0; i < elen; ++i) {
                    int32_t inherit = fd->environments[i];
                    if (inherit == -1) {
                        DstStackFrame *frame = (DstStackFrame *)stack - 1;
                        if (!frame->env) {
                            /* Lazy capture of current stack frame */
                            DstFuncEnv *env = dst_gcalloc(DST_MEMORY_FUNCENV, sizeof(DstFuncEnv));
                            env->offset = dst_vm_fiber->frame;
                            env->as.fiber = dst_vm_fiber;
                            env->length = func->def->slotcount;
                            frame->env = env;
                        }
                        fn->envs[i] = frame->env;
                    } else {
                        fn->envs[i] = func->envs[inherit];
                    }
                }
            } else {
                fn->envs = NULL;
            }
        }
        stack[oparg(1, 0xFF)] = dst_wrap_function(fn);
        pc++;
        vm_checkgc_next();
    }

    VM_OP(DOP_PUSH)
        dst_fiber_push(dst_vm_fiber, stack[oparg(1, 0xFFFFFF)]);
        pc++;
        stack = dst_vm_fiber->data + dst_vm_fiber->frame;
        vm_checkgc_next();

    VM_OP(DOP_PUSH_2)
        dst_fiber_push2(dst_vm_fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFFFF)]);
    pc++;
    stack = dst_vm_fiber->data + dst_vm_fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_PUSH_3)
        dst_fiber_push3(dst_vm_fiber, 
                stack[oparg(1, 0xFF)],
                stack[oparg(2, 0xFF)],
                stack[oparg(3, 0xFF)]);
    pc++;
    stack = dst_vm_fiber->data + dst_vm_fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_PUSH_ARRAY)
    {
        const Dst *vals;
        int32_t len;
        if (dst_seq_view(stack[oparg(1, 0xFFFFFF)], &vals, &len)) {
            dst_fiber_pushn(dst_vm_fiber, vals, len);        
        } else {
            vm_throw("expected array/tuple");
        }
    }
    pc++;
    stack = dst_vm_fiber->data + dst_vm_fiber->frame;
    vm_checkgc_next();

    VM_OP(DOP_CALL)
    {
        Dst callee = stack[oparg(2, 0xFFFF)];
#ifdef DST_STACK_MAX
        if (dst_vm_fiber->stacktop > DST_STACK_MAX) {
            vm_throw("stack overflow");
        }
#endif
        if (dst_checktype(callee, DST_FUNCTION)) {
            func = dst_unwrap_function(callee);
            dst_stack_frame(stack)->pc = pc;
            dst_fiber_funcframe(dst_vm_fiber, func);
            stack = dst_vm_fiber->data + dst_vm_fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (dst_checktype(callee, DST_CFUNCTION)) {
            DstArgs args;
            args.n = dst_vm_fiber->stacktop - dst_vm_fiber->stackstart;
            dst_fiber_cframe(dst_vm_fiber);
            retreg = dst_wrap_nil();
            args.v = dst_vm_fiber->data + dst_vm_fiber->frame;
            args.ret = &retreg;
            if (dst_unwrap_cfunction(callee)(args)) {
                goto vm_error;
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
            dst_fiber_funcframe_tail(dst_vm_fiber, func);
            stack = dst_vm_fiber->data + dst_vm_fiber->frame;
            pc = func->def->bytecode;
            vm_checkgc_next();
        } else if (dst_checktype(callee, DST_CFUNCTION)) {
            DstArgs args;
            args.n = dst_vm_fiber->stacktop - dst_vm_fiber->stackstart;
            dst_fiber_cframe(dst_vm_fiber);
            retreg = dst_wrap_nil();
            args.v = dst_vm_fiber->data + dst_vm_fiber->frame;
            args.ret = &retreg;
            if (dst_unwrap_cfunction(callee)(args)) {
                goto vm_error;
            }
            goto vm_return_cfunc_tail;
        }
        vm_throw("expected function");
    }

    VM_OP(DOP_TRANSFER)
    {
        int status;
        DstFiber *nextfiber;
        DstStackFrame *frame = dst_stack_frame(stack);
        Dst temp = stack[oparg(2, 0xFF)];
        retreg = stack[oparg(3, 0xFF)];
        vm_assert(dst_checktype(temp, DST_FIBER) ||
                  dst_checktype(temp, DST_NIL), "expected fiber");
        nextfiber = dst_checktype(temp, DST_FIBER)
            ? dst_unwrap_fiber(temp)
            : dst_vm_fiber->parent;
        /* Check for root fiber */
        if (NULL == nextfiber) {
            frame->pc = pc;
            *returnreg = retreg;
            return 0;
        }
        status = nextfiber->status;
        vm_assert(status == DST_FIBER_PENDING ||
                status == DST_FIBER_NEW, "can only transfer to new or pending fiber");
        frame->pc = pc;
        dst_vm_fiber->status = DST_FIBER_PENDING;
        dst_vm_fiber = nextfiber;
        vm_init_fiber_state();
        if (status == DST_FIBER_PENDING) {
            /* The next fiber is currently on a transfer instruction. */
            stack[oparg(1, 0xFF)] = retreg;
            pc++;
        } else {
            /* The next fiber is new and is on the first instruction */
            if ((func->def->flags & DST_FUNCDEF_FLAG_VARARG) &&
                    !func->def->arity) {
                /* Fully var arg function */
                Dst *tup = dst_tuple_begin(1);
                tup[0] = retreg;
                stack[0] = dst_wrap_tuple(dst_tuple_end(tup));
            } else if (func->def->arity) {
                /* Non zero arity function */
                stack[0] = retreg;
            }
        }
        vm_checkgc_next();
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

    /* Return from c function. Simpler than retuning from dst function */
    vm_return_cfunc:
    {
        dst_fiber_popframe(dst_vm_fiber);
        if (dst_update_fiber()) {
            *returnreg = retreg;
            return 0;
        }
        stack = dst_vm_fiber->data + dst_vm_fiber->frame;
        stack[oparg(1, 0xFF)] = retreg;
        pc++;
        vm_checkgc_next();
    }

    vm_return_cfunc_tail:
    {
        dst_fiber_popframe(dst_vm_fiber);
        if (dst_update_fiber()) {
            *returnreg = retreg;
            return 0;
        }
        /* Fall through to normal return */
    }

    /* Handle returning from stack frame. Expect return value in retreg */
    vm_return:
    {
        dst_fiber_popframe(dst_vm_fiber);
        if (dst_update_fiber()) {
            *returnreg = retreg;
            return 0;
        }
        stack = dst_vm_fiber->data + dst_vm_fiber->frame;
        func = dst_stack_frame(stack)->func;
        pc = dst_stack_frame(stack)->pc;
        stack[oparg(1, 0xFF)] = retreg;
        pc++;
        vm_checkgc_next();
    }

    /* Handle errors from c functions and vm opcodes */
    vm_error:
    {
        dst_vm_fiber->status = DST_FIBER_ERROR;
        if (dst_update_fiber()) {
            *returnreg = retreg;
            return 1;
        }
        stack = dst_vm_fiber->data + dst_vm_fiber->frame;
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
#undef vm_init_fiber_state

}

/* Run the vm with a given function. This function is
 * called to start the vm. */
int dst_run(Dst callee, Dst *returnreg) {
    if (dst_vm_fiber) {
       dst_fiber_reset(dst_vm_fiber); 
    } else {
        dst_vm_fiber = dst_fiber(64);
    }
    if (dst_checktype(callee, DST_CFUNCTION)) {
        DstArgs args;
        *returnreg = dst_wrap_nil();
        dst_fiber_cframe(dst_vm_fiber);
        args.n = 0;
        args.v = dst_vm_fiber->data + dst_vm_fiber->frame;
        args.ret = returnreg;
        return dst_unwrap_cfunction(callee)(args);
    } else if (dst_checktype(callee, DST_FUNCTION)) {
        dst_fiber_funcframe(dst_vm_fiber, dst_unwrap_function(callee));
        return dst_continue(returnreg);
    }
    *returnreg = dst_cstringv("expected function");
    return 1;
}

/* Run from inside a cfunction. This should only be used for
 * short functions as it prevents re-entering the current fiber
 * and suspend garbage collection. */
int dst_call(Dst callee, Dst *returnreg, int32_t argn, const Dst *argv) {
    int ret;
    int lock;
    DstFiber *oldfiber = dst_vm_fiber;
    lock = dst_vm_gc_suspend++;
    dst_vm_fiber = dst_fiber(64);
    dst_fiber_pushn(dst_vm_fiber, argv, argn);
    if (dst_checktype(callee, DST_CFUNCTION)) {
        DstArgs args;
        *returnreg = dst_wrap_nil();
        dst_fiber_cframe(dst_vm_fiber);
        args.n = argn;
        args.v = dst_vm_fiber->data + dst_vm_fiber->frame;
        args.ret = returnreg;
        ret = dst_unwrap_cfunction(callee)(args);
    } else if (dst_checktype(callee, DST_FUNCTION)) {
        dst_fiber_funcframe(dst_vm_fiber, dst_unwrap_function(callee));
        ret = dst_continue(returnreg);
    } else {
        *returnreg = dst_cstringv("expected function");
        ret = 1;
    }
    dst_vm_fiber = oldfiber;
    dst_vm_gc_suspend = lock;
    return ret;
}

/* Setup functions */
int dst_init() {
    /* Garbage collection */
    dst_vm_blocks = NULL;
    dst_vm_next_collection = 0;
    /* Setting memoryInterval to zero forces
     * a collection pretty much every cycle, which is
     * horrible for performance, but helps ensure
     * there are no memory bugs during dev */
    dst_vm_gc_interval = 0x100000;
    dst_symcache_init();
    /* Set thread */
    dst_vm_fiber = NULL;
    /* Initialize gc roots */
    dst_vm_roots = NULL;
    dst_vm_root_count = 0;
    dst_vm_root_capacity = 0;
    return 0;
}

/* Clear all memory associated with the VM */
void dst_deinit() {
    dst_clear_memory();
    dst_vm_fiber = NULL;
    dst_symcache_deinit();
    free(dst_vm_roots);
    dst_vm_roots = NULL;
    dst_vm_root_count = 0;
    dst_vm_root_capacity = 0;
}

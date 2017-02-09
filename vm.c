#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "value.h"
#include "array.h"
#include "vstring.h"
#include "dict.h"
#include "gc.h"
#include "buffer.h"
#include "opcodes.h"

#define VMArg(i) (vm->base + (i))
#define VMOpArg(i) (VMArg(vm->pc[(i)]))

static const char OOM[] = "Out of memory";
static const char NO_UPVALUE[] = "Out of memory";
static const char EXPECTED_FUNCTION[] = "Expected function";
static const char VMS_EXPECTED_NUMBER_ROP[] = "Expected right operand to be number";
static const char VMS_EXPECTED_NUMBER_LOP[] = "Expected left operand to be number";

/* Mark memory reachable by VM */
void VMMark(VM * vm) {
    Value thread;
    thread.type = TYPE_THREAD;
    thread.data.array = vm->thread;
    GCMark(&vm->gc, &thread);
    GCMark(&vm->gc, &vm->tempRoot);
}

/* Run garbage collection */
void VMCollect(VM * vm) {
    VMMark(vm);
    GCSweep(&vm->gc);
}

/* Run garbage collection if needed */
void VMMaybeCollect(VM * vm) {
    if (GCNeedsCollect(&vm->gc)) {
        VMCollect(vm);
    }
}

/* OOM handler for the vm's gc */
static void VMHandleOutOfMemory(GC * gc) {
    VM * vm = (VM *) gc->user;
    VMError(vm, OOM);
}

/* Push a stack frame onto a thread */
static void VMThreadPush(VM * vm, Array * thread, Value callee, uint32_t size) {
    uint16_t oldSize;
    uint32_t nextCount, i;
	if (thread->count) {
		oldSize = FrameSize(thread);
		nextCount = thread->count + oldSize + FRAME_SIZE;
    } else {
		oldSize = 0;
		nextCount = FRAME_SIZE;
    }
	ArrayEnsure(&vm->gc, thread, nextCount + size);
	/* Ensure values start out as nil so as to not confuse
	 * the garabage collector */
	for (i = nextCount; i < nextCount + size; ++i) {
		thread->data[i].type = TYPE_NIL;
	}
    thread->count = nextCount;
    FramePrevSize(thread) = oldSize;
    FrameSize(thread) = size;
    FrameEnvValue(thread).type = TYPE_NIL;
    FrameEnv(thread) = NULL;
    FrameCallee(thread) = callee;
    FrameMeta(thread).type = TYPE_NUMBER;
    FramePCValue(thread).type = TYPE_NUMBER;
    vm->base = ThreadStack(thread);
}

/* Copy the current function stack to the current closure
   environment */
static void VMThreadSplitStack(VM * vm, Array * thread) {
	FuncEnv * env = FrameEnv(thread);
	/* Check for closures */
	if (env) {
    	uint32_t size = FrameSize(thread);
    	env->thread = NULL;
    	env->stackOffset = size;
    	env->values = GCAlloc(&vm->gc, sizeof(Value) * size);
    	memcpy(env->values, ThreadStack(thread), size * sizeof(Value));
	} 
}

/* Pop the top-most stack frame from stack */
static void VMThreadPop(VM * vm, Array * thread) {
	if (thread->count) {
    	VMThreadSplitStack(vm, thread);
    	thread->count -= FRAME_SIZE + FramePrevSize(thread);
	} else {
		VMError(vm, "Nothing to pop from stack.");
	}
    vm->base = ThreadStack(thread);
}

/* Get an upvalue */
static Value * GetUpValue(VM * vm, Func * fn, uint16_t level, uint16_t index) {
    FuncEnv * env;
    Value * stack;
    while (fn && level--)
        fn = fn->parent;
    VMAssert(vm, fn, NO_UPVALUE);
    env = fn->env;
    if (env->thread)
        stack = env->thread->data + env->stackOffset;
    else
        stack = env->values;
    return stack + index;
}

/* Get a constant */
static Value * LoadConstant(VM * vm, Func * fn, uint16_t index) {
    if (index > fn->def->literalsLen) {
        VMError(vm, NO_UPVALUE);
    }
    return fn->def->literals + index;
}

/* Truthiness definition in VM */
static int truthy(Value * v) {
    return v->type != TYPE_NIL && !(v->type == TYPE_BOOLEAN && !v->data.boolean);
}

/* Pushes a function on the call stack. */
static void VMPushCallee(VM * vm, uint32_t ret, uint32_t arity, Value callee) {
    Array * thread = vm->thread;
    FrameReturn(thread) = ret;
    if (callee.type == TYPE_FUNCTION) {
        Func * fn = callee.data.func;
        VMThreadPush(vm, thread, callee, fn->def->locals);
    } else if (callee.type == TYPE_CFUNCTION) {
        VMThreadPush(vm, thread, callee, arity);
    } else {
        VMError(vm, EXPECTED_FUNCTION);
        return;
    }
    /* Reset the base and frame after changing the stack */
    vm->base = ThreadStack(thread);
}

/* Return from the vm */
static void VMReturn(VM * vm, Value ret) {
	VMThreadPop(vm, vm->thread);
    if (vm->thread->count == 0) {
        VMExit(vm, ret);
    }
    vm->base = ThreadStack(vm->thread);
    vm->pc = FramePC(vm->thread);
    vm->base[FrameReturn(vm->thread)] = ret;
}

/* Implementation of the opcode for function calls */
static void VMCallOp(VM * vm) {
    uint32_t ret = vm->pc[1];
    uint32_t arity = vm->pc[2];
    Value callee = *VMOpArg(3);
    uint32_t i;
    Value * argWriter;
   	FramePC(vm->thread) = vm->pc + 4 + arity;
    VMPushCallee(vm, ret, arity, callee);
    argWriter = vm->base;
    if (callee.type == TYPE_CFUNCTION) {
        for (i = 0; i < arity; ++i)
            *(argWriter++) = *VMOpArg(4 + i);
        VMReturn(vm, callee.data.cfunction(vm));
        VMMaybeCollect(vm);
	} else if (callee.type == TYPE_FUNCTION) {
    	Func * f = callee.data.func;
    	uint32_t extraNils = f->def->locals;
    	if (arity > f->def->arity) {
            arity = f->def->arity;
        } else if (arity < f->def->arity) {
            extraNils += f->def->arity - arity;
        }
		for (i = 0; i < arity; ++i)
    		*(argWriter++) = *VMOpArg(4 + i);
    	for (i = 0; i < extraNils; ++i)
        	(argWriter++)->type = TYPE_NIL;
        vm->pc = f->def->byteCode;
	} else {
		VMError(vm, EXPECTED_FUNCTION);
	}
}

/* Implementation of the opcode for tail calls */
static void VMTailCallOp(VM * vm) {
    GC * gc = &vm->gc;
    uint32_t arity = vm->pc[1];
    Value callee = *VMOpArg(2);
    Value * extra, * argWriter;
    Array * thread = vm->thread;
    uint16_t newFrameSize;
    uint32_t i;
    /* Check for closures */
    if (FrameEnvValue(thread).type == TYPE_FUNCENV) {
        FuncEnv * env = FrameEnv(thread);
        uint16_t frameSize = FrameSize(thread);
        Value * envValues = GCAlloc(gc, FrameSize(thread) * sizeof(Value));
       	env->values = envValues;
        memcpy(envValues, vm->base, frameSize * sizeof(Value));
        env->stackOffset = frameSize;
        env->thread = NULL;
    }
    if (callee.type == TYPE_CFUNCTION) {
	   	newFrameSize = arity;
	} else if (callee.type == TYPE_FUNCTION) {
    	Func * f = callee.data.func;
    	newFrameSize = f->def->locals;
	} else {
		VMError(vm, EXPECTED_FUNCTION);
	}
	/* Ensure that stack is zeroed in this spot */
	ArrayEnsure(&vm->gc, thread, thread->count + newFrameSize + arity);
    vm->base = ThreadStack(thread);
	extra = argWriter = vm->base + FrameSize(thread) + FRAME_SIZE;
    for (i = 0; i < arity; ++i) {
        *argWriter++ = *VMOpArg(3 + i);
    }
    /* Copy the end of the stack to the parameter position */
    memcpy(vm->base, extra, arity * sizeof(Value));
	/* nil the new stack for gc */
	argWriter = vm->base + arity;
    for (i = arity; i < newFrameSize; ++i) {
		(argWriter++)->type = TYPE_NIL;
    }
	FrameSize(thread) = newFrameSize;
    FrameCallee(thread) = callee;
    if (callee.type == TYPE_CFUNCTION) {
        VMReturn(vm, callee.data.cfunction(vm));
        VMMaybeCollect(vm);
	} else {
    	Func * f = callee.data.func;
        vm->pc = f->def->byteCode;
	}
}

/* Instantiate a closure */
static void VMMakeClosure(VM * vm, uint16_t ret, uint16_t literal) {
    Value * vRet = VMArg(ret);
    if (FrameCallee(vm->thread).type != TYPE_FUNCTION) {
        VMError(vm, EXPECTED_FUNCTION);
    } else {
        Func * fn, * current;
        Value * constant;
        Array * thread = vm->thread;
        FuncEnv * env = FrameEnv(vm->thread);
        if (!env) {
            env = GCAlloc(&vm->gc, sizeof(FuncEnv));
            env->thread = thread;
            env->stackOffset = thread->count;
            env->values = NULL;
            FrameEnvValue(vm->thread).data.funcenv = env;
            FrameEnvValue(vm->thread).type = TYPE_FUNCENV;
        }
        current = FrameCallee(vm->thread).data.func;
        constant = LoadConstant(vm, current, literal);
        if (constant->type != TYPE_FUNCDEF) {
            VMError(vm, EXPECTED_FUNCTION);
        }
        fn = GCAlloc(&vm->gc, sizeof(Func));
        fn->def = constant->data.funcdef;
        fn->parent = current;
        fn->env = env;
        vRet->type = TYPE_FUNCTION;
        vRet->data.func = fn;
        VMMaybeCollect(vm);
    }
}

/* Start running the VM */
int VMStart(VM * vm) {

    /* Set jmp_buf to jump back to for return. */
    {
        int n;
        if ((n = setjmp(vm->jump))) {
            /* Good return */
            if (n == 1) {
                return 0;
            } else {
                /* Error */
                return n;
            }
        }
    }

    for (;;) {
        uint16_t opcode = *vm->pc;

        switch (opcode) {
            Value *vRet, *v1, *v2;

            case VM_OP_ADD: /* Addition */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                VMAssert(vm, v1->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_LOP);
                VMAssert(vm, v2->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_ROP);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = v1->data.number + v2->data.number;
                vm->pc += 4;
                break;

            case VM_OP_SUB: /* Subtraction */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                VMAssert(vm, v1->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_LOP);
                VMAssert(vm, v2->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_ROP);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = v1->data.number - v2->data.number;
                vm->pc += 4;
                break;

            case VM_OP_MUL: /* Multiplication */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                VMAssert(vm, v1->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_LOP);
                VMAssert(vm, v2->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_ROP);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = v1->data.number * v2->data.number;
                vm->pc += 4;
                break;

            case VM_OP_DIV: /* Division */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                VMAssert(vm, v1->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_LOP);
                VMAssert(vm, v2->type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_ROP);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = v1->data.number / v2->data.number;
                vm->pc += 4;
                break;

           case VM_OP_NOT: /* Boolean unary (Boolean not) */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                vm->pc += 3;
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = !truthy(v1);
                break;

            case VM_OP_LD0: /* Load 0 */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = 0;
                vm->pc += 2;
                break;

            case VM_OP_LD1: /* Load 1 */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = 1;
                vm->pc += 2;
                break;

            case VM_OP_FLS: /* Load False */
                vRet = VMOpArg(1);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = 0;
                vm->pc += 2;
                break;

            case VM_OP_TRU: /* Load True */
                vRet = VMOpArg(1);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = 1;
                vm->pc += 2;
                break;

            case VM_OP_NIL: /* Load Nil */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NIL;
                vm->pc += 2;
                break;

            case VM_OP_I16: /* Load Small Integer */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = ((int16_t *)(vm->pc))[2];
                vm->pc += 3;
                break;

            case VM_OP_UPV: /* Load Up Value */
            	{
                	Value callee;
                	callee = FrameCallee(vm->thread);
                    VMAssert(vm, callee.type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                    vRet = VMOpArg(1);
                    *vRet = *GetUpValue(vm, callee.data.func, vm->pc[2], vm->pc[3]);
                    vm->pc += 4;
                	}
                break;

            case VM_OP_JIF: /* Jump If */
                if (truthy(VMOpArg(1))) {
                    vm->pc += 4;
                } else {
                    vm->pc += *((int32_t *)(vm->pc + 2));
                }
                break;

            case VM_OP_JMP: /* Jump */
                vm->pc += *((int32_t *)(vm->pc + 1));
                break;

            case VM_OP_CAL: /* Call */
                VMCallOp(vm);
                break;

            case VM_OP_RET: /* Return */
            	VMReturn(vm, *VMOpArg(1));
                break;

            case VM_OP_SUV: /* Set Up Value */
                VMAssert(vm, FrameCallee(vm->thread).type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                vRet = VMOpArg(1);
                *GetUpValue(vm, FrameCallee(vm->thread).data.func, vm->pc[2], vm->pc[3]) = *vRet;
                vm->pc += 4;
                break;

            case VM_OP_CST: /* Load constant value */
                VMAssert(vm, FrameCallee(vm->thread).type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                vRet = VMOpArg(1);
                *vRet = *LoadConstant(vm, FrameCallee(vm->thread).data.func, vm->pc[2]);
                vm->pc += 3;
                break;

            case VM_OP_I32: /* Load 32 bit integer */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = *((int32_t *)(vm->pc + 2));
                vm->pc += 4;
                break;

            case VM_OP_F64: /* Load 64 bit float */
                vRet = VMOpArg(1);
                vRet->type = TYPE_NUMBER;
                vRet->data.number = (Number) *((double *)(vm->pc + 2));
                vm->pc += 6;
                break;

            case VM_OP_MOV: /* Move Values */
                vRet = VMOpArg(1);
                v1 = vm->base + *((uint32_t *)(vm->pc + 2));
                *vRet = *v1;
                vm->pc += 4;
                break;

            case VM_OP_CLN: /* Create closure from constant FuncDef */
                VMMakeClosure(vm, vm->pc[1], vm->pc[2]);
                vm->pc += 3;
                break;

            case VM_OP_EQL: /* Equality */
                vRet = VMOpArg(1);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = ValueEqual(VMOpArg(2), VMOpArg(3));
                vm->pc += 4;
                break;

            case VM_OP_LTN: /* Less Than */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = (ValueCompare(VMOpArg(2), VMOpArg(3)) == -1);
                vm->pc += 4;
                break;

            case VM_OP_LTE: /* Less Than or Equal to */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = (ValueCompare(VMOpArg(2), VMOpArg(3)) != 1);
                vm->pc += 4;
                break;

            case VM_OP_ARR: /* Array literal */
            	vRet = VMOpArg(1);
            	{
                	uint32_t i;
					uint32_t arrayLen = vm->pc[2];
					Array * array = ArrayNew(&vm->gc, arrayLen);
					array->count = arrayLen;
					for (i = 0; i < arrayLen; ++i)
    					array->data[i] = *VMOpArg(3 + i);
    				vRet->type = TYPE_ARRAY;
    				vRet->data.array = array;
                    vm->pc += 3 + arrayLen;
                    VMMaybeCollect(vm);
            	}
                break;

            case VM_OP_DIC: /* Dictionary literal */
            	vRet = VMOpArg(1);
            	{
					uint32_t i = 3;
					uint32_t kvs = vm->pc[2];
					Dictionary * dict = DictNew(&vm->gc, kvs);
					kvs = kvs * 2 + 3;
					while (i < kvs) {
                        v1 = VMOpArg(i++);
                        v2 = VMOpArg(i++);
					    DictPut(&vm->gc, dict, v1, v2);
                    }
					vRet->type = TYPE_DICTIONARY;
					vRet->data.dict = dict;
					vm->pc += kvs;
					VMMaybeCollect(vm);
            	}
                break;

           case VM_OP_TCL: /* Tail call */
                VMTailCallOp(vm);
                break;

            /* Macro for generating some math operators */
			#define DO_MULTI_MATH(op, start) { \
                uint16_t i; \
                uint16_t count = vm->pc[1]; \
                Number accum = start; \
                vRet = VMOpArg(2); \
                for (i = 0; i < count; ++i) { \
                    Value * x = VMOpArg(3 + i); \
                    VMAssert(vm, x->type == TYPE_NUMBER, "Expected number"); \
                    accum = accum op x->data.number; \
                } \
                vRet->type = TYPE_NUMBER; vRet->data.number = accum; \
                vm->pc += 3 + count; \
            }

            /* Vectorized math */
            case VM_OP_ADM:
                DO_MULTI_MATH(+, 0)
                break;

            case VM_OP_SBM:
                DO_MULTI_MATH(-, 0)
                break;

            case VM_OP_MUM:
                DO_MULTI_MATH(*, 1)
                break;

            case VM_OP_DVM:
                DO_MULTI_MATH(/, 1)
                break;

			#undef DO_MULTI_MATH

            case VM_OP_RTN: /* Return nil */
                {
                    Value temp;
                    temp.type = TYPE_NIL;
                    VMReturn(vm, temp);
                }
                break;

            default:
                VMError(vm, "Unknown opcode");
                break;
        }
    }
}

/* Initialize the VM */
void VMInit(VM * vm) {
    GCInit(&vm->gc, 100000000);
    vm->gc.handleOutOfMemory = VMHandleOutOfMemory;
    vm->tempRoot.type = TYPE_NIL;
    vm->base = NULL;
    vm->pc = NULL;
    vm->error = NULL;
    vm->thread = ArrayNew(&vm->gc, 20);
}

/* Load a function into the VM. The function will be called with
 * no arguments when run */
void VMLoad(VM * vm, Func * func) {
    Value callee;
    callee.type = TYPE_FUNCTION;
    callee.data.func = func;
    vm->thread = ArrayNew(&vm->gc, 20);
    VMThreadPush(vm, vm->thread, callee, func->def->locals);
    vm->pc = func->def->byteCode;
}

/* Clear all memory associated with the VM */
void VMDeinit(VM * vm) {
    GCClear(&vm->gc);
}

#undef VMOpArg
#undef VMArg

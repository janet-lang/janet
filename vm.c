#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "value.h"
#include "ds.h"

#define VMArg(i) (vm->base + (i))
#define VMOpArg(i) (VMArg(vm->pc[(i)]))

static const char OOM[] = "Out of memory";
static const char NO_UPVALUE[] = "Out of memory";
static const char EXPECTED_FUNCTION[] = "Expected function";
static const char VMS_EXPECTED_NUMBER_ROP[] = "Expected right operand to be number";
static const char VMS_EXPECTED_NUMBER_LOP[] = "Expected left operand to be number";

/* The metadata header associated with an allocated block of memory */
#define GCHeader(mem) ((GCMemoryHeader *)(mem) - 1)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color;
};

/* Forward declaration */
static void VMMark(VM * vm, Value * x);

/* Helper to mark function environments */
static void VMMarkFuncEnv(VM * vm, FuncEnv * env) {
    if (GCHeader(env)->color != vm->black) {
        Value temp;
        GCHeader(env)->color = vm->black;
        if (env->thread) {
            temp.type = TYPE_THREAD;
            temp.data.array = env->thread;
            VMMark(vm, &temp);
        } else {
            uint32_t count = env->stackOffset;
            uint32_t i;
            GCHeader(env->values)->color = vm->black;
            for (i = 0; i < count; ++i) {
                VMMark(vm, env->values + i);
            }
        }
    }
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing garbage collection. */
static void VMMark(VM * vm, Value * x) {
    switch (x->type) {
        case TYPE_NIL:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_CFUNCTION:
            break;

        case TYPE_STRING:
        case TYPE_SYMBOL:
            GCHeader(VStringRaw(x->data.string))->color = vm->black;
            break;

        case TYPE_BYTEBUFFER:
            GCHeader(x->data.buffer)->color = vm->black;
            GCHeader(x->data.buffer->data)->color = vm->black;
            break;

        case TYPE_ARRAY:
        case TYPE_FORM:
            if (GCHeader(x->data.array)->color != vm->black) {
                uint32_t i, count;
                count = x->data.array->count;
                GCHeader(x->data.array)->color = vm->black;
                GCHeader(x->data.array->data)->color = vm->black;
                for (i = 0; i < count; ++i)
                   	VMMark(vm, x->data.array->data + i);
            }
            break;

        case TYPE_THREAD:
            if (GCHeader(x->data.array)->color != vm->black) {
                uint32_t i, count;
                count = x->data.array->count;
                GCHeader(x->data.array)->color = vm->black;
                GCHeader(x->data.array->data)->color = vm->black;
                if (count) {
                    count += FrameSize(x->data.array);
                    for (i = 0; i < count; ++i)
                        VMMark(vm, x->data.array->data + i);
                }               
            }
            break;

        case TYPE_FUNCTION:
            if (GCHeader(x->data.func)->color != vm->black) {
                Func * f = x->data.func;
                GCHeader(f)->color = vm->black;
                VMMarkFuncEnv(vm, f->env);
                {
                    Value temp;
                    temp.type = TYPE_FUNCDEF;
                    temp.data.funcdef = x->data.funcdef;
                    VMMark(vm, &temp);
                    if (f->parent) {
                        temp.type = TYPE_FUNCTION;
                        temp.data.func = f->parent;
                        VMMark(vm, &temp);
                    }
                }
            }
            break;

        case TYPE_DICTIONARY:
            if (GCHeader(x->data.dict)->color != vm->black) {
                DictionaryIterator iter;
                DictBucket * bucket;
                GCHeader(x->data.dict)->color = vm->black;
                GCHeader(x->data.dict->buckets)->color = vm->black;
                DictIterate(x->data.dict, &iter);
                while (DictIterateNext(&iter, &bucket)) {
                    GCHeader(bucket)->color = vm->black;
                    VMMark(vm, &bucket->key);
                    VMMark(vm, &bucket->value);
                }
            }
            break;

        case TYPE_FUNCDEF:
            if (GCHeader(x->data.funcdef)->color != vm->black) {
                GCHeader(x->data.funcdef->byteCode)->color = vm->black;
                uint32_t count, i;
                count = x->data.funcdef->literalsLen;
                if (x->data.funcdef->literals) {
                    GCHeader(x->data.funcdef->literals)->color = vm->black;
                    for (i = 0; i < count; ++i)
                        VMMark(vm, x->data.funcdef->literals + i);
                }
            }
            break;

       	case TYPE_FUNCENV:
			VMMarkFuncEnv(vm, x->data.funcenv);
           	break;

    }

}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
static void VMSweep(VM * vm) {
    GCMemoryHeader * previous = NULL;
    GCMemoryHeader * current = vm->blocks;
    while (current) {
        if (current->color != vm->black) {
            if (previous) {
                previous->next = current->next;
            } else {
                vm->blocks = current->next;
            }
            free(current);
        } else {
            previous = current;
        }
        current = current->next;
    }
    /* Rotate flag */
    vm->black = !vm->black;
}

/* Prepare a memory block */
static void * VMAllocPrepare(VM * vm, char * rawBlock, uint32_t size) {
    GCMemoryHeader * mdata;
    if (rawBlock == NULL) {
        VMCrash(vm, OOM);
    }
    vm->nextCollection += size;
    mdata = (GCMemoryHeader *) rawBlock;
    mdata->next = vm->blocks;
    vm->blocks = mdata;
    mdata->color = !vm->black;
    return rawBlock + sizeof(GCMemoryHeader);
}

/* Allocate some memory that is tracked for garbage collection */
void * VMAlloc(VM * vm, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return VMAllocPrepare(vm, malloc(totalSize), totalSize);
}

/* Allocate some zeroed memory that is tracked for garbage collection */
void * VMZalloc(VM * vm, uint32_t size) {
    uint32_t  totalSize = size + sizeof(GCMemoryHeader);
    return VMAllocPrepare(vm, calloc(1, totalSize), totalSize);
}

/* Run garbage collection */
void VMCollect(VM * vm) {
    if (vm->lock > 0) return;
    Value thread;
    thread.type = TYPE_THREAD;
    thread.data.array = vm->thread;
    VMMark(vm, &thread);
    VMMark(vm, &vm->tempRoot);
    VMSweep(vm);
    vm->nextCollection = 0;
}

/* Run garbage collection if needed */
void VMMaybeCollect(VM * vm) {
    if (vm->nextCollection >= vm->memoryInterval) {
        VMCollect(vm);
    }
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
	ArrayEnsure(vm, thread, nextCount + size);
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
    	env->values = VMAlloc(vm, sizeof(Value) * size);
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
        ++vm->lock;
        VMReturn(vm, callee.data.cfunction(vm));
        --vm->lock;
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
        Value * envValues = VMAlloc(vm, FrameSize(thread) * sizeof(Value));
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
	ArrayEnsure(vm, thread, thread->count + newFrameSize + arity);
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
        ++vm->lock;
        VMReturn(vm, callee.data.cfunction(vm));
        --vm->lock;
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
            env = VMAlloc(vm, sizeof(FuncEnv));
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
        fn = VMAlloc(vm, sizeof(Func));
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
                /* Error or crash. Handling TODO. */
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
                vRet->data.boolean = ValueEqual(*VMOpArg(2), *VMOpArg(3));
                vm->pc += 4;
                break;

            case VM_OP_LTN: /* Less Than */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = (ValueCompare(*VMOpArg(2), *VMOpArg(3)) == -1);
                vm->pc += 4;
                break;

            case VM_OP_LTE: /* Less Than or Equal to */
                vRet = VMOpArg(1);
                v1 = VMOpArg(2);
                v2 = VMOpArg(3);
                vRet->type = TYPE_BOOLEAN;
                vRet->data.boolean = (ValueCompare(*VMOpArg(2), *VMOpArg(3)) != 1);
                vm->pc += 4;
                break;

            case VM_OP_ARR: /* Array literal */
            	vRet = VMOpArg(1);
            	{
                	uint32_t i;
					uint32_t arrayLen = vm->pc[2];
					Array * array = ArrayNew(vm, arrayLen);
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
					Dictionary * dict = DictNew(vm, kvs);
					kvs = kvs * 2 + 3;
					while (i < kvs) {
                        v1 = VMOpArg(i++);
                        v2 = VMOpArg(i++);
					    DictPut(vm, dict, *v1, *v2);
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

/* Get an argument from the stack */
Value VMGetArg(VM * vm, uint16_t index) {
    uint16_t frameSize = FrameSize(vm->thread);
    VMAssert(vm, frameSize > index, "Cannot get arg out of stack bounds");
	return *VMArg(index);
}

/* Put a value on the stack */
void VMSetArg(VM * vm, uint16_t index, Value x) {
    uint16_t frameSize = FrameSize(vm->thread);
    VMAssert(vm, frameSize > index, "Cannot set arg out of stack bounds");
	*VMArg(index) = x;
}

#undef VMOpArg
#undef VMArg

/* Initialize the VM */
void VMInit(VM * vm) {
    vm->tempRoot.type = TYPE_NIL;
    vm->base = NULL;
    vm->pc = NULL;
    vm->error = NULL;
	/* Garbage collection */
    vm->blocks = NULL;
    vm->nextCollection = 0;
    vm->memoryInterval = 1024 * 256;
    vm->black = 0;
    vm->lock = 0;
    /* Create new thread */
    vm->thread = ArrayNew(vm, 20);
}

/* Load a function into the VM. The function will be called with
 * no arguments when run */
void VMLoad(VM * vm, Func * func) {
    Value callee;
    callee.type = TYPE_FUNCTION;
    callee.data.func = func;
    vm->thread = ArrayNew(vm, 20);
    VMThreadPush(vm, vm->thread, callee, func->def->locals);
    vm->pc = func->def->byteCode;
}

/* Clear all memory associated with the VM */
void VMDeinit(VM * vm) {
    GCMemoryHeader * current = vm->blocks;
    while (current) {
        GCMemoryHeader * next = current->next;
        free(current);
        current = next;
    }
    vm->blocks = NULL;
}

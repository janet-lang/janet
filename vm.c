#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "value.h"
#include "ds.h"

static const char OOM[] = "Out of memory";
static const char NO_UPVALUE[] = "No upvalue";
static const char EXPECTED_FUNCTION[] = "Expected function";
static const char VMS_EXPECTED_NUMBER_ROP[] = "Expected right operand to be number";
static const char VMS_EXPECTED_NUMBER_LOP[] = "Expected left operand to be number";

/* The size of a StackFrame in units of Values. */
#define FRAME_SIZE ((sizeof(StackFrame) + sizeof(Value) - 1) / sizeof(Value))

/* Get the stack frame pointer for a thread */
static StackFrame * ThreadFrame(Array * thread) {
    return (StackFrame *)(thread->data + thread->count - FRAME_SIZE);
}

/* The metadata header associated with an allocated block of memory */
#define GCHeader(mem) ((GCMemoryHeader *)(mem) - 1)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color : 1;
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
        }
        if (env->values) {
            uint32_t count = env->stackOffset;
            uint32_t i;
            GCHeader(env->values)->color = vm->black;
            for (i = 0; i < count; ++i)
                VMMark(vm, env->values + i);
        }
    }
}

/* GC helper to mark a FuncDef */
static void VMMarkFuncDef(VM * vm, FuncDef * def) {
    if (GCHeader(def)->color != vm->black) {
        GCHeader(def)->color = vm->black;
        GCHeader(def->byteCode)->color = vm->black;
        uint32_t count, i;
        if (def->literals) {
            count = def->literalsLen;
            GCHeader(def->literals)->color = vm->black;
            for (i = 0; i < count; ++i)
                VMMark(vm, def->literals + i);
        }
    }
}

/* Helper to mark a stack frame. Returns the next frame. */
static StackFrame * VMMarkStackFrame(VM * vm, StackFrame * frame) {
    uint32_t i;
    Value * stack = (Value *)frame + FRAME_SIZE;
    VMMark(vm, &frame->callee);
    if (frame->env)
        VMMarkFuncEnv(vm, frame->env);
    for (i = 0; i < frame->size; ++i)
        VMMark(vm, stack + i);
    return (StackFrame *)(stack + frame->size);
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing the garbage collection mark phase. */
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
                Array * thread = x->data.array;
                StackFrame * frame = (StackFrame *)thread->data;
                StackFrame * end = ThreadFrame(thread);
                GCHeader(thread)->color = vm->black;
                GCHeader(thread->data)->color = vm->black;
                while (frame <= end)
                    frame = VMMarkStackFrame(vm, frame);
            }
            break;

        case TYPE_FUNCTION:
            if (GCHeader(x->data.func)->color != vm->black) {
                Func * f = x->data.func;
                GCHeader(f)->color = vm->black;
                VMMarkFuncDef(vm, f->def);
                if (f->env)
                    VMMarkFuncEnv(vm, f->env);
                if (f->parent) {
                    Value temp;
                    temp.type = TYPE_FUNCTION;
                    temp.data.func = f->parent;
                    VMMark(vm, &temp);
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
			VMMarkFuncDef(vm, x->data.funcdef);
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
    GCMemoryHeader * next;
    while (current) {
        next = current->next;
        if (current->color != vm->black) {
            if (previous) {
                previous->next = next;
            } else {
                vm->blocks = next;
            }
            free(current);
        } else {
            previous = current;
        }
        current = next;
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
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return VMAllocPrepare(vm, calloc(1, totalSize), totalSize);
}

/* Run garbage collection */
void VMCollect(VM * vm) {
    if (vm->lock > 0) return;
    /* Thread can be null */
    if (vm->thread) {
        Value thread;
        thread.type = TYPE_THREAD;
        thread.data.array = vm->thread;
        VMMark(vm, &thread);
    }
    VMMark(vm, &vm->ret);
    VMSweep(vm);
    vm->nextCollection = 0;
}

/* Run garbage collection if needed */
void VMMaybeCollect(VM * vm) {
    if (vm->nextCollection >= vm->memoryInterval)
        VMCollect(vm);
}

/* Push a stack frame onto a thread */
static void VMThreadPush(VM * vm, Array * thread, Value callee, uint32_t size) {
    uint16_t oldSize;
    uint32_t nextCount, i;
    StackFrame * frame;
    if (thread->count) {
        frame = ThreadFrame(thread);
        oldSize = frame->size;
    } else {
        oldSize = 0;
    }
    nextCount = thread->count + oldSize + FRAME_SIZE;
    ArrayEnsure(vm, thread, nextCount + size);
    thread->count = nextCount;
    /* Ensure values start out as nil so as to not confuse
     * the garabage collector */
    for (i = nextCount; i < nextCount + size; ++i)
        thread->data[i].type = TYPE_NIL;
    vm->base = thread->data + thread->count;
    vm->frame = frame = (StackFrame *)(vm->base - FRAME_SIZE);
    /* Set up the new stack frame */
    frame->prevSize = oldSize;
    frame->size = size;
    frame->env = NULL;
    frame->callee = callee;
}

/* Copy the current function stack to the current closure
   environment */
static void VMThreadSplitStack(VM * vm) {
    StackFrame * frame = vm->frame;
    FuncEnv * env = frame->env;
    /* Check for closures */
    if (env) {
        Array * thread = vm->thread;
        uint32_t size = frame->size;
        env->thread = NULL;
        env->stackOffset = size;
        env->values = VMAlloc(vm, sizeof(Value) * size);
        memcpy(env->values, thread->data + thread->count, size * sizeof(Value));
    }
}

/* Pop the top-most stack frame from stack */
static void VMThreadPop(VM * vm) {
    Array * thread = vm->thread;
    StackFrame * frame = vm->frame;
    uint32_t delta = FRAME_SIZE + frame->prevSize;
    if (thread->count) {
        VMThreadSplitStack(vm);
    } else {
        VMError(vm, "Nothing to pop from stack.");
    }
    thread->count -= delta;
    vm->base -= delta;
    vm->frame = (StackFrame *)(vm->base - FRAME_SIZE);
}

/* Get an upvalue */
static Value * GetUpValue(VM * vm, Func * fn, uint16_t level, uint16_t index) {
    FuncEnv * env;
    Value * stack;
    if (!level) {
        return vm->base + index;
    }
    while (fn && --level)
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
static Value LoadConstant(VM * vm, Func * fn, uint16_t index) {
    if (index > fn->def->literalsLen) {
        VMError(vm, NO_UPVALUE);
    }
    return fn->def->literals[index];
}

/* Boolean truth definition */
static int truthy(Value v) {
    return v.type != TYPE_NIL && !(v.type == TYPE_BOOLEAN && !v.data.boolean);
}

/* Return from the vm */
static void VMReturn(VM * vm, Value ret) {
    VMThreadPop(vm);
    if (vm->thread->count == 0) {
        VMExit(vm, ret);
    }
    vm->pc = vm->frame->pc;
    vm->base[vm->frame->ret] = ret;
}

/* Implementation of the opcode for function calls */
static void VMCallOp(VM * vm) {
    Array * thread = vm->thread;
    Value callee = vm->base[vm->pc[1]];
    uint32_t arity = vm->pc[3];
    uint32_t oldCount = thread->count;
    uint32_t i;
    Value * oldBase;
    vm->frame->pc = vm->pc + 4 + arity;
    vm->frame->ret = vm->pc[2];
    if (callee.type == TYPE_FUNCTION) {
        Func * fn = callee.data.func;
        VMThreadPush(vm, thread, callee, fn->def->locals);
    } else if (callee.type == TYPE_CFUNCTION) {
        VMThreadPush(vm, thread, callee, arity);
    } else {
        VMError(vm, EXPECTED_FUNCTION);
    }
    oldBase = thread->data + oldCount;
    if (callee.type == TYPE_CFUNCTION) {
        for (i = 0; i < arity; ++i)
            vm->base[i] = oldBase[vm->pc[4 + i]];
        ++vm->lock;
        VMReturn(vm, callee.data.cfunction(vm));
        --vm->lock;
    } else {
        Func * f = callee.data.func;
        uint32_t locals = f->def->locals;
        for (i = 0; i < arity; ++i)
            vm->base[i] = oldBase[vm->pc[4 + i]];
        for (; i < locals; ++i)
            vm->base[i].type = TYPE_NIL;
        vm->pc = f->def->byteCode;
    }
}

/* Implementation of the opcode for tail calls */
static void VMTailCallOp(VM * vm) {
    Array * thread = vm->thread;
    Value callee = vm->base[vm->pc[1]];
    uint32_t arity = vm->pc[2];
    uint16_t newFrameSize, currentFrameSize;
    uint32_t i;
    /* Check for closures */
    VMThreadSplitStack(vm);
    if (callee.type == TYPE_CFUNCTION) {
        newFrameSize = arity;
    } else if (callee.type == TYPE_FUNCTION) {
        Func * f = callee.data.func;
        newFrameSize = f->def->locals;
    } else {
        VMError(vm, EXPECTED_FUNCTION);
    }
    /* Ensure stack has enough space for copies of arguments */
    currentFrameSize = vm->frame->size;
    ArrayEnsure(vm, thread, thread->count + currentFrameSize + arity);
    vm->base = thread->data + thread->count;
    /* Copy the arguments into the extra space */
    for (i = 0; i < arity; ++i) {
        vm->base[currentFrameSize + i] = vm->base[vm->pc[3 + i]];
    }
    /* Copy the end of the stack to the parameter position */
    memcpy(vm->base, vm->base + currentFrameSize, arity * sizeof(Value));
    /* nil the non argument part of the stack for gc */
    for (i = arity; i < newFrameSize; ++i) {
        vm->base[i].type = TYPE_NIL;
    }
    /* Update the stack frame */
    vm->frame->size = newFrameSize;
    vm->frame->callee = callee;
    vm->frame->env = NULL;
    if (callee.type == TYPE_CFUNCTION) {
        ++vm->lock;
        VMReturn(vm, callee.data.cfunction(vm));
        --vm->lock;
    } else {
        Func * f = callee.data.func;
        vm->pc = f->def->byteCode;
    }
}

/* Instantiate a closure */
static Value VMMakeClosure(VM * vm, uint16_t literal) {
    Array * thread = vm->thread;
    if (vm->frame->callee.type != TYPE_FUNCTION) {
        VMError(vm, EXPECTED_FUNCTION);
    } else {
        Value constant, ret;
        Func * fn, * current;
        FuncEnv * env = vm->frame->env;
        if (!env) {
            env = VMAlloc(vm, sizeof(FuncEnv));
            env->thread = thread;
            env->stackOffset = thread->count;
            env->values = NULL;
            vm->frame->env = env;
        }
        current = vm->frame->callee.data.func;
        constant = LoadConstant(vm, current, literal);
        if (constant.type != TYPE_FUNCDEF) {
            VMError(vm, EXPECTED_FUNCTION);
        }
        fn = VMAlloc(vm, sizeof(Func));
        fn->def = constant.data.funcdef;
        fn->parent = current;
        fn->env = env;
        ret.type = TYPE_FUNCTION;
        ret.data.func = fn;
        return ret;
    }
}

/* Start running the VM */
int VMStart(VM * vm) {

    /* Set jmp_buf to jump back to for return. */
    {
        int n;
        if ((n = setjmp(vm->jump))) {
            vm->lock = 0;
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
            Value temp, v1, v2;

            #define DO_BINARY_MATH(op) \
                v1 = vm->base[vm->pc[2]]; \
                v2 = vm->base[vm->pc[3]]; \
                VMAssert(vm, v1.type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_LOP); \
                VMAssert(vm, v2.type == TYPE_NUMBER, VMS_EXPECTED_NUMBER_ROP); \
                temp.type = TYPE_NUMBER; \
                temp.data.number = v1.data.number op v2.data.number; \
                vm->base[vm->pc[1]] = temp; \
                vm->pc += 4; \
                break;

            case VM_OP_ADD: /* Addition */
                DO_BINARY_MATH(+)

            case VM_OP_SUB: /* Subtraction */
                DO_BINARY_MATH(-)

            case VM_OP_MUL: /* Multiplication */
                DO_BINARY_MATH(*)

            case VM_OP_DIV: /* Division */
                DO_BINARY_MATH(/)

            #undef DO_BINARY_MATH

            case VM_OP_NOT: /* Boolean unary (Boolean not) */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = !truthy(vm->base[vm->pc[2]]);
                vm->base[vm->pc[1]] = temp;
                vm->pc += 3;
                break;

            case VM_OP_LD0: /* Load 0 */
                temp.type = TYPE_NUMBER;
                temp.data.number = 0;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 2;
                break;

            case VM_OP_LD1: /* Load 1 */
                temp.type = TYPE_NUMBER;
                temp.data.number = 1;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 2;
                break;

            case VM_OP_FLS: /* Load False */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = 0;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 2;
                break;

            case VM_OP_TRU: /* Load True */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = 1;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 2;
                break;

            case VM_OP_NIL: /* Load Nil */
                temp.type = TYPE_NIL;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 2;
                break;

            case VM_OP_I16: /* Load Small Integer */
                temp.type = TYPE_NUMBER;
                temp.data.number = ((int16_t *)(vm->pc))[2];
                vm->base[vm->pc[1]] = temp;
                vm->pc += 3;
                break;

            case VM_OP_UPV: /* Load Up Value */
                temp = vm->frame->callee;
                VMAssert(vm, temp.type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                vm->base[vm->pc[1]] = *GetUpValue(vm, temp.data.func, vm->pc[2], vm->pc[3]);
                vm->pc += 4;
                break;

            case VM_OP_JIF: /* Jump If */
                if (truthy(vm->base[vm->pc[1]])) {
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
                VMReturn(vm, vm->base[vm->pc[1]]);
                break;

            case VM_OP_SUV: /* Set Up Value */
                temp = vm->frame->callee;
                VMAssert(vm, temp.type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                *GetUpValue(vm, temp.data.func, vm->pc[2], vm->pc[3]) = vm->base[vm->pc[1]];
                vm->pc += 4;
                break;

            case VM_OP_CST: /* Load constant value */
                temp = vm->frame->callee;
                VMAssert(vm, temp.type == TYPE_FUNCTION, EXPECTED_FUNCTION);
                vm->base[vm->pc[1]] = LoadConstant(vm, temp.data.func, vm->pc[2]);
                vm->pc += 3;
                break;

            case VM_OP_I32: /* Load 32 bit integer */
                temp.type = TYPE_NUMBER;
                temp.data.number = *((int32_t *)(vm->pc + 2));
                vm->base[vm->pc[1]] = temp;
                vm->pc += 4;
                break;

            case VM_OP_F64: /* Load 64 bit float */
                temp.type = TYPE_NUMBER;
                temp.data.number = (Number) *((double *)(vm->pc + 2));
                vm->base[vm->pc[1]] = temp;
                vm->pc += 6;
                break;

            case VM_OP_MOV: /* Move Values */
                vm->base[vm->pc[1]] = vm->base[vm->pc[2]];
                vm->pc += 3;
                break;

            case VM_OP_CLN: /* Create closure from constant FuncDef */
                vm->base[vm->pc[1]] = VMMakeClosure(vm, vm->pc[2]);
                vm->pc += 3;
                break;

            case VM_OP_EQL: /* Equality */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = ValueEqual(vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
                vm->base[vm->pc[1]] = temp;
                vm->pc += 4;
                break;

            case VM_OP_LTN: /* Less Than */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = (ValueCompare(vm->base[vm->pc[2]], vm->base[vm->pc[3]]) == -1);
                vm->base[vm->pc[1]] = temp;
                vm->pc += 4;
                break;

            case VM_OP_LTE: /* Less Than or Equal to */
                temp.type = TYPE_BOOLEAN;
                temp.data.boolean = (ValueEqual(vm->base[vm->pc[2]], vm->base[vm->pc[3]]) != 1);
                vm->base[vm->pc[1]] = temp;
                vm->pc += 4;
                break;

            case VM_OP_ARR: /* Array literal */
                {
                    uint32_t i;
                    uint32_t arrayLen = vm->pc[2];
                    Array * array = ArrayNew(vm, arrayLen);
                    array->count = arrayLen;
                    for (i = 0; i < arrayLen; ++i)
                        array->data[i] = vm->base[vm->pc[3 + i]];
                    temp.type = TYPE_ARRAY;
                    temp.data.array = array;
                    vm->base[vm->pc[1]] = temp;
                    vm->pc += 3 + arrayLen;
                }
                break;

            case VM_OP_DIC: /* Dictionary literal */
                {
                    uint32_t i = 3;
                    uint32_t kvs = vm->pc[2];
                    Dictionary * dict = DictNew(vm, kvs + 2);
                    kvs = kvs + 3;
                    while (i < kvs) {
                        v1 = vm->base[vm->pc[i++]];
                        v2 = vm->base[vm->pc[i++]];
                        DictPut(vm, dict, v1, v2);
                    }
                    temp.type = TYPE_DICTIONARY;
                    temp.data.dict = dict;
                    vm->base[vm->pc[1]] = temp;
                    vm->pc += kvs;
                }
                break;

            case VM_OP_TCL: /* Tail call */
                VMTailCallOp(vm);
                break;

            /* Macro for generating some math operators */
            #define DO_MULTI_MATH(op, start) { \
                uint16_t count = vm->pc[2]; \
                uint16_t i; \
                Number accum = start; \
                for (i = 0; i < count; ++i) { \
                    v1 = vm->base[vm->pc[3 + i]]; \
                    VMAssert(vm, v1.type == TYPE_NUMBER, "Expected number"); \
                    accum = accum op v1.data.number; \
                } \
                temp.type = TYPE_NUMBER; \
                temp.data.number = accum; \
                vm->base[vm->pc[1]] = temp; \
                vm->pc += 3 + count; \
                break; \
            }

            /* Vectorized math */
            case VM_OP_ADM:
                DO_MULTI_MATH(+, 0)

            case VM_OP_SBM:
                DO_MULTI_MATH(-, 0)

            case VM_OP_MUM:
                DO_MULTI_MATH(*, 1)

            case VM_OP_DVM:
                DO_MULTI_MATH(/, 1)

            #undef DO_MULTI_MATH

            case VM_OP_RTN: /* Return nil */
                temp.type = TYPE_NIL;
                VMReturn(vm, temp);
                break;

            case VM_OP_GET:
				temp = ValueGet(vm, vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
				vm->base[vm->pc[1]] = temp;
				vm->pc += 4;
                break;

            case VM_OP_SET:
				ValueSet(vm, vm->base[vm->pc[1]], vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
				vm->pc += 4;
                break;

            default:
                VMError(vm, "Unknown opcode");
                break;
        }
        VMMaybeCollect(vm);
    }
}

/* Get an argument from the stack */
Value VMGetArg(VM * vm, uint16_t index) {
    uint16_t frameSize = vm->frame->size;
    VMAssert(vm, frameSize > index, "Cannot get arg out of stack bounds");
    return vm->base[index];
}

/* Put a value on the stack */
void VMSetArg(VM * vm, uint16_t index, Value x) {
    uint16_t frameSize = vm->frame->size;
    VMAssert(vm, frameSize > index, "Cannot set arg out of stack bounds");
    vm->base[index] = x;
}

/* Get the size of the VMStack */
uint16_t VMCountArgs(VM * vm) {
    return vm->frame->size;
}

/* Initialize the VM */
void VMInit(VM * vm) {
    vm->ret.type = TYPE_NIL;
    vm->base = NULL;
    vm->frame = NULL;
    vm->pc = NULL;
    vm->error = NULL;
    /* Garbage collection */
    vm->blocks = NULL;
    vm->nextCollection = 0;
    vm->memoryInterval = 0;
    vm->black = 0;
    vm->lock = 0;
    /* Add thread */
    vm->thread = NULL;
}

/* Load a function into the VM. The function will be called with
 * no arguments when run */
void VMLoad(VM * vm, Value func) {
    Array * thread = ArrayNew(vm, 100);
    vm->thread = thread;
    if (func.type == TYPE_FUNCTION) {
        Func * fn = func.data.func;
        VMThreadPush(vm, thread, func, fn->def->locals);
        vm->pc = fn->def->byteCode;
    } else if (func.type == TYPE_CFUNCTION) {
        VMThreadPush(vm, thread, func, 0);
        vm->pc = NULL;
    } else {
        return;
    }
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

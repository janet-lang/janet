#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "value.h"
#include "ds.h"
#include "gc.h"
#include "thread.h"

static const char GST_NO_UPVALUE[] = "no upvalue";
static const char GST_EXPECTED_FUNCTION[] = "expected function";
static const char GST_EXPECTED_NUMBER_ROP[] = "expected right operand to be number";
static const char GST_EXPECTED_NUMBER_LOP[] = "expected left operand to be number";

/* Get an upvalue */
static GstValue *gst_vm_upvalue_location(Gst *vm, GstFunction *fn, uint16_t level, uint16_t index) {
    GstFuncEnv *env;
    GstValue *stack;
    if (!level)
        return vm->base + index;
    while (fn && --level)
        fn = fn->parent;
    gst_assert(vm, fn, GST_NO_UPVALUE);
    env = fn->env;
    if (env->thread)
        stack = env->thread->data + env->stackOffset;
    else
        stack = env->values;
    return stack + index;
}

/* Get a literal */
static GstValue gst_vm_literal(Gst *vm, GstFunction *fn, uint16_t index) {
    if (index > fn->def->literalsLen) {
        gst_error(vm, GST_NO_UPVALUE);
    }
    return fn->def->literals[index];
}

/* Return from the vm */
static void gst_vm_return(Gst *vm, GstValue ret) {
    gst_thread_pop(vm);
    if (vm->thread->count == 0) {
        gst_exit(vm, ret);
    }
    vm->pc = vm->frame->pc;
    vm->base[vm->frame->ret] = ret;
}

/* Implementation of the opcode for function calls */
static void gst_vm_call(Gst *vm) {
    GstThread *thread = vm->thread;
    GstValue callee = vm->base[vm->pc[1]];
    uint32_t arity = vm->pc[3];
    uint32_t oldCount = thread->count;
    uint32_t i;
    GstValue *oldBase;
    vm->frame->pc = vm->pc + 4 + arity;
    vm->frame->ret = vm->pc[2];
    if (callee.type == GST_FUNCTION) {
        GstFunction *fn = callee.data.function;
        gst_thread_push(vm, thread, callee, fn->def->locals);
    } else if (callee.type == GST_CFUNCTION) {
        gst_thread_push(vm, thread, callee, arity);
    } else {
        gst_error(vm, GST_EXPECTED_FUNCTION);
    }
    oldBase = thread->data + oldCount;
    if (callee.type == GST_CFUNCTION) {
        for (i = 0; i < arity; ++i)
            vm->base[i] = oldBase[vm->pc[4 + i]];
        ++vm->lock;
        gst_vm_return(vm, callee.data.cfunction(vm));
        --vm->lock;
    } else {
        GstFunction *f = callee.data.function;
        uint32_t locals = f->def->locals;
        for (i = 0; i < arity; ++i)
            vm->base[i] = oldBase[vm->pc[4 + i]];
        for (; i < locals; ++i)
            vm->base[i].type = GST_NIL;
        vm->pc = f->def->byteCode;
    }
}

/* Implementation of the opcode for tail calls */
static void gst_vm_tailcall(Gst *vm) {
    GstThread *thread = vm->thread;
    GstValue callee = vm->base[vm->pc[1]];
    uint32_t arity = vm->pc[2];
    uint16_t newFrameSize, currentFrameSize;
    uint32_t i;
    /* Check for closures */
    gst_thread_split_env(vm);
    if (callee.type == GST_CFUNCTION) {
        newFrameSize = arity;
    } else if (callee.type == GST_FUNCTION) {
        GstFunction * f = callee.data.function;
        newFrameSize = f->def->locals;
    } else {
        gst_error(vm, GST_EXPECTED_FUNCTION);
    }
    /* Ensure stack has enough space for copies of arguments */
    currentFrameSize = vm->frame->size;
    gst_thread_ensure(vm, thread, thread->count + currentFrameSize + arity);
    vm->base = thread->data + thread->count;
    /* Copy the arguments into the extra space */
    for (i = 0; i < arity; ++i)
        vm->base[currentFrameSize + i] = vm->base[vm->pc[3 + i]];
    /* Copy the end of the stack to the parameter position */
    memcpy(vm->base, vm->base + currentFrameSize, arity * sizeof(GstValue));
    /* nil the non argument part of the stack for gc */
    for (i = arity; i < newFrameSize; ++i)
        vm->base[i].type = GST_NIL;
    /* Update the stack frame */
    vm->frame->size = newFrameSize;
    vm->frame->callee = callee;
    vm->frame->env = NULL;
    if (callee.type == GST_CFUNCTION) {
        ++vm->lock;
        gst_vm_return(vm, callee.data.cfunction(vm));
        --vm->lock;
    } else {
        GstFunction *f = callee.data.function;
        vm->pc = f->def->byteCode;
    }
}

/* Instantiate a closure */
static GstValue gst_vm_closure(Gst *vm, uint16_t literal) {
    GstThread *thread = vm->thread;
    if (vm->frame->callee.type != GST_FUNCTION) {
        gst_error(vm, GST_EXPECTED_FUNCTION);
    } else {
        GstValue constant, ret;
        GstFunction *fn, *current;
        GstFuncEnv *env = vm->frame->env;
        if (!env) {
            env = gst_alloc(vm, sizeof(GstFuncEnv));
            env->thread = thread;
            env->stackOffset = thread->count;
            env->values = NULL;
            vm->frame->env = env;
        }
        current = vm->frame->callee.data.function;
        constant = gst_vm_literal(vm, current, literal);
        if (constant.type != GST_NIL) {
            gst_error(vm, "cannot create closure");
        }
        fn = gst_alloc(vm, sizeof(GstFunction));
        fn->def = (GstFuncDef *) constant.data.pointer;
        fn->parent = current;
        fn->env = env;
        ret.type = GST_FUNCTION;
        ret.data.function = fn;
        return ret;
    }
}

/* Start running the VM */
int gst_start(Gst *vm) {

    /* Set jmp_buf to jump back to for return. */
    {
        int n;
        if ((n = setjmp(vm->jump))) {
            /* Good return */
            if (n == 1) {
                vm->lock = 0;
                return 0;
            } else if (n == 2) {
                /* Error. */
                while (vm->thread->count && !vm->frame->errorJump) {
					gst_thread_pop(vm);
                }
                if (vm->thread->count == 0)
                    return n;
                /* Jump to the error location */
                vm->pc = vm->frame->errorJump;
                /* Set error */
                vm->base[vm->frame->errorSlot] = vm->error;
                vm->lock = 0;
            } else {
                /* Crash. just return */
                vm->lock = 0;
                return n;
            }
        }
    }

    for (;;) {
        GstValue temp, v1, v2;
        uint16_t opcode = *vm->pc;

        switch (opcode) {

        #define DO_BINARY_MATH(op) \
            v1 = vm->base[vm->pc[2]]; \
            v2 = vm->base[vm->pc[3]]; \
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP); \
            gst_assert(vm, v2.type == GST_NUMBER, GST_EXPECTED_NUMBER_ROP); \
            temp.type = GST_NUMBER; \
            temp.data.number = v1.data.number op v2.data.number; \
            vm->base[vm->pc[1]] = temp; \
            vm->pc += 4; \
            break;

        case GST_OP_ADD: /* Addition */
            DO_BINARY_MATH(+)

        case GST_OP_SUB: /* Subtraction */
            DO_BINARY_MATH(-)

        case GST_OP_MUL: /* Multiplication */
            DO_BINARY_MATH(*)

        case GST_OP_DIV: /* Division */
            DO_BINARY_MATH(/)

        #undef DO_BINARY_MATH

        case GST_OP_NOT: /* Boolean unary (Boolean not) */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = !gst_truthy(vm->base[vm->pc[2]]);
            vm->base[vm->pc[1]] = temp;
            vm->pc += 3;
            break;

        case GST_OP_LD0: /* Load 0 */
            temp.type = GST_NUMBER;
            temp.data.number = 0;
            vm->base[vm->pc[1]] = temp;
            vm->pc += 2;
            break;

        case GST_OP_LD1: /* Load 1 */
            temp.type = GST_NUMBER;
            temp.data.number = 1;
            vm->base[vm->pc[1]] = temp;
            vm->pc += 2;
            break;

        case GST_OP_FLS: /* Load False */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 0;
            vm->base[vm->pc[1]] = temp;
            vm->pc += 2;
            break;

        case GST_OP_TRU: /* Load True */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 1;
            vm->base[vm->pc[1]] = temp;
            vm->pc += 2;
            break;

        case GST_OP_NIL: /* Load Nil */
            temp.type = GST_NIL;
            vm->base[vm->pc[1]] = temp;
            vm->pc += 2;
            break;

        case GST_OP_I16: /* Load Small Integer */
            temp.type = GST_NUMBER;
            temp.data.number = ((int16_t *)(vm->pc))[2];
            vm->base[vm->pc[1]] = temp;
            vm->pc += 3;
            break;

        case GST_OP_UPV: /* Load Up Value */
            temp = vm->frame->callee;
            gst_assert(vm, temp.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            vm->base[vm->pc[1]] = *gst_vm_upvalue_location(vm, temp.data.function, vm->pc[2], vm->pc[3]);
            vm->pc += 4;
            break;

        case GST_OP_JIF: /* Jump If */
            if (gst_truthy(vm->base[vm->pc[1]])) {
                vm->pc += 4;
            } else {
                vm->pc += *((int32_t *)(vm->pc + 2));
            }
            break;

        case GST_OP_JMP: /* Jump */
            vm->pc += *((int32_t *)(vm->pc + 1));
            break;

        case GST_OP_CAL: /* Call */
            gst_vm_call(vm);
            break;

        case GST_OP_RET: /* Return */
            gst_vm_return(vm, vm->base[vm->pc[1]]);
            break;

        case GST_OP_SUV: /* Set Up Value */
            temp = vm->frame->callee;
            gst_assert(vm, temp.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            *gst_vm_upvalue_location(vm, temp.data.function, vm->pc[2], vm->pc[3]) = vm->base[vm->pc[1]];
            vm->pc += 4;
            break;

        case GST_OP_CST: /* Load constant value */
            temp = vm->frame->callee;
            gst_assert(vm, temp.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            vm->base[vm->pc[1]] = gst_vm_literal(vm, temp.data.function, vm->pc[2]);
            vm->pc += 3;
            break;

        case GST_OP_I32: /* Load 32 bit integer */
            temp.type = GST_NUMBER;
            temp.data.number = *((int32_t *)(vm->pc + 2));
            vm->base[vm->pc[1]] = temp;
            vm->pc += 4;
            break;

        case GST_OP_F64: /* Load 64 bit float */
            temp.type = GST_NUMBER;
            temp.data.number = (GstNumber) *((double *)(vm->pc + 2));
            vm->base[vm->pc[1]] = temp;
            vm->pc += 6;
            break;

        case GST_OP_MOV: /* Move Values */
            vm->base[vm->pc[1]] = vm->base[vm->pc[2]];
            vm->pc += 3;
            break;

        case GST_OP_CLN: /* Create closure from constant FuncDef */
            vm->base[vm->pc[1]] = gst_vm_closure(vm, vm->pc[2]);
            vm->pc += 3;
            break;

        case GST_OP_EQL: /* Equality */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = gst_equals(vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
            vm->base[vm->pc[1]] = temp;
            vm->pc += 4;
            break;

        case GST_OP_LTN: /* Less Than */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(vm->base[vm->pc[2]], vm->base[vm->pc[3]]) == -1);
            vm->base[vm->pc[1]] = temp;
            vm->pc += 4;
            break;

        case GST_OP_LTE: /* Less Than or Equal to */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(vm->base[vm->pc[2]], vm->base[vm->pc[3]]) != 1);
            vm->base[vm->pc[1]] = temp;
            vm->pc += 4;
            break;

        case GST_OP_ARR: /* Array literal */
            {
                uint32_t i;
                uint32_t arrayLen = vm->pc[2];
                GstArray *array = gst_array(vm, arrayLen);
                array->count = arrayLen;
                for (i = 0; i < arrayLen; ++i)
                    array->data[i] = vm->base[vm->pc[3 + i]];
                temp.type = GST_ARRAY;
                temp.data.array = array;
                vm->base[vm->pc[1]] = temp;
                vm->pc += 3 + arrayLen;
            }
            break;

        case GST_OP_DIC: /* Object literal */
            {
                uint32_t i = 3;
                uint32_t kvs = vm->pc[2];
                GstObject *o = gst_object(vm, kvs + 2);
                kvs = kvs + 3;
                while (i < kvs) {
                    v1 = vm->base[vm->pc[i++]];
                    v2 = vm->base[vm->pc[i++]];
                    gst_object_put(vm, o, v1, v2);
                }
                temp.type = GST_OBJECT;
                temp.data.object = o;
                vm->base[vm->pc[1]] = temp;
                vm->pc += kvs;
            }
            break;

        case GST_OP_TCL: /* Tail call */
            gst_vm_tailcall(vm);
            break;

        /* Macro for generating some math operators */
        #define DO_MULTI_MATH(op, start) { \
            uint16_t count = vm->pc[2]; \
            uint16_t i; \
            GstNumber accum = start; \
            for (i = 0; i < count; ++i) { \
                v1 = vm->base[vm->pc[3 + i]]; \
                gst_assert(vm, v1.type == GST_NUMBER, "expected number"); \
                accum = accum op v1.data.number; \
            } \
            temp.type = GST_NUMBER; \
            temp.data.number = accum; \
            vm->base[vm->pc[1]] = temp; \
            vm->pc += 3 + count; \
            break; \
        }

        /* Vectorized math */
        case GST_OP_ADM:
            DO_MULTI_MATH(+, 0)

        case GST_OP_SBM:
            DO_MULTI_MATH(-, 0)

        case GST_OP_MUM:
            DO_MULTI_MATH(*, 1)

        case GST_OP_DVM:
            DO_MULTI_MATH(/, 1)

        #undef DO_MULTI_MATH

        case GST_OP_RTN: /* Return nil */
            temp.type = GST_NIL;
            gst_vm_return(vm, temp);
            break;

        case GST_OP_GET:
			temp = gst_get(vm, vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
			vm->base[vm->pc[1]] = temp;
			vm->pc += 4;
            break;

        case GST_OP_SET:
			gst_set(vm, vm->base[vm->pc[1]], vm->base[vm->pc[2]], vm->base[vm->pc[3]]);
			vm->pc += 4;
            break;

    	case GST_OP_ERR:
			vm->error = vm->base[vm->pc[1]];
			longjmp(vm->jump, 2);
        	break;

		case GST_OP_TRY:
    		vm->frame->errorSlot = vm->pc[1];
    		vm->frame->errorJump = vm->pc + *(uint32_t *)(vm->pc + 2);
    		vm->pc += 4;
    		break;

    	case GST_OP_UTY:
        	vm->frame->errorJump = NULL;
			vm->pc++;
        	break;

        default:
           	gst_error(vm, "unknown opcode");
            break;
        }

        /* Move collection only to places that allocate memory */
        /* This, however, is good for testing to ensure no memory leaks */
        gst_maybe_collect(vm);
    }
}

/* Get an argument from the stack */
GstValue gst_arg(Gst *vm, uint16_t index) {
    uint16_t frameSize = vm->frame->size;
    gst_assert(vm, frameSize > index, "cannot get arg out of stack bounds");
    return vm->base[index];
}

/* Put a value on the stack */
void gst_set_arg(Gst* vm, uint16_t index, GstValue x) {
    uint16_t frameSize = vm->frame->size;
    gst_assert(vm, frameSize > index, "cannot set arg out of stack bounds");
    vm->base[index] = x;
}

/* Get the size of the VMStack */
uint16_t gst_count_args(Gst *vm) {
    return vm->frame->size;
}

/* Initialize the VM */
void gst_init(Gst *vm) {
    vm->ret.type = GST_NIL;
    vm->error.type = GST_NIL;
    vm->base = NULL;
    vm->frame = NULL;
    vm->pc = NULL;
    vm->crash = NULL;
    /* Garbage collection */
    vm->blocks = NULL;
    vm->nextCollection = 0;
    /* Setting memoryInterval to zero currently forces
     * a collection pretty much every cycle, which is
     * obviously horrible for performance. It helps ensure
     * there are no memory bugs during dev */
    vm->memoryInterval = 2000;
    vm->black = 0;
    vm->lock = 0;
    /* Add thread */
    vm->thread = NULL;
}

/* Load a function into the VM. The function will be called with
 * no arguments when run */
void gst_load(Gst *vm, GstValue callee) {
    uint32_t startCapacity = 100;
    GstThread *thread = gst_alloc(vm, sizeof(GstThread));
    thread->data = gst_alloc(vm, sizeof(GstValue) * startCapacity);
    thread->capacity = startCapacity;
    thread->count = 0;
    vm->thread = thread;
    if (callee.type == GST_FUNCTION) {
        GstFunction *fn = callee.data.function;
        gst_thread_push(vm, thread, callee, fn->def->locals);
        vm->pc = fn->def->byteCode;
    } else if (callee.type == GST_CFUNCTION) {
        gst_thread_push(vm, thread, callee, 0);
        vm->pc = NULL;
    } else {
        return;
    }
}

/* Clear all memory associated with the VM */
void gst_deinit(Gst *vm) {
    gst_clear_memory(vm);
}

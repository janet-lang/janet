#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "value.h"
#include "ds.h"
#include "gc.h"

static const char GST_NO_UPVALUE[] = "no upvalue";
static const char GST_EXPECTED_FUNCTION[] = "expected function";
static const char GST_EXPECTED_NUMBER_ROP[] = "expected right operand to be number";
static const char GST_EXPECTED_NUMBER_LOP[] = "expected left operand to be number";

/* Get a literal */
static GstValue gst_vm_literal(Gst *vm, GstFunction *fn, uint16_t index) {
    if (index > fn->def->literalsLen) {
        gst_error(vm, GST_NO_UPVALUE);
    }
    return fn->def->literals[index];
}

/* Start running the VM */
int gst_start(Gst *vm) {
    /* VM state */
    GstThread thread = *vm->thread;
    GstValue *stack;
    GstStackFrame frame;
    GstValue temp, v1, v2;
    uint16_t *pc;

    /* Check for proper initialization */
    if (thread.count == 0) {
        gst_error(vm, "need thread in vm state");
    }
    stack = thread.data + thread.count;
    frame = *((GstStackFrame *)(stack - GST_FRAME_SIZE));
    pc = frame.pc;

    /* Set jmp_buf to jump back to for return. */
    {
        int n;
        if ((n = setjmp(vm->jump))) {
            /* Good return */
            if (n == 1) {
                return 0;
            } else if (n == 2) {
                /* Error. */
                while (!frame.errorJump) {
                    /* Check for closure */
                    if (frame.env) {
                        frame.env->thread = NULL;
                        frame.env->stackOffset = frame.size;
                        frame.env->values = gst_alloc(vm, sizeof(GstValue) * frame.size);
                        memcpy(frame.env->values, 
                            thread.data + thread.count, 
                            frame.size * sizeof(GstValue));
                    }
                    stack -= frame.prevSize + GST_FRAME_SIZE;
                    if (stack <= thread.data) {
                        thread.count = 0;
                        break;
                    }
                    frame = *((GstStackFrame *)(stack - GST_FRAME_SIZE));
                }
                if (thread.count < GST_FRAME_SIZE)
                    return n;
                /* Jump to the error location */
                pc = frame.errorJump;
                /* Set error */
                stack[frame.errorSlot] = vm->error;
            } else {
                /* Crash. just return */
                return n;
            }
        }
    }

    /* Main interpreter loop */
    for (;;) {
        
        switch (*pc) {

        #define DO_BINARY_MATH(op) \
            v1 = stack[pc[2]]; \
            v2 = stack[pc[3]]; \
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP); \
            gst_assert(vm, v2.type == GST_NUMBER, GST_EXPECTED_NUMBER_ROP); \
            temp.type = GST_NUMBER; \
            temp.data.number = v1.data.number op v2.data.number; \
            stack[pc[1]] = temp; \
            pc += 4; \
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
            temp.data.boolean = !gst_truthy(stack[pc[2]]);
            stack[pc[1]] = temp;
            pc += 3;
            break;

        case GST_OP_LD0: /* Load 0 */
            temp.type = GST_NUMBER;
            temp.data.number = 0;
            stack[pc[1]] = temp;
            pc += 2;
            break;

        case GST_OP_LD1: /* Load 1 */
            temp.type = GST_NUMBER;
            temp.data.number = 1;
            stack[pc[1]] = temp;
            pc += 2;
            break;

        case GST_OP_FLS: /* Load False */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 0;
            stack[pc[1]] = temp;
            pc += 2;
            break;

        case GST_OP_TRU: /* Load True */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 1;
            stack[pc[1]] = temp;
            pc += 2;
            break;

        case GST_OP_NIL: /* Load Nil */
            temp.type = GST_NIL;
            stack[pc[1]] = temp;
            pc += 2;
            break;

        case GST_OP_I16: /* Load Small Integer */
            temp.type = GST_NUMBER;
            temp.data.number = ((int16_t *)(pc))[2];
            stack[pc[1]] = temp;
            pc += 3;
            break;

        case GST_OP_UPV: /* Load Up Value */
        case GST_OP_SUV: /* Set Up Value */
            gst_assert(vm, frame.callee.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            {
                GstValue *upv;
                GstFunction *fn = frame.callee.data.function;
                GstFuncEnv *env;
                uint16_t level = pc[2];
                if (level == 0)
                    upv = stack + pc[3];
                else {
                    while (fn && --level)
                        fn = fn->parent;
                    gst_assert(vm, fn, GST_NO_UPVALUE);
                    env = fn->env;
                    if (env->thread)
                        upv = env->thread->data + env->stackOffset + pc[3];
                    else
                        upv = env->values + pc[3];
                }
                if (pc[0] == GST_OP_UPV) {
                    stack[pc[1]] = *upv;
                } else {
                    *upv = stack[pc[1]];
                }
                pc += 4;
            }
            break;

        case GST_OP_JIF: /* Jump If */
            if (gst_truthy(stack[pc[1]])) {
                pc += 4;
            } else {
                pc += *((int32_t *)(pc + 2));
            }
            break;

        case GST_OP_JMP: /* Jump */
            pc += *((int32_t *)(pc + 1));
            break;

        case GST_OP_CAL: /* Call */
            {
                temp = stack[pc[1]];
                uint32_t arity = pc[3];
                uint32_t oldCount = thread.count;
                uint32_t i, locals;
                GstValue *oldBase;
                frame.pc = pc + 4 + arity;
                frame.ret = pc[2];

                /* Save current stack frame */
                *((GstStackFrame *)(stack - GST_FRAME_SIZE)) = frame;

                /* Get the size of next stack frame */
                if (temp.type == GST_FUNCTION) {
                    GstFunction *fn = temp.data.function;
                    locals = fn->def->locals;
                } else if (temp.type == GST_CFUNCTION) {
                    locals = arity;
                } else {
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                }

                /* Push next stack frame */
                {
                    uint32_t nextCount = thread.count + frame.size + GST_FRAME_SIZE;
                    /* Ensure capacity */
                    if (nextCount + locals > thread.capacity) {
                        uint32_t newCap = (nextCount + locals) * 2;
                        GstValue *newData = gst_alloc(vm, sizeof(GstValue) * newCap);
                        memcpy(newData, thread.data, thread.capacity * sizeof(GstValue));
                        thread.data = newData;
                        thread.capacity = newCap;
                    }
                    thread.count = nextCount;

                    /* Ensure values start out as nil so as to not confuse
                     * the garabage collector */
                    for (i = nextCount; i < nextCount + locals; ++i)
                        thread.data[i].type = GST_NIL;

                    /* Set up the new stack frame */
                    stack = thread.data + thread.count;
                    frame.prevSize = frame.size;
                    frame.size = locals;
                    frame.env = NULL;
                    frame.callee = temp;
                    frame.errorJump = NULL;
                }

                /* Prepare to copy arguments to next stack frame */
                oldBase = thread.data + oldCount;
                
                /* Write arguments to new stack */
                for (i = 0; i < arity; ++i)
                    stack[i] = oldBase[pc[4 + i]];

                /* Call the function */
                if (temp.type == GST_CFUNCTION) {
                    /* Save current state to vm thread */
                    *((GstStackFrame *)(stack - GST_FRAME_SIZE)) = frame;
                    *vm->thread = thread;
                    v2 = temp.data.cfunction(vm);
                    goto ret;
                } else {
                    for (; i < locals; ++i)
                        stack[i].type = GST_NIL;
                    pc = temp.data.function->def->byteCode;
                }
            }
            break;

        case GST_OP_RET: /* Return */
            v2 = stack[pc[1]];
            goto ret;

        case GST_OP_CST: /* Load constant value */
            gst_assert(vm, frame.callee.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            stack[pc[1]] = gst_vm_literal(vm, frame.callee.data.function, pc[2]);
            pc += 3;
            break;

        case GST_OP_I32: /* Load 32 bit integer */
            temp.type = GST_NUMBER;
            temp.data.number = *((int32_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 4;
            break;

        case GST_OP_F64: /* Load 64 bit float */
            temp.type = GST_NUMBER;
            temp.data.number = (GstNumber) *((double *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            break;

        case GST_OP_MOV: /* Move Values */
            stack[pc[1]] = stack[pc[2]];
            pc += 3;
            break;

        case GST_OP_CLN: /* Create closure from constant FuncDef */
            {
                GstFunction *fn, *current;
                if (frame.callee.type != GST_FUNCTION)
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                if (!frame.env) {
                    frame.env = gst_alloc(vm, sizeof(GstFuncEnv));
                    *vm->thread = thread;
                    frame.env->thread = vm->thread;
                    frame.env->stackOffset = thread.count;
                    frame.env->values = NULL;
                }
                current = frame.callee.data.function;
                temp = gst_vm_literal(vm, current, pc[2]);
                if (temp.type != GST_NIL)
                    gst_error(vm, "cannot create closure");
                fn = gst_alloc(vm, sizeof(GstFunction));
                fn->def = (GstFuncDef *) temp.data.pointer;
                fn->parent = current;
                fn->env = frame.env;
                temp.type = GST_FUNCTION;
                temp.data.function = fn;
                stack[pc[1]] = temp;
                pc += 3;
            }
            break;

        case GST_OP_EQL: /* Equality */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = gst_equals(stack[pc[2]], stack[pc[3]]);
            stack[pc[1]] = temp;
            pc += 4;
            break;

        case GST_OP_LTN: /* Less Than */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(stack[pc[2]], stack[pc[3]]) == -1);
            stack[pc[1]] = temp;
            pc += 4;
            break;

        case GST_OP_LTE: /* Less Than or Equal to */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(stack[pc[2]], stack[pc[3]]) != 1);
            stack[pc[1]] = temp;
            pc += 4;
            break;

        case GST_OP_ARR: /* Array literal */
            {
                uint32_t i;
                uint32_t arrayLen = pc[2];
                GstArray *array = gst_array(vm, arrayLen);
                array->count = arrayLen;
                for (i = 0; i < arrayLen; ++i)
                    array->data[i] = stack[pc[3 + i]];
                temp.type = GST_ARRAY;
                temp.data.array = array;
                stack[pc[1]] = temp;
                pc += 3 + arrayLen;
            }
            break;

        case GST_OP_DIC: /* Object literal */
            {
                uint32_t i = 3;
                uint32_t kvs = pc[2];
                GstObject *o = gst_object(vm, kvs + 2);
                kvs = kvs + 3;
                while (i < kvs) {
                    v1 = stack[pc[i++]];
                    v2 = stack[pc[i++]];
                    gst_object_put(vm, o, v1, v2);
                }
                temp.type = GST_OBJECT;
                temp.data.object = o;
                stack[pc[1]] = temp;
                pc += kvs;
            }
            break;

        case GST_OP_TCL: /* Tail call */
            {
                temp = stack[pc[1]];
                uint32_t arity = pc[2];
                uint16_t locals;
                uint32_t i, workspace, totalCapacity;

                /* Check for closures */
                if (frame.env) {
                    frame.env->thread = NULL;
                    frame.env->stackOffset = frame.size;
                    frame.env->values = gst_alloc(vm, sizeof(GstValue) * frame.size);
                    memcpy(frame.env->values, 
                        thread.data + thread.count, 
                        frame.size * sizeof(GstValue));
                    frame.env = NULL;
                }

                /* Get size of new stack frame */
                if (temp.type == GST_CFUNCTION) {
                    locals = arity;
                } else if (temp.type == GST_FUNCTION) {
                    locals = temp.data.function->def->locals;
                } else {
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                }

                /* Get enough space for manipulating args */
                if (arity > frame.size) {
                    workspace = arity;
                } else {
                    workspace = frame.size;
                }

                /* Ensure stack has enough space for copies of arguments */
                totalCapacity = thread.count + arity + workspace + locals;
                if (totalCapacity > thread.capacity) {
                    uint32_t newCap = totalCapacity * 2;
                    GstValue *newData = gst_alloc(vm, sizeof(GstValue) * newCap);
                    memcpy(newData, thread.data, thread.capacity * sizeof(GstValue));
                    thread.data = newData;
                    thread.capacity = newCap;
                    stack = thread.data + thread.count;
                }
                
                /* Copy the arguments into the extra space */
                for (i = 0; i < arity; ++i)
                    stack[workspace + i] = stack[pc[3 + i]];

                /* Copy the end of the stack to the parameter position */
                memcpy(stack, stack + workspace, arity * sizeof(GstValue));

                /* Update the stack frame */
                frame.callee = temp;
                frame.size = locals;

                /* Nil the non argument part of the stack for gc */
                for (i = arity; i < frame.size; ++i)
                    stack[i].type = GST_NIL;

                /* Call the function */
                if (temp.type == GST_CFUNCTION) {
                    /* Save current state to vm thread */
                    *((GstStackFrame *)(stack - GST_FRAME_SIZE)) = frame;
                    *vm->thread = thread;
                    v2 = temp.data.cfunction(vm);
                    goto ret;
                } else {
                    pc = temp.data.function->def->byteCode;
                }
            }
            break;

        case GST_OP_RTN: /* Return nil */
            v2.type = GST_NIL;
            goto ret;

        case GST_OP_GET:
			temp = gst_get(vm, stack[pc[2]], stack[pc[3]]);
            stack[pc[1]] = temp;
			pc += 4;
            break;

        case GST_OP_SET:
			gst_set(vm, stack[pc[1]], stack[pc[2]], stack[pc[3]]);
			pc += 4;
            break;

    	case GST_OP_ERR:
			vm->error = stack[pc[1]];
			longjmp(vm->jump, 2);
        	break;

		case GST_OP_TRY:
    		frame.errorSlot = pc[1];
    		frame.errorJump = pc + *(uint32_t *)(pc + 2);
    		pc += 4;
    		break;

    	case GST_OP_UTY:
        	frame.errorJump = NULL;
			pc++;
        	break;

        default:
           	gst_error(vm, "unknown opcode");
            break;

        /* Label for return */
        ret:
            {
                /* Check for closure */
                if (frame.env) {
                    frame.env->thread = NULL;
                    frame.env->stackOffset = frame.size;
                    frame.env->values = gst_alloc(vm, sizeof(GstValue) * frame.size);
                    memcpy(frame.env->values, 
                        thread.data + thread.count, 
                        frame.size * sizeof(GstValue));
                }
                if (thread.count <= GST_FRAME_SIZE)
                    gst_exit(vm, v2);
                stack -= frame.prevSize + GST_FRAME_SIZE;
                thread.count -= frame.prevSize + GST_FRAME_SIZE;
                frame = *((GstStackFrame *)(stack - GST_FRAME_SIZE));
                pc = frame.pc;
                stack[frame.ret] = v2;
                break;
            }
        }
        /* TODO: Move collection only to places that allocate memory */
        /* This, however, is good for testing to ensure no memory leaks */
        *vm->thread = thread;
        gst_maybe_collect(vm);
    }
}

/* Get an argument from the stack */
GstValue gst_arg(Gst *vm, uint16_t index) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    GstStackFrame *frame = (GstStackFrame *)(stack - GST_FRAME_SIZE);
    uint16_t frameSize = frame->size;
    gst_assert(vm, frameSize > index, "cannot get arg out of stack bounds");
    return stack[index];
}

/* Put a value on the stack */
void gst_set_arg(Gst* vm, uint16_t index, GstValue x) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    GstStackFrame *frame = (GstStackFrame *)(stack - GST_FRAME_SIZE);
    uint16_t frameSize = frame->size;
    gst_assert(vm, frameSize > index, "cannot set arg out of stack bounds");
    stack[index] = x;
}

/* Get the size of the VMStack */
uint16_t gst_count_args(Gst *vm) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    GstStackFrame *frame = (GstStackFrame *)(stack - GST_FRAME_SIZE);
    return frame->size;
}

/* Initialize the VM */
void gst_init(Gst *vm) {
    vm->ret.type = GST_NIL;
    vm->error.type = GST_NIL;
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
    /* Add thread */
    vm->thread = NULL;
}

/* Load a function into the VM. The function will be called with
 * no arguments when run */
void gst_load(Gst *vm, GstValue callee) {
    uint32_t startCapacity;
    uint32_t locals, i;
    uint16_t *pc;
    GstStackFrame *frame;
    GstThread *thread = gst_alloc(vm, sizeof(GstThread));
    if (callee.type == GST_FUNCTION) {
        locals = callee.data.function->def->locals;
        pc = callee.data.function->def->byteCode;
    } else if (callee.type == GST_CFUNCTION) {
        locals = 0;
        pc = NULL;
    } else {
        return;
    }
    startCapacity = locals + GST_FRAME_SIZE + 10;
    thread->data = gst_alloc(vm, sizeof(GstValue) * startCapacity);
    thread->capacity = startCapacity;
    thread->count = GST_FRAME_SIZE;
    vm->thread = thread;
    frame = (GstStackFrame *)thread->data;
    frame->prevSize = 0;
    frame->size = locals;
    frame->callee = callee;
    frame->errorJump = NULL;
    frame->env = NULL;
    frame->pc = pc;
    /* Nil arguments */
    for (i = 0; i < locals; ++i)
        thread->data[GST_FRAME_SIZE + i].type = GST_NIL;
}

/* Clear all memory associated with the VM */
void gst_deinit(Gst *vm) {
    gst_clear_memory(vm);
}

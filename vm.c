#include "vm.h"
#include "util.h"
#include "value.h"
#include "ds.h"
#include "gc.h"

/* Macros for errors in the vm */

/* Exit from the VM normally */
#define gst_exit(vm, r) return ((vm)->ret = (r), GST_RETURN_OK)

/* Bail from the VM with an error string. */
#define gst_error(vm, e) do { (vm)->ret = gst_load_cstring((vm), (e)); goto vm_error; } while (0)

/* Crash. Not catchable, unlike error. */
#define gst_crash(vm, e) return ((vm)->crash = (e), GST_RETURN_CRASH)

/* Error if the condition is false */
#define gst_assert(vm, cond, e) do {if (!(cond)){gst_error((vm), (e));}} while (0)

static const char GST_NO_UPVALUE[] = "no upvalue";
static const char GST_EXPECTED_FUNCTION[] = "expected function";
static const char GST_EXPECTED_NUMBER_ROP[] = "expected right operand to be number";
static const char GST_EXPECTED_NUMBER_LOP[] = "expected left operand to be number";

/* Load a function into the VM. The function will be called with
 * no arguments when run */
static void gst_load(Gst *vm, GstValue callee) {
    uint32_t startCapacity;
    uint32_t locals, i;
    uint16_t *pc;
    GstValue *stack;
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
    stack = thread->data + GST_FRAME_SIZE;
    gst_frame_prevsize(stack) = 0;
    gst_frame_size(stack) = locals;
    gst_frame_callee(stack) = callee;
    gst_frame_env(stack) = NULL;
    gst_frame_errjmp(stack) = NULL;
    gst_frame_pc(stack) = pc;
    /* Nil arguments */
    for (i = 0; i < locals; ++i)
        stack[i].type = GST_NIL;
}

/* Contextual macro to state in function with VM */
#define GST_STATE_SYNC() do { \
    thread = *vm->thread; \
    stack = thread.data + thread.count; \
} while (0)

/* Write local state back to VM */
#define GST_STATE_WRITE() do { \
    *vm->thread = thread; \
} while (0)
       
/* Start running the VM from where it left off. Continue running
 * until the stack size is smaller than minStackSize. */
static int gst_continue_size(Gst *vm, uint32_t stackBase) {
    /* VM state */
    GstThread thread;
    GstValue *stack;
    GstValue temp, v1, v2;
    uint16_t *pc;

    /* Intialize local state */
    GST_STATE_SYNC();
    pc = gst_frame_pc(stack);

    /* Main interpreter loop */
    for (;;) {
        
        switch (*pc) {

        default:
            gst_error(vm, "unknown opcode");
            break;

        #define OP_BINARY_MATH(op) \
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
            OP_BINARY_MATH(+)

        case GST_OP_SUB: /* Subtraction */
            OP_BINARY_MATH(-)

        case GST_OP_MUL: /* Multiplication */
            OP_BINARY_MATH(*)

        case GST_OP_DIV: /* Division */
            OP_BINARY_MATH(/)

        #undef OP_BINARY_MATH

        case GST_OP_NOT: /* Boolean unary (Boolean not) */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = !gst_truthy(stack[pc[2]]);
            stack[pc[1]] = temp;
            pc += 3;
            break;

        case GST_OP_NEG: /* Unary negation */
            v1 = stack[pc[2]];
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP);
            temp.type = GST_NUMBER;
            temp.data.number = -v1.data.number;
            stack[pc[1]] = temp;
            pc += 3;
            break;

        case GST_OP_INV: /* Unary multiplicative inverse */
            v1 = stack[pc[2]];
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP);
            temp.type = GST_NUMBER;
            temp.data.number = 1 / v1.data.number;
            stack[pc[1]] = temp;
            pc += 3;
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
            {
                GstValue *upv;
                GstFunction *fn;
                GstFuncEnv *env;
                uint16_t level = pc[2];
                temp = gst_frame_callee(stack);
                gst_assert(vm, temp.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
                fn = temp.data.function;
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

        case GST_OP_CST: /* Load constant value */
            v1 = gst_frame_callee(stack);
            gst_assert(vm, v1.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            if (pc[2] > v1.data.function->def->literalsLen)
                gst_error(vm, GST_NO_UPVALUE);
            stack[pc[1]] = v1.data.function->def->literals[pc[2]];
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
                GstFunction *fn;
                v1 = gst_frame_callee(stack);
                if (v1.type != GST_FUNCTION)
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                if (gst_frame_env(stack) == NULL) {
                    gst_frame_env(stack) = gst_alloc(vm, sizeof(GstFuncEnv));
                    *vm->thread = thread;
                    gst_frame_env(stack)->thread = vm->thread;
                    gst_frame_env(stack)->stackOffset = thread.count;
                    gst_frame_env(stack)->values = NULL;
                }
                if (pc[2] > v1.data.function->def->literalsLen)
                    gst_error(vm, GST_NO_UPVALUE);
                temp = v1.data.function->def->literals[pc[2]];
                if (temp.type != GST_NIL)
                    gst_error(vm, "cannot create closure");
                fn = gst_alloc(vm, sizeof(GstFunction));
                fn->def = (GstFuncDef *) temp.data.pointer;
                fn->parent = v1.data.function;
                fn->env = gst_frame_env(stack);
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
            
        case GST_OP_TUP: /* Tuple literal */
            {
                uint32_t i;
                uint32_t len = pc[2];
                GstValue *tuple = gst_tuple(vm, len);
                for (i = 0; i < len; ++i)
                    tuple[i] = stack[pc[3 + i]];
                temp.type = GST_TUPLE;
                temp.data.tuple = tuple;
                stack[pc[1]] = temp;
                pc += 3 + len;
            }
            break;

        case GST_OP_GET: /* Associative get */
            {
                const char *err;
                err = gst_get(stack[pc[2]], stack[pc[3]], stack + pc[1]);
                if (err != NULL)
                    gst_error(vm, err);
                pc += 4;
                break;
            }

        case GST_OP_SET: /* Associative set */
            {
                const char *err;
                err = gst_set(vm, stack[pc[1]], stack[pc[2]], stack[pc[3]]);
                if (err != NULL)
                    gst_error(vm, err);
                pc += 4;
                break;
            }

        case GST_OP_ERR: /* Throw error */
            vm->ret = stack[pc[1]];
            goto vm_error;
            break;

        case GST_OP_TRY: /* Begin try block */
            gst_frame_errloc(stack) = pc[1];
            gst_frame_errjmp(stack) = pc + *(uint32_t *)(pc + 2);
            pc += 4;
            break;

        case GST_OP_UTY: /* End try block */
            gst_frame_errjmp(stack) = NULL;
            pc++;
            break;

        case GST_OP_RTN: /* Return nil */
            vm->ret.type = GST_NIL;
            goto ret;

        case GST_OP_RET: /* Return */
            vm->ret = stack[pc[1]];
            goto ret;

        case GST_OP_PSH: /* Push stack frame */
            {
                GstValue *nextStack;
                uint32_t expectedArity, normalArity, arity, varArgs, i, locals, nextCount;

                /* Get arguments to op */
                temp = stack[pc[1]];
                arity = pc[2];

                /* Get the size of next stack frame */
                if (temp.type == GST_FUNCTION) {
                    GstFunction *fn = temp.data.function;
                    locals = fn->def->locals;
                    varArgs = fn->def->flags & GST_FUNCDEF_FLAG_VARARG;
                    expectedArity = fn->def->arity;
                    if (arity > expectedArity)
                        normalArity = expectedArity;
                    else
                        normalArity = arity;
                } else if (temp.type == GST_CFUNCTION) {
                    locals = normalArity = expectedArity = arity;
                    varArgs = 0;
                } else {
                    gst_error(vm, GST_EXPECTED_FUNCTION);
                }

                /* Get next frame size */
                nextCount = thread.count + gst_frame_size(stack) + GST_FRAME_SIZE;
                
                /* Ensure capacity */
                if (nextCount + locals > thread.capacity) {
                    uint32_t newCap = (nextCount + locals) * 2;
                    GstValue *newData = gst_alloc(vm, sizeof(GstValue) * newCap);
                    gst_memcpy(newData, thread.data, thread.capacity * sizeof(GstValue));
                    thread.data = newData;
                    thread.capacity = newCap;
                    stack = thread.data + thread.count;
                }

                /* Set up the new stack frame */
                nextStack = thread.data + nextCount;
                gst_frame_prevsize(nextStack) = gst_frame_size(stack);
                gst_frame_size(nextStack) = locals;
                gst_frame_ret(nextStack) = 0;
                gst_frame_env(nextStack) = NULL;
                gst_frame_callee(nextStack) = temp;
                gst_frame_errjmp(nextStack) = NULL;
                
                /* Write arguments to new stack */
                for (i = 0; i < normalArity; ++i)
                    nextStack[i] = stack[pc[3 + i]];

                /* Clear stack */
                for (; i < locals; ++i)
                    nextStack[i].type = GST_NIL;

                /* Check for varargs and put them in a tuple */
                if (varArgs) {
                    GstValue *tuple;
                    uint32_t j;
                    tuple = gst_tuple(vm, arity - expectedArity);
                    for (j = expectedArity; j < arity; ++j)
                        tuple[j - expectedArity] = stack[pc[3 + j]];
                    nextStack[expectedArity].type = GST_TUPLE;
                    nextStack[expectedArity].data.tuple = tuple;
                }

                /* Increment pc */
                pc += 3 + arity;
            }
            break;

        case GST_OP_CAL: /* Call */
        case GST_OP_TCL: /* Tail call */
            if (pc[0] == GST_OP_CAL) {
                gst_frame_ret(stack) = pc[1];
                gst_frame_pc(stack) = pc + 2;
                thread.count += gst_frame_size(stack) + GST_FRAME_SIZE;
                stack = thread.data + thread.count;
            } else {
                uint32_t i;
                GstValue *nextStack = stack + gst_frame_size(stack) + GST_FRAME_SIZE;
                uint32_t nextSize = gst_frame_size(nextStack);
                /* Check for closures */
                if (gst_frame_env(stack) != NULL) {
                    gst_frame_env(stack)->thread = NULL;
                    gst_frame_env(stack)->stackOffset = gst_frame_size(stack);
                    gst_frame_env(stack)->values = gst_alloc(vm, sizeof(GstValue) * gst_frame_size(stack));
                    gst_memcpy(gst_frame_env(stack)->values,
                        thread.data + thread.count,
                        gst_frame_size(stack) * sizeof(GstValue));
                }
                /* Copy over most of stack frame */
                gst_frame_callee(stack) = gst_frame_callee(nextStack);
                gst_frame_size(stack) = gst_frame_size(nextStack);
                gst_frame_env(stack) = NULL;
                gst_frame_errjmp(stack) = NULL;
                /* Replace current stack frame with next */
                for (i = 0; i < nextSize; ++i)
                    stack[i] = nextStack[i];
            }
            v1 = gst_frame_callee(stack);
            if (v1.type == GST_FUNCTION) {
                pc = v1.data.function->def->byteCode;
            } else if (v1.type == GST_CFUNCTION) {
                int status;
                GST_STATE_WRITE();
                status = v1.data.cfunction(vm);
                GST_STATE_SYNC();
                if (status == GST_RETURN_OK)
                    goto ret;
                else
                    goto vm_error;
            } else {
                gst_error(vm, GST_EXPECTED_FUNCTION);
            }
            break;

        /* Macro for popping stack frame */
        #define pop_frame(onUnderflow) do { \
            if (gst_frame_env(stack) != NULL) { \
                gst_frame_env(stack)->thread = NULL; \
                gst_frame_env(stack)->stackOffset = gst_frame_size(stack); \
                gst_frame_env(stack)->values = gst_alloc(vm, sizeof(GstValue) * gst_frame_size(stack)); \
                gst_memcpy(gst_frame_env(stack)->values, \
                    thread.data + thread.count, \
                    gst_frame_size(stack) * sizeof(GstValue)); \
            } \
            if (thread.count <= stackBase) { \
                thread.count -= gst_frame_prevsize(stack) + GST_FRAME_SIZE; \
                return (onUnderflow); \
            } \
            thread.count -= gst_frame_prevsize(stack) + GST_FRAME_SIZE; \
            stack = thread.data + thread.count; \
        } while (0)

        /* Label for return */
        ret:
            /* Check for closure */
            pop_frame(GST_RETURN_OK);
            pc = gst_frame_pc(stack);
            stack[gst_frame_ret(stack)] = vm->ret;
            break;

        /* Handle errors from c functions and vm opcodes */
        vm_error:
            while (gst_frame_errjmp(stack) == NULL)
                pop_frame(GST_RETURN_ERROR);
            pc = gst_frame_errjmp(stack);
            stack[gst_frame_errloc(stack)] = vm->ret;
            break;

        #undef pop_frame

      } /* end switch */

        /* TODO: Move collection only to places that allocate memory */
        /* This, however, is good for testing to ensure no memory leaks */
        *vm->thread = thread;
        gst_maybe_collect(vm);

    } /* end for */

}

/* Continue running the VM after it has stopped */
int gst_continue(Gst *vm) {
    return gst_continue_size(vm, vm->thread->count);
}

/* Run the vm with a given function */
int gst_run(Gst *vm, GstValue func) {
    gst_load(vm, func);
    return gst_continue(vm);
}

/* Raw function call implementation for use from c code. Beware code
 * duplication between this function and GST_OP_PSH and GST_OP_CAL/GST_OP_TCL */
int gst_call(Gst *vm, GstValue callee, uint32_t arity, GstValue *args) {
    GstThread thread;
    GstValue *stack;
    uint32_t expectedArity, normalArity, varArgs, i, locals, nextCount, size;

    /* Initialize some state */
    GST_STATE_SYNC();

    /* Get the size of next stack frame */
    if (callee.type == GST_FUNCTION) {
        GstFunction *fn = callee.data.function;
        locals = fn->def->locals;
        varArgs = fn->def->flags & GST_FUNCDEF_FLAG_VARARG;
        expectedArity = fn->def->arity;
        gst_frame_pc(stack) = fn->def->byteCode;
        if (arity > expectedArity)
            normalArity = expectedArity;
        else
            normalArity = arity;
    } else if (callee.type == GST_CFUNCTION) {
        locals = normalArity = expectedArity = arity;
        varArgs = 0;
    } else {
        gst_c_throwc(vm, GST_EXPECTED_FUNCTION);
    }

    /* Get next frame size */
    nextCount = thread.count + gst_frame_size(stack) + GST_FRAME_SIZE;
    
    /* Ensure capacity */
    if (nextCount + locals > thread.capacity) {
        uint32_t newCap = (nextCount + locals) * 2;
        GstValue *newData = gst_alloc(vm, sizeof(GstValue) * newCap);
        gst_memcpy(newData, thread.data, thread.capacity * sizeof(GstValue));
        thread.data = newData;
        thread.capacity = newCap;
    }

    /* Save modified thread object */
    thread.count = nextCount;
    *vm->thread = thread;

    /* Set up the new stack frame */
    size = gst_frame_size(stack);
    stack = thread.data + nextCount;
    gst_frame_prevsize(stack) = size;
    gst_frame_size(stack) = locals;
    gst_frame_env(stack) = NULL;
    gst_frame_callee(stack) = callee;
    gst_frame_errjmp(stack) = NULL;
    
    /* Write arguments to new stack */
    for (i = 0; i < normalArity; ++i)
        stack[i] = args[i];

    /* Clear stack */
    for (; i < locals; ++i)
        stack[i].type = GST_NIL;

    /* Check for varargs and put them in a tuple */
    if (varArgs) {
        GstValue *tuple;
        uint32_t j;
        tuple = gst_tuple(vm, arity - expectedArity);
        for (j = expectedArity; j < arity; ++j)
            tuple[j - expectedArity] = args[j];
        stack[expectedArity].type = GST_TUPLE;
        stack[expectedArity].data.tuple = tuple;
    }

    /* Call the function */
    if (callee.type == GST_FUNCTION) {
        return gst_continue_size(vm, thread.count);
    } else {
        int status = callee.data.cfunction(vm);
        GST_STATE_SYNC();
        /* Check for closures */
        if (gst_frame_env(stack) != NULL) {
            gst_frame_env(stack)->thread = NULL;
            gst_frame_env(stack)->stackOffset = gst_frame_size(stack);
            gst_frame_env(stack)->values = gst_alloc(vm, sizeof(GstValue) * gst_frame_size(stack));
            gst_memcpy(gst_frame_env(stack)->values,
                thread.data + thread.count,
                gst_frame_size(stack) * sizeof(GstValue));
        }
        vm->thread->count -= gst_frame_prevsize(stack) + GST_FRAME_SIZE;
        return status;
    }
}

/* Get an argument from the stack */
GstValue gst_arg(Gst *vm, uint16_t index) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    uint16_t frameSize = gst_frame_size(stack);
    if (frameSize <= index) {
        GstValue ret;
        ret.type = GST_NIL;
        return ret;
    }
    return stack[index];
}

/* Put a value on the stack */
void gst_set_arg(Gst* vm, uint16_t index, GstValue x) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    uint16_t frameSize = gst_frame_size(stack);
    if (frameSize <= index) return;
    stack[index] = x;
}

/* Get the size of the VMStack */
uint16_t gst_count_args(Gst *vm) {
    GstValue *stack = vm->thread->data + vm->thread->count;
    return gst_frame_size(stack);
}

/* Initialize the VM */
void gst_init(Gst *vm) {
    vm->ret.type = GST_NIL;
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

/* Clear all memory associated with the VM */
void gst_deinit(Gst *vm) {
    gst_clear_memory(vm);
}

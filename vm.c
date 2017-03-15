#include "vm.h"
#include "util.h"
#include "value.h"
#include "ds.h"
#include "gc.h"
#include "thread.h"

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
            continue;

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
            continue;

        case GST_OP_NEG: /* Unary negation */
            v1 = stack[pc[2]];
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP);
            temp.type = GST_NUMBER;
            temp.data.number = -v1.data.number;
            stack[pc[1]] = temp;
            pc += 3;
            continue;

        case GST_OP_INV: /* Unary multiplicative inverse */
            v1 = stack[pc[2]];
            gst_assert(vm, v1.type == GST_NUMBER, GST_EXPECTED_NUMBER_LOP);
            temp.type = GST_NUMBER;
            temp.data.number = 1 / v1.data.number;
            stack[pc[1]] = temp;
            pc += 3;
            continue;

        case GST_OP_FLS: /* Load False */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 0;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_TRU: /* Load True */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = 1;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_NIL: /* Load Nil */
            temp.type = GST_NIL;
            stack[pc[1]] = temp;
            pc += 2;
            continue;

        case GST_OP_I16: /* Load Small Integer */
            temp.type = GST_NUMBER;
            temp.data.number = ((int16_t *)(pc))[2];
            stack[pc[1]] = temp;
            pc += 3;
            continue;

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
            continue;

        case GST_OP_JIF: /* Jump If */
            if (gst_truthy(stack[pc[1]])) {
                pc += 4;
            } else {
                pc += *((int32_t *)(pc + 2));
            }
            continue;

        case GST_OP_JMP: /* Jump */
            pc += *((int32_t *)(pc + 1));
            continue;

        case GST_OP_CST: /* Load constant value */
            v1 = gst_frame_callee(stack);
            gst_assert(vm, v1.type == GST_FUNCTION, GST_EXPECTED_FUNCTION);
            if (pc[2] > v1.data.function->def->literalsLen)
                gst_error(vm, GST_NO_UPVALUE);
            stack[pc[1]] = v1.data.function->def->literals[pc[2]];
            pc += 3;
            continue;

        case GST_OP_I32: /* Load 32 bit integer */
            temp.type = GST_NUMBER;
            temp.data.number = *((int32_t *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 4;
            continue;

        case GST_OP_F64: /* Load 64 bit float */
            temp.type = GST_NUMBER;
            temp.data.number = (GstNumber) *((double *)(pc + 2));
            stack[pc[1]] = temp;
            pc += 6;
            continue;

        case GST_OP_MOV: /* Move Values */
            stack[pc[1]] = stack[pc[2]];
            pc += 3;
            continue;

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
            continue;

        case GST_OP_LTN: /* Less Than */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(stack[pc[2]], stack[pc[3]]) == -1);
            stack[pc[1]] = temp;
            pc += 4;
            continue;

        case GST_OP_LTE: /* Less Than or Equal to */
            temp.type = GST_BOOLEAN;
            temp.data.boolean = (gst_compare(stack[pc[2]], stack[pc[3]]) != 1);
            stack[pc[1]] = temp;
            pc += 4;
            continue;

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
            }
            continue;

        case GST_OP_SET: /* Associative set */
            {
                const char *err;
                err = gst_set(vm, stack[pc[1]], stack[pc[2]], stack[pc[3]]);
                if (err != NULL)
                    gst_error(vm, err);
                pc += 4;
            }
            break;

        case GST_OP_ERR: /* Throw error */
            vm->ret = stack[pc[1]];
            goto vm_error;

        case GST_OP_TRY: /* Begin try block */
            gst_frame_errloc(stack) = pc[1];
            gst_frame_errjmp(stack) = pc + *(uint32_t *)(pc + 2);
            pc += 4;
            continue;

        case GST_OP_UTY: /* End try block */
            gst_frame_errjmp(stack) = NULL;
            pc++;
            continue;

        case GST_OP_RTN: /* Return nil */
            stack = gst_thread_popframe(vm, &thread);
            if (thread.count < stackBase) {
                vm->ret.type = GST_NIL;
                GST_STATE_WRITE();
                return GST_RETURN_OK;
            }
            pc = gst_frame_pc(stack);
            stack[gst_frame_ret(stack)].type = GST_NIL;
            continue;

        case GST_OP_RET: /* Return */
            temp = stack[pc[1]];
            stack = gst_thread_popframe(vm, &thread);
            if (thread.count < stackBase) {
                vm->ret = temp;
                GST_STATE_WRITE();
                return GST_RETURN_OK;
            }
            pc = gst_frame_pc(stack);
            stack[gst_frame_ret(stack)] = temp;
            continue;

        case GST_OP_CAL: /* Call */
        case GST_OP_TCL: /* Tail call */
            {
                GstValue *oldStack;
                temp = stack[pc[1]];
                int isTCall = *pc == GST_OP_TCL;
                uint32_t i, arity, offset, size;
                uint16_t ret = pc[2];
                offset = isTCall ? 3 : 4;
                arity = pc[offset - 1];
                /* Push new frame */
                stack = gst_thread_beginframe(vm, &thread, temp, arity);
                oldStack = stack - GST_FRAME_SIZE - gst_frame_prevsize(stack);
                /* Write arguments */
                size = gst_frame_size(stack);
                for (i = 0; i < arity; ++i)
                    stack[i + size - arity] = oldStack[pc[offset + i]];
                /* Finish new frame */
                gst_thread_endframe(vm, &thread);
                /* Check tail call - if so, replace frame. */
                if (isTCall) {
                    stack = gst_thread_tail(vm, &thread);
                } else {
                    gst_frame_ret(oldStack) = ret;
                }
                /* Call function */
                temp = gst_frame_callee(stack);
                if (temp.type == GST_FUNCTION) {
                    /* Save pc and set new pc */
                    if (!isTCall)
                        gst_frame_pc(oldStack) = pc + offset + arity;
                    pc = temp.data.function->def->byteCode;
                } else {
                    int status;
                    GST_STATE_WRITE();
                    vm->ret.type = GST_NIL;
                    status = temp.data.cfunction(vm);
                    GST_STATE_SYNC();
                    stack = gst_thread_popframe(vm, &thread);
                    if (status == GST_RETURN_OK)
                        if (thread.count < stackBase) {
                            GST_STATE_WRITE();
                            return status;
                        } else { 
                            stack[gst_frame_ret(stack)] = vm->ret;
                            if (isTCall)
                                pc = gst_frame_pc(stack);
                            else
                                pc += offset + arity;
                        }
                    else
                        goto vm_error;
                }
            }
            break;

        /* Handle errors from c functions and vm opcodes */
        vm_error:
            while (gst_frame_errjmp(stack) == NULL) {
                stack = gst_thread_popframe(vm, &thread);
                if (thread.count < stackBase)
                    return GST_RETURN_ERROR;
            }
            pc = gst_frame_errjmp(stack);
            stack[gst_frame_errloc(stack)] = vm->ret;
            break;

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
int gst_run(Gst *vm, GstValue callee) {
    GstValue *stack;
    vm->thread = gst_thread(vm, callee, 64);
    if (vm->thread == NULL)
        return GST_RETURN_CRASH;
    stack = gst_thread_stack(vm->thread);
    /* If callee was not actually a function, get the delegate function */
    callee = gst_frame_callee(stack);
    if (callee.type == GST_CFUNCTION) {
        int status;
        vm->ret.type = GST_NIL;
        status = callee.data.cfunction(vm);
        gst_thread_popframe(vm, vm->thread);
        return status;
    } else {
        return gst_continue(vm);
    }
}

/* Call a gst function */
int gst_call(Gst *vm, GstValue callee, uint32_t arity, GstValue *args) {
    GstValue *stack;
    uint32_t i, size;
    int status;

    /* Set the return position */
    stack = gst_thread_stack(vm->thread);
    gst_frame_ret(stack) = gst_frame_size(stack);

    /* Add extra space for returning value */
    gst_thread_pushnil(vm, vm->thread, 1);
    stack = gst_thread_beginframe(vm, vm->thread, callee, arity);

    /* Write args to stack */
    size = gst_frame_size(stack) - arity; 
    for (i = 0; i < arity; ++i) 
        stack[i + size] = args[i];
    gst_thread_endframe(vm, vm->thread);

    /* Call function */
    callee = gst_frame_callee(stack);
    if (callee.type == GST_FUNCTION) {
        gst_frame_pc(stack) = callee.data.function->def->byteCode;
        status = gst_continue(vm);
    } else {
        vm->ret.type = GST_NIL;
        status = callee.data.cfunction(vm);
        gst_thread_popframe(vm, vm->thread);
    }

    /* Pop the extra nil */
    --gst_frame_size(gst_thread_stack(vm->thread));

    return status;
}

/* Get an argument from the stack */
GstValue gst_arg(Gst *vm, uint16_t index) {
    GstValue *stack = gst_thread_stack(vm->thread);
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
    GstValue *stack = gst_thread_stack(vm->thread);
    uint16_t frameSize = gst_frame_size(stack);
    if (frameSize <= index) return;
    stack[index] = x;
}

/* Get the size of the VMStack */
uint16_t gst_count_args(Gst *vm) {
    GstValue *stack = gst_thread_stack(vm->thread);
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
    vm->rootenv.type = GST_NIL;
}

/* Clear all memory associated with the VM */
void gst_deinit(Gst *vm) {
    gst_clear_memory(vm);
    vm->thread = NULL;
    vm->rootenv.type = GST_NIL;
    vm->ret.type = GST_NIL;
}

#include <gst/compile.h>
#include <gst/ds.h>
#include <gst/value.h>
#include <gst/vm.h>
#include <gst/util.h>

/* During compilation, FormOptions are passed to ASTs
 * as configuration options to allow for some optimizations. */
typedef struct FormOptions FormOptions;
struct FormOptions {
    /* The location the returned Slot must be in. Can be ignored
     * if either canDrop or canChoose is true */
    uint16_t target;
    /* If the result of the value being compiled is not going to
     * be used, some forms can simply return a nil slot and save
     * copmutation */
    uint16_t resultUnused : 1;
    /* Allows the sub expression to evaluate into a
     * temporary slot of it's choice. A temporary Slot
     * can be allocated with GstCompilerGetLocal. */
    uint16_t canChoose : 1;
    /* True if the form is in the tail position. This allows
     * for tail call optimization. If a helper receives this
     * flag, it is free to return a returned slot and generate bytecode
     * for a return, including tail calls. */
    uint16_t isTail : 1;
};

/* A Slot represent a location of a local variable
 * on the stack. Also contains some meta information. */
typedef struct Slot Slot;
struct Slot {
    /* The index of the Slot on the stack. */
    uint16_t index;
    /* A nil Slot should not be expected to contain real data. (ignore index).
     * Forms that have side effects but don't evaulate to
     * anything will try to return nil slots. */
    uint16_t isNil : 1;
    /* A temp Slot is a Slot on the stack that does not
     * belong to a named local. They can be freed whenever,
     * and so are used in intermediate calculations. */
    uint16_t isTemp : 1;
    /* Flag indicating if byteCode for returning this slot
     * has been written to the buffer. Should only ever be true
     * when the isTail option is passed */
    uint16_t hasReturned : 1;
};

/* A SlotTracker provides a handy way to keep track of
 * Slots on the stack and free them in bulk. */
typedef struct SlotTracker SlotTracker;
struct SlotTracker {
    Slot *slots;
    uint32_t count;
    uint32_t capacity;
};

/* A GstScope is a lexical scope in the program. It is
 * responsible for aliasing programmer facing names to
 * Slots and for keeping track of literals. It also
 * points to the parent GstScope, and its current child
 * GstScope. */
struct GstScope {
    uint32_t level;
    uint16_t nextLocal;
    uint16_t frameSize;
    uint32_t heapCapacity;
    uint32_t heapSize;
    uint16_t *freeHeap;
    GstObject *literals;
    GstArray *literalsArray;
    GstObject *locals;
    GstScope *parent;
};

/* Provides default FormOptions */
static FormOptions form_options_default() {
    FormOptions opts;
    opts.canChoose = 1;
    opts.isTail = 0;
    opts.resultUnused = 0;
    opts.target = 0;
    return opts;
}

/* Create some helpers that allows us to push more than just raw bytes
 * to the byte buffer. This helps us create the byte code for the compiled
 * functions. */
BUFFER_DEFINE(i32, int32_t)
BUFFER_DEFINE(number, GstNumber)
BUFFER_DEFINE(u16, uint16_t)
BUFFER_DEFINE(i16, int16_t)

/* If there is an error during compilation,
 * jump back to start */
static void c_error(GstCompiler *c, const char *e) {
    c->error = e;
    longjmp(c->onError, 1);
}

/* Push a new scope in the compiler and return
 * a pointer to it for configuration. There is
 * more configuration that needs to be done if
 * the new scope is a function declaration. */
static GstScope *compiler_push_scope(GstCompiler *c, int sameFunction) {
    GstScope *scope = gst_alloc(c->vm, sizeof(GstScope));
    scope->locals = gst_object(c->vm, 10);
    scope->freeHeap = gst_alloc(c->vm, 10 * sizeof(uint16_t));
    scope->heapSize = 0;
    scope->heapCapacity = 10;
    scope->parent = c->tail;
    scope->frameSize = 0;
    if (c->tail) {
        scope->level = c->tail->level + (sameFunction ? 0 : 1);
    } else {
        scope->level = 0;
    }
    if (sameFunction) {
        if (!c->tail) {
            c_error(c, "cannot inherit scope when root scope");
        }
        scope->nextLocal = c->tail->nextLocal;
        scope->literals = c->tail->literals;
        scope->literalsArray = c->tail->literalsArray;
    } else {
        scope->nextLocal = 0;
        scope->literals = gst_object(c->vm, 10);
        scope->literalsArray = gst_array(c->vm, 10);
    }
    c->tail = scope;
    return scope;
}

/* Remove the inner most scope from the compiler stack */
static void compiler_pop_scope(GstCompiler *c) {
    GstScope *last = c->tail;
    if (last == NULL) {
        c_error(c, "no scope to pop");
    } else {
        if (last->nextLocal > last->frameSize) {
            last->frameSize = last->nextLocal;
        }
        c->tail = last->parent;
        if (c->tail) {
            if (last->frameSize > c->tail->frameSize) {
                c->tail->frameSize = last->frameSize;
            }
        }
    }
}

/* Get the next stack position that is open for
 * a variable */
static uint16_t compiler_get_local(GstCompiler *c, GstScope *scope) {
    if (scope->heapSize == 0) {
        if (scope->nextLocal + 1 == 0) {
            c_error(c, "too many local variables");
        }
        return scope->nextLocal++;
    } else {
        return scope->freeHeap[--scope->heapSize];
    }
    return 0;
}

/* Free a slot on the stack for other locals and/or
 * intermediate values */
static void compiler_free_local(GstCompiler *c, GstScope *scope, uint16_t slot) {
    /* Ensure heap has space */
    if (scope->heapSize >= scope->heapCapacity) {
        uint32_t newCap = 2 * scope->heapSize;
        uint16_t *newData = gst_alloc(c->vm, newCap * sizeof(uint16_t));
        gst_memcpy(newData, scope->freeHeap, scope->heapSize * sizeof(uint16_t));
        scope->freeHeap = newData;
        scope->heapCapacity = newCap;
    }
    scope->freeHeap[scope->heapSize++] = slot;
}

/* Initializes a SlotTracker. SlotTrackers
 * are used during compilation to free up slots on the stack
 * after they are no longer needed. */
static void tracker_init(GstCompiler *c, SlotTracker *tracker) {
    tracker->slots = gst_alloc(c->vm, 10 * sizeof(Slot));
    tracker->count = 0;
    tracker->capacity = 10;
}

/* Free up a slot if it is a temporary slot (does not
 * belong to a named local). If the slot does belong
 * to a named variable, does nothing. */
static void compiler_drop_slot(GstCompiler *c, GstScope *scope, Slot slot) {
    if (!slot.isNil && slot.isTemp) {
        compiler_free_local(c, scope, slot.index);
    }
}

/* Helper function to return a slot. Useful for compiling things that return
 * nil. (set, while, etc.). Use this to wrap compilation calls that need
 * to return things. */
static Slot compiler_return(GstCompiler *c, Slot slot) {
    Slot ret;
    ret.hasReturned = 1;
    ret.isNil = 1;
    if (slot.hasReturned) {
        /* Do nothing */
    } else if (slot.isNil) {
        /* Return nil */
        gst_buffer_push_u16(c->vm, c->buffer, GST_OP_RTN);
    } else {
        /* Return normal value */
        gst_buffer_push_u16(c->vm, c->buffer, GST_OP_RET);
        gst_buffer_push_u16(c->vm, c->buffer, slot.index);
    }
    return ret;
}

/* Gets a temporary slot for the bottom-most scope. */
static Slot compiler_get_temp(GstCompiler *c) {
    GstScope *scope = c->tail;
    Slot ret;
    ret.isTemp = 1;
    ret.isNil = 0;
    ret.hasReturned = 0;
    ret.index = compiler_get_local(c, scope);
    return ret;
}

/* Return a slot that is the target Slot given some FormOptions. Will
 * Create a temporary slot if needed, so be sure to drop the slot after use. */
static Slot compiler_get_target(GstCompiler *c, FormOptions opts) {
    if (opts.canChoose) {
        return compiler_get_temp(c);
    } else {
        Slot ret;
        ret.isTemp = 0;
        ret.isNil = 0;
        ret.hasReturned = 0;
        ret.index = opts.target;
        return ret;
    }
}

/* If a slot is a nil slot, create a slot that has
 * an actual location on the stack. */
static Slot compiler_realize_slot(GstCompiler *c, Slot slot) {
    if (slot.isNil) {
        slot = compiler_get_temp(c);
        gst_buffer_push_u16(c->vm, c->buffer, GST_OP_NIL);
        gst_buffer_push_u16(c->vm, c->buffer, slot.index);
    }
    return slot;
}

/* Helper to get a nil slot */
static Slot nil_slot() { Slot ret; ret.isNil = 1; ret.hasReturned = 0; return ret; }

/* Writes all of the slots in the tracker to the compiler */
static void compiler_tracker_write(GstCompiler *c, SlotTracker *tracker, int reverse) {
    uint32_t i;
    GstBuffer *buffer = c->buffer;
    for (i = 0; i < tracker->count; ++i) {
        Slot s;
        if (reverse)
            s = tracker->slots[tracker->count - 1 - i];
        else
            s = tracker->slots[i];
        if (s.isNil)
            c_error(c, "trying to write nil slot");
        gst_buffer_push_u16(c->vm, buffer, s.index);
    }
}

/* Free the tracker after creation. This unlocks the memory
 * that was allocated by the GC an allows it to be collected. Also
 * frees slots that were tracked by this tracker in the given scope. */
static void compiler_tracker_free(GstCompiler *c, GstScope *scope, SlotTracker *tracker) {
    uint32_t i;
    /* Free in reverse order */
    for (i = tracker->count - 1; i < tracker->count; --i) {
        compiler_drop_slot(c, scope, tracker->slots[i]);
    }
}

/* Add a new Slot to a slot tracker. */
static void compiler_tracker_push(GstCompiler *c, SlotTracker *tracker, Slot slot) {
    if (tracker->count >= tracker->capacity) {
        uint32_t newCap = 2 * tracker->count;
        Slot *newData = gst_alloc(c->vm, newCap * sizeof(Slot));
        gst_memcpy(newData, tracker->slots, tracker->count * sizeof(Slot));
        tracker->slots = newData;
        tracker->capacity = newCap;
    }
    tracker->slots[tracker->count++] = slot;
}

/* Registers a literal in the given scope. If an equal literal is found, uses
 * that one instead of creating a new literal. This allows for some reuse
 * of things like string constants.*/
static uint16_t compiler_add_literal(GstCompiler *c, GstScope *scope, GstValue x) {
    GstValue checkDup = gst_object_get(scope->literals, x);
    uint16_t literalIndex = 0;
    if (checkDup.type != GST_NIL) {
        /* An equal literal is already registered in the current scope */
        return (uint16_t) checkDup.data.number;
    } else {
        /* Add our literal for tracking */
        GstValue valIndex;
        valIndex.type = GST_NUMBER;
        literalIndex = scope->literalsArray->count;
        valIndex.data.number = literalIndex;
        gst_object_put(c->vm, scope->literals, x, valIndex);
        gst_array_push(c->vm, scope->literalsArray, x);
    }
    return literalIndex;
}

/* Declare a symbol in a given scope. */
static uint16_t compiler_declare_symbol(GstCompiler *c, GstScope *scope, GstValue sym) {
    GstValue x;
    uint16_t target;
    if (sym.type != GST_SYMBOL)
        c_error(c, "expected symbol");
    target = compiler_get_local(c, scope);
    x.type = GST_NUMBER;
    x.data.number = target;
    gst_object_put(c->vm, scope->locals, sym, x);
    return target;
}

/* Try to resolve a symbol. If the symbol can be resovled, return true and
 * pass back the level and index by reference. */
static int symbol_resolve(GstScope *scope, GstValue x, uint16_t *level, uint16_t *index) {
    uint32_t currentLevel = scope->level;
    while (scope) {
        GstValue check = gst_object_get(scope->locals, x);
        if (check.type != GST_NIL) {
            *level = currentLevel - scope->level;
            *index = (uint16_t) check.data.number;
            return 1;
        }
        scope = scope->parent;
    }
    return 0;
}

/* Forward declaration */
/* Compile a value and return it stack location after loading.
 * If a target > 0 is passed, the returned value must be equal
 * to the targtet. If target < 0, the GstCompiler can choose whatever
 * slot location it likes. If, for example, a symbol resolves to
 * whatever is in a given slot, it makes sense to use that location
 * to 'return' the value. For other expressions, like function
 * calls, the compiler will just pick the lowest free slot
 * as the location on the stack. */
static Slot compile_value(GstCompiler *c, FormOptions opts, GstValue x);

/* Compile a structure that evaluates to a literal value. Useful
 * for objects like strings, or anything else that cannot be instatiated
 * from bytecode and doesn't do anything in the AST. */
static Slot compile_literal(GstCompiler *c, FormOptions opts, GstValue x) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (opts.resultUnused) return nil_slot();
    ret = compiler_get_target(c, opts);
    literalIndex = compiler_add_literal(c, scope, x);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_CST);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, literalIndex);
    return ret;
}

/* Compile boolean, nil, and number values. */
static Slot compile_nonref_type(GstCompiler *c, FormOptions opts, GstValue x) {
    GstBuffer *buffer = c->buffer;
    Slot ret;
    if (opts.resultUnused) return nil_slot();
    ret = compiler_get_target(c, opts);
    if (x.type == GST_NIL) {
        gst_buffer_push_u16(c->vm, buffer, GST_OP_NIL);
        gst_buffer_push_u16(c->vm, buffer, ret.index);
    } else if (x.type == GST_BOOLEAN) {
        gst_buffer_push_u16(c->vm, buffer, x.data.boolean ? GST_OP_TRU : GST_OP_FLS);
        gst_buffer_push_u16(c->vm, buffer, ret.index);
    } else if (x.type == GST_NUMBER) {
        GstNumber number = x.data.number;
        int32_t int32Num = (int32_t) number;
        if (number == (GstNumber) int32Num) {
            if (int32Num <= 32767 && int32Num >= -32768) {
                int16_t int16Num = (int16_t) number;
                gst_buffer_push_u16(c->vm, buffer, GST_OP_I16);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                gst_buffer_push_i16(c->vm, buffer, int16Num);
            } else {
                gst_buffer_push_u16(c->vm, buffer, GST_OP_I32);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                gst_buffer_push_i32(c->vm, buffer, int32Num);
            }
        } else {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_F64);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_number(c->vm, buffer, number);
        }
    } else {
        c_error(c, "expected boolean, nil, or number type");
    }
    return ret;
}

/* Compile a symbol. Resolves any kind of symbol. */
static Slot compile_symbol(GstCompiler *c, FormOptions opts, GstValue sym) {
    GstBuffer * buffer = c->buffer;
    GstScope * scope = c->tail;
    uint16_t index = 0;
    uint16_t level = 0;
    Slot ret;
    if (opts.resultUnused) return nil_slot();
    if (!symbol_resolve(scope, sym, &level, &index))
        c_error(c, "undefined symbol");
    if (level > 0) {
        /* We have an upvalue */
        ret = compiler_get_target(c, opts);
        gst_buffer_push_u16(c->vm, buffer, GST_OP_UPV);
        gst_buffer_push_u16(c->vm, buffer, ret.index);
        gst_buffer_push_u16(c->vm, buffer, level);
        gst_buffer_push_u16(c->vm, buffer, index);
    } else {
        /* Local variable on stack */
        ret.isTemp = 0;
        ret.isNil = 0;
        ret.hasReturned = 0;
        if (opts.canChoose) {
            ret.index = index;
        } else {
            /* We need to move the variable. This
             * would occur in a simple assignment like a = b. */
            ret.index = opts.target;
            gst_buffer_push_u16(c->vm, buffer, GST_OP_MOV);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_u16(c->vm, buffer, index);
        }
    }
    return ret;
}

/* Compile values in a sequence and track the returned slots.
 * If the result is unused, immediately drop slots we don't need. Can
 * also ignore the end of the tuple sequence. */
static void tracker_init_tuple(GstCompiler *c, FormOptions opts, 
        SlotTracker *tracker, GstValue *tuple, uint32_t start, uint32_t fromEnd) {
    GstScope *scope = c->tail;
    FormOptions subOpts = form_options_default();
    uint32_t i, count;
    count = gst_tuple_length(tuple);
    /* Calculate sub flags */
    subOpts.resultUnused = opts.resultUnused;
    /* Compile all of the arguments */
    tracker_init(c, tracker);
    /* Nothing to compile */
    if (count <= fromEnd) return;
    /* Compile body of array */
    for (i = start; i < (count - fromEnd); ++i) {
        Slot slot = compile_value(c, subOpts, tuple[i]);
        if (subOpts.resultUnused)
            compiler_drop_slot(c, scope, slot);
        else
            compiler_tracker_push(c, tracker, compiler_realize_slot(c, slot));
    }
}

/* Define some flags for operators */
#define OP_REVERSE 1
#define OP_FOLD 2
#define OP_DEFAULT_INT 4
#define OP_1_REPEAT 8
#define OP_1_BOOLEAN 16
#define OP_0_BOOLEAN 32

/* Compile a special form in the form of an operator. There
 * are four choices for opcodes - when the operator is called
 * with 0, 1, 2, or n arguments. When the operator form is
 * called with n arguments, the number of arguments is written
 * after the op code, followed by those arguments.
 *
 * This function also takes flags to modify the behavior of the operators
 * And give them capabilities beyond binary and unary operator. */
static Slot compile_operator(GstCompiler *c, FormOptions opts, GstValue *form,
        int16_t op0, int16_t op1, int16_t op2, int16_t opn, int flags) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot ret;
    SlotTracker tracker;
    uint32_t count = gst_tuple_length(form);
    /* Check for some early exit conditions */
    if (count == 2 && (flags & OP_1_REPEAT)) {
        return compile_value(c, opts, form[1]);
    }
    if (opts.resultUnused) {
        ret = nil_slot();
    } else {
        ret = compiler_get_target(c, opts);
        /* Write the correct opcode */
        if (count < 2) {
            if (flags & OP_DEFAULT_INT) {
                gst_buffer_push_u16(c->vm, buffer, GST_OP_I16);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                gst_buffer_push_i16(c->vm, buffer, op0);
            } else if (flags & OP_0_BOOLEAN) {
                gst_buffer_push_u16(c->vm, buffer, op0 ? GST_OP_TRU : GST_OP_FLS);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
            } else if (op0 < 0) {
                if (opn < 0) c_error(c, "this operator does not take 0 arguments");
                goto opn;
            } else {
                gst_buffer_push_u16(c->vm, buffer, op0);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
            }
        } else if (count == 2) {
            if (flags & OP_1_BOOLEAN) {
                gst_buffer_push_u16(c->vm, buffer, op1 ? GST_OP_TRU : GST_OP_FLS);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                return ret;
            } else if (op1 < 0) {
                if (opn < 0) c_error(c, "this operator does not take 1 argument");
                goto opn;
            } else {
                tracker_init_tuple(c, opts, &tracker, form, 1, 0);
                compiler_tracker_free(c, scope, &tracker);
                gst_buffer_push_u16(c->vm, buffer, op1);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                compiler_tracker_write(c, &tracker, flags & OP_REVERSE);
            }
        } else if (count == 3) {
            if (op2 < 0) {
                if (opn < 0) c_error(c, "this operator does not take 2 arguments");
                goto opn;
            } else {
                tracker_init_tuple(c, opts, &tracker, form, 1, 0);
                compiler_tracker_free(c, scope, &tracker);
                gst_buffer_push_u16(c->vm, buffer, op2);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                compiler_tracker_write(c, &tracker, flags & OP_REVERSE);
            }
        } else {
            opn:
            /* Use a left-fold for arithmetic operators */
            if (flags & OP_FOLD) {
                uint32_t i;
                FormOptions subOpts = form_options_default();
                Slot lhs = compile_value(c, subOpts, form[1]);
                Slot rhs = compile_value(c, subOpts, form[2]);
                gst_buffer_push_u16(c->vm, buffer, op2);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                gst_buffer_push_u16(c->vm, buffer, lhs.index);
                gst_buffer_push_u16(c->vm, buffer, rhs.index);
                compiler_drop_slot(c, scope, lhs);
                compiler_drop_slot(c, scope, rhs);
                for (i = 3; i < count; ++i) {
                    rhs = compile_value(c, subOpts, form[i]);
                    gst_buffer_push_u16(c->vm, buffer, op2);
                    gst_buffer_push_u16(c->vm, buffer, ret.index);
                    gst_buffer_push_u16(c->vm, buffer, ret.index);
                    gst_buffer_push_u16(c->vm, buffer, rhs.index);
                    compiler_drop_slot(c, scope, rhs);
                }
            } else {
                if (opn < 0) c_error(c, "this operator does not take n arguments");
                tracker_init_tuple(c, opts, &tracker, form, 1, 0);
                compiler_tracker_free(c, scope, &tracker);
                gst_buffer_push_u16(c->vm, buffer, opn);
                gst_buffer_push_u16(c->vm, buffer, ret.index);
                gst_buffer_push_u16(c->vm, buffer, count - 1);
                compiler_tracker_write(c, &tracker, flags & OP_REVERSE);
            }
        }
    }
    return ret;
}

/* Quickly define some specials */
#define MAKE_SPECIAL(name, op0, op1, op2, opn, flags) \
static Slot compile_##name (GstCompiler *c, FormOptions opts, GstValue *form) {\
    return compile_operator(c, opts, form, (op0), (op1), (op2), (opn), (flags));\
}

MAKE_SPECIAL(addition, 0, -1, GST_OP_ADD, -1, OP_FOLD | OP_DEFAULT_INT | OP_1_REPEAT)
MAKE_SPECIAL(subtraction, 0, GST_OP_NEG, GST_OP_SUB, -1, OP_FOLD | OP_DEFAULT_INT)
MAKE_SPECIAL(multiplication, 1, -1, GST_OP_MUL, -1, OP_FOLD | OP_DEFAULT_INT | OP_1_REPEAT)
MAKE_SPECIAL(division, 1, GST_OP_INV, GST_OP_DIV, -1, OP_FOLD | OP_DEFAULT_INT)
MAKE_SPECIAL(equals, 1, 1, GST_OP_EQL, -1, OP_0_BOOLEAN | OP_1_BOOLEAN)
MAKE_SPECIAL(lt, 1, 1, GST_OP_LTN, -1, OP_0_BOOLEAN | OP_1_BOOLEAN)
MAKE_SPECIAL(lte, 1, 1, GST_OP_LTE, -1, OP_0_BOOLEAN | OP_1_BOOLEAN)
MAKE_SPECIAL(gt, 1, 1, GST_OP_LTN, -1, OP_0_BOOLEAN | OP_1_BOOLEAN | OP_REVERSE)
MAKE_SPECIAL(gte, 1, 1, GST_OP_LTE, -1, OP_0_BOOLEAN | OP_1_BOOLEAN | OP_REVERSE)
MAKE_SPECIAL(not, -1, GST_OP_NOT, -1, -1, 0)
MAKE_SPECIAL(get, -1, -1, GST_OP_GET, -1, 0)
MAKE_SPECIAL(make_tuple, -1, -1, -1, GST_OP_TUP, 0)
MAKE_SPECIAL(length, -1, GST_OP_LEN, -1, -1, 0)

#undef MAKE_SPECIAL

/* Associative set */
static Slot compile_set(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstBuffer *buffer = c->buffer;
    FormOptions subOpts = form_options_default();
    Slot ds, key, val;
    if (gst_tuple_length(form) != 4) c_error(c, "set expects 4 arguments");
    if (opts.resultUnused) {
        ds = compiler_realize_slot(c, compile_value(c, subOpts, form[1]));
    } else {
        subOpts = opts;
        subOpts.isTail = 0;
        ds = compiler_realize_slot(c, compile_value(c, subOpts, form[1]));
        subOpts = form_options_default();
    }
    key = compiler_realize_slot(c, compile_value(c, subOpts, form[2]));
    val = compiler_realize_slot(c, compile_value(c, subOpts, form[3]));
    gst_buffer_push_u16(c->vm, buffer, GST_OP_SET);
    gst_buffer_push_u16(c->vm, buffer, ds.index);
    gst_buffer_push_u16(c->vm, buffer, key.index);
    gst_buffer_push_u16(c->vm, buffer,  val.index);
    compiler_drop_slot(c, c->tail, key);
    compiler_drop_slot(c, c->tail, val);
    if (opts.resultUnused) {
        compiler_drop_slot(c, c->tail, ds);
        return nil_slot();
    } else {
        return ds;
    }
}

/* Compile an assignment operation */
static Slot compile_assign(GstCompiler *c, FormOptions opts, GstValue left, GstValue right) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    FormOptions subOpts;
    uint16_t target = 0;
    uint16_t level = 0;
    Slot slot;
    subOpts.isTail = 0;
    subOpts.resultUnused = 0;
    if (symbol_resolve(scope, left, &level, &target)) {
        /* Check if we have an up value. Otherwise, it's just a normal
         * local variable */
        if (level != 0) {
            subOpts.canChoose = 1;
            /* Evaluate the right hand side */
            slot = compiler_realize_slot(c, compile_value(c, subOpts, right));
            /* Set the up value */
            gst_buffer_push_u16(c->vm, buffer, GST_OP_SUV);
            gst_buffer_push_u16(c->vm, buffer, slot.index);
            gst_buffer_push_u16(c->vm, buffer, level);
            gst_buffer_push_u16(c->vm, buffer, target);
        } else {
            /* Local variable */
            subOpts.canChoose = 0;
            subOpts.target = target;
            slot = compile_value(c, subOpts, right);
        }
    } else {
        /* We need to declare a new symbol */
        subOpts.target = compiler_declare_symbol(c, scope, left);
        subOpts.canChoose = 0;
        slot = compile_value(c, subOpts, right);
    }
    if (opts.resultUnused) {
        compiler_drop_slot(c, scope, slot);
        return nil_slot();
    } else {
        return slot;
    }
}

/* Compile series of expressions. This compiles the meat of
 * function definitions and the inside of do forms. */
static Slot compile_block(GstCompiler *c, FormOptions opts, GstValue *form, uint32_t startIndex) {
    GstScope *scope = c->tail;
    FormOptions subOpts = form_options_default();
    uint32_t current = startIndex;
    /* Check for empty body */
    if (gst_tuple_length(form) <= startIndex) return nil_slot();
    /* Compile the body */
    subOpts.resultUnused = 1;
    subOpts.isTail = 0;
    subOpts.canChoose = 1;
    while (current < gst_tuple_length(form) - 1) {
        compiler_drop_slot(c, scope, compile_value(c, subOpts, form[current]));
        ++current;
    }
    /* Compile the last expression in the body */
    return compile_value(c, opts, form[gst_tuple_length(form) - 1]);
}

/* Extract the last n bytes from the buffer and use them to construct
 * a function definition. */
static GstFuncDef *compiler_gen_funcdef(GstCompiler *c, uint32_t lastNBytes, uint32_t arity) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    GstFuncDef *def = gst_alloc(c->vm, sizeof(GstFuncDef));
    /* Create enough space for the new byteCode */
    if (lastNBytes > buffer->count)
        c_error(c, "trying to extract more bytes from buffer than in buffer");
    uint8_t * byteCode = gst_alloc(c->vm, lastNBytes);
    def->byteCode = (uint16_t *)byteCode;
    def->byteCodeLen = lastNBytes / 2;
    /* Copy the last chunk of bytes in the buffer into the new
     * memory for the function's byteCOde */
    gst_memcpy(byteCode, buffer->data + buffer->count - lastNBytes, lastNBytes);
    /* Remove the byteCode from the end of the buffer */
    buffer->count -= lastNBytes;
    /* Create the literals used by this function */
    if (scope->literalsArray->count) {
        def->literals = gst_alloc(c->vm, scope->literalsArray->count * sizeof(GstValue));
        gst_memcpy(def->literals, scope->literalsArray->data,
                scope->literalsArray->count * sizeof(GstValue));
    } else {
        def->literals = NULL;
    }
    def->literalsLen = scope->literalsArray->count;
    /* Delete the sub scope */
    compiler_pop_scope(c);
    /* Initialize the new FuncDef */
    def->locals = scope->frameSize;
    def->arity = arity;
    def->flags = 0;
    return def;
}

/* Compile a function from a function literal source form */
static Slot compile_function(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    uint32_t current = 1;
    uint32_t i;
    uint32_t sizeBefore; /* Size of buffer before compiling function */
    GstScope *subGstScope;
    GstArray *params;
    FormOptions subOpts = form_options_default();
    Slot ret;
    if (opts.resultUnused) return nil_slot();
    ret = compiler_get_target(c, opts);
    subGstScope = compiler_push_scope(c, 0);
    /* Check for function documentation - for now just ignore. */
    if (form[current].type == GST_STRING)
        ++current;
    /* Define the function parameters */
    if (form[current].type != GST_ARRAY)
        c_error(c, "expected function arguments array");
    params = form[current++].data.array;
    for (i = 0; i < params->count; ++i) {
        GstValue param = params->data[i];
        if (param.type != GST_SYMBOL)
            c_error(c, "function parameters should be symbols");
        /* The compiler puts the parameter locals
         * in the right place by default - at the beginning
         * of the stack frame. */
        compiler_declare_symbol(c, subGstScope, param);
    }
    /* Mark where we are on the stack so we can
     * return to it later. */
    sizeBefore = buffer->count;
    /* Compile the body in the subscope */
    subOpts.isTail = 1;
    compiler_return(c, compile_block(c, subOpts, form, current));
    /* Create a new FuncDef as a constant in original scope by splicing
     * out the relevant code from the buffer. */
    {
        GstValue newVal;
        uint16_t literalIndex;
        GstFuncDef *def = compiler_gen_funcdef(c, buffer->count - sizeBefore, params->count);
        /* Add this FuncDef as a literal in the outer scope */
        newVal.type = GST_FUNCDEF;
        newVal.data.def = def;
        literalIndex = compiler_add_literal(c, scope, newVal);
        gst_buffer_push_u16(c->vm, buffer, GST_OP_CLN);
        gst_buffer_push_u16(c->vm, buffer, ret.index);
        gst_buffer_push_u16(c->vm, buffer, literalIndex);
    }
    return ret;
}

/* Branching special */
static Slot compile_if(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    FormOptions condOpts = opts;
    FormOptions branchOpts = opts;
    Slot left, right, condition;
    uint32_t countAtJumpIf = 0;
    uint32_t countAtJump = 0;
    uint32_t countAfterFirstBranch = 0;
    /* Check argument count */
    if (gst_tuple_length(form) < 3 || gst_tuple_length(form) > 4)
        c_error(c, "if takes either 2 or 3 arguments");
    /* Compile the condition */
    condOpts.isTail = 0;
    condOpts.resultUnused = 0;
    condition = compile_value(c, condOpts, form[1]);
    /* If the condition is nil, just compile false path */
    if (condition.isNil) {
        if (gst_tuple_length(form) == 4) {
            return compile_value(c, opts, form[3]);
        }
        return condition;
    }
    /* Mark where the buffer is now so we can write the jump
     * length later */
    countAtJumpIf = buffer->count;
    buffer->count += sizeof(int32_t) + 2 * sizeof(uint16_t);
    /* Configure branch form options */
    branchOpts.canChoose = 0;
    branchOpts.target = condition.index;
    /* Compile true path */
    left = compile_value(c, branchOpts, form[2]);
    if (opts.isTail) {
        compiler_return(c, left);
    } else {
        /* If we need to jump again, do so */
        if (gst_tuple_length(form) == 4) {
            countAtJump = buffer->count;
            buffer->count += sizeof(int32_t) + sizeof(uint16_t);
        }
    }
    compiler_drop_slot(c, scope, left);
    /* Reinsert jump with correct index */
    countAfterFirstBranch = buffer->count;
    buffer->count = countAtJumpIf;
    gst_buffer_push_u16(c->vm, buffer, GST_OP_JIF);
    gst_buffer_push_u16(c->vm, buffer, condition.index);
    gst_buffer_push_i32(c->vm, buffer, (countAfterFirstBranch - countAtJumpIf) / 2);
    buffer->count = countAfterFirstBranch;
    /* Compile false path */
    if (gst_tuple_length(form) == 4) {
        right = compile_value(c, branchOpts, form[3]);
        if (opts.isTail) compiler_return(c, right);
        compiler_drop_slot(c, scope, right);
    } else if (opts.isTail) {
        compiler_return(c, condition);
    }
    /* Reset the second jump length */
    if (!opts.isTail && gst_tuple_length(form) == 4) {
        countAfterFirstBranch = buffer->count;
        buffer->count = countAtJump;
        gst_buffer_push_u16(c->vm, buffer, GST_OP_JMP);
        gst_buffer_push_i32(c->vm, buffer, (countAfterFirstBranch - countAtJump) / 2);
        buffer->count = countAfterFirstBranch;
    }
    if (opts.isTail)
        condition.hasReturned = 1;
    return condition;
}

/* Special to throw an error */
static Slot compile_error(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstBuffer *buffer = c->buffer;
    Slot ret;
    GstValue x;
    if (gst_tuple_length(form) != 2)
        c_error(c, "error takes exactly 1 argument");
    x = form[1];
    ret = compiler_realize_slot(c, compile_value(c, opts, x));
    gst_buffer_push_u16(c->vm, buffer, GST_OP_ERR);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    return nil_slot();
}

/* Try catch special */
static Slot compile_try(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot body;
    uint16_t errorIndex;
    uint32_t countAtTry, countTemp, countAtJump;
    /* Check argument count */
    if (gst_tuple_length(form) < 3 || gst_tuple_length(form) > 4)
        c_error(c, "try takes either 2 or 3 arguments");
    /* Check for symbol to bind error to */
    if (form[1].type != GST_SYMBOL)
        c_error(c, "expected symbol at start of try");
    /* Add subscope for error variable */
    GstScope *subScope = compiler_push_scope(c, 1);
    errorIndex = compiler_declare_symbol(c, subScope, form[1]);
    /* Leave space for try instruction */
    countAtTry = buffer->count;
    buffer->count += sizeof(uint32_t) + 2 * sizeof(uint16_t);
    /* Compile the body */
    body = compile_value(c, opts, form[2]);
    if (opts.isTail) {
        compiler_return(c, body);
    } else {
        /* If we need to jump over the catch, do so */
        if (gst_tuple_length(form) == 4) {
            countAtJump = buffer->count;
            buffer->count += sizeof(int32_t) + sizeof(uint16_t);
        }
    }
    /* Reinsert try jump with correct index */
    countTemp = buffer->count;
    buffer->count = countAtTry;
    gst_buffer_push_u16(c->vm, buffer, GST_OP_TRY);
    gst_buffer_push_u16(c->vm, buffer, errorIndex);
    gst_buffer_push_i32(c->vm, buffer, (countTemp - countAtTry) / 2);
    buffer->count = countTemp;
    /* Compile catch path */
    if (gst_tuple_length(form) == 4) {
        Slot catch;
        countAtJump = buffer->count;
        catch = compile_value(c, opts, form[3]);
        if (opts.isTail) compiler_return(c, catch);
        compiler_drop_slot(c, scope, catch);
    } else if (opts.isTail) {
        compiler_return(c, nil_slot());
    }
    /* Reset the second jump length */
    if (!opts.isTail && gst_tuple_length(form) == 4) {
        countTemp = buffer->count;
        buffer->count = countAtJump;
        gst_buffer_push_u16(c->vm, buffer, GST_OP_JMP);
        gst_buffer_push_i32(c->vm, buffer, (countTemp - countAtJump) / 2);
        buffer->count = countTemp;
    }
    /* Untry */
    gst_buffer_push_u16(c->vm, buffer, GST_OP_UTY);
    /* Pop the error scope */
    compiler_pop_scope(c);
    if (opts.isTail)
        body.hasReturned = 1;
    return body;
}

/* While special */
static Slot compile_while(GstCompiler *c, FormOptions opts, GstValue *form) {
    Slot cond;
    uint32_t countAtStart = c->buffer->count;
    uint32_t countAtJumpDelta;
    uint32_t countAtFinish;
    FormOptions defaultOpts = form_options_default();
    compiler_push_scope(c, 1);
    /* Compile condition */
    cond = compile_value(c, defaultOpts, form[1]);
    /* Assert that cond is a real value - otherwise do nothing (nil is false,
     * so loop never runs.) */
    if (cond.isNil) return cond;
    /* Leave space for jump later */
    countAtJumpDelta = c->buffer->count;
    c->buffer->count += sizeof(uint16_t) * 2 + sizeof(int32_t);
    /* Compile loop body */
    defaultOpts.resultUnused = 1;
    compiler_drop_slot(c, c->tail, compile_block(c, defaultOpts, form, 2));
    /* Jump back to the loop start */
    countAtFinish = c->buffer->count;
    gst_buffer_push_u16(c->vm, c->buffer, GST_OP_JMP);
    gst_buffer_push_i32(c->vm, c->buffer, (int32_t)(countAtFinish - countAtStart) / -2);
    countAtFinish = c->buffer->count;
    /* Set the jump to the correct length */
    c->buffer->count = countAtJumpDelta;
    gst_buffer_push_u16(c->vm, c->buffer, GST_OP_JIF);
    gst_buffer_push_u16(c->vm, c->buffer, cond.index);
    gst_buffer_push_i32(c->vm, c->buffer, (int32_t)(countAtFinish - countAtJumpDelta) / 2);
    /* Pop scope */
    c->buffer->count = countAtFinish;
    compiler_pop_scope(c);
    /* Return nil */
    if (opts.resultUnused)
        return nil_slot();
    else
        return cond;
}

/* Do special */
static Slot compile_do(GstCompiler *c, FormOptions opts, GstValue *form) {
    Slot ret;
    compiler_push_scope(c, 1);
    ret = compile_block(c, opts, form, 1);
    compiler_pop_scope(c);
    return ret;
}

/* Quote special - returns its argument as is. */
static Slot compile_quote(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (gst_tuple_length(form) != 2)
        c_error(c, "quote takes exactly 1 argument");
    GstValue x = form[1];
    if (x.type == GST_NIL ||
            x.type == GST_BOOLEAN ||
            x.type == GST_NUMBER) {
        return compile_nonref_type(c, opts, x);
    }
    if (opts.resultUnused) return nil_slot();
    ret = compiler_get_target(c, opts);
    literalIndex = compiler_add_literal(c, scope, x);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_CST);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, literalIndex);
    return ret;
}

/* Assignment special */
static Slot compile_var(GstCompiler *c, FormOptions opts, GstValue *form) {
    if (gst_tuple_length(form) != 3)
        c_error(c, "assignment expects 2 arguments");
    return compile_assign(c, opts, form[1], form[2]);
}

/* Define a function type for Special Form helpers */
typedef Slot (*SpecialFormHelper) (GstCompiler *c, FormOptions opts, GstValue *form);

/* Dispatch to a special form */
static SpecialFormHelper get_special(GstValue *form) {
    uint8_t *name;
    if (gst_tuple_length(form) < 1 || form[0].type != GST_SYMBOL)
        return NULL;
    name = form[0].data.string;
    /* If we have a symbol with a zero length name, we have other
     * problems. */
    if (gst_string_length(name) == 0)
        return NULL;
    /* One character specials. Mostly math. */
    if (gst_string_length(name) == 1) {
        switch(name[0]) {
            case '+': return compile_addition;
            case '-': return compile_subtraction;
            case '*': return compile_multiplication;
            case '/': return compile_division;
            case '>': return compile_gt;
            case '<': return compile_lt;
            case '=': return compile_equals;
            case ':': return compile_var;
            default:
                break;
        }
    }
    /* Multi character specials. Mostly control flow. */
    switch (name[0]) {
        case '>':
            {
                if (gst_string_length(name) == 2 &&
                        name[1] == '=') {
                    return compile_gte;
                }
            }
            break;
        case '<':
            {
                if (gst_string_length(name) == 2 &&
                        name[1] == '=') {
                    return compile_lte;
                }
            }
            break;
        case 'e':
            {
                if (gst_string_length(name) == 5 &&
                        name[1] == 'r' &&
                        name[2] == 'r' &&
                        name[3] == 'o' &&
                        name[4] == 'r') {
                    return compile_error;
                }
            }
        case 'g':
            {
                if (gst_string_length(name) == 3 &&
                        name[1] == 'e' &&
                        name[2] == 't') {
                    return compile_get;
                }
            }
        case 'd':
            {
                if (gst_string_length(name) == 2 &&
                        name[1] == 'o') {
                    return compile_do;
                }
            }
            break;
        case 'i':
            {
                if (gst_string_length(name) == 2 &&
                        name[1] == 'f') {
                    return compile_if;
                }
            }
            break;
        case 'f':
            {
                if (gst_string_length(name) == 2 &&
                        name[1] == 'n') {
                    return compile_function;
                }
            }
            break;
        case 'l':
            {
                if (gst_string_length(name) == 6 &&
                        name[1] == 'e' &&
                        name[2] == 'n' &&
                        name[3] == 'g' &&
                        name[4] == 't' &&
                        name[5] == 'h') {
                    return compile_length;
                }
            }
        case 'n':
            {
                if (gst_string_length(name) == 3 &&
                        name[1] == 'o' &&
                        name[2] == 't') {
                    return compile_not;
                }
            }
            break;
        case 'q':
            {
                if (gst_string_length(name) == 5 &&
                        name[1] == 'u' &&
                        name[2] == 'o' &&
                        name[3] == 't' &&
                        name[4] == 'e') {
                    return compile_quote;
                }
            }
            break;
        case 's':
            {
                if (gst_string_length(name) == 3 &&
                        name[1] == 'e' &&
                        name[2] == 't') {
                    return compile_set;
                }
            }
            break;
        case 't':
            {
                if (gst_string_length(name) == 3 &&
                        name[1] == 'r' &&
                        name[2] == 'y') {
                    return compile_try;
                } else if (gst_string_length(name) == 5 &&
                        name[1] == 'u' &&
                        name[2] == 'p' &&
                        name[3] == 'l' &&
                        name[4] == 'e') {
                    return compile_make_tuple;
                }
            }
        case 'w':
            {
                if (gst_string_length(name) == 5 &&
                        name[1] == 'h' &&
                        name[2] == 'i' &&
                        name[3] == 'l' &&
                        name[4] == 'e') {
                    return compile_while;
                }
            }
            break;
        default:
            break;
    }
    return NULL;
}

/* Compile an array */
static Slot compile_array(GstCompiler *c, FormOptions opts, GstArray *array) {
    GstScope *scope = c->tail;
    FormOptions subOpts = form_options_default();
    GstBuffer *buffer = c->buffer;
    Slot ret;
    SlotTracker tracker;
    uint32_t i, count;
    count = array->count;
    ret = compiler_get_target(c, opts);
    tracker_init(c, &tracker);
    for (i = 0; i < count; ++i) {
        Slot slot = compile_value(c, subOpts, array->data[i]);
        compiler_tracker_push(c, &tracker, compiler_realize_slot(c, slot));
    }
    compiler_tracker_free(c, scope, &tracker);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_ARR);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, count);
    compiler_tracker_write(c, &tracker, 0);
    return ret;
}

/* Compile an object literal */
static Slot compile_object(GstCompiler *c, FormOptions opts, GstObject *obj) {
    GstScope *scope = c->tail;
    FormOptions subOpts = form_options_default();
    GstBuffer *buffer = c->buffer;
    GstBucket *bucket;
    Slot ret;
    SlotTracker tracker;
    uint32_t i, cap;
    cap = obj->capacity;
    ret = compiler_get_target(c, opts);
    tracker_init(c, &tracker);
    for (i = 0; i < cap; ++i) {
        bucket = obj->buckets[i];
        while (bucket != NULL) {
            Slot slot = compile_value(c, subOpts, bucket->key);
            compiler_tracker_push(c, &tracker, compiler_realize_slot(c, slot));
            slot = compile_value(c, subOpts, bucket->value);
            compiler_tracker_push(c, &tracker, compiler_realize_slot(c, slot));
            bucket = bucket->next;
        }
    }
    compiler_tracker_free(c, scope, &tracker);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_DIC);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, obj->count * 2);
    compiler_tracker_write(c, &tracker, 0);
    return ret;
}

/* Compile a form. Checks for special forms and macros. */
static Slot compile_form(GstCompiler *c, FormOptions opts, GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    SpecialFormHelper helper;
    /* Empty forms evaluate to nil. */
    if (gst_tuple_length(form) == 0) {
        GstValue temp;
        temp.type = GST_NIL;
        return compile_nonref_type(c, opts, temp);
    }
    /* Check and handle special forms */
    helper = get_special(form);
    if (helper != NULL) {
        return helper(c, opts, form);
    } else {
        Slot ret, callee;
        SlotTracker tracker;
        FormOptions subOpts = form_options_default();
        uint32_t i;
        tracker_init(c, &tracker);
        /* Compile function to be called */
        callee = compiler_realize_slot(c, compile_value(c, subOpts, form[0]));
        /* Compile all of the arguments */
        for (i = 1; i < gst_tuple_length(form); ++i) {
            Slot slot = compile_value(c, subOpts, form[i]);
            compiler_tracker_push(c, &tracker, slot);
        }
        /* Free up some slots */
        compiler_drop_slot(c, scope, callee);
        compiler_tracker_free(c, scope, &tracker);
        /* If this is in tail position do a tail call. */
        if (opts.isTail) {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_TCL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            gst_buffer_push_u16(c->vm, buffer, gst_tuple_length(form) - 1);
            ret.hasReturned = 1;
            ret.isNil = 1;
        } else {
            ret = compiler_get_target(c, opts);
            gst_buffer_push_u16(c->vm, buffer, GST_OP_CAL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_u16(c->vm, buffer, gst_tuple_length(form) - 1);
        }
        /* Write the location of all of the arguments */
        compiler_tracker_write(c, &tracker, 0);
        return ret;
    }
}

/* Recursively compile any value or form */
static Slot compile_value(GstCompiler *c, FormOptions opts, GstValue x) {
    switch (x.type) {
        case GST_NIL:
        case GST_BOOLEAN:
        case GST_NUMBER:
            return compile_nonref_type(c, opts, x);
        case GST_SYMBOL:
            return compile_symbol(c, opts, x);
        case GST_TUPLE:
            return compile_form(c, opts, x.data.tuple);
        case GST_ARRAY:
            return compile_array(c, opts, x.data.array);
        case GST_OBJECT:
            return compile_object(c, opts, x.data.object);
        default:
            return compile_literal(c, opts, x);
    }
}

/* Initialize a GstCompiler struct */
void gst_compiler(GstCompiler *c, Gst *vm) {
    c->vm = vm;
    c->buffer = gst_buffer(vm, 128);
    c->env = gst_array(vm, 10);
    c->tail = NULL;
    c->error = NULL;
    compiler_push_scope(c, 0);
}

/* Add environment */
void gst_compiler_env(GstCompiler *c, GstValue env) {
    uint32_t i;
    GstBucket *bucket;
    /* Register everything in environment */
    if (env.type == GST_OBJECT) {
        for (i = 0; i < env.data.object->capacity; ++i) {
            bucket = env.data.object->buckets[i];
            while (bucket) {
                if (bucket->key.type == GST_SYMBOL) {
                    compiler_declare_symbol(c, c->tail, bucket->key);
                    gst_array_push(c->vm, c->env, bucket->value);
                }
                bucket = bucket->next;
            }
        }
    }
}

/* Register a global for the compilation environment. */
void gst_compiler_add_global(GstCompiler *c, const char *name, GstValue x) {
    GstValue sym = gst_load_cstring(c->vm, name);
    sym.type = GST_SYMBOL;
    compiler_declare_symbol(c, c->tail, sym);
    gst_array_push(c->vm, c->env, x);
}

/* Register a global c function for the compilation environment. */
void gst_compiler_add_global_cfunction(GstCompiler *c, const char *name, GstCFunction f) {
    GstValue func;
    func.type = GST_CFUNCTION;
    func.data.cfunction = f;
    gst_compiler_add_global(c, name, func);
}

/* Compile interface. Returns a function that evaluates the
 * given AST. Returns NULL if there was an error during compilation. */
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form) {
    FormOptions opts = form_options_default();
    GstFuncDef *def;
    if (setjmp(c->onError)) {
        /* Clear all but root scope */
        if (c->tail)
            c->tail->parent = NULL;
        if (c->error == NULL)
            c->error = "unknown error";
        return NULL;
    }
    /* Create a scope */
    opts.isTail = 1;
    compiler_push_scope(c, 0);
    compiler_return(c, compile_value(c, opts, form));
    def = compiler_gen_funcdef(c, c->buffer->count, 0);
    {
        uint32_t envSize = c->env->count;
        GstFuncEnv *env = gst_alloc(c->vm, sizeof(GstFuncEnv));
        GstFunction *func = gst_alloc(c->vm, sizeof(GstFunction));
        if (envSize) {
            env->values = gst_alloc(c->vm, sizeof(GstValue) * envSize);
            gst_memcpy(env->values, c->env->data, envSize * sizeof(GstValue));
        } else {
            env->values = NULL;
        }
        env->stackOffset = envSize;
        env->thread = NULL;
        func->parent = NULL;
        func->def = def;
        func->env = env;
        return func;
    }
}

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

#include <gst/gst.h>
#include <gst/compile.h>

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
    SlotTracker *next;
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
    GstTable *literals;
    GstArray *literalsArray;
    GstTable *namedLiterals;
    GstTable *nilNamedLiterals; /* Work around tables not containg nil */
    GstTable *locals;
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
BUFFER_DEFINE(i64, int64_t)
BUFFER_DEFINE(real, GstReal)
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
    scope->locals = gst_table(c->vm, 10);
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
        scope->namedLiterals = c->tail->namedLiterals;
        scope->nilNamedLiterals = c->tail->nilNamedLiterals;
    } else {
        scope->nextLocal = 0;
        scope->literals = gst_table(c->vm, 10);
        scope->literalsArray = gst_array(c->vm, 10);
        scope->namedLiterals = gst_table(c->vm, 10);
        scope->nilNamedLiterals = gst_table(c->vm, 10);
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
    /* Push to tracker stack */
    tracker->next = (SlotTracker *) c->trackers;
    c->trackers = tracker;
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
    /* Pop from tracker stack */
    c->trackers = tracker->next;
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
    GstValue checkDup = gst_table_get(scope->literals, x);
    uint16_t literalIndex = 0;
    if (checkDup.type != GST_NIL) {
        /* An equal literal is already registered in the current scope */
        return (uint16_t) checkDup.data.integer;
    } else {
        /* Add our literal for tracking */
        GstValue valIndex;
        valIndex.type = GST_INTEGER;
        literalIndex = scope->literalsArray->count;
        valIndex.data.integer = literalIndex;
        gst_table_put(c->vm, scope->literals, x, valIndex);
        gst_array_push(c->vm, scope->literalsArray, x);
    }
    return literalIndex;
}

/* Declare a symbol in a given scope. */
static uint16_t compiler_declare_symbol(GstCompiler *c, GstScope *scope, GstValue sym) {
    GstValue x;
    uint16_t target;
    if (sym.type != GST_STRING)
        c_error(c, "expected string");
    target = compiler_get_local(c, scope);
    x.type = GST_INTEGER;
    x.data.integer = target;
    gst_table_put(c->vm, scope->locals, sym, x);
    return target;
}

/* Try to resolve a symbol. If the symbol can be resovled, return true and
 * pass back the level and index by reference. */
static int symbol_resolve(GstCompiler *c, GstValue x, uint16_t *level, uint16_t *index, GstValue *out) {
    GstScope *scope = c->tail;
    uint32_t currentLevel = scope->level;
    while (scope) {
        GstValue check = gst_table_get(scope->locals, x);
        if (check.type != GST_NIL) {
            *level = currentLevel - scope->level;
            *index = (uint16_t) check.data.integer;
            return 1;
        }
        /* Check for named literals */
        check = gst_table_get(scope->namedLiterals, x);
        if (check.type != GST_NIL) {
            *out = check;
            return 2; 
        }
        /* Check for nil named literal */
        check = gst_table_get(scope->nilNamedLiterals, x);
        if (check.type != GST_NIL) {
            *out = gst_wrap_nil();
            return 2;
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
    } else if (x.type == GST_REAL) {
        gst_buffer_push_u16(c->vm, buffer, GST_OP_F64);
        gst_buffer_push_u16(c->vm, buffer, ret.index);
        gst_buffer_push_real(c->vm, buffer, x.data.real);
    } else if (x.type == GST_INTEGER) {
        if (x.data.integer <= 32767 && x.data.integer >= -32768) {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_I16);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_i16(c->vm, buffer, x.data.integer);
        } else if (x.data.integer <= 2147483647 && x.data.integer >= -2147483648) {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_I32);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_i32(c->vm, buffer, x.data.integer);
        } else {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_I64);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
            gst_buffer_push_i64(c->vm, buffer, x.data.integer);
        }
    } else {
        c_error(c, "expected boolean, nil, or number type");
    }
    return ret;
}

/* Compile a structure that evaluates to a literal value. Useful
 * for objects like strings, or anything else that cannot be instantiated
 * from bytecode and doesn't do anything in the AST. */
static Slot compile_literal(GstCompiler *c, FormOptions opts, GstValue x) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (opts.resultUnused) return nil_slot();
    switch (x.type) {
        case GST_INTEGER:
        case GST_REAL:
        case GST_BOOLEAN:
        case GST_NIL:
            return compile_nonref_type(c, opts, x);
        default:
            break;
    }
    ret = compiler_get_target(c, opts);
    literalIndex = compiler_add_literal(c, scope, x);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_CST);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, literalIndex);
    return ret;
}

/* Compile a symbol. Resolves any kind of symbol. */
static Slot compile_symbol(GstCompiler *c, FormOptions opts, GstValue sym) {
    GstValue lit = gst_wrap_nil();
    GstBuffer * buffer = c->buffer;
    uint16_t index = 0;
    uint16_t level = 0;
    Slot ret;
    int status = symbol_resolve(c, sym, &level, &index, &lit);
    if (!status) {
        c_error(c, "undefined symbol");
    }
    if (opts.resultUnused) return nil_slot();
    if (status == 2) {
        /* We have a named literal */
        return compile_literal(c, opts, lit);
    } else if (level > 0) {
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

/* Compile an assignment operation */
static Slot compile_assign(GstCompiler *c, FormOptions opts, GstValue left, GstValue right) {
    GstValue lit = gst_wrap_nil();
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    FormOptions subOpts;
    uint16_t target = 0;
    uint16_t level = 0;
    Slot slot;
    int status;
    subOpts.isTail = 0;
    subOpts.resultUnused = 0;
    status = symbol_resolve(c, left, &level, &target, &lit);
    if (status == 2) {
        c_error(c, "cannot set binding");
    } else if (status == 1) {
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
static Slot compile_block(GstCompiler *c, FormOptions opts, const GstValue *form, uint32_t startIndex) {
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
static GstFuncDef *compiler_gen_funcdef(GstCompiler *c, uint32_t lastNBytes, uint32_t arity, int varargs) {
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
    def->flags = varargs ? GST_FUNCDEF_FLAG_VARARG : 0;
    return def;
}

/* Check if a string a cstring are equal */
static int equal_cstr(const uint8_t *str, const char *cstr) {
    uint32_t i;
    for (i = 0; i < gst_string_length(str); ++i) {
        if (cstr[i] == 0) return 0;
        if (str[i] != ((const uint8_t *)cstr)[i]) return 0;
    }
    return cstr[i] == 0;
}

/* Compile a function from a function literal source form */
static Slot compile_function(GstCompiler *c, FormOptions opts, const GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    uint32_t current = 1;
    uint32_t i;
    uint32_t sizeBefore; /* Size of buffer before compiling function */
    GstScope *subGstScope;
    GstArray *params;
    FormOptions subOpts = form_options_default();
    Slot ret;
    int varargs;
    uint32_t arity;
    if (opts.resultUnused) return nil_slot();
    ret = compiler_get_target(c, opts);
    subGstScope = compiler_push_scope(c, 0);
    /* Define the function parameters */
    if (form[current].type != GST_ARRAY)
        c_error(c, "expected function arguments array");
    params = form[current++].data.array;
    arity = params->count;
    for (i = 0; i < params->count; ++i) {
        GstValue param = params->data[i];
        if (param.type != GST_STRING)
            c_error(c, "function parameters should be strings");
        /* Check for varargs */
        if (equal_cstr(param.data.string, "&")) {
            if (i != params->count - 1) {
                c_error(c, "& is reserved for vararg argument in function");
            }
            varargs = 1;
            arity--;
        }
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
        GstFuncDef *def = compiler_gen_funcdef(c, buffer->count - sizeBefore, arity, varargs);
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
static Slot compile_if(GstCompiler *c, FormOptions opts, const GstValue *form) {
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

/* While special */
static Slot compile_while(GstCompiler *c, FormOptions opts, const GstValue *form) {
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
static Slot compile_do(GstCompiler *c, FormOptions opts, const GstValue *form) {
    Slot ret;
    compiler_push_scope(c, 1);
    ret = compile_block(c, opts, form, 1);
    compiler_pop_scope(c);
    return ret;
}

/* Quote special - returns its argument as is. */
static Slot compile_quote(GstCompiler *c, FormOptions opts, const GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (gst_tuple_length(form) != 2)
        c_error(c, "quote takes exactly 1 argument");
    GstValue x = form[1];
    if (x.type == GST_NIL ||
            x.type == GST_BOOLEAN ||
            x.type == GST_REAL ||
            x.type == GST_INTEGER) {
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
static Slot compile_var(GstCompiler *c, FormOptions opts, const GstValue *form) {
    if (gst_tuple_length(form) != 3)
        c_error(c, "assignment expects 2 arguments");
    return compile_assign(c, opts, form[1], form[2]);
}

/* Apply special */
static Slot compile_apply(GstCompiler *c, FormOptions opts, const GstValue *form) {
    GstScope *scope = c->tail;
    GstBuffer *buffer = c->buffer;
    /* Empty forms evaluate to nil. */
    if (gst_tuple_length(form) < 3)
        c_error(c, "apply expects at least 2 arguments");
    {
        Slot ret, callee;
        SlotTracker tracker;
        FormOptions subOpts = form_options_default();
        uint32_t i;
        tracker_init(c, &tracker);
        /* Compile function to be called */
        callee = compiler_realize_slot(c, compile_value(c, subOpts, form[1]));
        /* Compile all of the arguments */
        for (i = 2; i < gst_tuple_length(form) - 1; ++i) {
            Slot slot = compile_value(c, subOpts, form[i]);
            compiler_tracker_push(c, &tracker, slot);
        }
        /* Write last item */
        {
            Slot slot = compile_value(c, subOpts, form[gst_tuple_length(form) - 1]);
            slot = compiler_realize_slot(c, slot);
            /* Free up some slots */
            compiler_drop_slot(c, scope, callee);
            compiler_drop_slot(c, scope, slot);
            compiler_tracker_free(c, scope, &tracker);
            /* Write first arguments */
            gst_buffer_push_u16(c->vm, buffer, GST_OP_PSK);
            gst_buffer_push_u16(c->vm, buffer, tracker.count);
            /* Write the location of all of the arguments */
            compiler_tracker_write(c, &tracker, 0);
            /* Write last arguments */
            gst_buffer_push_u16(c->vm, buffer, GST_OP_PAR);
            gst_buffer_push_u16(c->vm, buffer, slot.index);
        }
        /* If this is in tail position do a tail call. */
        if (opts.isTail) {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_TCL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            ret.hasReturned = 1;
            ret.isNil = 1;
        } else {
            ret = compiler_get_target(c, opts);
            gst_buffer_push_u16(c->vm, buffer, GST_OP_CAL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
        }
        return ret;
    }
}

/* Define a function type for Special Form helpers */
typedef Slot (*SpecialFormHelper) (GstCompiler *c, FormOptions opts, const GstValue *form);

/* Dispatch to a special form */
static SpecialFormHelper get_special(const GstValue *form) {
    const uint8_t *name;
    if (gst_tuple_length(form) < 1 || form[0].type != GST_STRING)
        return NULL;
    name = form[0].data.string;
    /* If we have a symbol with a zero length name, we have other
     * problems. */
    if (gst_string_length(name) == 0)
        return NULL;
    /* One character specials. */
    if (gst_string_length(name) == 1) {
        switch(name[0]) {
            case ':': return compile_var;
            default:
                break;
        }
    }
    /* Multi character specials. Mostly control flow. */
    switch (name[0]) {
        case 'a':
            {
                if (gst_string_length(name) == 5 &&
                        name[1] == 'p' &&
                        name[2] == 'p' &&
                        name[3] == 'l' &&
                        name[4] == 'y') {
                    return compile_apply;
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
static Slot compile_table(GstCompiler *c, FormOptions opts, GstTable *tab) {
    GstScope *scope = c->tail;
    FormOptions subOpts = form_options_default();
    GstBuffer *buffer = c->buffer;
    Slot ret;
    SlotTracker tracker;
    uint32_t i, cap;
    cap = tab->capacity;
    ret = compiler_get_target(c, opts);
    tracker_init(c, &tracker);
    for (i = 0; i < cap; i += 2) {
        if (tab->data[i].type != GST_NIL) {
            Slot slot = compile_value(c, subOpts, tab->data[i]);
            compiler_tracker_push(c, &tracker, compiler_realize_slot(c, slot));
            slot = compile_value(c, subOpts, tab->data[i + 1]);
            compiler_tracker_push(c, &tracker, compiler_realize_slot(c, slot));
        }
    }
    compiler_tracker_free(c, scope, &tracker);
    gst_buffer_push_u16(c->vm, buffer, GST_OP_DIC);
    gst_buffer_push_u16(c->vm, buffer, ret.index);
    gst_buffer_push_u16(c->vm, buffer, tab->count * 2);
    compiler_tracker_write(c, &tracker, 0);
    return ret;
}

/* Compile a form. Checks for special forms and macros. */
static Slot compile_form(GstCompiler *c, FormOptions opts, const GstValue *form) {
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
        /* Prepare next stack frame */
        gst_buffer_push_u16(c->vm, buffer, GST_OP_PSK);
        gst_buffer_push_u16(c->vm, buffer, gst_tuple_length(form) - 1);
        /* Write the location of all of the arguments */
        compiler_tracker_write(c, &tracker, 0);
        /* If this is in tail position do a tail call. */
        if (opts.isTail) {
            gst_buffer_push_u16(c->vm, buffer, GST_OP_TCL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            ret.hasReturned = 1;
            ret.isNil = 1;
        } else {
            ret = compiler_get_target(c, opts);
            gst_buffer_push_u16(c->vm, buffer, GST_OP_CAL);
            gst_buffer_push_u16(c->vm, buffer, callee.index);
            gst_buffer_push_u16(c->vm, buffer, ret.index);
        }
        return ret;
    }
}

/* Recursively compile any value or form */
static Slot compile_value(GstCompiler *c, FormOptions opts, GstValue x) {
    switch (x.type) {
        case GST_NIL:
        case GST_BOOLEAN:
        case GST_REAL:
        case GST_INTEGER:
            return compile_nonref_type(c, opts, x);
        case GST_STRING:
            return compile_symbol(c, opts, x);
        case GST_TUPLE:
            return compile_form(c, opts, x.data.tuple);
        case GST_ARRAY:
            return compile_array(c, opts, x.data.array);
        case GST_TABLE:
            return compile_table(c, opts, x.data.table);
        default:
            return compile_literal(c, opts, x);
    }
}

/* Initialize a GstCompiler struct */
void gst_compiler(GstCompiler *c, Gst *vm) {
    c->vm = vm;
    c->buffer = gst_buffer(vm, 128);
    c->tail = NULL;
    c->error = NULL;
    c->trackers = NULL;
    compiler_push_scope(c, 0);
}

/* Add a global variable */
void gst_compiler_global(GstCompiler *c, const char *name, GstValue x) {
    GstScope *scope = c->tail;
    GstValue sym = gst_string_cv(c->vm, name);
    if (x.type == GST_NIL)
        gst_table_put(c->vm, scope->nilNamedLiterals, sym, gst_wrap_boolean(1));
    else
        gst_table_put(c->vm, scope->namedLiterals, sym, x);
}

/* Add many global variables */
void gst_compiler_globals(GstCompiler *c, GstValue env) {
    GstScope *scope = c->tail;
    const GstValue *data;
    uint32_t len;
    uint32_t i;
    if (gst_hashtable_view(env, &data, &len))
        for (i = 0; i < len; i += 2)
            if (data[i].type == GST_STRING)
                gst_table_put(c->vm, scope->namedLiterals, data[i], data[i + 1]);
}

/* Use a module that was loaded into the vm */
void gst_compiler_usemodule(GstCompiler *c, const char *modulename) {
    GstValue mod = gst_table_get(c->vm->modules, gst_string_cv(c->vm, modulename));
    gst_compiler_globals(c, mod);
}

/* Compile interface. Returns a function that evaluates the
 * given AST. Returns NULL if there was an error during compilation. */
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form) {
    FormOptions opts = form_options_default();
    GstFuncDef *def;
    if (setjmp(c->onError)) {
        /* Clear all but root scope */
        c->trackers = NULL;
        if (c->tail)
            c->tail->parent = NULL;
        if (c->error == NULL)
            c->error = "unknown error";
        return NULL;
    }
    /* Create a scope */
    opts.isTail = 1;
    compiler_return(c, compile_value(c, opts, form));
    def = compiler_gen_funcdef(c, c->buffer->count, 0, 0);
    {
        GstFuncEnv *env = gst_alloc(c->vm, sizeof(GstFuncEnv));
        GstFunction *func = gst_alloc(c->vm, sizeof(GstFunction));
        env->values = NULL;
        env->stackOffset = 0;
        env->thread = NULL;
        func->parent = NULL;
        func->def = def;
        func->env = env;
        return func;
    }
}

/***/
/* Stl */
/***/

/* GC mark all memory used by the compiler */
static void gst_compiler_mark(Gst *vm, void *data, uint32_t len) {
    SlotTracker *st;
    GstScope *scope;
	GstCompiler *c = (GstCompiler *) data;
	if (len != sizeof(GstCompiler))
    	return;
    /* Mark compiler */
    gst_mark_value(vm, gst_wrap_buffer(c->buffer));
    /* Mark trackers - the trackers themselves are all on the stack. */
    st = (SlotTracker *) c->trackers;
    while (st) {
        if (st->slots)
            gst_mark_mem(vm, st->slots);
		st = st->next;
    }
    /* Mark scopes */
    scope = c->tail;
    while (scope) {
        gst_mark_mem(vm, scope);
        if (scope->freeHeap)
            gst_mark_mem(vm, scope->freeHeap);
        gst_mark_value(vm, gst_wrap_array(scope->literalsArray));
        gst_mark_value(vm, gst_wrap_table(scope->locals));
        gst_mark_value(vm, gst_wrap_table(scope->literals));
        gst_mark_value(vm, gst_wrap_table(scope->namedLiterals));
        gst_mark_value(vm, gst_wrap_table(scope->nilNamedLiterals));
		scope = scope->parent;
    }
}

/* Compiler userdata type */
static const GstUserType gst_stl_compilertype = {
	"std.compiler",
	NULL,
	NULL,
    NULL,
	&gst_compiler_mark
};

/* Create a compiler userdata */
static int gst_stl_compiler(Gst *vm) {
	GstCompiler *c = gst_userdata(vm, sizeof(GstCompiler), &gst_stl_compilertype);
	gst_compiler(c, vm);
	gst_c_return(vm, gst_wrap_userdata(c));
}

/* Add a binding to the compiler's current scope. */
static int gst_stl_compiler_binding(Gst *vm) {
	GstCompiler *c = gst_check_userdata(vm, 0, &gst_stl_compilertype);
    GstScope *scope;
    GstValue sym;
	const uint8_t *data;
	uint32_t len;
	if (!c)
    	gst_c_throwc(vm, "expected compiler");
    if (!gst_chararray_view(gst_arg(vm, 1), &data, &len))
        gst_c_throwc(vm, "expected string/buffer");
    scope = c->tail;
    sym = gst_wrap_string(gst_string_b(c->vm, data, len));
    gst_table_put(c->vm, scope->namedLiterals, sym, gst_arg(vm, 2));
    gst_c_return(vm, gst_wrap_userdata(c));
}

/* Compile a value */
static int gst_stl_compiler_compile(Gst *vm) {
    GstFunction *ret;
	GstCompiler *c = gst_check_userdata(vm, 0, &gst_stl_compilertype);
	if (!c)
    	gst_c_throwc(vm, "expected compiler");
    ret = gst_compiler_compile(c, gst_arg(vm, 1));
    if (ret == NULL)
        gst_c_throwc(vm, c->error);
    gst_c_return(vm, gst_wrap_function(ret));
}

/* Use an environment during compilation. Names that are declared more than
 * once should use their final declared value. */
static int gst_stl_compiler_bindings(Gst *vm) {
    GstValue env;
    GstCompiler *c = gst_check_userdata(vm, 0, &gst_stl_compilertype);
    if (!c)
        gst_c_throwc(vm, "expected compiler");
    env = gst_arg(vm, 1);
    if (env.type != GST_TABLE && env.type != GST_STRUCT)
        gst_c_throwc(vm, "expected table/struct");
    gst_compiler_globals(c, env);
    gst_c_return(vm, gst_wrap_userdata(c));
}

/* The module stuff */
static const GstModuleItem gst_compile_module[] = {
    {"compiler", gst_stl_compiler},
    {"compile", gst_stl_compiler_compile},
    {"binding!", gst_stl_compiler_binding},
    {"bindings!", gst_stl_compiler_bindings},
    {NULL, NULL}
};

/* Load compiler library */
void gst_compile_load(Gst *vm) {
    gst_module_put(vm, "std.compile", gst_cmodule_struct(vm, gst_compile_module));
}

#include "compile.h"
#include "ds.h"
#include "value.h"
#include "vm.h"
#include <string.h>

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
     * can be allocated with CompilerGetLocal. */
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
     * anything will try to return bil slots. */
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
    Slot * slots;
    uint32_t count;
    uint32_t capacity;
};

/* A Scope is a lexical scope in the program. It is
 * responsible for aliasing programmer facing names to
 * Slots and for keeping track of literals. It also
 * points to the parent Scope, and its current child
 * Scope. */
struct Scope {
    uint32_t level;
    uint16_t nextLocal;
    uint16_t frameSize;
    uint32_t heapCapacity;
    uint32_t heapSize;
    uint16_t * freeHeap;
    Dictionary * literals;
    Array * literalsArray;
    Dictionary * locals;
    Scope * parent;
};

/* Provides default FormOptions */
static FormOptions FormOptionsDefault() {
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
BufferDefine(UInt32, uint32_t)
BufferDefine(Int32, int32_t)
BufferDefine(Number, Number)
BufferDefine(UInt16, uint16_t)
BufferDefine(Int16, int16_t)

/* If there is an error during compilation,
 * jump back to start */
static void CError(Compiler * c, const char * e) {
    c->error = e;
    longjmp(c->onError, 1);
}

/* Push a new scope in the compiler and return
 * a pointer to it for configuration. There is
 * more configuration that needs to be done if
 * the new scope is a function declaration. */
static Scope * CompilerPushScope(Compiler * c, int sameFunction) {
    Scope * scope = VMAlloc(c->vm, sizeof(Scope));
    scope->locals = DictNew(c->vm, 10);
    scope->freeHeap = VMAlloc(c->vm, 10 * sizeof(uint16_t));
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
            CError(c, "Cannot inherit scope when root scope");
        }
        scope->nextLocal = c->tail->nextLocal;
        scope->literals = c->tail->literals;
        scope->literalsArray = c->tail->literalsArray;
    } else {
        scope->nextLocal = 0;
        scope->literals = DictNew(c->vm, 10);
        scope->literalsArray = ArrayNew(c->vm, 10);
    }
    c->tail = scope;
    return scope;
}

/* Remove the inner most scope from the compiler stack */
static void CompilerPopScope(Compiler * c) {
    Scope * last = c->tail;
    if (last == NULL) {
        CError(c, "No scope to pop.");
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
static uint16_t CompilerGetLocal(Compiler * c, Scope * scope) {
    if (scope->heapSize == 0) {
        if (scope->nextLocal + 1 == 0) {
            CError(c, "Too many local variables. Try splitting up your functions :)");
        }
        return scope->nextLocal++;
    } else {
        return scope->freeHeap[--scope->heapSize];
    }
    return 0;
}

/* Free a slot on the stack for other locals and/or
 * intermediate values */
static void CompilerFreeLocal(Compiler * c, Scope * scope, uint16_t slot) {
    /* Ensure heap has space */
    if (scope->heapSize >= scope->heapCapacity) {
        uint32_t newCap = 2 * scope->heapSize;
        uint16_t * newData = VMAlloc(c->vm, newCap * sizeof(uint16_t));
        memcpy(newData, scope->freeHeap, scope->heapSize * sizeof(uint16_t));
        scope->freeHeap = newData;
        scope->heapCapacity = newCap;
    }
    scope->freeHeap[scope->heapSize++] = slot;
}

/* Initializes a SlotTracker. SlotTrackers
 * are used during compilation to free up slots on the stack
 * after they are no longer needed. */
static void CompilerTrackerInit(Compiler * c, SlotTracker * tracker) {
    tracker->slots = VMAlloc(c->vm, 10 * sizeof(Slot));
    tracker->count = 0;
    tracker->capacity = 10;
}

/* Free up a slot if it is a temporary slot (does not
 * belong to a named local). If the slot does belong
 * to a named variable, does nothing. */
static void CompilerDropSlot(Compiler * c, Scope * scope, Slot slot) {
    if (!slot.isNil && slot.isTemp) {
        CompilerFreeLocal(c, scope, slot.index);
    }
}

/* Helper function to return a slot. Useful for compiling things that return
 * nil. (set, while, etc.). Use this to wrap compilation calls that need
 * to return things. */
static Slot CompilerReturn(Compiler * c, Slot slot) {
    Slot ret;
    ret.hasReturned = 1;
    ret.isNil = 1;
    if (slot.hasReturned) {
        /* Do nothing */
    } else if (slot.isNil) {
        /* Return nil */
        BufferPushUInt16(c->vm, c->buffer, VM_OP_RTN);
    } else {
        /* Return normal value */
        BufferPushUInt16(c->vm, c->buffer, VM_OP_RET);
        BufferPushUInt16(c->vm, c->buffer, slot.index);
    }
    return ret;
}

/* Gets a temporary slot for the bottom-most scope. */
static Slot CompilerGetTemp(Compiler * c) {
    Scope * scope = c->tail;
    Slot ret;
    ret.isTemp = 1;
    ret.isNil = 0;
    ret.hasReturned = 0;
    ret.index = CompilerGetLocal(c, scope);
    return ret;
}

/* Return a slot that is the target Slot given some FormOptions. Will
 * Create a temporary slot if needed, so be sure to drop the slot after use. */
static Slot CompilerGetTarget(Compiler * c, FormOptions opts) {
    if (opts.canChoose) {
        return CompilerGetTemp(c);
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
static Slot CompilerRealizeSlot(Compiler * c, Slot slot) {
    if (slot.isNil) {
        slot = CompilerGetTemp(c);
        BufferPushUInt16(c->vm, c->buffer, VM_OP_NIL);
        BufferPushUInt16(c->vm, c->buffer, slot.index);
    }
    return slot;
}

/* Helper to get a nil slot */
static Slot NilSlot() { Slot ret; ret.isNil = 1; return ret; }

/* Writes all of the slots in the tracker to the compiler */
static void CompilerTrackerWrite(Compiler * c, SlotTracker * tracker, int reverse) {
    uint32_t i;
    Buffer * buffer = c->buffer;
    for (i = 0; i < tracker->count; ++i) {
        Slot s;
        if (reverse)
            s = tracker->slots[tracker->count - 1 - i];
        else
            s = tracker->slots[i];
        if (s.isNil)
            CError(c, "Trying to write nil slot.");
        BufferPushUInt16(c->vm, buffer, s.index);
    }
}

/* Free the tracker after creation. This unlocks the memory
 * that was allocated by the GC an allows it to be collected. Also
 * frees slots that were tracked by this tracker in the given scope. */
static void CompilerTrackerFree(Compiler * c, Scope * scope, SlotTracker * tracker) {
    uint32_t i;
    /* Free in reverse order */
    for (i = tracker->count - 1; i < tracker->count; --i) {
        CompilerDropSlot(c, scope, tracker->slots[i]);
    }
}

/* Add a new Slot to a slot tracker. */
static void CompilerTrackerPush(Compiler * c, SlotTracker * tracker, Slot slot) {
    if (tracker->count >= tracker->capacity) {
        uint32_t newCap = 2 * tracker->count;
        Slot * newData = VMAlloc(c->vm, newCap * sizeof(Slot));
        memcpy(newData, tracker->slots, tracker->count * sizeof(Slot));
        tracker->slots = newData;
        tracker->capacity = newCap;
    }
    tracker->slots[tracker->count++] = slot;
}

/* Registers a literal in the given scope. If an equal literal is found, uses
 * that one instead of creating a new literal. This allows for some reuse
 * of things like string constants.*/
static uint16_t CompilerAddLiteral(Compiler * c, Scope * scope, Value x) {
    Value checkDup = DictGet(scope->literals, x);
    uint16_t literalIndex = 0;
    if (checkDup.type != TYPE_NIL) {
        /* An equal literal is already registered in the current scope */
        return (uint16_t) checkDup.data.number;
    } else {
        /* Add our literal for tracking */
        Value valIndex;
        valIndex.type = TYPE_NUMBER;
        literalIndex = scope->literalsArray->count;
        valIndex.data.number = literalIndex;
        DictPut(c->vm, scope->literals, x, valIndex);
        ArrayPush(c->vm, scope->literalsArray, x);
    }
    return literalIndex;
}

/* Declare a symbol in a given scope. */
static uint16_t CompilerDeclareSymbol(Compiler * c, Scope * scope, Value sym) {
    if (sym.type != TYPE_STRING) {
        CError(c, "Expected symbol");
    }
    Value x;
    uint16_t target = CompilerGetLocal(c, scope);
    x.type = TYPE_NUMBER;
    x.data.number = target;
    DictPut(c->vm, scope->locals, sym, x);
    return target;
}

/* Try to resolve a symbol. If the symbol can be resovled, return true and
 * pass back the level and index by reference. */
static int ScopeSymbolResolve(Scope * scope, Value x,
        uint16_t * level, uint16_t * index) {
    uint32_t currentLevel = scope->level;
    while (scope) {
        Value check = DictGet(scope->locals, x);
        if (check.type != TYPE_NIL) {
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
 * to the targtet. If target < 0, the Compiler can choose whatever
 * slot location it likes. If, for example, a symbol resolves to
 * whatever is in a given slot, it makes sense to use that location
 * to 'return' the value. For other expressions, like function
 * calls, the compiler will just pick the lowest free slot
 * as the location on the stack. */
static Slot CompileValue(Compiler * c, FormOptions opts, Value x);

/* Compile a structure that evaluates to a literal value. Useful
 * for objects like strings, or anything else that cannot be instatiated else {
 break;
 }
 * from bytecode and doesn't do anything in the AST. */
static Slot CompileLiteral(Compiler * c, FormOptions opts, Value x) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (opts.resultUnused) return NilSlot();
    ret = CompilerGetTarget(c, opts);
    literalIndex = CompilerAddLiteral(c, scope, x);
    BufferPushUInt16(c->vm, buffer, VM_OP_CST);
    BufferPushUInt16(c->vm, buffer, ret.index);
    BufferPushUInt16(c->vm, buffer, literalIndex);
    return ret;
}

/* Compile boolean, nil, and number values. */
static Slot CompileNonReferenceType(Compiler * c, FormOptions opts, Value x) {
    Buffer * buffer = c->buffer;
    Slot ret;
    if (opts.resultUnused) return NilSlot();
    ret = CompilerGetTarget(c, opts);
    if (x.type == TYPE_NIL) {
        BufferPushUInt16(c->vm, buffer, VM_OP_NIL);
        BufferPushUInt16(c->vm, buffer, ret.index);
    } else if (x.type == TYPE_BOOLEAN) {
        BufferPushUInt16(c->vm, buffer, x.data.boolean ? VM_OP_TRU : VM_OP_FLS);
        BufferPushUInt16(c->vm, buffer, ret.index);
    } else if (x.type == TYPE_NUMBER) {
        Number number = x.data.number;
        int32_t int32Num = (int32_t) number;
        if (number == (Number) int32Num) {
            if (int32Num <= 32767 && int32Num >= -32768) {
                int16_t int16Num = (int16_t) number;
                BufferPushUInt16(c->vm, buffer, VM_OP_I16);
                BufferPushUInt16(c->vm, buffer, ret.index);
                BufferPushInt16(c->vm, buffer, int16Num);
            } else {
                BufferPushUInt16(c->vm, buffer, VM_OP_I32);
                BufferPushUInt16(c->vm, buffer, ret.index);
                BufferPushInt32(c->vm, buffer, int32Num);
            }
        } else {
            BufferPushUInt16(c->vm, buffer, VM_OP_F64);
            BufferPushUInt16(c->vm, buffer, ret.index);
            BufferPushNumber(c->vm, buffer, number);
        }
    } else {
        CError(c, "Expected boolean, nil, or number type.");
    }
    return ret;
}

/* Compile a symbol. Resolves any kind of symbol. */
static Slot CompileSymbol(Compiler * c, FormOptions opts, Value sym) {
    Buffer * buffer = c->buffer;
    Scope * scope = c->tail;
    uint16_t index = 0;
    uint16_t level = 0;
    Slot ret;
    if (opts.resultUnused) return NilSlot();
    if (!ScopeSymbolResolve(scope, sym, &level, &index))
        CError(c, "Undefined symbol");
    if (level > 0) {
        /* We have an upvalue */
        ret = CompilerGetTarget(c, opts);
        BufferPushUInt16(c->vm, buffer, VM_OP_UPV);
        BufferPushUInt16(c->vm, buffer, ret.index);
        BufferPushUInt16(c->vm, buffer, level);
        BufferPushUInt16(c->vm, buffer, index);
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
            BufferPushUInt16(c->vm, buffer, VM_OP_MOV);
            BufferPushUInt16(c->vm, buffer, ret.index);
            BufferPushUInt16(c->vm, buffer, index);
        }
    }
    return ret;
}

/* Compile values in an array sequentail and track the returned slots.
 * If the result is unused, immediately drop slots we don't need. Can
 * also ignore the end of an array. */
static void CompilerTrackerInitArray(Compiler * c, FormOptions opts, 
        SlotTracker * tracker, Array * array, uint32_t start, uint32_t fromEnd) {
    Scope * scope = c->tail;
    FormOptions subOpts = FormOptionsDefault();
    uint32_t i;
    /* Calculate sub flags */
    subOpts.resultUnused = opts.resultUnused;
    /* Compile all of the arguments */
    CompilerTrackerInit(c, tracker);
    /* Nothing to compile */
    if (array->count <= fromEnd) return;
    /* Compile body of array */
    for (i = start; i < (array->count - fromEnd); ++i) {
        Slot slot = CompileValue(c, subOpts, array->data[i]);
        if (subOpts.resultUnused)
            CompilerDropSlot(c, scope, slot);
        else
            CompilerTrackerPush(c, tracker, CompilerRealizeSlot(c, slot));
    }
}

/* Compile a special form in the form of an operator. There
 * are four choices for opcodes - when the operator is called
 * with 0, 1, 2, or n arguments. When the operator form is
 * called with n arguments, the number of arguments is written
 * after the op code, followed by those arguments.
 *
 * This makes a few assumptions about the operators. One, no side
 * effects. With this assumptions, if the result of the operator
 * is unused, it's calculation can be ignored (the evaluation of
 * its argument is still carried out, but their results can
 * also be ignored). */
static Slot CompileOperator(Compiler * c, FormOptions opts, Array * form,
        int16_t op0, int16_t op1, int16_t op2, int16_t opn, int reverseOperands) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret;
    SlotTracker tracker;
    /* Compile operands */
    CompilerTrackerInitArray(c, opts, &tracker, form, 1, 0);
    /* Free up space */
    CompilerTrackerFree(c, scope, &tracker);
    if (opts.resultUnused) {
        ret = NilSlot();
    } else {
        ret = CompilerGetTarget(c, opts);
        /* Write the correct opcode */
        if (form->count < 2) {
            if (op0 < 0) {
                if (opn < 0) CError(c, "This operator does not take 0 arguments.");
                goto opn;
            } else {
                BufferPushUInt16(c->vm, buffer, op0);
                BufferPushUInt16(c->vm, buffer, ret.index);
            }
        } else if (form->count == 2) {
            if (op1 < 0) {
                if (opn < 0) CError(c, "This operator does not take 1 argument.");
                goto opn;
            } else {
                BufferPushUInt16(c->vm, buffer, op1);
                BufferPushUInt16(c->vm, buffer, ret.index);
            }
        } else if (form->count == 3) {
            if (op2 < 0) {
                if (opn < 0) CError(c, "This operator does not take 2 arguments.");
                goto opn;
            } else {
                BufferPushUInt16(c->vm, buffer, op2);
                BufferPushUInt16(c->vm, buffer, ret.index);
            }
        } else {
            opn:
            if (opn < 0) CError(c, "This operator does not take n arguments.");
            BufferPushUInt16(c->vm, buffer, opn);
            BufferPushUInt16(c->vm, buffer, ret.index);
            BufferPushUInt16(c->vm, buffer, form->count - 1);
        }
    }
    /* Write the location of all of the arguments */
    CompilerTrackerWrite(c, &tracker, reverseOperands);
    return ret;
}

/* Math specials */
static Slot CompileAddition(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD0, -1, VM_OP_ADD, VM_OP_ADM, 0);
}
static Slot CompileSubtraction(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD0, -1, VM_OP_SUB, VM_OP_SBM, 0);
}
static Slot CompileMultiplication(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD1, -1, VM_OP_MUL, VM_OP_MUM, 0);
}
static Slot CompileDivision(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD1, -1, VM_OP_DIV, VM_OP_DVM, 0);
}
static Slot CompileEquals(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_TRU, VM_OP_TRU, VM_OP_EQL, -1, 0);
}
static Slot CompileLessThan(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_TRU, VM_OP_TRU, VM_OP_LTN, -1, 0);
}
static Slot CompileLessThanOrEqual(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_TRU, VM_OP_TRU, VM_OP_LTE, -1, 0);
}
static Slot CompileGreaterThan(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_TRU, VM_OP_TRU, VM_OP_LTN, -1, 1);
}
static Slot CompileGreaterThanOrEqual(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_TRU, VM_OP_TRU, VM_OP_LTE, -1, 1);
}
static Slot CompileNot(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_FLS, VM_OP_NOT, -1, -1, 0);
}
static Slot CompileGet(Compiler * c, FormOptions opts, Array * form) {
	return CompileOperator(c, opts, form, -1, -1, VM_OP_GET, -1, 0);
}
static Slot CompileArray(Compiler * c, FormOptions opts, Array * form) {
	return CompileOperator(c, opts, form, -1, -1, -1, VM_OP_ARR, 0);
}
static Slot CompileDict(Compiler * c, FormOptions opts, Array * form) {
    if ((form->count % 2) == 0) {
        CError(c, "Dictionary literal requires an even number of arguments");
        return NilSlot();
    } else {
    	return CompileOperator(c, opts, form, -1, -1, -1, VM_OP_DIC, 0);
    }
}

/* Associative set */
static Slot CompileSet(Compiler * c, FormOptions opts, Array * form) {
    Buffer * buffer = c->buffer;
    FormOptions subOpts = FormOptionsDefault();
    Slot ds, key, val;
    if (form->count != 4) CError(c, "Set expects 4 arguments");
    if (opts.resultUnused) {
        ds = CompilerRealizeSlot(c, CompileValue(c, subOpts, form->data[1]));
    } else {
        subOpts = opts;
        subOpts.isTail = 0;
        ds = CompilerRealizeSlot(c, CompileValue(c, subOpts, form->data[1]));
        subOpts = FormOptionsDefault();
    }
    key = CompilerRealizeSlot(c, CompileValue(c, subOpts, form->data[2]));
   	val = CompilerRealizeSlot(c, CompileValue(c, subOpts, form->data[3]));
    BufferPushUInt16(c->vm, buffer, VM_OP_SET);
    BufferPushUInt16(c->vm, buffer, ds.index);
    BufferPushUInt16(c->vm, buffer, key.index);
    BufferPushUInt16(c->vm, buffer,	val.index);
    CompilerDropSlot(c, c->tail, key);
    CompilerDropSlot(c, c->tail, val);
    if (opts.resultUnused) {
        CompilerDropSlot(c, c->tail, ds);
        return NilSlot();
    } else {
		return ds;
    }
}

/* Compile an assignment operation */
static Slot CompileAssign(Compiler * c, FormOptions opts, Value left, Value right) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    FormOptions subOpts;
    uint16_t target = 0;
    uint16_t level = 0;
    Slot slot;
    subOpts.isTail = 0;
    subOpts.resultUnused = 0;
    if (ScopeSymbolResolve(scope, left, &level, &target)) {
        /* Check if we have an up value. Otherwise, it's just a normal
         * local variable */
        if (level != 0) {
            subOpts.canChoose = 1;
            /* Evaluate the right hand side */
            slot = CompilerRealizeSlot(c, CompileValue(c, subOpts, right));
            /* Set the up value */
            BufferPushUInt16(c->vm, buffer, VM_OP_SUV);
            BufferPushUInt16(c->vm, buffer, slot.index);
            BufferPushUInt16(c->vm, buffer, level);
            BufferPushUInt16(c->vm, buffer, target);
        } else {
            /* Local variable */
            subOpts.canChoose = 0;
            subOpts.target = target;
            slot = CompileValue(c, subOpts, right);
        }
    } else {
        /* We need to declare a new symbol */
        subOpts.target = CompilerDeclareSymbol(c, scope, left);
        subOpts.canChoose = 0;
        slot = CompileValue(c, subOpts, right);
    }
    if (opts.resultUnused) {
        CompilerDropSlot(c, scope, slot);
        return NilSlot();
    } else {
        return slot;
    }
}

/* Compile series of expressions. This compiles the meat of
 * function definitions and the inside of do forms. */
static Slot CompileBlock(Compiler * c, FormOptions opts, Array * form, uint32_t startIndex) {
    Scope * scope = c->tail;
    FormOptions subOpts = FormOptionsDefault();
    uint32_t current = startIndex;
    /* Check for empty body */
    if (form->count <= startIndex) return NilSlot();
    /* Compile the body */
    subOpts.resultUnused = 1;
    subOpts.isTail = 0;
    subOpts.canChoose = 1;
    while (current < form->count - 1) {
        CompilerDropSlot(c, scope, CompileValue(c, subOpts, form->data[current]));
        ++current;
    }
    /* Compile the last expression in the body */
    return CompileValue(c, opts, form->data[form->count - 1]);
}

/* Extract the last n bytes from the buffer and use them to construct
 * a function definition. */
static FuncDef * CompilerGenFuncDef(Compiler * c, uint32_t lastNBytes, uint32_t arity) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    FuncDef * def = VMAlloc(c->vm, sizeof(FuncDef));
    /* Create enough space for the new byteCode */
    if (lastNBytes > buffer->count)
        CError(c, "Trying to extract more bytes from buffer than in buffer.");
    uint8_t * byteCode = VMAlloc(c->vm, lastNBytes);
    def->byteCode = (uint16_t *) byteCode;
    def->byteCodeLen = lastNBytes / 2;
    /* Copy the last chunk of bytes in the buffer into the new
     * memory for the function's byteCOde */
    memcpy(byteCode, buffer->data + buffer->count - lastNBytes, lastNBytes);
    /* Remove the byteCode from the end of the buffer */
    buffer->count -= lastNBytes;
    /* Create the literals used by this function */
    if (scope->literalsArray->count) {
        def->literals = VMAlloc(c->vm, scope->literalsArray->count * sizeof(Value));
        memcpy(def->literals, scope->literalsArray->data,
                scope->literalsArray->count * sizeof(Value));
    } else {
        def->literals = NULL;
    }
    def->literalsLen = scope->literalsArray->count;
    /* Delete the sub scope */
    CompilerPopScope(c);
    /* Initialize the new FuncDef */
    def->locals = scope->frameSize;
    def->arity = arity;
    return def;
}

/* Compile a function from a function literal */
static Slot CompileFunction(Compiler * c, FormOptions opts, Array * form) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    uint32_t current = 1;
    uint32_t i;
    uint32_t sizeBefore; /* Size of buffer before compiling function */
    Scope * subScope;
    Array * params;
    FormOptions subOpts = FormOptionsDefault();
    Slot ret;
    if (opts.resultUnused) return NilSlot();
    ret = CompilerGetTarget(c, opts);
    subScope = CompilerPushScope(c, 0);
    /* Check for function documentation - for now just ignore. */
    if (form->data[current].type == TYPE_STRING)
        ++current;
    /* Define the function parameters */
    if (form->data[current].type != TYPE_ARRAY)
        CError(c, "Expected function arguments");
    params = form->data[current++].data.array;
    for (i = 0; i < params->count; ++i) {
        Value param = params->data[i];
        if (param.type != TYPE_STRING)
            CError(c, "Function parameters should be symbols");
        /* The compiler puts the parameter locals
         * in the right place by default - at the beginning
         * of the stack frame. */
        CompilerDeclareSymbol(c, subScope, param);
    }
    /* Mark where we are on the stack so we can
     * return to it later. */
    sizeBefore = buffer->count;
    /* Compile the body in the subscope */
    subOpts.isTail = 1;
    CompilerReturn(c, CompileBlock(c, subOpts, form, current));
    /* Create a new FuncDef as a constant in original scope by splicing
     * out the relevant code from the buffer. */
    {
        Value newVal;
        uint16_t literalIndex;
        FuncDef * def = CompilerGenFuncDef(c, buffer->count - sizeBefore, params->count);
        /* Add this FuncDef as a literal in the outer scope */
        newVal.type = TYPE_NIL;
        newVal.data.pointer = def;
        literalIndex = CompilerAddLiteral(c, scope, newVal);
        BufferPushUInt16(c->vm, buffer, VM_OP_CLN);
        BufferPushUInt16(c->vm, buffer, ret.index);
        BufferPushUInt16(c->vm, buffer, literalIndex);
    }
    return ret;
}

/* Branching special */
static Slot CompileIf(Compiler * c, FormOptions opts, Array * form) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    FormOptions condOpts = opts;
    FormOptions branchOpts = opts;
    Slot left, right, condition;
    uint32_t countAtJumpIf;
    uint32_t countAtJump;
    uint32_t countAfterFirstBranch;
    /* Check argument count */
    if (form->count < 3 || form->count > 4)
        CError(c, "if takes either 2 or 3 arguments");
    /* Compile the condition */
    condOpts.isTail = 0;
    condOpts.resultUnused = 0;
    condition = CompileValue(c, condOpts, form->data[1]);
    /* If the condition is nil, just compile false path */
    if (condition.isNil) {
        if (form->count == 4) {
            return CompileValue(c, opts, form->data[3]);
        }
        return condition;
    }
    /* Mark where the buffer is now so we can write the jump
     * length later */
    countAtJumpIf = buffer->count;
    /* Write jump instruction. Will later be replaced with correct index. */
    BufferPushUInt16(c->vm, buffer, VM_OP_JIF);
    BufferPushUInt16(c->vm, buffer, condition.index);
    BufferPushUInt32(c->vm, buffer, 0);
    /* Configure branch form options */
    branchOpts.canChoose = 0;
    branchOpts.target = condition.index;
    /* Compile true path */
    left = CompileValue(c, branchOpts, form->data[2]);
    if (opts.isTail) {
        CompilerReturn(c, left);
    } else {
        /* If we need to jump again, do so */
        if (form->count == 4) {
            countAtJump = buffer->count;
            BufferPushUInt16(c->vm, buffer, VM_OP_JMP);
            BufferPushUInt32(c->vm, buffer, 0);
        }
    }
    CompilerDropSlot(c, scope, left);
    /* Reinsert jump with correct index */
    countAfterFirstBranch = buffer->count;
    buffer->count = countAtJumpIf;
    BufferPushUInt16(c->vm, buffer, VM_OP_JIF);
    BufferPushUInt16(c->vm, buffer, condition.index);
    BufferPushUInt32(c->vm, buffer, (countAfterFirstBranch - countAtJumpIf) / 2);
    buffer->count = countAfterFirstBranch;
    /* Compile false path */
    if (form->count == 4) {
        right = CompileValue(c, branchOpts, form->data[3]);
        if (opts.isTail) CompilerReturn(c, right);
        CompilerDropSlot(c, scope, right);
    } else if (opts.isTail) {
        CompilerReturn(c, condition);
    }
    /* Reset the second jump length */
    if (!opts.isTail && form->count == 4) {
        countAfterFirstBranch = buffer->count;
        buffer->count = countAtJump;
        BufferPushUInt16(c->vm, buffer, VM_OP_JMP);
        BufferPushUInt32(c->vm, buffer, (countAfterFirstBranch - countAtJump) / 2);
        buffer->count = countAfterFirstBranch;
    }
    if (opts.isTail)
        condition.hasReturned = 1;
    return condition;
}

/* While special */
static Slot CompileWhile(Compiler * c, FormOptions opts, Array * form) {
    Slot cond;
    uint32_t countAtStart = c->buffer->count;
    uint32_t countAtJumpDelta;
    uint32_t countAtFinish;
    FormOptions defaultOpts = FormOptionsDefault();
    CompilerPushScope(c, 1);
    /* Compile condition */
    cond = CompileValue(c, defaultOpts, form->data[1]);
    /* Assert that cond is a real value - otherwise do nothing (nil is false,
     * so loop never runs.) */
    if (cond.isNil) return cond;
    /* Leave space for jump later */
    countAtJumpDelta = c->buffer->count;
    c->buffer->count += sizeof(uint16_t) * 2 + sizeof(int32_t);
    /* Compile loop body */
    defaultOpts.resultUnused = 1;
    CompilerDropSlot(c, c->tail, CompileBlock(c, defaultOpts, form, 2));
    /* Jump back to the loop start */
    countAtFinish = c->buffer->count;
    BufferPushUInt16(c->vm, c->buffer, VM_OP_JMP);
    BufferPushInt32(c->vm, c->buffer, (int32_t)(countAtFinish - countAtStart) / -2);
    countAtFinish = c->buffer->count;
    /* Set the jump to the correct length */
    c->buffer->count = countAtJumpDelta;
    BufferPushUInt16(c->vm, c->buffer, VM_OP_JIF);
    BufferPushUInt16(c->vm, c->buffer, cond.index);
    BufferPushInt32(c->vm, c->buffer, (int32_t)(countAtFinish - countAtJumpDelta) / 2);
    /* Pop scope */
    c->buffer->count = countAtFinish;
    CompilerPopScope(c);
    /* Return nil */
    if (opts.resultUnused)
        return NilSlot();
    else
        return cond;
}

/* Do special */
static Slot CompileDo(Compiler * c, FormOptions opts, Array * form) {
    Slot ret;
    CompilerPushScope(c, 1);
    ret = CompileBlock(c, opts, form, 1);
    CompilerPopScope(c);
    return ret;
}

/* Quote special - returns its argument as is. */
static Slot CompileQuote(Compiler * c, FormOptions opts, Array * form) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret;
    uint16_t literalIndex;
    if (form->count != 2)
        CError(c, "Quote takes exactly 1 argument.");
    Value x = form->data[1];
    if (x.type == TYPE_NIL ||
            x.type == TYPE_BOOLEAN ||
            x.type == TYPE_NUMBER) {
        return CompileNonReferenceType(c, opts, x);
    }
    if (opts.resultUnused) return NilSlot();
    ret = CompilerGetTarget(c, opts);
    literalIndex = CompilerAddLiteral(c, scope, x);
    BufferPushUInt16(c->vm, buffer, VM_OP_CST);
    BufferPushUInt16(c->vm, buffer, ret.index);
    BufferPushUInt16(c->vm, buffer, literalIndex);
    return ret;
}

/* Assignment special */
static Slot CompileVar(Compiler * c, FormOptions opts, Array * form) {
    if (form->count != 3)
        CError(c, "Assignment expects 2 arguments");
    return CompileAssign(c, opts, form->data[1], form->data[2]);
}

/* Define a function type for Special Form helpers */
typedef Slot (*SpecialFormHelper) (Compiler * c, FormOptions opts, Array * form);

/* Dispatch to a special form */
static SpecialFormHelper GetSpecial(Array * form) {
    uint8_t * name;
    if (form->count < 1 || form->data[0].type != TYPE_STRING)
        return NULL;
    name = form->data[0].data.string;
    /* If we have a symbol with a zero length name, we have other
     * problems. */
    if (VStringSize(name) == 0)
        return NULL;
    /* One character specials. Mostly math. */
    if (VStringSize(name) == 1) {
        switch(name[0]) {
            case '+': return CompileAddition;
            case '-': return CompileSubtraction;
            case '*': return CompileMultiplication;
            case '/': return CompileDivision;
            case '>': return CompileGreaterThan;
            case '<': return CompileLessThan;
            case '=': return CompileEquals;
            default:
                       break;
        }
    }
    /* Multi character specials. Mostly control flow. */
    switch (name[0]) {
        case '>':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == '=') {
                    return CompileGreaterThanOrEqual;
                }
            }
            break;
        case '<':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == '=') {
                    return CompileLessThanOrEqual;
                }
            }
            break;
        case 'a':
        	{
            	if (VStringSize(name) == 5 &&
                	    name[1] == 'r' &&
                	    name[2] == 'r' &&
                	    name[3] == 'a' &&
                	    name[4] == 'y') {
					return CompileArray;
        	    }
    	    }
        case 'g':
            {
				if (VStringSize(name) == 3 &&
    				    name[1] == 'e' &&
    				    name[2] == 't') {
					return CompileGet;
			    }
            }
        case 'd':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == 'o') {
                    return CompileDo;
                } else if (VStringSize(name) == 4 &&
						name[1] == 'i' &&
						name[2] == 'c' &&
						name[3] == 't') {
					return CompileDict;
                }
            }
            break;
        case 'i':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == 'f') {
                    return CompileIf;
                }
            }
            break;
        case 'f':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == 'n') {
                    return CompileFunction;
                }
            }
            break;
        case 'n':
            {
                if (VStringSize(name) == 3 &&
                        name[1] == 'o' &&
                        name[2] == 't') {
                    return CompileNot;
                }
            }
        case 'q':
            {
                if (VStringSize(name) == 5 &&
                        name[1] == 'u' &&
                        name[2] == 'o' &&
                        name[3] == 't' &&
                        name[4] == 'e') {
                    return CompileQuote;
                }
            }
            break;
        case 's':
            {
                if (VStringSize(name) == 3 &&
                        name[1] == 'e' &&
                        name[2] == 't') {
                    return CompileSet;
                }
            }
            break;
        case 'w':
            {
                if (VStringSize(name) == 5 &&
                        name[1] == 'h' &&
                        name[2] == 'i' &&
                        name[3] == 'l' &&
                        name[4] == 'e') {
                    return CompileWhile;
                }
            }
            break;
        case ':':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == '=') {
                    return CompileVar;
                }
            }
            break;
        default:
            break;
    }
    return NULL;
}

/* Compile a form. Checks for special forms and macros. */
static Slot CompileForm(Compiler * c, FormOptions opts, Array * form) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    SpecialFormHelper helper;
    /* Empty forms evaluate to nil. */
    if (form->count == 0) {
        Value temp;
        temp.type = TYPE_NIL;
        return CompileNonReferenceType(c, opts, temp);
    }
    /* Check and handle special forms */
    helper = GetSpecial(form);
    if (helper != NULL) {
        return helper(c, opts, form);
    } else {
        Slot ret, callee;
        SlotTracker tracker;
        FormOptions subOpts = FormOptionsDefault();
        uint32_t i;
        CompilerTrackerInit(c, &tracker);
        /* Compile function to be called */
        callee = CompilerRealizeSlot(c, CompileValue(c, subOpts, form->data[0]));
        /* Compile all of the arguments */
        for (i = 1; i < form->count; ++i) {
            Slot slot = CompileValue(c, subOpts, form->data[i]);
            CompilerTrackerPush(c, &tracker, slot);
        }
        /* Free up some slots */
        CompilerDropSlot(c, scope, callee);
        CompilerTrackerFree(c, scope, &tracker);
        /* If this is in tail position do a tail call. */
        if (opts.isTail) {
            BufferPushUInt16(c->vm, buffer, VM_OP_TCL);
            BufferPushUInt16(c->vm, buffer, callee.index);
            ret.hasReturned = 1;
            ret.isNil = 1;
        } else {
            ret = CompilerGetTarget(c, opts);
            BufferPushUInt16(c->vm, buffer, VM_OP_CAL);
            BufferPushUInt16(c->vm, buffer, callee.index);
            BufferPushUInt16(c->vm, buffer, ret.index);
        }
        BufferPushUInt16(c->vm, buffer, form->count - 1);
        /* Write the location of all of the arguments */
        CompilerTrackerWrite(c, &tracker, 0);
        return ret;
    }
}

/* Recursively compile any value or form */
static Slot CompileValue(Compiler * c, FormOptions opts, Value x) {
    switch (x.type) {
        case TYPE_NIL:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
            return CompileNonReferenceType(c, opts, x);
        case TYPE_STRING:
            return CompileSymbol(c, opts, x);
        case TYPE_ARRAY:
            return CompileForm(c, opts, x.data.array);
        default:
            return CompileLiteral(c, opts, x);
    }
}

/* Initialize a Compiler struct */
void CompilerInit(Compiler * c, VM * vm) {
    c->vm = vm;
    c->buffer = BufferNew(vm, 128);
    c->env = ArrayNew(vm, 10);
    c->tail = NULL;
    c->error = NULL;
    CompilerPushScope(c, 0);
}

/* Register a global for the compilation environment. */
void CompilerAddGlobal(Compiler * c, const char * name, Value x) {
    Value sym = ValueLoadCString(c->vm, name);
    sym.type = TYPE_STRING;
    CompilerDeclareSymbol(c, c->tail, sym);
    ArrayPush(c->vm, c->env, x);
}

/* Register a global c function for the compilation environment. */
void CompilerAddGlobalCFunc(Compiler * c, const char * name, CFunction f) {
    Value func;
    func.type = TYPE_CFUNCTION;
    func.data.cfunction = f;
    CompilerAddGlobal(c, name, func);
}

/* Compile interface. Returns a function that evaluates the
 * given AST. Returns NULL if there was an error during compilation. */
Func * CompilerCompile(Compiler * c, Value form) {
    FormOptions opts = FormOptionsDefault();
    FuncDef * def;
    if (setjmp(c->onError)) {
        /* Clear all but root scope */
        if (c->tail)
            c->tail->parent = NULL;
        return NULL;
    }
    /* Create a scope */
    opts.isTail = 1;
    CompilerPushScope(c, 0);
    CompilerReturn(c, CompileValue(c, opts, form));
    def = CompilerGenFuncDef(c, c->buffer->count, 0);
    {
        uint32_t envSize = c->env->count;
        FuncEnv * env = VMAlloc(c->vm, sizeof(FuncEnv));
        Func * func = VMAlloc(c->vm, sizeof(Func));
        if (envSize) {
        	env->values = VMAlloc(c->vm, sizeof(Value) * envSize);
        	memcpy(env->values, c->env->data, envSize * sizeof(Value));
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

/* Macro expansion. Macro expansion happens prior to the compilation process
 * and is completely separate. This allows the compilation to not have to worry
 * about garbage collection and other issues that would complicate both the
 * runtime and the compilation. */
int CompileMacroExpand(VM * vm, Value x, Dictionary * macros, Value * out) {
    while (x.type == TYPE_ARRAY) {
        Array * form = x.data.array;
        Value sym, macroFn;
        if (form->count == 0) break;
        sym = form->data[0];
        macroFn = DictGet(macros, sym);
        if (macroFn.type != TYPE_FUNCTION && macroFn.type != TYPE_CFUNCTION) break;
        VMLoad(vm, macroFn);
        if (VMStart(vm)) {
            /* We encountered an error during parsing */        
            return 1;
        } else {
            x = vm->ret;
        }
    }
    *out = x;
    return 0;
}

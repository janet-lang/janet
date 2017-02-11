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
    /* Drop expression result flag - Allows for low level special
     * forms that generate reasonably efficient byteCode. When a compiler
     * helper is passed this flag, this allows the helper to return a dud
     * slot. */
    uint16_t canDrop : 1;
    /* Allows the sub expression to evaluate into a
     * temporary slot of it's choice. A temporary Slot
     * can be allocated with CompilerGetLocal. */
    uint16_t canChoose : 1;
    /* True if the form is in the tail position. This allows
     * for tail call optimization. If a helper receives this
     * flag, it is free to return a dud slot and generate bytecode
     * for a return, including tail calls. If a dud slot is returned, the
     * implementation should generate the instructions for return. If the slot
     * is not a dud, an upper function is in charge of generating the return. */
    uint16_t isTail : 1;
};

/* A Slot represent a location of a local variable
 * on the stack. Also contains some meta information. */
typedef struct Slot Slot;
struct Slot {
    /* The index of the Slot on the stack. */
    uint16_t index;
    /* A dud Slot should be expected to contain real data.
     * Forms that have side effects but don't evaulate to
     * anything will try to return dud slots if they can. If
     * they can't (FormOptions.canDrop is false), the will
     * usually load a Nil Slot and return that. That incurs
     * some overhead that is undesirable. */
    uint16_t isDud : 1;
    /* A temp Slot is a Slot on the stack that does not
     * belong to a named local. They can be freed whenever,
     * and so are used in intermediate calculations. */
    uint16_t isTemp : 1;
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
    uint32_t heapCapacity;
    uint32_t heapSize;
    uint16_t * freeHeap;
    Dictionary * literals;
    Array * literalsArray;
    Dictionary * locals;
    Scope * nextScope;
    Scope * previousScope;
};

/* Provides a default Slot. */
static Slot SlotDefault() {
    Slot slot;
    slot.index = 0;
    slot.isDud = 1;
    slot.isTemp = 0;
    return slot;
}

/* Provides default FormOptions */
static FormOptions FormOptionsDefault() {
    FormOptions opts;
    opts.canChoose = 1;
    opts.isTail = 0;
    opts.canDrop = 0;
    opts.target = 0;
    return opts;
}

/* Create some helpers that allows us to push more than just raw bytes
 * to the byte buffer. This helps us create the byte code for the compiled
 * functions. */
BufferDefine(UInt32, uint32_t);
BufferDefine(Int32, int32_t);
BufferDefine(Number, Number);
BufferDefine(UInt16, uint16_t);
BufferDefine(Int16, int16_t);

static void onError(Compiler * c) {
	;
}

/* If there is an error during compilation,
 * jump back to start */
#define CError(c, e) (onError(c), (c)->error = (e), longjmp((c)->onError, 1))

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
    scope->nextScope = NULL;
    scope->previousScope = c->tail;
    if (c->tail) {
        c->tail->nextScope = scope;
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
    if (!c->root)
        c->root = scope;
    return scope;
}

/* Remove the inner most scope from the compiler stack */
static void CompilerPopScope(Compiler * c) {
    if (c->tail == NULL) {
        CError(c, "No scope to pop.");
    } else {
        c->tail = c->tail->previousScope;
        if (c->tail) {
            c->tail->nextScope = NULL;
        } else {
            /* We deleted the last scope */
			c->root = NULL;
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
        uint16_t ret = scope->freeHeap[0];
        uint16_t * heap;
        uint32_t currentIndex = 0;
        if (scope->heapSize == 0) {
			CError(c, "Invalid freeing of slot.");
        }
        heap = scope->freeHeap;
        heap[0] = heap[--scope->heapSize];
        /* Min Heap Bubble down */
        for (;;) {
            uint32_t leftIndex = 2 * currentIndex + 1;
            uint32_t rightIndex = leftIndex + 1;
            uint32_t minIndex;
            if (rightIndex >= scope->heapSize) {
                if (leftIndex >= scope->heapSize) {
					break;
                } else {
    				minIndex = leftIndex;
                }
            } else {
				minIndex = heap[leftIndex] < heap[rightIndex] ? leftIndex : rightIndex;
            }
            if (heap[minIndex] < heap[currentIndex]) {
				uint16_t temp = heap[currentIndex];
				heap[currentIndex] = heap[minIndex];
				heap[minIndex] = temp;
				currentIndex = minIndex;
            } else if (heap[minIndex] == heap[currentIndex]) {
                CError(c, "Double slot allocation. Error in Compiler.");
            }
        }
        return ret;
    }
    return 0;
}

/* Free a slot on the stack for other locals and/or
 * intermediate values */
static void CompilerFreeLocal(Compiler * c, Scope * scope, uint16_t slot) {
    if (slot == scope->nextLocal - 1) {
        --scope->nextLocal;
        return;
    } else {
        uint32_t current;
        uint32_t parent;
        uint16_t * heap;
        /* Ensure heap has space */
        if (scope->heapSize >= scope->heapCapacity) {
            uint32_t newCap = 2 * scope->heapSize;
            uint16_t * newData = VMAlloc(c->vm, newCap * sizeof(uint16_t));
            memcpy(newData, scope->freeHeap, scope->heapSize * sizeof(uint16_t));
            scope->freeHeap = newData;
            scope->heapCapacity = newCap;
        }
        heap = scope->freeHeap;
        current = scope->heapSize++;
        /* Min heap bubble up */
        while (current > 0) {
            parent = (current - 1) / 2;
            if (slot == heap[parent]) {
                CError(c, "Double local free. Error in compiler.");
            }
            if (slot < heap[parent]) {
                heap[current] = slot;
                return;
            }
            heap[current] = heap[parent];
            current = parent;
        }
        heap[0] = slot;
    }
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
    if (!slot.isDud && slot.isTemp) {
        CompilerFreeLocal(c, scope, slot.index);
    }
}

/* Free the tracker after creation. This unlocks the memory
 * that was allocated by the GC an allows it to be collected. Also
 * frees slots that were tracked by this tracker in the given scope.
 * Also optionally write slot locations of all slots to the buffer.
 * Useful for dictionary literals, array literals, function calls, etc. */
static void CompilerTrackerFree(Compiler * c, Scope * scope, SlotTracker * tracker, int writeToBuffer) {
    uint32_t i;
    Buffer * buffer = c->buffer;
    if (writeToBuffer == 1) {
        for (i = 0; i < tracker->count; ++i) {
            Slot * s = tracker->slots + i;
            BufferPushUInt16(c->vm, buffer, s->index);
        }
    } else if (writeToBuffer == 2) { /* Write in reverse */
        for (i = 0; i < tracker->count; ++i) {
            Slot * s = tracker->slots + tracker->count - 1 - i;
            BufferPushUInt16(c->vm, buffer, s->index);
        }
    }
    /* Free in reverse order */
    for (i = tracker->count - 1; i < tracker->count; --i) {
        CompilerDropSlot(c, scope, tracker->slots[i]);
    }
    tracker->slots = NULL;
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
    if (sym.type != TYPE_SYMBOL) {
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
        scope = scope->previousScope;
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
 * for objects like strings, or anything else that cannot be instatiated
 * from bytecode and doesn't do anything in the AST. */
static Slot CompileLiteral(Compiler * c, FormOptions opts, Value x) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    uint16_t literalIndex;
    if (opts.canDrop) {
        return ret;
    }
    ret.isDud = 0;
    if (opts.canChoose) {
        ret.isTemp = 1;
        ret.index = CompilerGetLocal(c, scope);
    } else {
        ret.index = opts.target;
    }
    literalIndex = CompilerAddLiteral(c, scope, x);
    BufferPushUInt16(c->vm, buffer, VM_OP_CST);
    BufferPushUInt16(c->vm, buffer, ret.index);
    BufferPushUInt16(c->vm, buffer, literalIndex);
    return ret;
}

/* Compile boolean, nil, and number values. */
static Slot CompileNonReferenceType(Compiler * c, FormOptions opts, Value x) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    /* If the value is not used, the compiler can just immediately
     * ignore it as there are no side effects. */
    if (opts.canDrop) {
        return ret;
    }
	ret.isDud = 0;
    if (opts.canChoose) {
        ret.index = CompilerGetLocal(c, scope);
        ret.isTemp = 1;
    } else {
        ret.index = opts.target;
    }
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
    Slot ret = SlotDefault();
    uint16_t index = 0;
    uint16_t level = 0;
    /* We can just do nothing if we are dropping the
     * results, as dereferencing a symbol has no side effects. */
    if (opts.canDrop) {
        return ret;
    }
    ret.isDud = 0;
    if (ScopeSymbolResolve(scope, sym, &level, &index)) {
        if (level > 0) {
            /* We have an upvalue */
            if (opts.canChoose) {
                ret.index = CompilerGetLocal(c, scope);
                ret.isTemp = 1;
            } else {
                ret.index = opts.target;
            }
            BufferPushUInt16(c->vm, buffer, VM_OP_UPV);
            BufferPushUInt16(c->vm, buffer, ret.index);
            BufferPushUInt16(c->vm, buffer, level);
            BufferPushUInt16(c->vm, buffer, index);
        } else {
            /* Local variable on stack */
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
    } else {
        CError(c, "Undefined symbol");
    }
    return ret;
}

/* Compile a dictionary literal. The order of compilation
 * is undefined, although a key is evalated before its value,
 * assuming the dictionary is unchanged by macros. */
static Slot CompileDict(Compiler * c, FormOptions opts, Dictionary * dict) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    FormOptions subOpts = FormOptionsDefault();
    DictionaryIterator iter;
    DictBucket * bucket;
    SlotTracker tracker;
    /* Calculate sub flags */
    subOpts.canDrop = opts.canDrop;
    /* Compile all of the arguments */
    CompilerTrackerInit(c, &tracker);
    DictIterate(dict, &iter);
    while (DictIterateNext(&iter, &bucket)) {
        Slot keySlot = CompileValue(c, subOpts, bucket->key);
        if (subOpts.canDrop) CompilerDropSlot(c, scope, keySlot);
        Slot valueSlot = CompileValue(c, subOpts, bucket->value);
        if (subOpts.canDrop) CompilerDropSlot(c, scope, valueSlot);
        if (!subOpts.canDrop) {
            CompilerTrackerPush(c, &tracker, keySlot);
            CompilerTrackerPush(c, &tracker, valueSlot);
        }
    }
    if (!opts.canDrop) {
        ret.isDud = 0;
        /* Write Dictionary literal opcode */
        if (opts.canChoose) {
            ret.isTemp = 1;
            ret.index = CompilerGetLocal(c, scope);
        } else {
            ret.index = opts.target;
        }
        BufferPushUInt16(c->vm, buffer, VM_OP_DIC);
        BufferPushUInt16(c->vm, buffer, ret.index);
        BufferPushUInt16(c->vm, buffer, dict->count * 2);
    }
    /* Write the location of all of the arguments */
    CompilerTrackerFree(c, scope, &tracker, 1);
    return ret;
}

/* Compile an array literal. The array is evaluated left
 * to right. Arrays are normally compiled as forms to be evaluated, however. */
static Slot CompileArray(Compiler * c, FormOptions opts, Array * array) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    FormOptions subOpts = FormOptionsDefault();
    SlotTracker tracker;
    uint32_t i;
    /* Calculate sub flags */
    subOpts.canDrop = opts.canDrop;
    /* Compile all of the arguments */
    CompilerTrackerInit(c, &tracker);
    for (i = 0; i < array->count; ++i) {
        Slot slot = CompileValue(c, subOpts, array->data[i]);
        if (subOpts.canDrop)
            CompilerDropSlot(c, scope, slot);
        else
            CompilerTrackerPush(c, &tracker, slot);
    }
    if (!opts.canDrop) {
        ret.isDud = 0;
        /* Write Array literal opcode */
        if (opts.canChoose) {
            ret.isTemp = 1;
            ret.index = CompilerGetLocal(c, scope);
        } else {
            ret.index = opts.target;
        }
        BufferPushUInt16(c->vm, buffer, VM_OP_ARR);
        BufferPushUInt16(c->vm, buffer, ret.index);
        BufferPushUInt16(c->vm, buffer, array->count);
    }
    /* Write the location of all of the arguments */
    CompilerTrackerFree(c, scope, &tracker, 1);
    return ret;
}

/* Compile a special form in the form of an operator. There
 * are four choices for opcodes - when the operator is called
 * with 0, 1, 2, or n arguments. When the operator form is
 * called with n arguments, the number of arguments is written
 * after the op code, followed by those arguments.
 *
 * This makes a few assumptions about the opertors. One, no side
 * effects. With this assumptions, if the result of the operator
 * is unused, it's calculation can be ignored (the evaluation of
 * its argument is still carried out, but their results can
 * also be ignored). */
static Slot CompileOperator(Compiler * c, FormOptions opts, Array * form,
        int16_t op0, int16_t op1, int16_t op2, int16_t opn, int reverseOperands) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    FormOptions subOpts = FormOptionsDefault();
    SlotTracker tracker;
    uint32_t i;
    /* Compile all of the arguments */
    CompilerTrackerInit(c, &tracker);
    for (i = 1; i < form->count; ++i) {
        Slot slot = CompileValue(c, subOpts, form->data[i]);
        CompilerTrackerPush(c, &tracker, slot);
    }
    ret.isDud = 0;
    /* Write the correct opcode */
    if (form->count < 2) {
        if (op0 < 0) CError(c, "This operator does not take 0 arguments.");
        BufferPushUInt16(c->vm, buffer, op0);
    } else if (form->count == 2) {
        if (op1 < 0) CError(c, "This operator does not take 1 argument.");
        BufferPushUInt16(c->vm, buffer, op1);
    } else if (form->count == 3) {
        if (op2 < 0) CError(c, "This operator does not take 2 arguments.");
        BufferPushUInt16(c->vm, buffer, op2);
    } else {
        if (opn < 0) CError(c, "This operator does not take n arguments.");
        BufferPushUInt16(c->vm, buffer, opn);
        BufferPushUInt16(c->vm, buffer, form->count - 1);
    }
    if (opts.canChoose) {
        ret.isTemp = 1;
        ret.index = CompilerGetLocal(c, scope);
    } else {
        ret.index = opts.target;
    }
    BufferPushUInt16(c->vm, buffer, ret.index);
    /* Write the location of all of the arguments */
    CompilerTrackerFree(c, scope, &tracker, reverseOperands ? 2 : 1);
    return ret;
}

/* Math specials */
static Slot CompileAddition(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD0, VM_OP_ADM, VM_OP_ADD, VM_OP_ADM, 0);
}
static Slot CompileSubtraction(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD0, VM_OP_SBM, VM_OP_SUB, VM_OP_SBM, 0);
}
static Slot CompileMultiplication(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD1, VM_OP_MUM, VM_OP_MUL, VM_OP_MUM, 0);
}
static Slot CompileDivision(Compiler * c, FormOptions opts, Array * form) {
    return CompileOperator(c, opts, form, VM_OP_LD1, VM_OP_DVM, VM_OP_DIV, VM_OP_DVM, 0);
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

/* Helper function to return nil from a form that doesn't do anything 
 (set, while, etc.) */
static Slot CompileReturnNil(Compiler * c, FormOptions opts) {
    Slot ret = SlotDefault();
    /* If we need a return value, we just use nil */
    if (opts.isTail) {
        BufferPushUInt16(c->vm, c->buffer, VM_OP_RTN);
    } else if (!opts.canDrop) {
        ret.isDud = 0;
        if (opts.canChoose) {
            ret.isTemp = 1;
            ret.index = CompilerGetLocal(c, c->tail);
        } else {
            ret.isTemp = 0;
            ret.index = opts.target;
        }
        BufferPushUInt16(c->vm, c->buffer, VM_OP_NIL);
        BufferPushUInt16(c->vm, c->buffer, ret.index);
    }
    return ret;
}

/* Compile an assignment operation */
static Slot CompileAssign(Compiler * c, FormOptions opts, Value left, Value right) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    FormOptions subOpts = FormOptionsDefault();
    uint16_t target = 0;
    uint16_t level = 0;
    if (ScopeSymbolResolve(scope, left, &level, &target)) {
        /* Check if we have an up value. Otherwise, it's just a normal
         * local variable */
        if (level != 0) {
            /* Evaluate the right hand side */
            Slot slot = CompileValue(c, subOpts, right);
            /* Set the up value */
            BufferPushUInt16(c->vm, buffer, VM_OP_SUV);
            BufferPushUInt16(c->vm, buffer, slot.index);
            BufferPushUInt16(c->vm, buffer, level);
            BufferPushUInt16(c->vm, buffer, target);
            /* Drop the possibly temporary slot if it is indeed temporary */
            CompilerDropSlot(c, scope, slot);
        } else {
            Slot slot;
            subOpts.canChoose = 0;
            subOpts.target = target;
            slot = CompileValue(c, subOpts, right);
            CompilerDropSlot(c, scope, slot);
        }
    } else {
        /* We need to declare a new symbol */
        subOpts.target = CompilerDeclareSymbol(c, scope, left);
        subOpts.canChoose = 0;
        CompileValue(c, subOpts, right);
    }
	return CompileReturnNil(c, opts);
}

/* Writes bytecode to return a slot */
static void CompilerReturnSlot(Compiler * c, Slot slot) {
    Buffer * buffer = c->buffer;
    if (slot.isDud) {
        BufferPushUInt16(c->vm, buffer, VM_OP_RTN);
    } else {
        BufferPushUInt16(c->vm, buffer, VM_OP_RET);
        BufferPushUInt16(c->vm, buffer, slot.index);
    }
}

/* Compile series of expressions. This compiles the meat of
 * function definitions and the inside of do forms. */
static Slot CompileBlock(Compiler * c, FormOptions opts, Array * form, uint32_t startIndex) {
    Scope * scope = c->tail;
    Slot ret = SlotDefault();
    FormOptions subOpts = FormOptionsDefault();
    uint32_t current = startIndex;
    /* Compile the body */
    while (current < form->count) {
        subOpts.canDrop = (current != form->count - 1);
        subOpts.isTail = opts.isTail && !subOpts.canDrop;
        ret = CompileValue(c, subOpts, form->data[current]);
        if (subOpts.canDrop) {
            CompilerDropSlot(c, scope, ret);
        }
        ++current;
    }
	if (opts.isTail) {
        CompilerReturnSlot(c, ret);
        ret.isDud = 1;
    }
    return ret;
}

/* Extract the last n bytes from the buffer and use them to construct
 * a function definition. */
static FuncDef * CompilerGenFuncDef(Compiler * c, uint32_t lastNBytes, uint32_t arity) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    FuncDef * def = VMAlloc(c->vm, sizeof(FuncDef));
    /* Create enough space for the new byteCode */
    if (lastNBytes > buffer->count) {
		CError(c, "Trying to extract more bytes from buffer than in buffer.");
    }
    uint8_t * byteCode = VMAlloc(c->vm, lastNBytes);
    def->byteCode = (uint16_t *) byteCode;
    def->byteCodeLen = lastNBytes / 2;
    /* Copy the last chunk of bytes in the buffer into the new
     * memory for the function's byteCOde */
    memcpy(byteCode, buffer->data + buffer->count - lastNBytes, lastNBytes);
    /* Remove the byteCode from the end of the buffer */
    buffer->count -= lastNBytes;
    /* Initialize the new FuncDef */
    def->locals = scope->nextLocal;
    def->arity = arity;
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
    Slot ret = SlotDefault();
    /* Do nothing if we can drop. This is rather pointless from
     * a language point of view, but it doesn't hurt. */
    if (opts.canDrop) {
        return ret;
    }
    ret.isDud = 0;
    subScope = CompilerPushScope(c, 0);
    /* Check for function documentation - for now just ignore. */
    if (form->data[current].type == TYPE_STRING) {
        ++current;
    }
    /* Define the function parameters */
    if (form->data[current].type != TYPE_ARRAY) {
        CError(c, "Expected function arguments");
    }
    params = form->data[current++].data.array;
    for (i = 0; i < params->count; ++i) {
        Value param = params->data[i];
        if (param.type != TYPE_SYMBOL) {
            CError(c, "Function parameters should be symbols");
        }
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
    CompileBlock(c, subOpts, form, current);
    /* Create a new FuncDef as a constant in original scope by splicing
     * out the relevant code from the buffer. */
    {
        Value newVal;
        uint16_t literalIndex;
        FuncDef * def = CompilerGenFuncDef(c, buffer->count - sizeBefore, params->count);
        /* Add this FuncDef as a literal in the outer scope */
        newVal.type = TYPE_FUNCDEF;
        newVal.data.funcdef = def;
        literalIndex = CompilerAddLiteral(c, scope, newVal);
        /* Generate byteCode to instatiate this FuncDef */
        if (opts.canChoose) {
            ret.isTemp = 1;
            ret.index = CompilerGetLocal(c, scope);
        } else {
            ret.index = opts.target;
        }
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
    FormOptions condOpts = FormOptionsDefault();
    FormOptions resOpts = FormOptionsDefault();
    Slot ret = SlotDefault();
    Slot left, right, condition;
    uint32_t countAtJump;
    uint32_t countAfterFirstBranch;
    /* Check argument count */
    if (form->count < 3 || form->count > 4) {
        CError(c, "if takes either 2 or 3 arguments");
    }
    condition = CompileValue(c, condOpts, form->data[1]);
    /* Configure options for bodies of if */
    if (opts.isTail) {
        ret.isDud = 1;
    } else if (opts.canDrop) {
        resOpts.canDrop = 1;
    } else if (opts.canChoose) {
        ret.isDud = 0;
        ret.isTemp = 1;
        ret.index = CompilerGetLocal(c, scope);
        resOpts.target = ret.index;
        resOpts.canChoose = 0;
        /* If we have only one possible body, set
         * the result to nil before doing anything.
         * Possible optimization - use the condition. */
        if (form->count == 3) {
            BufferPushUInt16(c->vm, buffer, VM_OP_NIL);
            BufferPushUInt16(c->vm, buffer, ret.index);
        }
    } else {
        ret.index = resOpts.target = opts.target;
        if (form->count == 3) {
            BufferPushUInt16(c->vm, buffer, VM_OP_NIL);
            BufferPushUInt16(c->vm, buffer, ret.index);
        }
    }
    /* Mark where the buffer is now so we can write the jump
     * length later */
    countAtJump = buffer->count;
    /* For now use a long if bytecode instruction.
     * A short if will probably ususually be sufficient. This
     * if byte code will be replaced later with the correct index. */
    BufferPushUInt16(c->vm, buffer, VM_OP_JIF);
    BufferPushUInt16(c->vm, buffer, condition.index);
    BufferPushUInt32(c->vm, buffer, 0);
    /* Compile true path */
    left = CompileValue(c, resOpts, form->data[2]);
    if (opts.isTail) CompilerReturnSlot(c, left);
    CompilerDropSlot(c, scope, left);
    /* If we need to jump again, do so */
    if (!opts.isTail && form->count == 4) {
        countAtJump = buffer->count;
        BufferPushUInt16(c->vm, buffer, VM_OP_JMP);
        BufferPushUInt32(c->vm, buffer, 0);
    }
    /* Reinsert jump with correct index */
    countAfterFirstBranch = buffer->count;
    buffer->count = countAtJump;
    BufferPushUInt16(c->vm, buffer, VM_OP_JIF);
    BufferPushUInt16(c->vm, buffer, condition.index);
    BufferPushUInt32(c->vm, buffer, (countAfterFirstBranch - countAtJump) / 2);
    buffer->count = countAfterFirstBranch;
    /* Compile false path */
    if (form->count == 4) {
        right = CompileValue(c, resOpts, form->data[3]);
        if (opts.isTail) CompilerReturnSlot(c, right);
        CompilerDropSlot(c, scope, right);
    }
    /* Set the jump length */
    if (!opts.isTail && form->count == 4) {
        countAfterFirstBranch = buffer->count;
        buffer->count = countAtJump;
        BufferPushUInt16(c->vm, buffer, VM_OP_JMP);
        BufferPushUInt32(c->vm, buffer, (countAfterFirstBranch - countAtJump) / 2);
        buffer->count = countAfterFirstBranch;
    }
    return ret;
}

/* While special */
static Slot CompileWhile(Compiler * c, FormOptions opts, Array * form) {
    Slot cond;
	uint32_t countAtStart = c->buffer->count;
	uint32_t countAtJumpDelta;
	uint32_t countAtFinish;
	FormOptions subOpts = FormOptionsDefault();
	subOpts.canDrop = 1;
    CompilerPushScope(c, 1);
    /* Compile condition */
	cond = CompileValue(c, FormOptionsDefault(), form->data[1]);
	/* Leave space for jump later */
	countAtJumpDelta = c->buffer->count;
	c->buffer->count += sizeof(uint16_t) * 2 + sizeof(int32_t);
	/* Compile loop body */
    CompilerDropSlot(c, c->tail, CompileBlock(c, subOpts, form, 2));
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
	/* Return dud */
    return CompileReturnNil(c, opts);
}

/* Do special */
static Slot CompileDo(Compiler * c, FormOptions opts, Array * form) {
    Slot ret;
    CompilerPushScope(c, 1);
    ret = CompileBlock(c, opts, form, 1);
    CompilerPopScope(c);
    return ret;
}

/* Quote special - returns its argument without parsing. */
static Slot CompileQuote(Compiler * c, FormOptions opts, Array * form) {
    Scope * scope = c->tail;
    Buffer * buffer = c->buffer;
    Slot ret = SlotDefault();
    uint16_t literalIndex;
    if (form->count != 2) {
        CError(c, "Quote takes exactly 1 argument.");
    }
    Value x = form->data[1];
    if (x.type == TYPE_NIL ||
            x.type == TYPE_BOOLEAN ||
            x.type == TYPE_NUMBER) {
        return CompileNonReferenceType(c, opts, x);
    }
    if (opts.canDrop) {
        return ret;
    }
    ret.isDud = 0;
    if (opts.canChoose) {
        ret.isTemp = 1;
        ret.index = CompilerGetLocal(c, scope);
    } else {
        ret.index = opts.target;
    }
    literalIndex = CompilerAddLiteral(c, scope, x);
    BufferPushUInt16(c->vm, buffer, VM_OP_CST);
    BufferPushUInt16(c->vm, buffer, ret.index);
    BufferPushUInt16(c->vm, buffer, literalIndex);
    return ret;
}

/* Assignment special */
static Slot CompileSet(Compiler * c, FormOptions opts, Array * form) {
    if (form->count != 3) {
        CError(c, "Assignment expects 2 arguments");
    }
    return CompileAssign(c, opts, form->data[1], form->data[2]);
}

/* Define a function type for Special Form helpers */
typedef Slot (*SpecialFormHelper) (Compiler * c, FormOptions opts, Array * form);

/* Dispatch to a special form */
static SpecialFormHelper GetSpecial(Array * form) {
    uint8_t * name;
    if (form->count < 1 || form->data[0].type != TYPE_SYMBOL)
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
            case '\'': return CompileQuote;
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
        case 'd':
            {
                if (VStringSize(name) == 2 &&
                        name[1] == 'o') {
                    return CompileDo;
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
        Slot ret = SlotDefault();
        SlotTracker tracker;
        FormOptions subOpts = FormOptionsDefault();
        uint32_t i;
        /* Compile all of the arguments */
        CompilerTrackerInit(c, &tracker);
        for (i = 0; i < form->count; ++i) {
            Slot slot = CompileValue(c, subOpts, form->data[i]);
            CompilerTrackerPush(c, &tracker, slot);
        }
        /* If this is in tail position do a tail call. */
        if (opts.isTail) {
            BufferPushUInt16(c->vm, buffer, VM_OP_TCL);
        } else {
            ret.isDud = 0;
            BufferPushUInt16(c->vm, buffer, VM_OP_CAL);
            if (opts.canDrop) {
                ret.isTemp = 1;
                ret.index = CompilerGetLocal(c, scope);
            } else {
                ret.index = opts.target;
            }
            BufferPushUInt16(c->vm, buffer, ret.index);
        }
        /* Push the number of arguments to the function */
        BufferPushUInt16(c->vm, buffer, form->count - 1);
        /* Write the location of all of the arguments */
        CompilerTrackerFree(c, scope, &tracker, 1);
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
        case TYPE_SYMBOL:
            return CompileSymbol(c, opts, x);
        case TYPE_FORM:
            return CompileForm(c, opts, x.data.array);
        case TYPE_ARRAY:
            return CompileArray(c, opts, x.data.array);
        case TYPE_DICTIONARY:
            return CompileDict(c, opts, x.data.dict);
        default:
            return CompileLiteral(c, opts, x);
    }
}

/* Initialize a Compiler struct */
void CompilerInit(Compiler * c, VM * vm) {
    c->vm = vm;
    c->buffer = BufferNew(vm, 128);
    c->env = ArrayNew(vm, 10);
    c->tail = c->root = NULL;
    c->error = NULL;
    CompilerPushScope(c, 0);
}

/* Register a global for the compilation environment. */
void CompilerAddGlobal(Compiler * c, const char * name, Value x) {
    Value sym = ValueLoadCString(c->vm, name);
    sym.type = TYPE_SYMBOL;
    CompilerDeclareSymbol(c, c->root, sym);
    ArrayPush(c->vm, c->env, x);
}

/* Register a global c function for the compilation environment. */
void CompilerAddGlobalCFunc(Compiler * c, const char * name, CFunction f) {
	Value func;
	func.type = TYPE_CFUNCTION;
	func.data.cfunction = f;
	return CompilerAddGlobal(c, name, func);
}

/* Compile interface. Returns a function that evaluates the
 * given AST. Returns NULL if there was an error during compilation. */
Func * CompilerCompile(Compiler * c, Value form) {
    FormOptions opts = FormOptionsDefault();
    FuncDef * def;
    if (setjmp(c->onError)) {
        /* Clear all but root scope */
        c->tail = c->root;
        c->root->nextScope = NULL;
        return NULL;
    }
    /* Create a scope */
    opts.isTail = 1;
    CompilerPushScope(c, 0);
    CompilerReturnSlot(c, CompileValue(c, opts, form));
    def = CompilerGenFuncDef(c, c->buffer->count, 0);
    {
        uint32_t envSize = c->env->count;
        FuncEnv * env = VMAlloc(c->vm, sizeof(FuncEnv));
        Func * func = VMAlloc(c->vm, sizeof(Func));
        env->values = VMAlloc(c->vm, sizeof(Value) * envSize);
        memcpy(env->values, c->env->data, envSize * sizeof(Value));
        env->stackOffset = envSize;
        env->thread = NULL;
        func->parent = NULL;
        func->def = def;
        func->env = env;
        return func;
    }
}

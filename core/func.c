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

#include <setjmp.h>

#include "internal.h"
#include "wrap.h"
#include "gc.h"

/* Bytecode op argument types */

/* s - a slot */
/* c - a constant */
/* i - a small integer */
/* t - a type (have a simple type for non unions) */
/* l - a label */

typedef enum DstOpArgType DstOpArgType;
enum DstOpArgType {
    DST_OAT_SLOT,
    DST_OAT_ENVIRONMENT,
    DST_OAT_CONSTANT,
    DST_OAT_INTEGER,
    DST_OAT_TYPE,
    DST_OAT_SIMPLETYPE,
    DST_OAT_LABEL
}

/* Convert a slot to to an integer for bytecode */

/* Types of instructions */
/* _0arg - op.---.--.-- (return-nil, noop, vararg arguments) 
 * _s - op.src.--.-- (push1)
 * _l - op.XX.XX.XX (jump)
 * _ss - op.dest.XX.XX (move, swap)
 * _sl - op.check.XX.XX (jump-if)
 * _st - op.check.TT.TT (typecheck)
 * _si - op.dest.XX.XX (load-integer)
 * _sss - op.dest.op1.op2 (add, subtract, arithmetic, comparison)
 * _ses - op.dest.up.which (load-upvalue, save-upvalue)
 * _sc - op.dest.CC.CC (load-constant, closure)
 */

/* Various types of instructions */
typedef enum DstInstructionType DstInstructionType;
enum DstInstructionType {
    DIT_0, /* No args */
    DIT_S, /* One slot */
    DIT_L, /* One label */
    DIT_SS, /* Two slots */
    DIT_SL,
    DIT_ST,
    DIT_SI,
    DIT_SSS,
    DIT_SES,
    DIT_SC
};

/* Definition for an instruction in the assembler */
typedef struct DstInstructionDef DstInstructionDef;
struct DstInstructionDef {
    const char *name;
    DstInstructionType type;
    uint8_t opcode;
};

/* Hold all state needed during assembly */
typedef struct DstAssembler DstAssembler;
struct DstAssembler {
    DstAssembler *parent;
    Dst *vm;
    DstFuncDef *def;
    DstValue name;
    jmp_buf onError;

    DstTable *labels; /* symbol -> bytecode index */
    DstTable *constants; /* symbol -> constant index */
    DstTable *slots; /* symbol -> slot index */
    DstTable *envs; /* symbol -> environment index */
    uint32_t *bytecode; /* Where to put bytecode */
    uint32_t bytecode_capacity; /* Set once */
    uint32_t bytecode_count;
}

/* The DST value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
static const char *types[] = {
    "nil",
    "real",
    "integer",
    "boolean",
    "string",
    "symbol",
    "array",
    "tuple",
    "table",
    "struct",
    "thread",
    "buffer",
    "function",
    "cfunction",
    "userdata"
};

/* Dst opcode descriptions in lexographic order. This
 * allows a binary search over the elements to find the
 * correct opcode given a name. This works in reasonable
 * time is easier to setup statically than a hash table or
 * prefix tree. */
static const char *dst_ops[] = {
    {"add", DIT_SSS, 0x01},
    {"bitand", DIT_SSS, 0x02},
    {"bitor", DIT_SSS, 0x03},
    {"bitxor", DIT_SSS, 0x04},
    {"call", DIT_SS, 0x05},
    {"closure", DIT_SC, 0x06},
    {"divide", DIT_SSS, 0x07},
    {"jump", DIT_L, 0x08},
    {"jump-if", DIT_SL, 0x09},
    {"load-constant", DIT_SC, 0x0A},
    {"load-false", DIT_S, 0x0B},
    {"load-integer", DIT_SI, 0x0C},
    {"load-nil", DIT_S, 0x0D},
    {"load-true", DIT_S, 0x0E},
    {"load-upvalue", DIT_SES, 0x0F},
    {"move", DIT_SS, 0x10},
    {"modulo", DIT_SSS, 0x11},
    {"multiply", DIT_SSS, 0x12},
    {"noop", DIT_0, 0x13},
    {"push", DIT_VARG, 0x14},
    {"push1", DIT_S, 0x15},
    {"push2", DIT_SS, 0x16},
    {"push3", DIT_SSS, 0x17},
    {"push-array", DIT_S, 0x18},
    {"return", DIT_S, 0x19},
    {"return-nil", DIT_0, 0x1A},
    {"save-upvalue", DIT_SES, 0x1B},
    {"shift-left", DIT_SSS, 0x1C},
    {"shift-right", DIT_SSS, 0x1D},
    {"shift-right-signed", DIT_SSS, 0x1E},
    {"subtract", DIT_SSS, 0x1F},
    {"swap", DIT_SS, 0x20},
    {"syscall", DIT_SI, 0x21},
    {"tail-call", DIT_S, 0x22},
    {"transfer", DIT_SSS, 0x23},
    {"typecheck", DIT_ST, 0x24},
};

/* Compare a DST string to a native 0 terminated c string. Used in the 
 * binary search for the instruction definition. */
static int dst_strcompare(const uint8_t *str, const char *other) {
    uint32_t len = dst_string_length(str);
    int index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)other)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (other[index] == '\0') ? 0 : -1;
}

/* Find an instruction definition given its name */
static DstInstructionDef *dst_findi(const uint8_t *key) {
    DstInstructionDef *low = dst_ops;
    DstInstructionDef *hi = dst_ops + (sizeof(dst_ops) / sizeof(DstInstructionDef));
    while (low < hi) {
        DstInstructionDef *mid = low + ((hi - low) / 2);
        int comp = dst_strcompare(key, mid->name);
        if (comp < 0) {
            hi = mid;
        } else if (comp > 0) {
            low = mid + 1;
        } else {
            return mid;
        }
    }
    return NULL;
}

/* Check a dst string against a bunch of test_strings. Return the 
 * index of the matching test_string, or -1 if not found. */
static int strsearch(const uint8_t *str, const char **test_strings) {
    uint32_t len = dst_string_length(str);
    int index;
    for (index = 0; ; index++) {
        uint32_t i;
        const char *testword = test_strings[index];
        if (NULL == testword)
            break;
        for (i = 0; i < len; i++) {
            if (testword[i] != str[i])
                goto nextword;
        }
        return index;
        :nextword
    }
    return -1;
}

/* Takes some dst assembly and gets the required capacity 
 * for the output bytecode. Does not do any memory allocation, */
static uint32_t estimate_capacity(const DstValue *assembly, uint32_t n) {
    uint32_t i;
    uint32_t cap = 0;
    for (i = 0; i < n; i++) {
        /* Ignore non tuple types, they are labels */
        if (assembly[i].type != DST_TUPLE) continue;
        cap++;
    }
    return cap;
}

/* Throw some kind of assembly error */
static void dst_asm_error(DstAssembler *a, const char *message) {
    printf("%s\n", message);
    exit(1);
}

/* Parse an argument to an assembly instruction, and return the result as an
 * integer. This integer will need to be trimmed and bound checked. */
static int64_t doarg_1(DstAssembler *a, DstOpArgType argtype, DstValue x) {
    DstTable *names;
    switch (argtype) {
        case DST_OAT_SLOT:
            c = a->slots;
            break;
        case DST_OAT_ENVIRONMENT:
            c = e->envs;
            break;
        case DST_OAT_CONSTANT:
            c = a->constants;
            break;
        case DST_OAT_INTEGER:
            c = NULL;
            break;
        case DST_OAT_TYPE:
        case DST_OAT_SIMPLETYPE:
            c = NULL;
            break;
        case DST_OAT_LABEL:
            c = a->labels;
            break;
    }
    switch (x.type) {
        default:
            break;
        case DST_INTEGER:
            return x.as.integer;
        case DST_TUPLE:
        {
            if (argtype == DST_OAT_TYPE) {
                int64_t result = 0;
                uint32_t i = 0;
                for (i = 0; i < dst_tuple_length(x.as.tuple); i++) {
                    result |= dst_asm_argument(a, DST_OAT_SIMPLETYPE, x.as.tuple[i]);
                }
                return result;
            }
            break;
        }
        case DST_SYMBOL:
        {
            if (NULL != names) {
                DstValue result = dst_table_get(names, x);
                if (result.type == DST_INTEGER) {
                    if (argtype == DST_OAT_LABEL)
                        return result.as.integer - a->bytecode_count;
                    return result.as.integer; 
                } else {
                    dst_asm_error(a, "unknown name");
                }
            } else if (argtype == DST_OAT_TYPE || argtype == DST_OAT_SIMPLETYPE) {
                int index = strsearch(x.as.string, types);
                if (index != -1) {
                    return (int64_t) index;
                } else {
                    dst_asm_error(a, "unknown type");
                }
            }
            break;
        }
    }
    dst_asm_error(a, "unexpected type parsing instruction argument");
}

/* Trim a bytecode operand to 1, 2, or 3 bytes. Error out if
 * the given argument doesn't fit in the required number of bytes. */
static uint32_t doarg_2(int nbytes, int hassign, int64_t arg) {
    /* Calculate the min and max values that can be stored given
     * nbytes, and whether or not the storage is signed */
    int64_t min = (-hassign) << ((nbytes << 3) - 1);
    int64_t max = ~((-1) << ((nbytes << 3) - hassign));
    if (arg < min)
        dst_asm_error(a, "instruction argument is too small");
    if (arg > max)
        dst_asm_error(a, "instruction argument is too large");
    return (uint32_t) (arg & 0xFFFFFFFF);
}

/* Parse a single argument to an instruction. Trims it as well as
 * try to convert arguments to bit patterns */
static uint32_t doarg(DstAssembler *a, DstOpArgType argtype, int nth, int nbytes, int hassign, DstValue x) {
    int64_t arg1 = doarg_1(a, argtype, x);
    return doarg_2(nbytes, hassign, arg1) << (nth << 3);
}

/* Provide parsing methods for the different kinds of arguments */
static uint32_t read_instruction(DstAssembler *a, const DstInstructionDef *idef, const DstValue *argt) {
    uint32_t instr = idef->opcode;
    switch (idef->type) {
        case DIT_0:
        {
            if (dst_tuple_lenth(argt) != 1)
                dst_asm_error(a, "expected 0 arguments: (op)");
            break;
        }
        case DIT_S:
        {
            if (dst_tuple_lenth(argt) != 2)
                dst_asm_error(a, "expected 1 argument: (op, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 3, 3, 0, argt[1]);
            break;
        }
        case DIT_L:
        {
            if (dst_tuple_lenth(argt) != 2)
                dst_asm_error(a, "expected 1 argument: (op, label)");
            instr |= doarg(a, DST_OAT_LABEL, 3, 3, 1, argt[1]);
            break;
        }
        case DIT_SS:
        {
            if (dst_tuple_lenth(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_SLOT, 3, 2, 0, argt[2]);
            break;
        }
        case DIT_SL:
        {
            if (dst_tuple_lenth(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, label)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_LABEL, 3, 2, 1, argt[2]);
            break;
        }
        case DIT_ST:
        {
            if (dst_tuple_lenth(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, type)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_TYPE, 3, 2, 0, argt[2]);
            break;
        }
        case DIT_SI:
        {
            if (dst_tuple_lenth(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, integer)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_INTEGER, 3, 2, 1, argt[2]);
            break;
        }
        case DIT_SSS:
        {
            if (dst_tuple_lenth(argt) != 4)
                dst_asm_error(a, "expected 3 arguments: (op, slot, slot, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_SLOT, 2, 1, 0, argt[2]);
            instr |= doarg(a, DST_OAT_SLOT, 3, 1, 0, argt[3]);
            break;
        }
        case DIT_SES:
        {
            DstAssembler *b = a;
            uint32_t envn;
            if (dst_tuple_lenth(argt) != 4)
                dst_asm_error(a, "expected 3 arguments: (op, slot, environment, envslot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            envn = doarg(a, DST_OAT_ENVIRONMENT, 0, 1, 0, argt[2]);
            instr |= envn << 16;
            for (env += 1; env > 0; env--) {
                b = b->parent;
                if (NULL == b)
                    dst_asm_error(a, "invalid environment index");
            }
            instr |= doarg(b, DST_OAT_SLOT, 3, 1, 0, argt[3]);
            break;
        }
        case DIT_SC:
        {
            if (dst_tuple_lenth(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, constant)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_CONSTANT, 3, 2, 0, argt[2]);
            break;
        }
    }
    return instr;
}

/* Do assembly. Return 0 if successful, else return an error code. */
static void dst_asm1(DstAssembler *a, DstValue src) {
    DstTable *t = src.as.table;
    DstFuncDef *def = a->def;
    uint32_t i;
    DstValue x;

    if (src.type != DST_TABLE) 
        dst_asm_error(a, "expected table");
    x = dst_table_get(t, dst_wrap_symbol(dst_cstring(a->vm, "arity")));
    def->arity = x.type == DST_INTEGER ? x.as.integer : 0;
    x = dst_table_get(t, dst_wrap_symbol(dst_cstring(a->vm, "stack")));
    def->locals = x.type == DST_INTEGER ? x.as.integer : 0;

    // Check name
    x = dst_table_get(t, dst_wrap_symbol(dst_cstring(a->vm, "name")));
    if (x.type == SYMBOL) {
       a->name = x; 
    }

    // Create slot aliases
    x = dst_table_get(t, dst_wrap_symbol(dst_cstring(a->vm, "slots")));
    if (x.type == DST_ARRAY) {
        for (i = 0; i < x.as.array->count; i++) {
            DstValue v = x.as.array->data[i];
            if (v.type == DST_TUPLE) {
                uint32_t j; 
                for (j = 0; j < dst_tuple_length(v.as.tuple); j++) {
                    if (v.as.tuple[j].type != SYMBOL)
                        dst_asm_error("slot names must be symbols");
                    dst_table_put(a->vm, a->slots, v.as.tuple[j], dst_wrap_integer(i));
                }
            } else if (v.type == DST_SYMBOL) {
                dst_table_put(a->vm, a->slots, v, dst_wrap_integer(i));
            } else {
                dst_asm_error(a, "slot names must be symbols or tuple of symbols");
            }
        }
    }

    // Create environment aliases
    x = dst_table_get(t, dst_wrap_symbol(dst_cstring(a->vm, "environments")));
    if (x.type == DST_ARRAY) {
        for (i = 0; i < x.as.array->count; i++) {
            DstAssembler *b = a->parent;
            DstValue v = x.as.array->data[i];
            if (v.type != DST_SYMBOL) {
                dst_asm_error(a, "expected a symbol");
                while (NULL != b) {
                    if (dst_equals(b->name, v)) {
                        break;
                    }
                    b = b->parent;
                }
            // Check parent assemblers to find the given environment
        }
    }
}

/* Detach an environment that was on the stack from the stack, and
 * ensure that its environment persists */
void dst_funcenv_detach_(Dst *vm, DstFuncEnv *env) {
    DstThread *thread = env->thread;
    DstValue *stack = thread->data + thread->count;
    uint32_t size = dst_frame_size(stack);
    DstValue *values = malloc(sizeof(DstValue * size));
    if (NULL == values) {
        DST_OUT_OF_MEMORY;
    }
    /* Copy stack into env values (the heap) */
    memcpy(values, stack, sizeof(DstValue) * size);
    /* Update env */
    env->thread = NULL;
    env->stackOffset = size;
    env->values = values;
}

/* Deinitialize an environment */
void dst_funcenv_deinit(DstFuncEnv *env) {
    if (NULL == env->thread && NULL != env->values) {
        free(env->values);
    }
}

/* Create the FuncEnv for the current stack frame. */
DstFuncEnv *dst_funcenv_init_(Dst *vm, DstFuncEnv *env, DstThread *thread, DstValue *stack) {
    env->thread = thread;
    env->stackOffset = thread->count;
    env->values = NULL;
    dst_frame_env(stack) = env;
    return env;
}


/* Create a funcdef */
DstFuncDef *dst_funcdef_init_(Dst *vm, DstFuncDef *def) {
    uint8_t * byteCode = dst_alloc(c->vm, lastNBytes);
    def->byteCode = (uint16_t *)byteCode;
    def->byteCodeLen = lastNBytes / 2;
    /* Copy the last chunk of bytes in the buffer into the new
     * memory for the function's byteCOde */
    dst_memcpy(byteCode, buffer->data + buffer->count - lastNBytes, lastNBytes);
    /* Remove the byteCode from the end of the buffer */
    buffer->count -= lastNBytes;
    /* Create the literals used by this function */
    if (scope->literalsArray->count) {
        def->literals = dst_alloc(c->vm, scope->literalsArray->count * sizeof(DstValue));
        dst_memcpy(def->literals, scope->literalsArray->data,
                scope->literalsArray->count * sizeof(DstValue));
    } else {
        def->literals = NULL;
    }
    def->literalsLen = scope->literalsArray->count;
    /* Delete the sub scope */
    compiler_pop_scope(c);
    /* Initialize the new FuncDef */
    def->locals = scope->frameSize;
    def->arity = arity;
    def->flags = (varargs ? DST_FUNCDEF_FLAG_VARARG : 0) |
        (scope->touchParent ? DST_FUNCDEF_FLAG_NEEDSPARENT : 0) |
        (scope->touchEnv ? DST_FUNCDEF_FLAG_NEEDSENV : 0);
    return def;
}

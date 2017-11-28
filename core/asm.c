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

#include <dst/dst.h>
#include "opcodes.h"

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
};

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
    DIT_SU, /* Unsigned */
    DIT_SSS,
    DIT_SSI,
    DIT_SSU,
    DIT_SES,
    DIT_SC
};

/* Definition for an instruction in the assembler */
typedef struct DstInstructionDef DstInstructionDef;
struct DstInstructionDef {
    const char *name;
    DstInstructionType type;
    DstOpCode opcode;
};

/* Hold all state needed during assembly */
typedef struct DstAssembler DstAssembler;
struct DstAssembler {
    DstAssembler *parent;
    DstFuncDef *def;
    jmp_buf on_error;
    const uint8_t *errmessage;

    int32_t environments_capacity;
    int32_t bytecode_count; /* Used for calculating labels */

    DstTable labels; /* symbol -> bytecode index */
    DstTable constants; /* symbol -> constant index */
    DstTable slots; /* symbol -> slot index */
    DstTable envs; /* symbol -> environment index */
};

/* Dst opcode descriptions in lexographic order. This
 * allows a binary search over the elements to find the
 * correct opcode given a name. This works in reasonable
 * time and is easier to setup statically than a hash table or
 * prefix tree. */
static const DstInstructionDef dst_ops[] = {
    {"add", DIT_SSS, DOP_ADD},
    {"add-immediate", DIT_SSI, DOP_ADD_IMMEDIATE},
    {"add-integer", DIT_SSS, DOP_ADD_INTEGER},
    {"add-real", DIT_SSS, DOP_ADD_REAL},
    {"bitand", DIT_SSS, DOP_BAND},
    {"bitnot", DIT_SS, DOP_BNOT},
    {"bitor", DIT_SSS, DOP_BOR},
    {"bitxor", DIT_SSS, DOP_BXOR},
    {"call", DIT_SS, DOP_CALL},
    {"closure", DIT_SC, DOP_CLOSURE},
    {"compare", DIT_SSS, DOP_COMPARE},
    {"divide", DIT_SSS, DOP_DIVIDE},
    {"divide-immediate", DIT_SSI, DOP_DIVIDE_IMMEDIATE},
    {"divide-integer", DIT_SSS, DOP_DIVIDE_INTEGER},
    {"divide-real", DIT_SSS, DOP_DIVIDE_REAL},
    {"equals", DIT_SSS, DOP_EQUALS},
    {"error", DIT_S, DOP_ERROR},
    {"get", DIT_SSS, DOP_GET},
    {"get-index", DIT_SSU, DOP_GET_INDEX},
    {"greater-than", DIT_SSS, DOP_GREATER_THAN},
    {"jump", DIT_L, DOP_JUMP},
    {"jump-if", DIT_SL, DOP_JUMP_IF},
    {"jump-if-not", DIT_SL, DOP_JUMP_IF_NOT},
    {"less-than", DIT_SSS, DOP_LESS_THAN},
    {"load-boolean", DIT_S, DOP_LOAD_BOOLEAN},
    {"load-constant", DIT_SC, DOP_LOAD_CONSTANT},
    {"load-integer", DIT_SI, DOP_LOAD_INTEGER},
    {"load-nil", DIT_S, DOP_LOAD_NIL},
    {"load-syscall", DIT_SU, DOP_LOAD_SYSCALL},
    {"load-upvalue", DIT_SES, DOP_LOAD_UPVALUE},
    {"move", DIT_SS, DOP_MOVE},
    {"multiply", DIT_SSS, DOP_MULTIPLY},
    {"multiply-immediate", DIT_SSI, DOP_MULTIPLY_IMMEDIATE},
    {"multiply-integer", DIT_SSS, DOP_MULTIPLY_INTEGER},
    {"multiply-real", DIT_SSS, DOP_MULTIPLY_REAL},
    {"noop", DIT_0, DOP_NOOP},
    {"push", DIT_S, DOP_PUSH},
    {"push-array", DIT_S, DOP_PUSH_ARRAY},
    {"push2", DIT_SS, DOP_PUSH_2},
    {"push3", DIT_SSS, DOP_PUSH_3},
    {"put", DIT_SSS, DOP_PUT},
    {"put-index", DIT_SSU, DOP_PUT_INDEX},
    {"return", DIT_S, DOP_RETURN},
    {"return-nil", DIT_0, DOP_RETURN_NIL},
    {"set-upvalue", DIT_SES, DOP_SET_UPVALUE},
    {"shift-left", DIT_SSS, DOP_SHIFT_LEFT},
    {"shift-left-immediate", DIT_SSI, DOP_SHIFT_LEFT_IMMEDIATE},
    {"shift-right", DIT_SSS, DOP_SHIFT_RIGHT},
    {"shift-right-immediate", DIT_SSI, DOP_SHIFT_RIGHT_IMMEDIATE},
    {"shift-right-unsigned", DIT_SSS, DOP_SHIFT_RIGHT_UNSIGNED},
    {"shift-right-unsigned-immediate", DIT_SSS, DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE},
    {"subtract", DIT_SSS, 0x1F},
    {"syscall", DIT_SU, DOP_SYSCALL},
    {"tailcall", DIT_S, DOP_TAILCALL},
    {"transfer", DIT_SSS, DOP_TRANSFER},
    {"typecheck", DIT_ST, DOP_TYPECHECK},
};

/* Compare a DST string to a native 0 terminated c string. Used in the 
 * binary search for the instruction definition. */
static int dst_strcompare(const uint8_t *str, const char *other) {
    int32_t len = dst_string_length(str);
    int32_t index;
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
static const DstInstructionDef *dst_findi(const uint8_t *key) {
    const DstInstructionDef *low = dst_ops;
    const DstInstructionDef *hi = dst_ops + (sizeof(dst_ops) / sizeof(DstInstructionDef));
    while (low < hi) {
        const DstInstructionDef *mid = low + ((hi - low) / 2);
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
static int32_t strsearch(const uint8_t *str, const char **test_strings) {
    int32_t len = dst_string_length(str);
    int index;
    for (index = 0; ; index++) {
        int32_t i;
        const char *testword = test_strings[index];
        if (NULL == testword)
            break;
        for (i = 0; i < len; i++) {
            if (testword[i] != str[i])
                goto nextword;
        }
        return index;
        nextword:
            continue;
    }
    return -1;
}

/* Deinitialize an Assembler. Does not deinitialize the parents. */
static void dst_asm_deinit(DstAssembler *a) {
    dst_table_deinit(&a->slots);
    dst_table_deinit(&a->labels);
    dst_table_deinit(&a->envs);
    dst_table_deinit(&a->constants);
}

/* Throw some kind of assembly error */
static void dst_asm_error(DstAssembler *a, const char *message) {
    a->errmessage = dst_cstring(message);
    longjmp(a->on_error, 1);
}
#define dst_asm_assert(a, c, m) do { if (!(c)) dst_asm_error((a), (m)); } while (0)

/* Throw some kind of assembly error */
static void dst_asm_errorv(DstAssembler *a, const uint8_t *m) {
    a->errmessage = m;
    longjmp(a->on_error, 1);
}

/* Parse an argument to an assembly instruction, and return the result as an
 * integer. This integer will need to be trimmed and bound checked. */
static int32_t doarg_1(DstAssembler *a, DstOpArgType argtype, DstValue x) {
    int32_t ret = -1;
    DstTable *c;
    switch (argtype) {
        case DST_OAT_SLOT:
            c = &a->slots;
            break;
        case DST_OAT_ENVIRONMENT:
            c = &a->envs;
            break;
        case DST_OAT_CONSTANT:
            c = &a->constants;
            break;
        case DST_OAT_INTEGER:
            c = NULL;
            break;
        case DST_OAT_TYPE:
        case DST_OAT_SIMPLETYPE:
            c = NULL;
            break;
        case DST_OAT_LABEL:
            c = &a->labels;
            break;
    }
    switch (dst_type(x)) {
        default:
            goto error;
            break;
        case DST_INTEGER:
            ret = dst_unwrap_integer(x);
            break;
        case DST_TUPLE:
        {
            const DstValue *t = dst_unwrap_tuple(x);
            if (argtype == DST_OAT_TYPE) {
                int32_t i = 0;
                ret = 0;
                for (i = 0; i < dst_tuple_length(t); i++) {
                    ret |= doarg_1(a, DST_OAT_SIMPLETYPE, t[i]);
                }
            } else {
                goto error;
            }
            break;
        }
        case DST_SYMBOL:
        {
            if (NULL != c) {
                DstValue result = dst_table_get(c, x);
                if (dst_checktype(result, DST_INTEGER)) {
                    if (argtype == DST_OAT_LABEL) {
                        ret = dst_unwrap_integer(result) - a->bytecode_count;
                    } else {
                        ret = dst_unwrap_integer(result);
                    }
                } else {
                    dst_asm_errorv(a, dst_formatc("unknown name %q", x));
                }
            } else if (argtype == DST_OAT_TYPE || argtype == DST_OAT_SIMPLETYPE) {
                int32_t index = strsearch(dst_unwrap_symbol(x), dst_type_names);
                if (index != -1) {
                    ret = index;
                } else {
                    dst_asm_errorv(a, dst_formatc("unknown type %q", x));
                }
            } else {
                goto error;
            }
            break;
        }
    }
    if (argtype == DST_OAT_SLOT && ret >= a->def->slotcount)
        a->def->slotcount = (int32_t) ret + 1;
    return ret;

    error:
    dst_asm_errorv(a, dst_formatc("error parsing instruction argument %v", x));
    return 0;
}

/* Parse a single argument to an instruction. Trims it as well as
 * try to convert arguments to bit patterns */
static uint32_t doarg(
        DstAssembler *a,
        DstOpArgType argtype,
        int nth,
        int nbytes,
        int hassign,
        DstValue x) {
    int32_t arg = doarg_1(a, argtype, x);
    /* Calculate the min and max values that can be stored given
     * nbytes, and whether or not the storage is signed */
    int32_t min = (-hassign) << ((nbytes << 3) - 1);
    int32_t max = ~((-1) << ((nbytes << 3) - hassign));
    if (arg < min)
        dst_asm_errorv(a, dst_formatc("instruction argument %v is too small, must be %d byte%s",
                    x, nbytes, nbytes > 1 ? "s" : ""));
    if (arg > max)
        dst_asm_errorv(a, dst_formatc("instruction argument %v is too large, must be %d byte%s",
                    x, nbytes, nbytes > 1 ? "s" : ""));
    return ((uint32_t) arg) << (nth << 3);
}

/* Provide parsing methods for the different kinds of arguments */
static uint32_t read_instruction(DstAssembler *a, const DstInstructionDef *idef, const DstValue *argt) {
    uint32_t instr = idef->opcode;
    switch (idef->type) {
        case DIT_0:
        {
            if (dst_tuple_length(argt) != 1)
                dst_asm_error(a, "expected 0 arguments: (op)");
            break;
        }
        case DIT_S:
        {
            if (dst_tuple_length(argt) != 2)
                dst_asm_error(a, "expected 1 argument: (op, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 3, 0, argt[1]);
            break;
        }
        case DIT_L:
        {
            if (dst_tuple_length(argt) != 2)
                dst_asm_error(a, "expected 1 argument: (op, label)");
            instr |= doarg(a, DST_OAT_LABEL, 1, 3, 1, argt[1]);
            break;
        }
        case DIT_SS:
        {
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_SLOT, 2, 2, 0, argt[2]);
            break;
        }
        case DIT_SL:
        {
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, label)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_LABEL, 2, 2, 1, argt[2]);
            break;
        }
        case DIT_ST:
        {
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, type)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_TYPE, 2, 2, 0, argt[2]);
            break;
        }
        case DIT_SI:
        case DIT_SU:
        {
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, integer)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_INTEGER, 2, 2, idef->type == DIT_SI, argt[2]);
            break;
        }
        case DIT_SSS:
        {
            if (dst_tuple_length(argt) != 4)
                dst_asm_error(a, "expected 3 arguments: (op, slot, slot, slot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_SLOT, 2, 1, 0, argt[2]);
            instr |= doarg(a, DST_OAT_SLOT, 3, 1, 0, argt[3]);
            break;
        }
        case DIT_SSI:
        case DIT_SSU:
        {
            if (dst_tuple_length(argt) != 4)
                dst_asm_error(a, "expected 3 arguments: (op, slot, slot, integer)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_SLOT, 2, 1, 0, argt[2]);
            instr |= doarg(a, DST_OAT_INTEGER, 3, 1, idef->type == DIT_SSI, argt[3]);
            break;
        }
        case DIT_SES:
        {
            DstAssembler *b = a;
            uint32_t env;
            if (dst_tuple_length(argt) != 4)
                dst_asm_error(a, "expected 3 arguments: (op, slot, environment, envslot)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            env = doarg(a, DST_OAT_ENVIRONMENT, 0, 1, 0, argt[2]);
            instr |= env << 16;
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
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, constant)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_CONSTANT, 2, 2, 0, argt[2]);
            break;
        }
    }
    return instr;
}

/* Add a closure environment to the assembler. Sub funcdefs may need
 * to reference outer function environments, and may change the outer environment. 
 * Returns the index of the environment in the assembler's environments, or -1
 * if not found.  */
static int32_t dst_asm_addenv(DstAssembler *a, DstValue envname) {
    DstValue check;
    DstFuncDef *def = a->def;
    int32_t oldlen;
    int64_t res;
    /* Check for memoized value */
    check = dst_table_get(&a->envs, envname);
    if (!dst_checktype(check, DST_NIL)) {
        return dst_unwrap_integer(check);
    }
    if (NULL == a->parent) {
        return -1;
    }
    res = dst_asm_addenv(a->parent, envname);
    if (res < 0)
        return res;
    oldlen = def->environments_length;
    dst_table_put(&a->envs, envname, dst_wrap_integer(def->environments_length));
    if (oldlen >= a->environments_capacity) {
        int32_t newcap = 2 + 2 * oldlen;
        def->environments = realloc(def->environments, newcap * sizeof(int32_t));
        if (NULL == def->environments) {
            DST_OUT_OF_MEMORY;
        }
        a->environments_capacity = newcap;
    }
    def->environments[def->environments_length++] = (int32_t) res;
    return (int32_t) oldlen;
}

/* Helper to assembly. Return the assembly result */
static DstAssembleResult dst_asm1(DstAssembler *parent, DstAssembleOptions opts) {
    DstAssembleResult result;
    DstAssembler a;
    const DstValue *st = dst_unwrap_struct(opts.source);
    DstFuncDef *def;
    int32_t count, i;
    const DstValue *arr;
    DstValue x;

    /* Initialize funcdef */
    def = dst_alloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
    def->environments = NULL;
    def->constants = NULL;
    def->bytecode = NULL;
    def->flags = 0;
    def->slotcount = 0;
    def->arity = 0;
    def->constants_length = 0;
    def->bytecode_length = 0;
    def->environments_length = 1;

    /* Initialize Assembler */
    a.def = def;
    a.parent = parent;
    a.errmessage = NULL;
    a.environments_capacity = 0;
    a.bytecode_count = 0;
    dst_table_init(&a.labels, 10);
    dst_table_init(&a.constants, 10);
    dst_table_init(&a.slots, 10);
    dst_table_init(&a.envs, 10);

    /* Set error jump */
    if (setjmp(a.on_error)) {
        dst_asm_deinit(&a);
        if (NULL != a.parent) {
            longjmp(a.parent->on_error, 1);
        }
        result.result.error = a.errmessage;
        result.status = DST_ASSEMBLE_ERROR;
        return result;
    }

    dst_asm_assert(&a, dst_checktype(opts.source, DST_STRUCT), "expected struct for assembly source");

    /* Set function arity */
    x = dst_struct_get(st, dst_csymbolv("arity"));
    def->arity = dst_checktype(x, DST_INTEGER) ? dst_unwrap_integer(x) : 0;

    /* Create slot aliases */
    x = dst_struct_get(st, dst_csymbolv("slots"));
    if (dst_seq_view(x, &arr, &count)) {
        for (i = 0; i < count; i++) {
            DstValue v = arr[i];
            if (dst_checktype(v, DST_TUPLE)) {
                const DstValue *t = dst_unwrap_tuple(v);
                int32_t j; 
                for (j = 0; j < dst_tuple_length(t); j++) {
                    if (!dst_checktype(t[j], DST_SYMBOL))
                        dst_asm_error(&a, "slot names must be symbols");
                    dst_table_put(&a.slots, t[j], dst_wrap_integer(i));
                }
            } else if (dst_checktype(v, DST_SYMBOL)) {
                dst_table_put(&a.slots, v, dst_wrap_integer(i));
            } else {
                dst_asm_error(&a, "slot names must be symbols or tuple of symbols");
            }
        }
    }

    /* Create environment aliases */
    x = dst_struct_get(st, dst_csymbolv("environments"));
    if (dst_seq_view(x, &arr, &count)) {
        for (i = 0; i < count; i++) {
            dst_asm_assert(&a, dst_checktype(arr[i], DST_SYMBOL), "environment must be a symbol");
            if (dst_asm_addenv(&a, arr[i]) < 0) {
                dst_asm_error(&a, "environment not found");
            }
        }
    }

    /* Parse constants */
    x = dst_struct_get(st, dst_csymbolv("constants"));
    if (dst_seq_view(x, &arr, &count)) {
        def->constants_length = count;
        def->constants = malloc(sizeof(DstValue) * count);
        if (NULL == def->constants) {
            DST_OUT_OF_MEMORY;
        }
        for (i = 0; i < count; i++) {
            DstValue ct = arr[i];
            if (dst_checktype(ct, DST_TUPLE) &&
                dst_tuple_length(dst_unwrap_tuple(ct)) > 1 &&
                dst_checktype(dst_unwrap_tuple(ct)[0], DST_SYMBOL)) {
                const DstValue *t = dst_unwrap_tuple(ct);
                int32_t tcount = dst_tuple_length(t);
                const uint8_t *macro = dst_unwrap_symbol(t[0]);
                if (0 == dst_strcompare(macro, "quote")) {
                    def->constants[i] = t[1];
                } else if (tcount == 3 &&
                        dst_checktype(t[1], DST_SYMBOL) &&
                        0 == dst_strcompare(macro, "def")) {
                    def->constants[i] = t[2];
                    dst_table_put(&a.constants, t[1], dst_wrap_integer(i));
                } else {
                    dst_asm_errorv(&a, dst_formatc("could not parse constant \"%v\"", ct));
                }
                /* Todo - parse nested funcdefs */
            } else {
                def->constants[i] = ct;
            }
        }
    } else {
        def->constants = NULL;
        def->constants_length = 0;
    }

    /* Parse bytecode and labels */
    x = dst_struct_get(st, dst_csymbolv("bytecode"));
    if (dst_seq_view(x, &arr, &count)) {
        /* Do labels and find length */
        int32_t blength = 0;
        for (i = 0; i < count; ++i) {
            DstValue instr = arr[i];
            if (dst_checktype(instr, DST_SYMBOL)) {
                dst_table_put(&a.labels, instr, dst_wrap_integer(blength));
            } else if (dst_checktype(instr, DST_TUPLE)) {
                blength++;
            } else {
                dst_asm_error(&a, "expected assembly instruction");
            }
        }
        /* Allocate bytecode array */
        def->bytecode_length = blength;
        def->bytecode = malloc(sizeof(int32_t) * blength);
        if (NULL == def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        /* Do bytecode */
        for (i = 0; i < count; ++i) {
            DstValue instr = arr[i];
            if (dst_checktype(instr, DST_SYMBOL)) {
                continue;
            } else {
                uint32_t op;
                const DstInstructionDef *idef;
                const DstValue *t;
                dst_asm_assert(&a, dst_checktype(instr, DST_TUPLE), "expected tuple");
                t = dst_unwrap_tuple(instr);
                if (dst_tuple_length(t) == 0) {
                    op = 0;
                } else {
                    dst_asm_assert(&a, dst_checktype(t[0], DST_SYMBOL),
                            "expected symbol in assembly instruction");
                    idef = dst_findi(dst_unwrap_symbol(t[0]));
                    if (NULL == idef)
                        dst_asm_errorv(&a, dst_formatc("unknown instruction %v", instr));
                    op = read_instruction(&a, idef, t);
                }
                def->bytecode[a.bytecode_count++] = op;
            }
        }
    } else {
        dst_asm_error(&a, "bytecode expected");
    }

    /* Finish everything and return funcdef */
    dst_asm_deinit(&a);
    def->environments =
        realloc(def->environments, def->environments_length * sizeof(int32_t));
    result.result.def = def;
    result.status = DST_ASSEMBLE_OK;
    return result;
}

/* Assemble a function */
DstAssembleResult dst_asm(DstAssembleOptions opts) {
    return dst_asm1(NULL, opts);
}

/* Build a function from the result */
DstFunction *dst_asm_func(DstAssembleResult result) {
    if (result.status != DST_ASSEMBLE_OK) {
        return NULL;
    }
    DstFunction *func = dst_alloc(DST_MEMORY_FUNCTION, sizeof(DstFunction));
    func->def = result.result.def;
    func->envs = NULL;
    return func;
}

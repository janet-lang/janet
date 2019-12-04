/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

#include <setjmp.h>

/* Conditionally compile this file */
#ifdef JANET_ASSEMBLER

/* Definition for an instruction in the assembler */
typedef struct JanetInstructionDef JanetInstructionDef;
struct JanetInstructionDef {
    const char *name;
    enum JanetOpCode opcode;
};

/* Hold all state needed during assembly */
typedef struct JanetAssembler JanetAssembler;
struct JanetAssembler {
    JanetAssembler *parent;
    JanetFuncDef *def;
    jmp_buf on_error;
    const uint8_t *errmessage;
    int32_t errindex;

    int32_t environments_capacity;
    int32_t defs_capacity;
    int32_t bytecode_count; /* Used for calculating labels */

    Janet name;
    JanetTable labels; /* keyword -> bytecode index */
    JanetTable constants; /* symbol -> constant index */
    JanetTable slots; /* symbol -> slot index */
    JanetTable envs; /* symbol -> environment index */
    JanetTable defs; /* symbol -> funcdefs index */
};

/* Janet opcode descriptions in lexicographic order. This
 * allows a binary search over the elements to find the
 * correct opcode given a name. This works in reasonable
 * time and is easier to setup statically than a hash table or
 * prefix tree. */
static const JanetInstructionDef janet_ops[] = {
    {"add", JOP_ADD},
    {"addim", JOP_ADD_IMMEDIATE},
    {"band", JOP_BAND},
    {"bnot", JOP_BNOT},
    {"bor", JOP_BOR},
    {"bxor", JOP_BXOR},
    {"call", JOP_CALL},
    {"clo", JOP_CLOSURE},
    {"cmp", JOP_COMPARE},
    {"div", JOP_DIVIDE},
    {"divim", JOP_DIVIDE_IMMEDIATE},
    {"eq", JOP_EQUALS},
    {"eqim", JOP_EQUALS_IMMEDIATE},
    {"eqn", JOP_NUMERIC_EQUAL},
    {"err", JOP_ERROR},
    {"get", JOP_GET},
    {"geti", JOP_GET_INDEX},
    {"gt", JOP_GREATER_THAN},
    {"gten", JOP_NUMERIC_GREATER_THAN_EQUAL},
    {"gtim", JOP_GREATER_THAN_IMMEDIATE},
    {"gtn", JOP_NUMERIC_GREATER_THAN},
    {"in", JOP_IN},
    {"jmp", JOP_JUMP},
    {"jmpif", JOP_JUMP_IF},
    {"jmpno", JOP_JUMP_IF_NOT},
    {"ldc", JOP_LOAD_CONSTANT},
    {"ldf", JOP_LOAD_FALSE},
    {"ldi", JOP_LOAD_INTEGER},
    {"ldn", JOP_LOAD_NIL},
    {"lds", JOP_LOAD_SELF},
    {"ldt", JOP_LOAD_TRUE},
    {"ldu", JOP_LOAD_UPVALUE},
    {"len", JOP_LENGTH},
    {"lt", JOP_LESS_THAN},
    {"lten", JOP_NUMERIC_LESS_THAN_EQUAL},
    {"ltim", JOP_LESS_THAN_IMMEDIATE},
    {"ltn", JOP_NUMERIC_LESS_THAN},
    {"mkarr", JOP_MAKE_ARRAY},
    {"mkbtp", JOP_MAKE_BRACKET_TUPLE},
    {"mkbuf", JOP_MAKE_BUFFER},
    {"mkstr", JOP_MAKE_STRING},
    {"mkstu", JOP_MAKE_STRUCT},
    {"mktab", JOP_MAKE_TABLE},
    {"mktup", JOP_MAKE_TUPLE},
    {"movf", JOP_MOVE_FAR},
    {"movn", JOP_MOVE_NEAR},
    {"mul", JOP_MULTIPLY},
    {"mulim", JOP_MULTIPLY_IMMEDIATE},
    {"noop", JOP_NOOP},
    {"prop", JOP_PROPAGATE},
    {"push", JOP_PUSH},
    {"push2", JOP_PUSH_2},
    {"push3", JOP_PUSH_3},
    {"pusha", JOP_PUSH_ARRAY},
    {"put", JOP_PUT},
    {"puti", JOP_PUT_INDEX},
    {"res", JOP_RESUME},
    {"ret", JOP_RETURN},
    {"retn", JOP_RETURN_NIL},
    {"setu", JOP_SET_UPVALUE},
    {"sig", JOP_SIGNAL},
    {"sl", JOP_SHIFT_LEFT},
    {"slim", JOP_SHIFT_LEFT_IMMEDIATE},
    {"sr", JOP_SHIFT_RIGHT},
    {"srim", JOP_SHIFT_RIGHT_IMMEDIATE},
    {"sru", JOP_SHIFT_RIGHT_UNSIGNED},
    {"sruim", JOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE},
    {"sub", JOP_SUBTRACT},
    {"tcall", JOP_TAILCALL},
    {"tchck", JOP_TYPECHECK}
};

/* Typename aliases for tchck instruction */
typedef struct TypeAlias {
    const char *name;
    int32_t mask;
} TypeAlias;

static const TypeAlias type_aliases[] = {
    {"abstract", JANET_TFLAG_ABSTRACT},
    {"array", JANET_TFLAG_ARRAY},
    {"boolean", JANET_TFLAG_BOOLEAN},
    {"buffer", JANET_TFLAG_BUFFER},
    {"callable", JANET_TFLAG_CALLABLE},
    {"cfunction", JANET_TFLAG_CFUNCTION},
    {"dictionary", JANET_TFLAG_DICTIONARY},
    {"fiber", JANET_TFLAG_FIBER},
    {"function", JANET_TFLAG_FUNCTION},
    {"indexed", JANET_TFLAG_INDEXED},
    {"keyword", JANET_TFLAG_KEYWORD},
    {"nil", JANET_TFLAG_NIL},
    {"number", JANET_TFLAG_NUMBER},
    {"pointer", JANET_TFLAG_POINTER},
    {"string", JANET_TFLAG_STRING},
    {"struct", JANET_TFLAG_STRUCT},
    {"symbol", JANET_TFLAG_SYMBOL},
    {"table", JANET_TFLAG_TABLE},
    {"tuple", JANET_TFLAG_TUPLE}
};

/* Deinitialize an Assembler. Does not deinitialize the parents. */
static void janet_asm_deinit(JanetAssembler *a) {
    janet_table_deinit(&a->slots);
    janet_table_deinit(&a->labels);
    janet_table_deinit(&a->envs);
    janet_table_deinit(&a->constants);
    janet_table_deinit(&a->defs);
}

/* Throw some kind of assembly error */
static void janet_asm_error(JanetAssembler *a, const char *message) {
    a->errmessage = janet_formatc("%s, instruction %d", message, a->errindex);
    longjmp(a->on_error, 1);
}
#define janet_asm_assert(a, c, m) do { if (!(c)) janet_asm_error((a), (m)); } while (0)

/* Throw some kind of assembly error */
static void janet_asm_errorv(JanetAssembler *a, const uint8_t *m) {
    a->errmessage = m;
    longjmp(a->on_error, 1);
}

/* Add a closure environment to the assembler. Sub funcdefs may need
 * to reference outer function environments, and may change the outer environment.
 * Returns the index of the environment in the assembler's environments, or -1
 * if not found. */
static int32_t janet_asm_addenv(JanetAssembler *a, Janet envname) {
    Janet check;
    JanetFuncDef *def = a->def;
    int32_t envindex;
    int32_t res;
    if (janet_equals(a->name, envname)) {
        return -1;
    }
    /* Check for memoized value */
    check = janet_table_get(&a->envs, envname);
    if (janet_checktype(check, JANET_NUMBER)) {
        return (int32_t) janet_unwrap_number(check);
    }
    if (NULL == a->parent) return -2;
    res = janet_asm_addenv(a->parent, envname);
    if (res < -1) {
        return res;
    }
    envindex = def->environments_length;
    janet_table_put(&a->envs, envname, janet_wrap_number(envindex));
    if (envindex >= a->environments_capacity) {
        int32_t newcap = 2 * envindex;
        def->environments = realloc(def->environments, newcap * sizeof(int32_t));
        if (NULL == def->environments) {
            JANET_OUT_OF_MEMORY;
        }
        a->environments_capacity = newcap;
    }
    def->environments[envindex] = (int32_t) res;
    def->environments_length = envindex + 1;
    return envindex;
}

/* Parse an argument to an assembly instruction, and return the result as an
 * integer. This integer will need to be bounds checked. */
static int32_t doarg_1(
    JanetAssembler *a,
    enum JanetOpArgType argtype,
    Janet x) {
    int32_t ret = -1;
    JanetTable *c;
    switch (argtype) {
        default:
            c = NULL;
            break;
        case JANET_OAT_SLOT:
            c = &a->slots;
            break;
        case JANET_OAT_ENVIRONMENT:
            c = &a->envs;
            break;
        case JANET_OAT_CONSTANT:
            c = &a->constants;
            break;
        case JANET_OAT_LABEL:
            c = &a->labels;
            break;
        case JANET_OAT_FUNCDEF:
            c = &a->defs;
            break;
    }
    switch (janet_type(x)) {
        default:
            goto error;
            break;
        case JANET_NUMBER: {
            double y = janet_unwrap_number(x);
            if (janet_checkintrange(y)) {
                ret = (int32_t) y;
            } else {
                goto error;
            }
            break;
        }
        case JANET_TUPLE: {
            const Janet *t = janet_unwrap_tuple(x);
            if (argtype == JANET_OAT_TYPE) {
                int32_t i = 0;
                ret = 0;
                for (i = 0; i < janet_tuple_length(t); i++) {
                    ret |= doarg_1(a, JANET_OAT_SIMPLETYPE, t[i]);
                }
            } else {
                goto error;
            }
            break;
        }
        case JANET_KEYWORD: {
            if (NULL != c && argtype == JANET_OAT_LABEL) {
                Janet result = janet_table_get(c, x);
                if (janet_checktype(result, JANET_NUMBER)) {
                    ret = janet_unwrap_integer(result) - a->bytecode_count;
                } else {
                    goto error;
                }
            } else if (argtype == JANET_OAT_TYPE || argtype == JANET_OAT_SIMPLETYPE) {
                const TypeAlias *alias = janet_strbinsearch(
                                             &type_aliases,
                                             sizeof(type_aliases) / sizeof(TypeAlias),
                                             sizeof(TypeAlias),
                                             janet_unwrap_keyword(x));
                if (alias) {
                    ret = alias->mask;
                } else {
                    janet_asm_errorv(a, janet_formatc("unknown type %v", x));
                }
            } else {
                goto error;
            }
            break;
        }
        case JANET_SYMBOL: {
            if (NULL != c) {
                Janet result = janet_table_get(c, x);
                if (janet_checktype(result, JANET_NUMBER)) {
                    ret = (int32_t) janet_unwrap_number(result);
                } else {
                    janet_asm_errorv(a, janet_formatc("unknown name %v", x));
                }
            } else {
                goto error;
            }
            if (argtype == JANET_OAT_ENVIRONMENT && ret == -1) {
                /* Add a new env */
                ret = janet_asm_addenv(a, x);
                if (ret < -1) {
                    janet_asm_errorv(a, janet_formatc("unknown environment %v", x));
                }
            }
            break;
        }
    }
    if (argtype == JANET_OAT_SLOT && ret >= a->def->slotcount)
        a->def->slotcount = (int32_t) ret + 1;
    return ret;

error:
    janet_asm_errorv(a, janet_formatc("error parsing instruction argument %v", x));
    return 0;
}

/* Parse a single argument to an instruction. Trims it as well as
 * try to convert arguments to bit patterns */
static uint32_t doarg(
    JanetAssembler *a,
    enum JanetOpArgType argtype,
    int nth,
    int nbytes,
    int hassign,
    Janet x) {
    int32_t arg = doarg_1(a, argtype, x);
    /* Calculate the min and max values that can be stored given
     * nbytes, and whether or not the storage is signed */
    int32_t max = (1 << ((nbytes << 3) - hassign)) - 1;
    int32_t min = hassign ? -max - 1 : 0;
    if (arg < min)
        janet_asm_errorv(a, janet_formatc("instruction argument %v is too small, must be %d byte%s",
                                          x, nbytes, nbytes > 1 ? "s" : ""));
    if (arg > max)
        janet_asm_errorv(a, janet_formatc("instruction argument %v is too large, must be %d byte%s",
                                          x, nbytes, nbytes > 1 ? "s" : ""));
    return ((uint32_t) arg) << (nth << 3);
}

/* Provide parsing methods for the different kinds of arguments */
static uint32_t read_instruction(
    JanetAssembler *a,
    const JanetInstructionDef *idef,
    const Janet *argt) {
    uint32_t instr = idef->opcode;
    enum JanetInstructionType type = janet_instructions[idef->opcode];
    switch (type) {
        case JINT_0: {
            if (janet_tuple_length(argt) != 1)
                janet_asm_error(a, "expected 0 arguments: (op)");
            break;
        }
        case JINT_S: {
            if (janet_tuple_length(argt) != 2)
                janet_asm_error(a, "expected 1 argument: (op, slot)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 2, 0, argt[1]);
            break;
        }
        case JINT_L: {
            if (janet_tuple_length(argt) != 2)
                janet_asm_error(a, "expected 1 argument: (op, label)");
            instr |= doarg(a, JANET_OAT_LABEL, 1, 3, 1, argt[1]);
            break;
        }
        case JINT_SS: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, slot)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_SLOT, 2, 2, 0, argt[2]);
            break;
        }
        case JINT_SL: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, label)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_LABEL, 2, 2, 1, argt[2]);
            break;
        }
        case JINT_ST: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, type)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_TYPE, 2, 2, 0, argt[2]);
            break;
        }
        case JINT_SI:
        case JINT_SU: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, integer)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_INTEGER, 2, 2, type == JINT_SI, argt[2]);
            break;
        }
        case JINT_SD: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, funcdef)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_FUNCDEF, 2, 2, 0, argt[2]);
            break;
        }
        case JINT_SSS: {
            if (janet_tuple_length(argt) != 4)
                janet_asm_error(a, "expected 3 arguments: (op, slot, slot, slot)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_SLOT, 2, 1, 0, argt[2]);
            instr |= doarg(a, JANET_OAT_SLOT, 3, 1, 0, argt[3]);
            break;
        }
        case JINT_SSI:
        case JINT_SSU: {
            if (janet_tuple_length(argt) != 4)
                janet_asm_error(a, "expected 3 arguments: (op, slot, slot, integer)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_SLOT, 2, 1, 0, argt[2]);
            instr |= doarg(a, JANET_OAT_INTEGER, 3, 1, type == JINT_SSI, argt[3]);
            break;
        }
        case JINT_SES: {
            JanetAssembler *b = a;
            uint32_t env;
            if (janet_tuple_length(argt) != 4)
                janet_asm_error(a, "expected 3 arguments: (op, slot, environment, envslot)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            env = doarg(a, JANET_OAT_ENVIRONMENT, 0, 1, 0, argt[2]);
            instr |= env << 16;
            for (env += 1; env > 0; env--) {
                b = b->parent;
                if (NULL == b)
                    janet_asm_error(a, "invalid environment index");
            }
            instr |= doarg(b, JANET_OAT_SLOT, 3, 1, 0, argt[3]);
            break;
        }
        case JINT_SC: {
            if (janet_tuple_length(argt) != 3)
                janet_asm_error(a, "expected 2 arguments: (op, slot, constant)");
            instr |= doarg(a, JANET_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, JANET_OAT_CONSTANT, 2, 2, 0, argt[2]);
            break;
        }
    }
    return instr;
}

/* Helper to get from a structure */
static Janet janet_get1(Janet ds, Janet key) {
    switch (janet_type(ds)) {
        default:
            return janet_wrap_nil();
        case JANET_TABLE:
            return janet_table_get(janet_unwrap_table(ds), key);
        case JANET_STRUCT:
            return janet_struct_get(janet_unwrap_struct(ds), key);
    }
}

/* Helper to assembly. Return the assembly result */
static JanetAssembleResult janet_asm1(JanetAssembler *parent, Janet source, int flags) {
    JanetAssembleResult result;
    JanetAssembler a;
    Janet s = source;
    JanetFuncDef *def;
    int32_t count, i;
    const Janet *arr;
    Janet x;
    (void) flags;

    /* Initialize funcdef */
    def = janet_funcdef_alloc();

    /* Initialize Assembler */
    a.def = def;
    a.parent = parent;
    a.errmessage = NULL;
    a.errindex = 0;
    a.environments_capacity = 0;
    a.bytecode_count = 0;
    a.defs_capacity = 0;
    a.name = janet_wrap_nil();
    janet_table_init(&a.labels, 0);
    janet_table_init(&a.constants, 0);
    janet_table_init(&a.slots, 0);
    janet_table_init(&a.envs, 0);
    janet_table_init(&a.defs, 0);

    /* Set error jump */
    if (setjmp(a.on_error)) {
        if (NULL != a.parent) {
            janet_asm_deinit(&a);
            longjmp(a.parent->on_error, 1);
        }
        result.funcdef = NULL;
        result.error = a.errmessage;
        result.status = JANET_ASSEMBLE_ERROR;
        janet_asm_deinit(&a);
        return result;
    }

    janet_asm_assert(&a,
                     janet_checktype(s, JANET_STRUCT) ||
                     janet_checktype(s, JANET_TABLE),
                     "expected struct or table for assembly source");

    /* Check for function name */
    a.name = janet_get1(s, janet_csymbolv("name"));
    if (!janet_checktype(a.name, JANET_NIL)) {
        def->name = janet_to_string(a.name);
    }

    /* Set function arity */
    x = janet_get1(s, janet_csymbolv("arity"));
    def->arity = janet_checkint(x) ? janet_unwrap_integer(x) : 0;
    janet_asm_assert(&a, def->arity >= 0, "arity must be non-negative");

    x = janet_get1(s, janet_csymbolv("max-arity"));
    def->max_arity = janet_checkint(x) ? janet_unwrap_integer(x) : def->arity;
    janet_asm_assert(&a, def->max_arity >= def->arity, "max-arity must be greater than or equal to arity");

    x = janet_get1(s, janet_csymbolv("min-arity"));
    def->min_arity = janet_checkint(x) ? janet_unwrap_integer(x) : def->arity;
    janet_asm_assert(&a, def->min_arity <= def->arity, "min-arity must be less than or equal to arity");

    /* Check vararg */
    x = janet_get1(s, janet_csymbolv("vararg"));
    if (janet_truthy(x)) def->flags |= JANET_FUNCDEF_FLAG_VARARG;

    /* Check source */
    x = janet_get1(s, janet_csymbolv("source"));
    if (janet_checktype(x, JANET_STRING)) def->source = janet_unwrap_string(x);

    /* Create slot aliases */
    x = janet_get1(s, janet_csymbolv("slots"));
    if (janet_indexed_view(x, &arr, &count)) {
        for (i = 0; i < count; i++) {
            Janet v = arr[i];
            if (janet_checktype(v, JANET_TUPLE)) {
                const Janet *t = janet_unwrap_tuple(v);
                int32_t j;
                for (j = 0; j < janet_tuple_length(t); j++) {
                    if (!janet_checktype(t[j], JANET_SYMBOL))
                        janet_asm_error(&a, "slot names must be symbols");
                    janet_table_put(&a.slots, t[j], janet_wrap_integer(i));
                }
            } else if (janet_checktype(v, JANET_SYMBOL)) {
                janet_table_put(&a.slots, v, janet_wrap_integer(i));
            } else {
                janet_asm_error(&a, "slot names must be symbols or tuple of symbols");
            }
        }
    }

    /* Parse constants */
    x = janet_get1(s, janet_csymbolv("constants"));
    if (janet_indexed_view(x, &arr, &count)) {
        def->constants_length = count;
        def->constants = malloc(sizeof(Janet) * count);
        if (NULL == def->constants) {
            JANET_OUT_OF_MEMORY;
        }
        for (i = 0; i < count; i++) {
            Janet ct = arr[i];
            if (janet_checktype(ct, JANET_TUPLE) &&
                    janet_tuple_length(janet_unwrap_tuple(ct)) > 1 &&
                    janet_checktype(janet_unwrap_tuple(ct)[0], JANET_SYMBOL)) {
                const Janet *t = janet_unwrap_tuple(ct);
                int32_t tcount = janet_tuple_length(t);
                const uint8_t *macro = janet_unwrap_symbol(t[0]);
                if (0 == janet_cstrcmp(macro, "quote")) {
                    def->constants[i] = t[1];
                } else if (tcount == 3 &&
                           janet_checktype(t[1], JANET_SYMBOL) &&
                           0 == janet_cstrcmp(macro, "def")) {
                    def->constants[i] = t[2];
                    janet_table_put(&a.constants, t[1], janet_wrap_integer(i));
                } else {
                    janet_asm_errorv(&a, janet_formatc("could not parse constant \"%v\"", ct));
                }
            } else {
                def->constants[i] = ct;
            }
        }
    } else {
        def->constants = NULL;
        def->constants_length = 0;
    }

    /* Parse sub funcdefs */
    x = janet_get1(s, janet_csymbolv("closures"));
    if (janet_indexed_view(x, &arr, &count)) {
        int32_t i;
        for (i = 0; i < count; i++) {
            JanetAssembleResult subres;
            Janet subname;
            int32_t newlen;
            subres = janet_asm1(&a, arr[i], flags);
            if (subres.status != JANET_ASSEMBLE_OK) {
                janet_asm_errorv(&a, subres.error);
            }
            subname = janet_get1(arr[i], janet_csymbolv("name"));
            if (!janet_checktype(subname, JANET_NIL)) {
                janet_table_put(&a.defs, subname, janet_wrap_integer(def->defs_length));
            }
            newlen = def->defs_length + 1;
            if (a.defs_capacity < newlen) {
                int32_t newcap = newlen;
                def->defs = realloc(def->defs, newcap * sizeof(JanetFuncDef *));
                if (NULL == def->defs) {
                    JANET_OUT_OF_MEMORY;
                }
                a.defs_capacity = newcap;
            }
            def->defs[def->defs_length] = subres.funcdef;
            def->defs_length = newlen;
        }
    }

    /* Parse bytecode and labels */
    x = janet_get1(s, janet_csymbolv("bytecode"));
    if (janet_indexed_view(x, &arr, &count)) {
        /* Do labels and find length */
        int32_t blength = 0;
        for (i = 0; i < count; ++i) {
            Janet instr = arr[i];
            if (janet_checktype(instr, JANET_KEYWORD)) {
                janet_table_put(&a.labels, instr, janet_wrap_integer(blength));
            } else if (janet_checktype(instr, JANET_TUPLE)) {
                blength++;
            } else {
                a.errindex = i;
                janet_asm_error(&a, "expected assembly instruction");
            }
        }
        /* Allocate bytecode array */
        def->bytecode_length = blength;
        def->bytecode = malloc(sizeof(uint32_t) * blength);
        if (NULL == def->bytecode) {
            JANET_OUT_OF_MEMORY;
        }
        /* Do bytecode */
        for (i = 0; i < count; ++i) {
            Janet instr = arr[i];
            if (janet_checktype(instr, JANET_KEYWORD)) {
                continue;
            } else {
                uint32_t op;
                const JanetInstructionDef *idef;
                const Janet *t;
                a.errindex = i;
                janet_asm_assert(&a, janet_checktype(instr, JANET_TUPLE), "expected tuple");
                t = janet_unwrap_tuple(instr);
                if (janet_tuple_length(t) == 0) {
                    op = 0;
                } else {
                    janet_asm_assert(&a, janet_checktype(t[0], JANET_SYMBOL),
                                     "expected symbol in assembly instruction");
                    idef = janet_strbinsearch(
                               &janet_ops,
                               sizeof(janet_ops) / sizeof(JanetInstructionDef),
                               sizeof(JanetInstructionDef),
                               janet_unwrap_symbol(t[0]));
                    if (NULL == idef)
                        janet_asm_errorv(&a, janet_formatc("unknown instruction %v", t[0]));
                    op = read_instruction(&a, idef, t);
                }
                def->bytecode[a.bytecode_count++] = op;
            }
        }
    } else {
        janet_asm_error(&a, "bytecode expected");
    }
    a.errindex = -1;

    /* Check for source mapping */
    x = janet_get1(s, janet_csymbolv("sourcemap"));
    if (janet_indexed_view(x, &arr, &count)) {
        janet_asm_assert(&a, count == def->bytecode_length, "sourcemap must have the same length as the bytecode");
        def->sourcemap = malloc(sizeof(JanetSourceMapping) * count);
        for (i = 0; i < count; i++) {
            const Janet *tup;
            Janet entry = arr[i];
            JanetSourceMapping mapping;
            if (!janet_checktype(entry, JANET_TUPLE)) {
                janet_asm_error(&a, "expected tuple");
            }
            tup = janet_unwrap_tuple(entry);
            if (!janet_checkint(tup[0])) {
                janet_asm_error(&a, "expected integer");
            }
            if (!janet_checkint(tup[1])) {
                janet_asm_error(&a, "expected integer");
            }
            mapping.line = janet_unwrap_integer(tup[0]);
            mapping.column = janet_unwrap_integer(tup[1]);
            def->sourcemap[i] = mapping;
        }
    }

    /* Set environments */
    def->environments =
        realloc(def->environments, def->environments_length * sizeof(int32_t));

    /* Verify the func def */
    if (janet_verify(def)) {
        janet_asm_error(&a, "invalid assembly");
    }

    /* Finish everything and return funcdef */
    janet_asm_deinit(&a);
    result.error = NULL;
    result.funcdef = def;
    result.status = JANET_ASSEMBLE_OK;
    return result;
}

/* Assemble a function */
JanetAssembleResult janet_asm(Janet source, int flags) {
    return janet_asm1(NULL, source, flags);
}

/* Disassembly */

/* Find the definition of an instruction given the instruction word. Return
 * NULL if not found. */
static const JanetInstructionDef *janet_asm_reverse_lookup(uint32_t instr) {
    size_t i;
    uint32_t opcode = instr & 0x7F;
    for (i = 0; i < sizeof(janet_ops) / sizeof(JanetInstructionDef); i++) {
        const JanetInstructionDef *def = janet_ops + i;
        if (def->opcode == opcode)
            return def;
    }
    return NULL;
}

/* Create some constant sized tuples */
static const Janet *tup1(Janet x) {
    Janet *tup = janet_tuple_begin(1);
    tup[0] = x;
    return janet_tuple_end(tup);
}
static const Janet *tup2(Janet x, Janet y) {
    Janet *tup = janet_tuple_begin(2);
    tup[0] = x;
    tup[1] = y;
    return janet_tuple_end(tup);
}
static const Janet *tup3(Janet x, Janet y, Janet z) {
    Janet *tup = janet_tuple_begin(3);
    tup[0] = x;
    tup[1] = y;
    tup[2] = z;
    return janet_tuple_end(tup);
}
static const Janet *tup4(Janet w, Janet x, Janet y, Janet z) {
    Janet *tup = janet_tuple_begin(4);
    tup[0] = w;
    tup[1] = x;
    tup[2] = y;
    tup[3] = z;
    return janet_tuple_end(tup);
}

/* Given an argument, convert it to the appropriate integer or symbol */
Janet janet_asm_decode_instruction(uint32_t instr) {
    const JanetInstructionDef *def = janet_asm_reverse_lookup(instr);
    Janet name;
    if (NULL == def) {
        return janet_wrap_integer((int32_t)instr);
    }
    name = janet_csymbolv(def->name);
    const Janet *ret = NULL;
#define oparg(shift, mask) ((instr >> ((shift) << 3)) & (mask))
    switch (janet_instructions[def->opcode]) {
        case JINT_0:
            ret = tup1(name);
            break;
        case JINT_S:
            ret = tup2(name, janet_wrap_integer(oparg(1, 0xFFFFFF)));
            break;
        case JINT_L:
            ret = tup2(name, janet_wrap_integer((int32_t)instr >> 8));
            break;
        case JINT_SS:
        case JINT_ST:
        case JINT_SC:
        case JINT_SU:
        case JINT_SD:
            ret = tup3(name,
                       janet_wrap_integer(oparg(1, 0xFF)),
                       janet_wrap_integer(oparg(2, 0xFFFF)));
            break;
        case JINT_SI:
        case JINT_SL:
            ret =  tup3(name,
                        janet_wrap_integer(oparg(1, 0xFF)),
                        janet_wrap_integer((int32_t)instr >> 16));
            break;
        case JINT_SSS:
        case JINT_SES:
        case JINT_SSU:
            ret = tup4(name,
                       janet_wrap_integer(oparg(1, 0xFF)),
                       janet_wrap_integer(oparg(2, 0xFF)),
                       janet_wrap_integer(oparg(3, 0xFF)));
            break;
        case JINT_SSI:
            ret = tup4(name,
                       janet_wrap_integer(oparg(1, 0xFF)),
                       janet_wrap_integer(oparg(2, 0xFF)),
                       janet_wrap_integer((int32_t)instr >> 24));
            break;
    }
#undef oparg
    if (ret) {
        /* Check if break point set */
        if (instr & 0x80) {
            janet_tuple_flag(ret) |= JANET_TUPLE_FLAG_BRACKETCTOR;
        }
        return janet_wrap_tuple(ret);
    }
    return janet_wrap_nil();
}

Janet janet_disasm(JanetFuncDef *def) {
    int32_t i;
    JanetArray *bcode = janet_array(def->bytecode_length);
    JanetArray *constants;
    JanetTable *ret = janet_table(10);
    janet_table_put(ret, janet_csymbolv("arity"), janet_wrap_integer(def->arity));
    janet_table_put(ret, janet_csymbolv("min-arity"), janet_wrap_integer(def->min_arity));
    janet_table_put(ret, janet_csymbolv("max-arity"), janet_wrap_integer(def->max_arity));
    janet_table_put(ret, janet_csymbolv("bytecode"), janet_wrap_array(bcode));
    if (NULL != def->source) {
        janet_table_put(ret, janet_csymbolv("source"), janet_wrap_string(def->source));
    }
    if (def->flags & JANET_FUNCDEF_FLAG_VARARG) {
        janet_table_put(ret, janet_csymbolv("vararg"), janet_wrap_true());
    }
    if (NULL != def->name) {
        janet_table_put(ret, janet_csymbolv("name"), janet_wrap_string(def->name));
    }

    /* Add constants */
    if (def->constants_length > 0) {
        constants = janet_array(def->constants_length);
        janet_table_put(ret, janet_csymbolv("constants"), janet_wrap_array(constants));
        for (i = 0; i < def->constants_length; i++) {
            Janet src = def->constants[i];
            Janet dest;
            if (janet_checktype(src, JANET_TUPLE)) {
                dest = janet_wrap_tuple(tup2(janet_csymbolv("quote"), src));
            } else {
                dest = src;
            }
            constants->data[i] = dest;
        }
        constants->count = def->constants_length;
    }

    /* Add bytecode */
    for (i = 0; i < def->bytecode_length; i++) {
        bcode->data[i] = janet_asm_decode_instruction(def->bytecode[i]);
    }
    bcode->count = def->bytecode_length;

    /* Add source map */
    if (NULL != def->sourcemap) {
        JanetArray *sourcemap = janet_array(def->bytecode_length);
        for (i = 0; i < def->bytecode_length; i++) {
            Janet *t = janet_tuple_begin(2);
            JanetSourceMapping mapping = def->sourcemap[i];
            t[0] = janet_wrap_integer(mapping.line);
            t[1] = janet_wrap_integer(mapping.column);
            sourcemap->data[i] = janet_wrap_tuple(janet_tuple_end(t));
        }
        sourcemap->count = def->bytecode_length;
        janet_table_put(ret, janet_csymbolv("sourcemap"), janet_wrap_array(sourcemap));
    }

    /* Add environments */
    if (NULL != def->environments) {
        JanetArray *envs = janet_array(def->environments_length);
        for (i = 0; i < def->environments_length; i++) {
            envs->data[i] = janet_wrap_integer(def->environments[i]);
        }
        envs->count = def->environments_length;
        janet_table_put(ret, janet_csymbolv("environments"), janet_wrap_array(envs));
    }

    /* Add closures */
    /* Funcdefs cannot be recursive */
    if (NULL != def->defs) {
        JanetArray *defs = janet_array(def->defs_length);
        for (i = 0; i < def->defs_length; i++) {
            defs->data[i] = janet_disasm(def->defs[i]);
        }
        defs->count = def->defs_length;
        janet_table_put(ret, janet_csymbolv("defs"), janet_wrap_array(defs));
    }

    /* Add slotcount */
    janet_table_put(ret, janet_csymbolv("slotcount"), janet_wrap_integer(def->slotcount));

    return janet_wrap_struct(janet_table_to_struct(ret));
}

/* C Function for assembly */
static Janet cfun_asm(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 1);
    JanetAssembleResult res;
    res = janet_asm(argv[0], 0);
    if (res.status != JANET_ASSEMBLE_OK) {
        janet_panics(res.error);
    }
    return janet_wrap_function(janet_thunk(res.funcdef));
}

static Janet cfun_disasm(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 1);
    JanetFunction *f = janet_getfunction(argv, 0);
    return janet_disasm(f->def);
}

static const JanetReg asm_cfuns[] = {
    {
        "asm", cfun_asm,
        JDOC("(asm assembly)\n\n"
             "Returns a new function that is the compiled result of the assembly.\n"
             "The syntax for the assembly can be found on the janet wiki. Will throw an\n"
             "error on invalid assembly.")
    },
    {
        "disasm", cfun_disasm,
        JDOC("(disasm func)\n\n"
             "Returns assembly that could be used be compile the given function.\n"
             "func must be a function, not a c function. Will throw on error on a badly\n"
             "typed argument.")
    },
    {NULL, NULL, NULL}
};

/* Load the library */
void janet_lib_asm(JanetTable *env) {
    janet_core_cfuns(env, NULL, asm_cfuns);
}

#endif

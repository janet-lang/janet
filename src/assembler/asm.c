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
#include <dst/dstasm.h>
#include <dst/dstopcodes.h>
#include <headerlibs/strbinsearch.h>

/* Convert a slot to to an integer for bytecode */

/* Types of instructions (some of them) */
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

/* Definition for an instruction in the assembler */
typedef struct DstInstructionDef DstInstructionDef;
struct DstInstructionDef {
    const char *name;
    enum DstOpCode opcode;
};

/* Hold all state needed during assembly */
typedef struct DstAssembler DstAssembler;
struct DstAssembler {
    DstAssembler *parent;
    DstFuncDef *def;
    jmp_buf on_error;
    const uint8_t *errmessage;
    int32_t errindex;

    int32_t environments_capacity;
    int32_t defs_capacity;
    int32_t bytecode_count; /* Used for calculating labels */

    Dst name;
    DstTable labels; /* symbol -> bytecode index */
    DstTable constants; /* symbol -> constant index */
    DstTable slots; /* symbol -> slot index */
    DstTable envs; /* symbol -> environment index */
    DstTable defs; /* symbol -> funcdefs index */
};

/* Dst opcode descriptions in lexographic order. This
 * allows a binary search over the elements to find the
 * correct opcode given a name. This works in reasonable
 * time and is easier to setup statically than a hash table or
 * prefix tree. */
static const DstInstructionDef dst_ops[] = {
    {"add", DOP_ADD},
    {"addi", DOP_ADD_INTEGER},
    {"addim", DOP_ADD_IMMEDIATE},
    {"addr", DOP_ADD_REAL},
    {"band", DOP_BAND},
    {"bnot", DOP_BNOT},
    {"bor", DOP_BOR},
    {"bxor", DOP_BXOR},
    {"call", DOP_CALL},
    {"clo", DOP_CLOSURE},
    {"cmp", DOP_COMPARE},
    {"debug", DOP_DEBUG},
    {"div", DOP_DIVIDE},
    {"divi", DOP_DIVIDE_INTEGER},
    {"divim", DOP_DIVIDE_IMMEDIATE},
    {"divr", DOP_DIVIDE_REAL},
    {"eq", DOP_EQUALS},
    {"eqi", DOP_EQUALS_INTEGER},
    {"eqim", DOP_EQUALS_IMMEDIATE},
    {"eqr", DOP_EQUALS_REAL},
    {"err", DOP_ERROR},
    {"get", DOP_GET},
    {"geti", DOP_GET_INDEX},
    {"gt", DOP_GREATER_THAN},
    {"gti", DOP_GREATER_THAN_INTEGER},
    {"gtim", DOP_GREATER_THAN_IMMEDIATE},
    {"gtr", DOP_GREATER_THAN_REAL},
    {"gter", DOP_GREATER_THAN_EQUAL_REAL},
    {"jmp", DOP_JUMP},
    {"jmpif", DOP_JUMP_IF},
    {"jmpno", DOP_JUMP_IF_NOT},
    {"ldc", DOP_LOAD_CONSTANT},
    {"ldf", DOP_LOAD_FALSE},
    {"ldi", DOP_LOAD_INTEGER},
    {"ldn", DOP_LOAD_NIL},
    {"lds", DOP_LOAD_SELF},
    {"ldt", DOP_LOAD_TRUE},
    {"ldu", DOP_LOAD_UPVALUE},
    {"lt", DOP_LESS_THAN},
    {"lti", DOP_LESS_THAN_INTEGER},
    {"ltim", DOP_LESS_THAN_IMMEDIATE},
    {"ltr", DOP_LESS_THAN_REAL},
    {"lter", DOP_LESS_THAN_EQUAL_REAL},
    {"movf", DOP_MOVE_FAR},
    {"movn", DOP_MOVE_NEAR},
    {"mul", DOP_MULTIPLY},
    {"muli", DOP_MULTIPLY_INTEGER},
    {"mulim", DOP_MULTIPLY_IMMEDIATE},
    {"mulr", DOP_MULTIPLY_REAL},
    {"noop", DOP_NOOP},
    {"push", DOP_PUSH},
    {"push2", DOP_PUSH_2},
    {"push3", DOP_PUSH_3},
    {"pusha", DOP_PUSH_ARRAY},
    {"put", DOP_PUT},
    {"puti", DOP_PUT_INDEX},
    {"res", DOP_RESUME},
    {"ret", DOP_RETURN},
    {"retn", DOP_RETURN_NIL},
    {"setu", DOP_SET_UPVALUE},
    {"sl", DOP_SHIFT_LEFT},
    {"slim", DOP_SHIFT_LEFT_IMMEDIATE},
    {"sr", DOP_SHIFT_RIGHT},
    {"srim", DOP_SHIFT_RIGHT_IMMEDIATE},
    {"sru", DOP_SHIFT_RIGHT_UNSIGNED},
    {"sruim", DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE},
    {"sub", DOP_SUBTRACT},
    {"tcall", DOP_TAILCALL},
    {"tchck", DOP_TYPECHECK},
    {"yield", DOP_YIELD}
};

/* Typename aliases for tchck instruction */
typedef struct TypeAlias {
    const char *name;
    int32_t mask;
} TypeAlias;

static const TypeAlias type_aliases[] = {
    {":abstract", DST_TFLAG_ABSTRACT},
    {":array", DST_TFLAG_ARRAY},
    {":boolean", DST_TFLAG_BOOLEAN},
    {":buffer", DST_TFLAG_BUFFER},
    {":callable", DST_TFLAG_CALLABLE},
    {":cfunction", DST_TFLAG_CFUNCTION},
    {":dictionary", DST_TFLAG_DICTIONARY},
    {":false", DST_TFLAG_FALSE},
    {":fiber", DST_TFLAG_FIBER},
    {":function", DST_TFLAG_FUNCTION},
    {":indexed", DST_TFLAG_INDEXED},
    {":integer", DST_TFLAG_INTEGER},
    {":nil", DST_TFLAG_NIL},
    {":number", DST_TFLAG_NUMBER},
    {":real", DST_TFLAG_REAL},
    {":string", DST_TFLAG_STRING},
    {":struct", DST_TFLAG_STRUCT},
    {":symbol", DST_TFLAG_SYMBOL},
    {":table", DST_TFLAG_BOOLEAN},
    {":true", DST_TFLAG_TRUE},
    {":tuple", DST_TFLAG_BOOLEAN}
};

/* Deinitialize an Assembler. Does not deinitialize the parents. */
static void dst_asm_deinit(DstAssembler *a) {
    dst_table_deinit(&a->slots);
    dst_table_deinit(&a->labels);
    dst_table_deinit(&a->envs);
    dst_table_deinit(&a->constants);
    dst_table_deinit(&a->defs);
}

/* Throw some kind of assembly error */
static void dst_asm_error(DstAssembler *a, const char *message) {
    a->errmessage = dst_formatc("%s, instruction %d", message, a->errindex);
    longjmp(a->on_error, 1);
}
#define dst_asm_assert(a, c, m) do { if (!(c)) dst_asm_error((a), (m)); } while (0)

/* Throw some kind of assembly error */
static void dst_asm_errorv(DstAssembler *a, const uint8_t *m) {
    a->errmessage = m;
    longjmp(a->on_error, 1);
}

/* Add a closure environment to the assembler. Sub funcdefs may need
 * to reference outer function environments, and may change the outer environment. 
 * Returns the index of the environment in the assembler's environments, or -1
 * if not found.  */
static int32_t dst_asm_addenv(DstAssembler *a, Dst envname) {
    Dst check;
    DstFuncDef *def = a->def;
    int32_t envindex;
    int32_t res;
    if (dst_equals(a->name, envname)) {
        return -1;
    }
    /* Check for memoized value */
    check = dst_table_get(&a->envs, envname);
    if (dst_checktype(check, DST_INTEGER)) {
        return dst_unwrap_integer(check);
    }
    if (NULL == a->parent) return -2;
    res = dst_asm_addenv(a->parent, envname);
    if (res < -1) {
        return res;
    }
    envindex = def->environments_length;
    dst_table_put(&a->envs, envname, dst_wrap_integer(envindex));
    if (envindex >= a->environments_capacity) {
        int32_t newcap = 2 * envindex;
        def->environments = realloc(def->environments, newcap * sizeof(int32_t));
        if (NULL == def->environments) {
            DST_OUT_OF_MEMORY;
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
        DstAssembler *a,
        enum DstOpArgType argtype,
        Dst x) {
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
        case DST_OAT_FUNCDEF:
            c = &a->defs;
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
            const Dst *t = dst_unwrap_tuple(x);
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
                Dst result = dst_table_get(c, x);
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
                const TypeAlias *alias = dst_strbinsearch(
                            &type_aliases,
                            sizeof(type_aliases)/sizeof(TypeAlias),
                            sizeof(TypeAlias),
                            dst_unwrap_symbol(x));
                if (alias) {
                    ret = alias->mask;
                } else {
                    dst_asm_errorv(a, dst_formatc("unknown type %q", x));
                }
            } else {
                goto error;
            }
            if (argtype == DST_OAT_ENVIRONMENT && ret == -1) {
                /* Add a new env */
                ret = dst_asm_addenv(a, x);
                if (ret < -1) {
                    dst_asm_errorv(a, dst_formatc("unknown environment %q", x));
                }
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
        enum DstOpArgType argtype,
        int nth,
        int nbytes,
        int hassign,
        Dst x) {
    int32_t arg = doarg_1(a, argtype, x);
    /* Calculate the min and max values that can be stored given
     * nbytes, and whether or not the storage is signed */
    int32_t max = (1 << ((nbytes << 3) - hassign)) - 1;
    int32_t min = hassign ? -max - 1 : 0;
    if (arg < min)
        dst_asm_errorv(a, dst_formatc("instruction argument %v is too small, must be %d byte%s",
                    x, nbytes, nbytes > 1 ? "s" : ""));
    if (arg > max)
        dst_asm_errorv(a, dst_formatc("instruction argument %v is too large, must be %d byte%s",
                    x, nbytes, nbytes > 1 ? "s" : ""));
    return ((uint32_t) arg) << (nth << 3);
}

/* Provide parsing methods for the different kinds of arguments */
static uint32_t read_instruction(
        DstAssembler *a, 
        const DstInstructionDef *idef,
        const Dst *argt) {
    uint32_t instr = idef->opcode;
    enum DstInstructionType type = dst_instructions[idef->opcode];
    switch (type) {
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
            instr |= doarg(a, DST_OAT_INTEGER, 2, 2, type == DIT_SI, argt[2]);
            break;
        }
        case DIT_SD:
        {
            if (dst_tuple_length(argt) != 3)
                dst_asm_error(a, "expected 2 arguments: (op, slot, funcdef)");
            instr |= doarg(a, DST_OAT_SLOT, 1, 1, 0, argt[1]);
            instr |= doarg(a, DST_OAT_FUNCDEF, 2, 2, 0, argt[2]);
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
            instr |= doarg(a, DST_OAT_INTEGER, 3, 1, type == DIT_SSI, argt[3]);
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

/* Helper to assembly. Return the assembly result */
static DstAssembleResult dst_asm1(DstAssembler *parent, Dst source, int flags) {
    DstAssembleResult result;
    DstAssembler a;
    Dst s = source;
    DstFuncDef *def;
    int32_t count, i;
    const Dst *arr;
    Dst x;
    (void) flags;

    /* Initialize funcdef */
    def = dst_funcdef_alloc();

    /* Initialize Assembler */
    a.def = def;
    a.parent = parent;
    a.errmessage = NULL;
    a.errindex = 0;
    a.environments_capacity = 0;
    a.bytecode_count = 0;
    a.defs_capacity = 0;
    a.name = dst_wrap_nil();
    dst_table_init(&a.labels, 0);
    dst_table_init(&a.constants, 0);
    dst_table_init(&a.slots, 0);
    dst_table_init(&a.envs, 0);
    dst_table_init(&a.defs, 0);

    /* Set error jump */
    if (setjmp(a.on_error)) {
        if (NULL != a.parent) {
            dst_asm_deinit(&a);
            longjmp(a.parent->on_error, 1);
        }
        result.error = a.errmessage;
        result.status = DST_ASSEMBLE_ERROR;
        dst_asm_deinit(&a);
        return result;
    }

    dst_asm_assert(&a, 
            dst_checktype(s, DST_STRUCT) ||
            dst_checktype(s, DST_TABLE),
            "expected struct or table for assembly source");

    /* Check for function name */
    a.name = dst_get(s, dst_csymbolv("name"));

    /* Set function arity */
    x = dst_get(s, dst_csymbolv("arity"));
    def->arity = dst_checktype(x, DST_INTEGER) ? dst_unwrap_integer(x) : 0;

    /* Check vararg */
    x = dst_get(s, dst_csymbolv("vararg"));
    if (dst_truthy(x)) def->flags |= DST_FUNCDEF_FLAG_VARARG;

    /* Check source */
    x = dst_get(s, dst_csymbolv("source"));
    if (dst_checktype(x, DST_STRING)) def->source = dst_unwrap_string(x);

    /* Check source path */
    x = dst_get(s, dst_csymbolv("sourcepath"));
    if (dst_checktype(x, DST_STRING)) def->sourcepath = dst_unwrap_string(x);

    /* Create slot aliases */
    x = dst_get(s, dst_csymbolv("slots"));
    if (dst_seq_view(x, &arr, &count)) {
        for (i = 0; i < count; i++) {
            Dst v = arr[i];
            if (dst_checktype(v, DST_TUPLE)) {
                const Dst *t = dst_unwrap_tuple(v);
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

    /* Parse constants */
    x = dst_get(s, dst_csymbolv("constants"));
    if (dst_seq_view(x, &arr, &count)) {
        def->constants_length = count;
        def->constants = malloc(sizeof(Dst) * count);
        if (NULL == def->constants) {
            DST_OUT_OF_MEMORY;
        }
        for (i = 0; i < count; i++) {
            Dst ct = arr[i];
            if (dst_checktype(ct, DST_TUPLE) &&
                dst_tuple_length(dst_unwrap_tuple(ct)) > 1 &&
                dst_checktype(dst_unwrap_tuple(ct)[0], DST_SYMBOL)) {
                const Dst *t = dst_unwrap_tuple(ct);
                int32_t tcount = dst_tuple_length(t);
                const uint8_t *macro = dst_unwrap_symbol(t[0]);
                if (0 == dst_cstrcmp(macro, "quote")) {
                    def->constants[i] = t[1];
                } else if (tcount == 3 &&
                        dst_checktype(t[1], DST_SYMBOL) &&
                        0 == dst_cstrcmp(macro, "def")) {
                    def->constants[i] = t[2];
                    dst_table_put(&a.constants, t[1], dst_wrap_integer(i));
                } else {
                    dst_asm_errorv(&a, dst_formatc("could not parse constant \"%v\"", ct));
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
    x = dst_get(s, dst_csymbolv("closures"));
    if (dst_seq_view(x, &arr, &count)) {
        int32_t i;
        for (i = 0; i < count; i++) {
            DstAssembleResult subres;
            Dst subname;
            int32_t newlen;
            subres = dst_asm1(&a, arr[i], flags);
            if (subres.status != DST_ASSEMBLE_OK) {
                dst_asm_errorv(&a, subres.error);
            }
            subname = dst_get(arr[i], dst_csymbolv("name"));
            if (!dst_checktype(subname, DST_NIL)) {
                dst_table_put(&a.defs, subname, dst_wrap_integer(def->defs_length));
            }
            newlen = def->defs_length + 1;
            if (a.defs_capacity < newlen) {
                int32_t newcap = newlen;
                def->defs = realloc(def->defs, newcap * sizeof(DstFuncDef *));
                if (NULL == def->defs) {
                    DST_OUT_OF_MEMORY;
                }
                a.defs_capacity = newcap;
            }
            def->defs[def->defs_length] = subres.funcdef;
            def->defs_length = newlen;
        }
    }

    /* Parse bytecode and labels */
    x = dst_get(s, dst_csymbolv("bytecode"));
    if (dst_seq_view(x, &arr, &count)) {
        /* Do labels and find length */
        int32_t blength = 0;
        for (i = 0; i < count; ++i) {
            Dst instr = arr[i];
            if (dst_checktype(instr, DST_SYMBOL)) {
                dst_table_put(&a.labels, instr, dst_wrap_integer(blength));
            } else if (dst_checktype(instr, DST_TUPLE)) {
                blength++;
            } else {
                a.errindex = i;
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
            Dst instr = arr[i];
            if (dst_checktype(instr, DST_SYMBOL)) {
                continue;
            } else {
                uint32_t op;
                const DstInstructionDef *idef;
                const Dst *t;
                a.errindex = i;
                dst_asm_assert(&a, dst_checktype(instr, DST_TUPLE), "expected tuple");
                t = dst_unwrap_tuple(instr);
                if (dst_tuple_length(t) == 0) {
                    op = 0;
                } else {
                    dst_asm_assert(&a, dst_checktype(t[0], DST_SYMBOL),
                            "expected symbol in assembly instruction");
                    idef = dst_strbinsearch(
                            &dst_ops,
                            sizeof(dst_ops)/sizeof(DstInstructionDef),
                            sizeof(DstInstructionDef),
                            dst_unwrap_symbol(t[0]));
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
    a.errindex = -1;
    
    /* Check for source mapping */
    x = dst_get(s, dst_csymbolv("sourcemap"));
    if (dst_seq_view(x, &arr, &count)) {
        dst_asm_assert(&a, count == def->bytecode_length, "sourcemap must have the same length as the bytecode");
        def->sourcemap = malloc(sizeof(DstSourceMapping) * count);
        for (i = 0; i < count; i++) {
            const Dst *tup;
            Dst entry = arr[i];
            DstSourceMapping mapping;
            if (!dst_checktype(entry, DST_TUPLE)) {
                dst_asm_error(&a, "expected tuple");
            }
            tup = dst_unwrap_tuple(entry);
            if (!dst_checktype(tup[0], DST_INTEGER)) {
                dst_asm_error(&a, "expected integer");
            }
            if (!dst_checktype(tup[1], DST_INTEGER)) {
                dst_asm_error(&a, "expected integer");
            }
            mapping.start = dst_unwrap_integer(tup[0]);
            mapping.end = dst_unwrap_integer(tup[1]);
            def->sourcemap[i] = mapping;
        }
    }

    /* Set environments */
    def->environments =
        realloc(def->environments, def->environments_length * sizeof(int32_t));

    /* Verify the func def */
    if (dst_verify(def)) {
        dst_asm_error(&a, "invalid assembly");
    }

    /* Finish everything and return funcdef */
    dst_asm_deinit(&a);
    result.funcdef = def;
    result.status = DST_ASSEMBLE_OK;
    return result;
}

/* Assemble a function */
DstAssembleResult dst_asm(Dst source, int flags) {
    return dst_asm1(NULL, source, flags);
}

/* Disassembly */

/* Find the deinfintion of an instruction given the instruction word. Return
 * NULL if not found. */
static const DstInstructionDef *dst_asm_reverse_lookup(uint32_t instr) {
    size_t i;
    uint32_t opcode = instr & 0x7F;
    for (i = 0; i < sizeof(dst_ops)/sizeof(DstInstructionDef); i++) {
        const DstInstructionDef *def = dst_ops + i;
        if (def->opcode == opcode) 
            return def;
    }
    return NULL;
}

/* Create some constant sized tuples */
static Dst tup1(Dst x) {
    Dst *tup = dst_tuple_begin(1);
    tup[0] = x;
    return dst_wrap_tuple(dst_tuple_end(tup));
}
static Dst tup2(Dst x, Dst y) {
    Dst *tup = dst_tuple_begin(2);
    tup[0] = x;
    tup[1] = y;
    return dst_wrap_tuple(dst_tuple_end(tup));
}
static Dst tup3(Dst x, Dst y, Dst z) {
    Dst *tup = dst_tuple_begin(3);
    tup[0] = x;
    tup[1] = y;
    tup[2] = z;
    return dst_wrap_tuple(dst_tuple_end(tup));
}
static Dst tup4(Dst w, Dst x, Dst y, Dst z) {
    Dst *tup = dst_tuple_begin(4);
    tup[0] = w;
    tup[1] = x;
    tup[2] = y;
    tup[3] = z;
    return dst_wrap_tuple(dst_tuple_end(tup));
}

/* Given an argument, convert it to the appriate integer or symbol */
Dst dst_asm_decode_instruction(uint32_t instr) {
    const DstInstructionDef *def = dst_asm_reverse_lookup(instr);
    Dst name;
    if (NULL == def) {
        return dst_wrap_integer((int32_t)instr);
    }
    name = dst_csymbolv(def->name);
#define oparg(shift, mask) ((instr >> ((shift) << 3)) & (mask))
    switch (dst_instructions[def->opcode]) {
        case DIT_0:
            return tup1(name);
        case DIT_S:
            return tup2(name, dst_wrap_integer(oparg(1, 0xFFFFFF)));
        case DIT_L:
            return tup2(name, dst_wrap_integer((int32_t)instr >> 8));
        case DIT_SS:
        case DIT_ST:
        case DIT_SC:
        case DIT_SU:
        case DIT_SD:
            return tup3(name, 
                    dst_wrap_integer(oparg(1, 0xFF)),
                    dst_wrap_integer(oparg(2, 0xFFFF)));
        case DIT_SI:
        case DIT_SL:
            return tup3(name, 
                    dst_wrap_integer(oparg(1, 0xFF)),
                    dst_wrap_integer((int32_t)instr >> 16));
        case DIT_SSS:
        case DIT_SES:
        case DIT_SSU:
            return tup4(name, 
                    dst_wrap_integer(oparg(1, 0xFF)),
                    dst_wrap_integer(oparg(2, 0xFF)),
                    dst_wrap_integer(oparg(3, 0xFF)));
        case DIT_SSI:
            return tup4(name, 
                    dst_wrap_integer(oparg(1, 0xFF)),
                    dst_wrap_integer(oparg(2, 0xFF)),
                    dst_wrap_integer((int32_t)instr >> 24));
    }
#undef oparg
    return dst_wrap_nil();
}

Dst dst_disasm(DstFuncDef *def) {
    int32_t i;
    DstArray *bcode = dst_array(def->bytecode_length);
    DstArray *constants;
    DstTable *ret = dst_table(10);
    dst_table_put(ret, dst_csymbolv("arity"), dst_wrap_integer(def->arity));
    dst_table_put(ret, dst_csymbolv("bytecode"), dst_wrap_array(bcode));
    if (NULL != def->sourcepath) {
        dst_table_put(ret, dst_csymbolv("sourcepath"), 
                dst_wrap_string(def->sourcepath));
    }
    if (NULL != def->source) {
        dst_table_put(ret, dst_csymbolv("source"), dst_wrap_string(def->source));
    }
    if (def->flags & DST_FUNCDEF_FLAG_VARARG) {
        dst_table_put(ret, dst_csymbolv("vararg"), dst_wrap_true());
    }

    /* Add constants */
    if (def->constants_length > 0) {
        constants = dst_array(def->constants_length);
        dst_table_put(ret, dst_csymbolv("constants"), dst_wrap_array(constants));
        for (i = 0; i < def->constants_length; i++) {
            Dst src = def->constants[i];
            Dst dest;
            if (dst_checktype(src, DST_TUPLE)) {
                dest = tup2(dst_csymbolv("quote"), src);
            } else {
                dest = src;
            }
            constants->data[i] = dest;
        }
        constants->count = def->constants_length;
    }

    /* Add bytecode */
    for (i = 0; i < def->bytecode_length; i++) {
        bcode->data[i] = dst_asm_decode_instruction(def->bytecode[i]);
    }
    bcode->count = def->bytecode_length;

    /* Add source map */
    if (NULL != def->sourcemap) {
        DstArray *sourcemap = dst_array(def->bytecode_length);
        for (i = 0; i < def->bytecode_length; i++) {
            Dst *t = dst_tuple_begin(2);
            DstSourceMapping mapping = def->sourcemap[i];
            t[0] = dst_wrap_integer(mapping.start);
            t[1] = dst_wrap_integer(mapping.end);
            sourcemap->data[i] = dst_wrap_tuple(dst_tuple_end(t));
        }
        sourcemap->count = def->bytecode_length;
        dst_table_put(ret, dst_csymbolv("sourcemap"), dst_wrap_array(sourcemap));
    }

    /* Add environments */
    if (NULL != def->environments) {
        DstArray *envs = dst_array(def->environments_length);
        for (i = 0; i < def->environments_length; i++) {
            envs->data[i] = dst_wrap_integer(def->environments[i]);
        }
        envs->count = def->environments_length;
        dst_table_put(ret, dst_csymbolv("environments"), dst_wrap_array(envs));
    }

    /* Add closures */
    /* Funcdefs cannot be recursive */
    if (NULL != def->defs) {
        DstArray *defs = dst_array(def->defs_length);
        for (i = 0; i < def->defs_length; i++) {
            defs->data[i] = dst_disasm(def->defs[i]);
        }
        defs->count = def->defs_length;
        dst_table_put(ret, dst_csymbolv("defs"), dst_wrap_array(defs));
    }

    /* Add slotcount */
    dst_table_put(ret, dst_csymbolv("slotcount"), dst_wrap_integer(def->slotcount));

    return dst_wrap_struct(dst_table_to_struct(ret));
}

/* C Function for assembly */
int dst_asm_cfun(DstArgs args) {
    DstAssembleResult res;
    DST_FIXARITY(args, 1);
    res = dst_asm(args.v[0], 0);
    if (res.status == DST_ASSEMBLE_OK) {
        DST_RETURN_FUNCTION(args, dst_thunk(res.funcdef));
    } else {
        DST_THROWV(args, dst_wrap_string(res.error));
    }
}

int dst_disasm_cfun(DstArgs args) {
    DstFunction *f;
    DST_FIXARITY(args, 1);
    DST_ARG_FUNCTION(f, args, 0);
    DST_RETURN(args, dst_disasm(f->def));
}

static const DstReg cfuns[] = {
    {"asm.asm", dst_asm_cfun},
    {"asm.disasm", dst_disasm_cfun},
    {NULL, NULL}
};

/* Load the library */
int dst_lib_asm(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

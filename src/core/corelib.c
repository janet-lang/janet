/*
* Copyright (c) 2018 Calvin Rose
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

#include <janet/janet.h>
#include "compile.h"
#include "state.h"
#include "util.h"

/* Generated header */
#include <generated/core.h>

/* Only include dynamic modules if enabled */
#ifdef JANET_DYNAMIC_MODULES

/* Use LoadLibrary on windows or dlopen on posix to load dynamic libaries
 * with native code. */
#ifdef JANET_WINDOWS
#include <windows.h>
typedef HINSTANCE Clib;
#define load_clib(name) LoadLibrary((name))
#define symbol_clib(lib, sym) GetProcAddress((lib), (sym))
#define error_clib() "could not load dynamic library"
#elif defined(JANET_WEB)
#include <emscripten.h>
/* TODO - figure out how loading modules will work in JS */
typedef int Clib;
#define load_clib(name) 0
#define symbol_clib(lib, sym) 0
#define error_clib() "dynamic libraries not supported"
#else
#include <dlfcn.h>
typedef void *Clib;
#define load_clib(name) dlopen((name), RTLD_NOW)
#define symbol_clib(lib, sym) dlsym((lib), (sym))
#define error_clib() dlerror()
#endif

JanetCFunction janet_native(const char *name, const uint8_t **error) {
    Clib lib = load_clib(name);
    JanetCFunction init;
    if (!lib) {
        *error = janet_cstring(error_clib());
        return NULL;
    }
    init = (JanetCFunction) symbol_clib(lib, "_janet_init");
    if (!init) {
        *error = janet_cstring("could not find _janet_init symbol");
        return NULL;
    }
    return init;
}

static int janet_core_native(JanetArgs args) {
    JanetCFunction init;
    const uint8_t *error = NULL;
    const uint8_t *path = NULL;
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(path, args, 0);
    init = janet_native((const char *)path, &error);
    if (!init) {
        JANET_THROWV(args, janet_wrap_string(error));
    }
    JANET_RETURN_CFUNCTION(args, init);
}

#endif
/* end JANET_DYNAMIC_MODULES */

static int janet_core_print(JanetArgs args) {
    int32_t i;
    for (i = 0; i < args.n; ++i) {
        int32_t j, len;
        const uint8_t *vstr = janet_to_string(args.v[i]);
        len = janet_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    JANET_RETURN_NIL(args);
}

static int janet_core_describe(JanetArgs args) {
    int32_t i;
    JanetBuffer b;
    janet_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = janet_description(args.v[i]);
        len = janet_string_length(str);
        janet_buffer_push_bytes(&b, str, len);
    }
    *args.ret = janet_stringv(b.data, b.count);
    janet_buffer_deinit(&b);
    return 0;
}

static int janet_core_string(JanetArgs args) {
    int32_t i;
    JanetBuffer b;
    janet_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = janet_to_string(args.v[i]);
        len = janet_string_length(str);
        janet_buffer_push_bytes(&b, str, len);
    }
    *args.ret = janet_stringv(b.data, b.count);
    janet_buffer_deinit(&b);
    return 0;
}

static int janet_core_symbol(JanetArgs args) {
    int32_t i;
    JanetBuffer b;
    janet_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = janet_to_string(args.v[i]);
        len = janet_string_length(str);
        janet_buffer_push_bytes(&b, str, len);
    }
    *args.ret = janet_symbolv(b.data, b.count);
    janet_buffer_deinit(&b);
    return 0;
}

static int janet_core_buffer(JanetArgs args) {
    int32_t i;
    JanetBuffer *b = janet_buffer(0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = janet_to_string(args.v[i]);
        len = janet_string_length(str);
        janet_buffer_push_bytes(b, str, len);
    }
    JANET_RETURN_BUFFER(args, b);
}

static int janet_core_scannumber(JanetArgs args) {
    const uint8_t *data;
    Janet x;
    int32_t len;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(data, len, args, 0);
    x = janet_scan_number(data, len);
    if (janet_checktype(x, JANET_NIL)) {
        JANET_THROW(args, "error parsing number");
    }
    JANET_RETURN(args, x);
}

static int janet_core_scaninteger(JanetArgs args) {
    const uint8_t *data;
    int32_t len, ret;
    int err = 0;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(data, len, args, 0);
    ret = janet_scan_integer(data, len, &err);
    if (err) {
        JANET_THROW(args, "error parsing integer");
    }
    JANET_RETURN_INTEGER(args, ret);
}

static int janet_core_scanreal(JanetArgs args) {
    const uint8_t *data;
    int32_t len;
    double ret;
    int err = 0;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(data, len, args, 0);
    ret = janet_scan_real(data, len, &err);
    if (err) {
        JANET_THROW(args, "error parsing real");
    }
    JANET_RETURN_REAL(args, ret);
}

static int janet_core_tuple(JanetArgs args) {
    JANET_RETURN_TUPLE(args, janet_tuple_n(args.v, args.n));
}

static int janet_core_array(JanetArgs args) {
    JanetArray *array = janet_array(args.n);
    array->count = args.n;
    memcpy(array->data, args.v, args.n * sizeof(Janet));
    JANET_RETURN_ARRAY(args, array);
}

static int janet_core_table(JanetArgs args) {
    int32_t i;
    JanetTable *table = janet_table(args.n >> 1);
    if (args.n & 1)
        JANET_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        janet_table_put(table, args.v[i], args.v[i + 1]);
    }
    JANET_RETURN_TABLE(args, table);
}

static int janet_core_struct(JanetArgs args) {
    int32_t i;
    JanetKV *st = janet_struct_begin(args.n >> 1);
    if (args.n & 1)
        JANET_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        janet_struct_put(st, args.v[i], args.v[i + 1]);
    }
    JANET_RETURN_STRUCT(args, janet_struct_end(st));
}

static int janet_core_gensym(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    JANET_RETURN_SYMBOL(args, janet_symbol_gen());
}

static int janet_core_gccollect(JanetArgs args) {
    (void) args;
    janet_collect();
    return 0;
}

static int janet_core_gcsetinterval(JanetArgs args) {
    int32_t val;
    JANET_FIXARITY(args, 1);
    JANET_ARG_INTEGER(val, args, 0);
    if (val < 0)
        JANET_THROW(args, "expected non-negative integer");
    janet_vm_gc_interval = val;
    JANET_RETURN_NIL(args);
}

static int janet_core_gcinterval(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    JANET_RETURN_INTEGER(args, janet_vm_gc_interval);
}

static int janet_core_type(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    JanetType t = janet_type(args.v[0]);
    if (t == JANET_ABSTRACT) {
        JANET_RETURN(args, janet_csymbolv(janet_abstract_type(janet_unwrap_abstract(args.v[0]))->name));
    } else {
        JANET_RETURN(args, janet_csymbolv(janet_type_names[t]));
    }
}

static int janet_core_next(JanetArgs args) {
    Janet ds;
    const JanetKV *kv;
    JANET_FIXARITY(args, 2);
    JANET_CHECKMANY(args, 0, JANET_TFLAG_DICTIONARY);
    ds = args.v[0];
    if (janet_checktype(ds, JANET_TABLE)) {
        JanetTable *t = janet_unwrap_table(ds);
        kv = janet_checktype(args.v[1], JANET_NIL)
            ? NULL
            : janet_table_find(t, args.v[1]);
        kv = janet_table_next(t, kv);
    } else {
        const JanetKV *st = janet_unwrap_struct(ds);
        kv = janet_checktype(args.v[1], JANET_NIL)
            ? NULL
            : janet_struct_find(st, args.v[1]);
        kv = janet_struct_next(st, kv);
    }
    if (kv)
        JANET_RETURN(args, kv->key);
    JANET_RETURN_NIL(args);
}

static int janet_core_hash(JanetArgs args) {
    JANET_FIXARITY(args, 1);
    JANET_RETURN_INTEGER(args, janet_hash(args.v[0]));
}

static const JanetReg cfuns[] = {
#ifdef JANET_DYNAMIC_MODULES
    {"native", janet_core_native},
#endif
    {"print", janet_core_print},
    {"describe", janet_core_describe},
    {"string", janet_core_string},
    {"symbol", janet_core_symbol},
    {"buffer", janet_core_buffer},
    {"table", janet_core_table},
    {"array", janet_core_array},
    {"scan-number", janet_core_scannumber},
    {"scan-integer", janet_core_scaninteger},
    {"scan-real", janet_core_scanreal},
    {"tuple", janet_core_tuple},
    {"struct", janet_core_struct},
    {"buffer", janet_core_buffer},
    {"gensym", janet_core_gensym},
    {"gccollect", janet_core_gccollect},
    {"gcsetinterval", janet_core_gcsetinterval},
    {"gcinterval", janet_core_gcinterval},
    {"type", janet_core_type},
    {"next", janet_core_next},
    {"hash", janet_core_hash},
    {NULL, NULL}
};

/* Utility for inline assembly */
static void janet_quick_asm(
        JanetTable *env,
        int32_t flags,
        const char *name,
        int32_t arity,
        int32_t slots,
        const uint32_t *bytecode,
        size_t bytecode_size) {
    JanetFuncDef *def = janet_funcdef_alloc();
    def->arity = arity;
    def->flags = flags;
    def->slotcount = slots;
    def->bytecode = malloc(bytecode_size);
    def->bytecode_length = (int32_t)(bytecode_size / sizeof(uint32_t));
    def->name = janet_cstring(name);
    if (!def->bytecode) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(def->bytecode, bytecode, bytecode_size);
    janet_def(env, name, janet_wrap_function(janet_thunk(def)));
    janet_register(name, janet_wrap_function(janet_thunk(def)));
}

/* Macros for easier inline janet assembly */
#define SSS(op, a, b, c) ((op) | ((a) << 8) | ((b) << 16) | ((c) << 24))
#define SS(op, a, b) ((op) | ((a) << 8) | ((b) << 16))
#define SSI(op, a, b, I) ((op) | ((a) << 8) | ((b) << 16) | ((uint32_t)(I) << 24))
#define S(op, a) ((op) | ((a) << 8))
#define SI(op, a, I) ((op) | ((a) << 8) | ((uint32_t)(I) << 16))

/* Templatize a varop */
static void templatize_varop(
        JanetTable *env,
        int32_t flags,
        const char *name,
        int32_t nullary,
        int32_t unary,
        uint32_t op) {

    /* Variadic operator assembly. Must be templatized for each different opcode. */
    /* Reg 0: Argument tuple (args) */
    /* Reg 1: Argument count (argn) */
    /* Reg 2: Jump flag (jump?) */
    /* Reg 3: Accumulator (accum) */
    /* Reg 4: Next operand (operand) */
    /* Reg 5: Loop iterator (i) */
    uint32_t varop_asm[] = {
        SS(JOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */

        /* Check nullary */
        SSS(JOP_EQUALS_IMMEDIATE, 2, 1, 0), /* Check if numargs equal to 0 */
        SI(JOP_JUMP_IF_NOT, 2, 3), /* If not 0, jump to next check */
        /* Nullary */
        SI(JOP_LOAD_INTEGER, 3, nullary),  /* accum = nullary value */
        S(JOP_RETURN, 3), /* return accum */

        /* Check unary */
        SSI(JOP_EQUALS_IMMEDIATE, 2, 1, 1), /* Check if numargs equal to 1 */
        SI(JOP_JUMP_IF_NOT, 2, 5), /* If not 1, jump to next check */
        /* Unary */
        SI(JOP_LOAD_INTEGER, 3, unary), /* accum = unary value */
        SSI(JOP_GET_INDEX, 4, 0, 0), /* operand = args[0] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        S(JOP_RETURN, 3), /* return accum */

        /* Mutli (2 or more) arity */
        /* Prime loop */
        SSI(JOP_GET_INDEX, 3, 0, 0), /* accum = args[0] */
        SI(JOP_LOAD_INTEGER, 5, 1), /* i = 1 */
        /* Main loop */
        SSS(JOP_GET, 4, 0, 5), /* operand = args[i] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        SSI(JOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SSI(JOP_EQUALS_INTEGER, 2, 5, 1), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF_NOT, 2, -4), /* if not jump? go back 4 */

        /* Done, do last and return accumulator */
        S(JOP_RETURN, 3) /* return accum */
    };

    janet_quick_asm(
            env,
            flags | JANET_FUNCDEF_FLAG_VARARG,
            name,
            0,
            6,
            varop_asm,
            sizeof(varop_asm));
}

/* Templatize variadic comparators */
static void templatize_comparator(
        JanetTable *env,
        int32_t flags,
        const char *name,
        int invert,
        uint32_t op) {

    /* Reg 0: Argument tuple (args) */
    /* Reg 1: Argument count (argn) */
    /* Reg 2: Jump flag (jump?) */
    /* Reg 3: Last value (last) */
    /* Reg 4: Next operand (next) */
    /* Reg 5: Loop iterator (i) */
    uint32_t comparator_asm[] = {
        SS(JOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */
        SSS(JOP_LESS_THAN_IMMEDIATE, 2, 1, 2), /* Check if numargs less than 2 */
        SI(JOP_JUMP_IF, 2, 10), /* If numargs < 2, jump to done */

        /* Prime loop */
        SSI(JOP_GET_INDEX, 3, 0, 0), /* last = args[0] */
        SI(JOP_LOAD_INTEGER, 5, 1), /* i = 1 */

        /* Main loop */
        SSS(JOP_GET, 4, 0, 5), /* next = args[i] */
        SSS(op, 2, 3, 4), /* jump? = last compare next */
        SI(JOP_JUMP_IF_NOT, 2, 7), /* if not jump? goto fail (return false) */
        SSI(JOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SS(JOP_MOVE_NEAR, 3, 4), /* last = next */
        SSI(JOP_EQUALS_INTEGER, 2, 5, 1), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF_NOT, 2, -6), /* if not jump? go back 6 */

        /* Done, return true */
        S(invert ? JOP_LOAD_FALSE : JOP_LOAD_TRUE, 3),
        S(JOP_RETURN, 3),

        /* Failed, return false */
        S(invert ? JOP_LOAD_TRUE : JOP_LOAD_FALSE, 3),
        S(JOP_RETURN, 3)
    };

    janet_quick_asm(
            env,
            flags | JANET_FUNCDEF_FLAG_VARARG,
            name,
            0,
            6,
            comparator_asm,
            sizeof(comparator_asm));
}

/* Make the apply function */
static void make_apply(JanetTable *env) {
    /* Reg 0: Function (fun) */
    /* Reg 1: Argument tuple (args) */
    /* Reg 2: Argument count (argn) */
    /* Reg 3: Jump flag (jump?) */
    /* Reg 4: Loop iterator (i) */
    /* Reg 5: Loop values (x) */
    uint32_t apply_asm[] = {
        SS(JOP_LENGTH, 2, 1),
        SSS(JOP_EQUALS_IMMEDIATE, 3, 2, 0), /* Immediate tail call if no args */
        SI(JOP_JUMP_IF, 3, 9),

        /* Prime loop */
        SI(JOP_LOAD_INTEGER, 4, 0), /* i = 0 */

        /* Main loop */
        SSS(JOP_GET, 5, 1, 4), /* x = args[i] */
        SSI(JOP_ADD_IMMEDIATE, 4, 4, 1), /* i++ */
        SSI(JOP_EQUALS_INTEGER, 3, 4, 2), /* jump? = (i == argn) */
        SI(JOP_JUMP_IF, 3, 3), /* if jump? go forward 3 */
        S(JOP_PUSH, 5),
        (JOP_JUMP | ((uint32_t)(-5) << 8)),

        /* Push the array */
        S(JOP_PUSH_ARRAY, 5),

        /* Call the funciton */
        S(JOP_TAILCALL, 0)
    };
    janet_quick_asm(env, JANET_FUN_APPLY | JANET_FUNCDEF_FLAG_VARARG,
            "apply", 1, 6, apply_asm, sizeof(apply_asm));
}

JanetTable *janet_core_env(void) {
    static const uint32_t error_asm[] = {
        JOP_ERROR
    };
    static const uint32_t debug_asm[] = {
       JOP_SIGNAL | (2 << 24),
       JOP_RETURN_NIL
    };
    static const uint32_t yield_asm[] = {
        JOP_SIGNAL | (3 << 24),
        JOP_RETURN
    };
    static const uint32_t resume_asm[] = {
        JOP_RESUME | (1 << 24),
        JOP_RETURN
    };
    static const uint32_t get_asm[] = {
        JOP_GET | (1 << 24),
        JOP_RETURN
    };
    static const uint32_t put_asm[] = {
        JOP_PUT | (1 << 16) | (2 << 24),
        JOP_RETURN
    };
    static const uint32_t length_asm[] = {
        JOP_LENGTH,
        JOP_RETURN
    };
    static const uint32_t bnot_asm[] = {
        JOP_BNOT,
        JOP_RETURN
    };

    JanetTable *env = janet_table(0);
    Janet ret = janet_wrap_table(env);

    /* Load main functions */
    janet_cfuns(env, NULL, cfuns);

    janet_quick_asm(env, JANET_FUN_YIELD, "debug", 0, 1, debug_asm, sizeof(debug_asm));
    janet_quick_asm(env, JANET_FUN_ERROR, "error", 1, 1, error_asm, sizeof(error_asm));
    janet_quick_asm(env, JANET_FUN_YIELD, "yield", 1, 2, yield_asm, sizeof(yield_asm));
    janet_quick_asm(env, JANET_FUN_RESUME, "resume", 2, 2, resume_asm, sizeof(resume_asm));
    janet_quick_asm(env, JANET_FUN_GET, "get", 2, 2, get_asm, sizeof(get_asm));
    janet_quick_asm(env, JANET_FUN_PUT, "put", 3, 3, put_asm, sizeof(put_asm));
    janet_quick_asm(env, JANET_FUN_LENGTH, "length", 1, 1, length_asm, sizeof(length_asm));
    janet_quick_asm(env, JANET_FUN_BNOT, "~", 1, 1, bnot_asm, sizeof(bnot_asm));
    make_apply(env);

    /* Variadic ops */
    templatize_varop(env, JANET_FUN_ADD, "+", 0, 0, JOP_ADD);
    templatize_varop(env, JANET_FUN_SUBTRACT, "-", 0, 0, JOP_SUBTRACT);
    templatize_varop(env, JANET_FUN_MULTIPLY, "*", 1, 1, JOP_MULTIPLY);
    templatize_varop(env, JANET_FUN_DIVIDE, "/", 1, 1, JOP_DIVIDE);
    templatize_varop(env, JANET_FUN_BAND, "&", -1, -1, JOP_BAND);
    templatize_varop(env, JANET_FUN_BOR, "|", 0, 0, JOP_BOR);
    templatize_varop(env, JANET_FUN_BXOR, "^", 0, 0, JOP_BXOR);
    templatize_varop(env, JANET_FUN_LSHIFT, "<<", 1, 1, JOP_SHIFT_LEFT);
    templatize_varop(env, JANET_FUN_RSHIFT, ">>", 1, 1, JOP_SHIFT_RIGHT);
    templatize_varop(env, JANET_FUN_RSHIFTU, ">>>", 1, 1, JOP_SHIFT_RIGHT_UNSIGNED);

    /* Variadic comparators */
    templatize_comparator(env, JANET_FUN_ORDER_GT, "order>", 0, JOP_GREATER_THAN);
    templatize_comparator(env, JANET_FUN_ORDER_LT, "order<", 0, JOP_LESS_THAN);
    templatize_comparator(env, JANET_FUN_ORDER_GTE, "order>=", 1, JOP_LESS_THAN);
    templatize_comparator(env, JANET_FUN_ORDER_LTE, "order<=", 1, JOP_GREATER_THAN);
    templatize_comparator(env, JANET_FUN_ORDER_EQ, "=", 0, JOP_EQUALS);
    templatize_comparator(env, JANET_FUN_ORDER_NEQ, "not=", 1, JOP_EQUALS);
    templatize_comparator(env, JANET_FUN_GT, ">", 0, JOP_NUMERIC_GREATER_THAN);
    templatize_comparator(env, JANET_FUN_LT, "<", 0, JOP_NUMERIC_LESS_THAN);
    templatize_comparator(env, JANET_FUN_GTE, ">=", 0, JOP_NUMERIC_GREATER_THAN_EQUAL);
    templatize_comparator(env, JANET_FUN_LTE, "<=", 0, JOP_NUMERIC_LESS_THAN_EQUAL);
    templatize_comparator(env, JANET_FUN_EQ, "==", 0, JOP_NUMERIC_EQUAL);
    templatize_comparator(env, JANET_FUN_NEQ, "not==", 1, JOP_NUMERIC_EQUAL);

    /* Platform detection */
    janet_def(env, "janet.version", janet_cstringv(JANET_VERSION));

    /* Set as gc root */
    janet_gcroot(janet_wrap_table(env));

    /* Load auxiliary envs */
    {
        JanetArgs args;
        args.n = 1;
        args.v = &ret;
        args.ret = &ret;
        janet_lib_io(args);
        janet_lib_math(args);
        janet_lib_array(args);
        janet_lib_tuple(args);
        janet_lib_buffer(args);
        janet_lib_table(args);
        janet_lib_fiber(args);
        janet_lib_os(args);
        janet_lib_parse(args);
        janet_lib_compile(args);
        janet_lib_asm(args);
        janet_lib_string(args);
        janet_lib_marsh(args);
    }

    /* Allow references to the environment */
    janet_def(env, "_env", ret);

    /* Run bootstrap source */
    janet_dobytes(env, janet_gen_core, sizeof(janet_gen_core), "core.janet");

    return env;
}

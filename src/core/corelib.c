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

#include <dst/dst.h>
#include "compile.h"
#include "state.h"
#include "util.h"

/* Generated header */
#include <generated/core.h>

/* Only include dynamic modules if enabled */
#ifdef DST_DYNAMIC_MODULES

/* Use LoadLibrary on windows or dlopen on posix to load dynamic libaries
 * with native code. */
#ifdef DST_WINDOWS
#include <windows.h>
typedef HINSTANCE Clib;
#define load_clib(name) LoadLibrary((name))
#define symbol_clib(lib, sym) GetProcAddress((lib), (sym))
#define error_clib() "could not load dynamic library"
#elif defined(DST_WEB)
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

DstCFunction dst_native(const char *name, const uint8_t **error) {
    Clib lib = load_clib(name);
    DstCFunction init;
    if (!lib) {
        *error = dst_cstring(error_clib());
        return NULL;
    }
    init = (DstCFunction) symbol_clib(lib, "_dst_init");
    if (!init) {
        *error = dst_cstring("could not find _dst_init symbol");
        return NULL;
    }
    return init;
}

static int dst_core_native(DstArgs args) {
    DstCFunction init;
    const uint8_t *error = NULL;
    const uint8_t *path = NULL;
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(path, args, 0);
    init = dst_native((const char *)path, &error);
    if (!init) {
        DST_THROWV(args, dst_wrap_string(error));
    }
    DST_RETURN_CFUNCTION(args, init);
}

#endif
/* end DST_DYNAMIC_MODULES */

static int dst_core_print(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n; ++i) {
        int32_t j, len;
        const uint8_t *vstr = dst_to_string(args.v[i]);
        len = dst_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    DST_RETURN_NIL(args);
}

static int dst_core_describe(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_description(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_stringv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

static int dst_core_string(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_stringv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

static int dst_core_symbol(DstArgs args) {
    int32_t i;
    DstBuffer b;
    dst_buffer_init(&b, 0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(&b, str, len);
    }
    *args.ret = dst_symbolv(b.data, b.count);
    dst_buffer_deinit(&b);
    return 0;
}

static int dst_core_buffer(DstArgs args) {
    int32_t i;
    DstBuffer *b = dst_buffer(0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(b, str, len);
    }
    DST_RETURN_BUFFER(args, b);
}

static int dst_core_scannumber(DstArgs args) {
    const uint8_t *data;
    Dst x;
    int32_t len;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    x = dst_scan_number(data, len);
    if (dst_checktype(x, DST_NIL)) {
        DST_THROW(args, "error parsing number");
    }
    DST_RETURN(args, x);
}

static int dst_core_scaninteger(DstArgs args) {
    const uint8_t *data;
    int32_t len, ret;
    int err = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    ret = dst_scan_integer(data, len, &err);
    if (err) {
        DST_THROW(args, "error parsing integer");
    }
    DST_RETURN_INTEGER(args, ret);
}

static int dst_core_scanreal(DstArgs args) {
    const uint8_t *data;
    int32_t len;
    double ret;
    int err = 0;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(data, len, args, 0);
    ret = dst_scan_real(data, len, &err);
    if (err) {
        DST_THROW(args, "error parsing real");
    }
    DST_RETURN_REAL(args, ret);
}

static int dst_core_tuple(DstArgs args) {
    DST_RETURN_TUPLE(args, dst_tuple_n(args.v, args.n));
}

static int dst_core_array(DstArgs args) {
    DstArray *array = dst_array(args.n);
    array->count = args.n;
    memcpy(array->data, args.v, args.n * sizeof(Dst));
    DST_RETURN_ARRAY(args, array);
}

static int dst_core_table(DstArgs args) {
    int32_t i;
    DstTable *table = dst_table(args.n >> 1);
    if (args.n & 1)
        DST_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_table_put(table, args.v[i], args.v[i + 1]);
    }
    DST_RETURN_TABLE(args, table);
}

static int dst_core_struct(DstArgs args) {
    int32_t i;
    DstKV *st = dst_struct_begin(args.n >> 1);
    if (args.n & 1)
        DST_THROW(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_struct_put(st, args.v[i], args.v[i + 1]);
    }
    DST_RETURN_STRUCT(args, dst_struct_end(st));
}

static int dst_core_gensym(DstArgs args) {
    DST_FIXARITY(args, 0);
    DST_RETURN_SYMBOL(args, dst_symbol_gen());
}

static int dst_core_gccollect(DstArgs args) {
    (void) args;
    dst_collect();
    return 0;
}

static int dst_core_gcsetinterval(DstArgs args) {
    int32_t val;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(val, args, 0);
    if (val < 0)
        DST_THROW(args, "expected non-negative integer");
    dst_vm_gc_interval = val;
    DST_RETURN_NIL(args);
}

static int dst_core_gcinterval(DstArgs args) {
    DST_FIXARITY(args, 0);
    DST_RETURN_INTEGER(args, dst_vm_gc_interval);
}

static int dst_core_type(DstArgs args) {
    DST_FIXARITY(args, 1);
    DstType t = dst_type(args.v[0]);
    if (t == DST_ABSTRACT) {
        DST_RETURN(args, dst_csymbolv(dst_abstract_type(dst_unwrap_abstract(args.v[0]))->name));
    } else {
        DST_RETURN(args, dst_csymbolv(dst_type_names[t]));
    }
}

static int dst_core_next(DstArgs args) {
    Dst ds;
    const DstKV *kv;
    DST_FIXARITY(args, 2);
    DST_CHECKMANY(args, 0, DST_TFLAG_DICTIONARY);
    ds = args.v[0];
    if (dst_checktype(ds, DST_TABLE)) {
        DstTable *t = dst_unwrap_table(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_table_find(t, args.v[1]);
        kv = dst_table_next(t, kv);
    } else {
        const DstKV *st = dst_unwrap_struct(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_struct_find(st, args.v[1]);
        kv = dst_struct_next(st, kv);
    }
    if (kv)
        DST_RETURN(args, kv->key);
    DST_RETURN_NIL(args);
}

static int dst_core_hash(DstArgs args) {
    DST_FIXARITY(args, 1);
    DST_RETURN_INTEGER(args, dst_hash(args.v[0]));
}

static const DstReg cfuns[] = {
#ifdef DST_DYNAMIC_MODULES
    {"native", dst_core_native},
#endif
    {"print", dst_core_print},
    {"describe", dst_core_describe},
    {"string", dst_core_string},
    {"symbol", dst_core_symbol},
    {"buffer", dst_core_buffer},
    {"table", dst_core_table},
    {"array", dst_core_array},
    {"scan-number", dst_core_scannumber},
    {"scan-integer", dst_core_scaninteger},
    {"scan-real", dst_core_scanreal},
    {"tuple", dst_core_tuple},
    {"struct", dst_core_struct},
    {"buffer", dst_core_buffer},
    {"gensym", dst_core_gensym},
    {"gccollect", dst_core_gccollect},
    {"gcsetinterval", dst_core_gcsetinterval},
    {"gcinterval", dst_core_gcinterval},
    {"type", dst_core_type},
    {"next", dst_core_next},
    {"hash", dst_core_hash},
    {NULL, NULL}
};

/* Utility for inline assembly */
static void dst_quick_asm(
        DstTable *env,
        int32_t flags,
        const char *name,
        int32_t arity,
        int32_t slots,
        const uint32_t *bytecode,
        size_t bytecode_size) {
    DstFuncDef *def = dst_funcdef_alloc();
    def->arity = arity;
    def->flags = flags;
    def->slotcount = slots;
    def->bytecode = malloc(bytecode_size);
    def->bytecode_length = (int32_t)(bytecode_size / sizeof(uint32_t));
    def->name = dst_cstring(name);
    if (!def->bytecode) {
        DST_OUT_OF_MEMORY;
    }
    memcpy(def->bytecode, bytecode, bytecode_size);
    dst_env_def(env, name, dst_wrap_function(dst_thunk(def)));
}

/* Macros for easier inline dst assembly */
#define SSS(op, a, b, c) ((op) | ((a) << 8) | ((b) << 16) | ((c) << 24))
#define SS(op, a, b) ((op) | ((a) << 8) | ((b) << 16))
#define SSI(op, a, b, I) ((op) | ((a) << 8) | ((b) << 16) | ((uint32_t)(I) << 24))
#define S(op, a) ((op) | ((a) << 8))
#define SI(op, a, I) ((op) | ((a) << 8) | ((uint32_t)(I) << 16))

/* Templatize a varop */
static void templatize_varop(
        DstTable *env,
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
        SS(DOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */

        /* Check nullary */
        SSS(DOP_EQUALS_IMMEDIATE, 2, 1, 0), /* Check if numargs equal to 0 */
        SI(DOP_JUMP_IF_NOT, 2, 3), /* If not 0, jump to next check */
        /* Nullary */
        SI(DOP_LOAD_INTEGER, 3, nullary),  /* accum = nullary value */
        S(DOP_RETURN, 3), /* return accum */

        /* Check unary */
        SSI(DOP_EQUALS_IMMEDIATE, 2, 1, 1), /* Check if numargs equal to 1 */
        SI(DOP_JUMP_IF_NOT, 2, 5), /* If not 1, jump to next check */
        /* Unary */
        SI(DOP_LOAD_INTEGER, 3, unary), /* accum = unary value */
        SSI(DOP_GET_INDEX, 4, 0, 0), /* operand = args[0] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        S(DOP_RETURN, 3), /* return accum */

        /* Mutli (2 or more) arity */
        /* Prime loop */
        SSI(DOP_GET_INDEX, 3, 0, 0), /* accum = args[0] */
        SI(DOP_LOAD_INTEGER, 5, 1), /* i = 1 */
        /* Main loop */
        SSS(DOP_GET, 4, 0, 5), /* operand = args[i] */
        SSS(op, 3, 3, 4), /* accum = accum op operand */
        SSI(DOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SSI(DOP_EQUALS_INTEGER, 2, 5, 1), /* jump? = (i == argn) */
        SI(DOP_JUMP_IF_NOT, 2, -4), /* if not jump? go back 4 */

        /* Done, do last and return accumulator */
        S(DOP_RETURN, 3) /* return accum */
    };

    dst_quick_asm(
            env,
            flags | DST_FUNCDEF_FLAG_VARARG,
            name,
            0,
            6,
            varop_asm,
            sizeof(varop_asm));
}

/* Templatize variadic comparators */
static void templatize_comparator(
        DstTable *env,
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
        SS(DOP_LENGTH, 1, 0), /* Put number of arguments in register 1 -> argn = count(args) */
        SSS(DOP_LESS_THAN_IMMEDIATE, 2, 1, 2), /* Check if numargs less than 2 */
        SI(DOP_JUMP_IF, 2, 10), /* If numargs < 2, jump to done */

        /* Prime loop */
        SSI(DOP_GET_INDEX, 3, 0, 0), /* last = args[0] */
        SI(DOP_LOAD_INTEGER, 5, 1), /* i = 1 */

        /* Main loop */
        SSS(DOP_GET, 4, 0, 5), /* next = args[i] */
        SSS(op, 2, 3, 4), /* jump? = last compare next */
        SI(DOP_JUMP_IF_NOT, 2, 7), /* if not jump? goto fail (return false) */
        SSI(DOP_ADD_IMMEDIATE, 5, 5, 1), /* i++ */
        SS(DOP_MOVE_NEAR, 3, 4), /* last = next */
        SSI(DOP_EQUALS_INTEGER, 2, 5, 1), /* jump? = (i == argn) */
        SI(DOP_JUMP_IF_NOT, 2, -6), /* if not jump? go back 6 */

        /* Done, return true */
        S(invert ? DOP_LOAD_FALSE : DOP_LOAD_TRUE, 3),
        S(DOP_RETURN, 3),

        /* Failed, return false */
        S(invert ? DOP_LOAD_TRUE : DOP_LOAD_FALSE, 3),
        S(DOP_RETURN, 3)
    };

    dst_quick_asm(
            env,
            flags | DST_FUNCDEF_FLAG_VARARG,
            name,
            0,
            6,
            comparator_asm,
            sizeof(comparator_asm));
}

DstTable *dst_core_env(void) {
    static const uint32_t error_asm[] = {
        DOP_ERROR
    };
    static const uint32_t apply_asm[] = {
       DOP_PUSH_ARRAY | (1 << 8),
       DOP_TAILCALL
    };
    static const uint32_t debug_asm[] = {
       DOP_SIGNAL | (2 << 24),
       DOP_RETURN_NIL
    };
    static const uint32_t yield_asm[] = {
        DOP_SIGNAL | (3 << 24),
        DOP_RETURN
    };
    static const uint32_t resume_asm[] = {
        DOP_RESUME | (1 << 24),
        DOP_RETURN
    };
    static const uint32_t get_asm[] = {
        DOP_GET | (1 << 24),
        DOP_RETURN
    };
    static const uint32_t put_asm[] = {
        DOP_PUT | (1 << 16) | (2 << 24),
        DOP_RETURN
    };
    static const uint32_t length_asm[] = {
        DOP_LENGTH,
        DOP_RETURN
    };
    static const uint32_t bnot_asm[] = {
        DOP_BNOT,
        DOP_RETURN
    };

    DstTable *env = dst_table(0);
    Dst ret = dst_wrap_table(env);

    /* Load main functions */
    dst_env_cfuns(env, cfuns);

    dst_quick_asm(env, DST_FUN_YIELD, "debug", 0, 1, debug_asm, sizeof(debug_asm));
    dst_quick_asm(env, DST_FUN_ERROR, "error", 1, 1, error_asm, sizeof(error_asm));
    dst_quick_asm(env, DST_FUN_APPLY1, "apply1", 2, 2, apply_asm, sizeof(apply_asm));
    dst_quick_asm(env, DST_FUN_YIELD, "yield", 1, 2, yield_asm, sizeof(yield_asm));
    dst_quick_asm(env, DST_FUN_RESUME, "resume", 2, 2, resume_asm, sizeof(resume_asm));
    dst_quick_asm(env, DST_FUN_GET, "get", 2, 2, get_asm, sizeof(get_asm));
    dst_quick_asm(env, DST_FUN_PUT, "put", 3, 3, put_asm, sizeof(put_asm));
    dst_quick_asm(env, DST_FUN_LENGTH, "length", 1, 1, length_asm, sizeof(length_asm));
    dst_quick_asm(env, DST_FUN_BNOT, "~", 1, 1, bnot_asm, sizeof(bnot_asm));

    /* Variadic ops */
    templatize_varop(env, DST_FUN_ADD, "+", 0, 0, DOP_ADD);
    templatize_varop(env, DST_FUN_SUBTRACT, "-", 0, 0, DOP_SUBTRACT);
    templatize_varop(env, DST_FUN_MULTIPLY, "*", 1, 1, DOP_MULTIPLY);
    templatize_varop(env, DST_FUN_DIVIDE, "/", 1, 1, DOP_DIVIDE);
    templatize_varop(env, DST_FUN_BAND, "&", -1, -1, DOP_BAND);
    templatize_varop(env, DST_FUN_BOR, "|", 0, 0, DOP_BOR);
    templatize_varop(env, DST_FUN_BXOR, "^", 0, 0, DOP_BXOR);
    templatize_varop(env, DST_FUN_LSHIFT, "<<", 1, 1, DOP_SHIFT_LEFT);
    templatize_varop(env, DST_FUN_RSHIFT, ">>", 1, 1, DOP_SHIFT_RIGHT);
    templatize_varop(env, DST_FUN_RSHIFTU, ">>>", 1, 1, DOP_SHIFT_RIGHT_UNSIGNED);

    /* Variadic comparators */
    templatize_comparator(env, DST_FUN_ORDER_GT, "order>", 0, DOP_GREATER_THAN);
    templatize_comparator(env, DST_FUN_ORDER_LT, "order<", 0, DOP_LESS_THAN);
    templatize_comparator(env, DST_FUN_ORDER_GTE, "order>=", 1, DOP_LESS_THAN);
    templatize_comparator(env, DST_FUN_ORDER_LTE, "order<=", 1, DOP_GREATER_THAN);
    templatize_comparator(env, DST_FUN_ORDER_EQ, "=", 0, DOP_EQUALS);
    templatize_comparator(env, DST_FUN_ORDER_NEQ, "not=", 1, DOP_EQUALS);
    templatize_comparator(env, DST_FUN_GT, ">", 0, DOP_NUMERIC_GREATER_THAN);
    templatize_comparator(env, DST_FUN_LT, "<", 0, DOP_NUMERIC_LESS_THAN);
    templatize_comparator(env, DST_FUN_GTE, ">=", 0, DOP_NUMERIC_GREATER_THAN_EQUAL);
    templatize_comparator(env, DST_FUN_LTE, "<=", 0, DOP_NUMERIC_LESS_THAN_EQUAL);
    templatize_comparator(env, DST_FUN_EQ, "==", 0, DOP_NUMERIC_EQUAL);
    templatize_comparator(env, DST_FUN_NEQ, "not==", 1, DOP_NUMERIC_EQUAL);

    /* Platform detection */
    dst_env_def(env, "dst.version", dst_cstringv(DST_VERSION));

    /* Set as gc root */
    dst_gcroot(dst_wrap_table(env));

    /* Load auxiliary envs */
    {
        DstArgs args;
        args.n = 1;
        args.v = &ret;
        args.ret = &ret;
        dst_lib_io(args);
        dst_lib_math(args);
        dst_lib_array(args);
        dst_lib_tuple(args);
        dst_lib_buffer(args);
        dst_lib_table(args);
        dst_lib_fiber(args);
        dst_lib_os(args);
        dst_lib_parse(args);
        dst_lib_compile(args);
        dst_lib_asm(args);
        dst_lib_string(args);
        dst_lib_marsh(args);
    }

    /* Allow references to the environment */
    dst_env_def(env, "_env", ret);

    /* Run bootstrap source */
    dst_dobytes(env, dst_gen_core, sizeof(dst_gen_core), "core.dst");

    return env;
}

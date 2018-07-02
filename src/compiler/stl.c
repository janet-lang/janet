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
#include <dst/dstopcodes.h>
#include <dst/dstcorelib.h>
#include <dst/dstcompile.h>
#include "compile.h"

/* Generated header */
#include "dststlbootstrap.gen.h"

static const DstReg cfuns[] = {
    {"native", dst_core_native},
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
    def->bytecode_length = bytecode_size / sizeof(uint32_t);
    def->name = dst_cstring(name);
    if (!def->bytecode) {
        DST_OUT_OF_MEMORY;
    }
    memcpy(def->bytecode, bytecode, bytecode_size);
    dst_env_def(env, name, dst_wrap_function(dst_thunk(def)));
}

#define SSS(op, a, b, c) (op | (a << 8) | (b << 16) | (c << 24))
#define SS(op, a, b) SSS(op, a, b, 0)
#define S(op, a) SSS(op, a, 0, 0)
/* Variadic operator assembly. Must be templatized for each different opcode. */
/* Reg 0: Argument tuple (args) */
/* Reg 1: Argument count (argn) */
/* Reg 2: Jump flag (jump?) */
/* Reg 3: Accumulator (accum) */
/* Reg 4: Next operand (operand) */
/* Reg 5: Loop iterator (i) */
static DST_THREAD_LOCAL uint32_t varop_asm[] = {
    DOP_LENGTH | (1 << 8), /* Put number of arguments in register 1 -> argn = count(args) */

    /* Cheack nullary */
    DOP_EQUALS_IMMEDIATE | (2 << 8) | (1 << 16) | (0 << 24), /* Check if numargs equal to 0 */
    DOP_JUMP_IF_NOT | (2 << 8) | (3 << 16), /* If not 0, jump to next check */
    /* Nullary */
    DOP_LOAD_INTEGER | (3 << 8),  /* accum = nullary value */
    DOP_RETURN | (3 << 8), /* return accum */

    /* Check unary */
    DOP_EQUALS_IMMEDIATE | (2 << 8) | (1 << 16) | (1 << 24), /* Check if numargs equal to 1 */
    DOP_JUMP_IF_NOT | (2 << 8) | (5 << 16), /* If not 1, jump to next check */
    /* Unary */
    DOP_LOAD_INTEGER | (3 << 8), /* accum = unary value */
    DOP_GET_INDEX | (4 << 8) | (0 << 16) | (0 << 24), /* operand = args[0] */
    DOP_NOOP | (3 << 8) | (3 << 16) | (4 << 24), /* accum = accum op operand */
    DOP_RETURN | (3 << 8), /* return accum */

    /* Mutli (2 or more) arity */
    /* Prime loop */
    DOP_GET_INDEX | (3 << 8) | (0 << 16) | (0 << 24), /* accum = args[0] */
    DOP_LOAD_INTEGER | (5 << 8) | (1 << 16), /* i = 1 */
    /* Main loop */
    DOP_GET | (4 << 8) | (0 << 16) | (5 << 24), /* operand = args[i] */
    DOP_NOOP | (3 << 8) | (3 << 16) | (4 << 24), /* accum = accum op operand */
    DOP_ADD_IMMEDIATE | (5 << 8) | (5 << 16) | (1 << 24), /* i++ */
    DOP_EQUALS_INTEGER | (2 << 8) | (5 << 16) | (1 << 24), /* jump? = (i == argn) */
    DOP_JUMP_IF_NOT | (2 << 8) | ((uint32_t)(-4) << 16), /* if not jump? go back 4 */
    /* Done, do last and return accumulator */
    DOP_RETURN | (3 << 8) /* return accum */
};

#define VAROP_NULLARY_LOC 3
#define VAROP_UNARY_LOC 7
#define VAROP_OP_LOC1 9
#define VAROP_OP_LOC2 14

/* Templatize a varop */
static void templatize_varop(
        DstTable *env,
        int32_t flags,
        const char *name,
        int32_t nullary,
        int32_t unary,
        uint32_t op) {
    varop_asm[VAROP_NULLARY_LOC] = SS(DOP_LOAD_INTEGER, 3, nullary);
    varop_asm[VAROP_UNARY_LOC] = SS(DOP_LOAD_INTEGER, 3, unary);
    varop_asm[VAROP_OP_LOC1] = SSS(op, 3, 3, 4);
    varop_asm[VAROP_OP_LOC2] = SSS(op, 3, 3, 4);
    dst_quick_asm(
            env,
            flags | DST_FUNCDEF_FLAG_VARARG,
            name,
            0,
            6,
            varop_asm,
            sizeof(varop_asm));
}

DstTable *dst_stl_env(int flags) {
    static uint32_t error_asm[] = {
        DOP_ERROR
    };
    static uint32_t apply_asm[] = {
       DOP_PUSH_ARRAY | (1 << 8),
       DOP_TAILCALL
    };
    static uint32_t debug_asm[] = {
       DOP_SIGNAL | (2 << 24),
       DOP_RETURN_NIL
    };
    static uint32_t yield_asm[] = {
        DOP_SIGNAL | (3 << 24),
        DOP_RETURN
    };
    static uint32_t resume_asm[] = {
        DOP_RESUME | (1 << 24),
        DOP_RETURN
    };
    static uint32_t get_asm[] = {
        DOP_GET | (1 << 24),
        DOP_RETURN
    };
    static uint32_t put_asm[] = {
        DOP_PUT | (1 << 16) | (2 << 24),
        DOP_RETURN
    };
    static uint32_t length_asm[] = {
        DOP_LENGTH,
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

    dst_env_def(env, "VERSION", dst_cstringv(DST_VERSION));

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
    dst_dobytes(env, dst_stl_bootstrap_gen, sizeof(dst_stl_bootstrap_gen), "boot.dst");

    if (flags & DST_STL_NOGCROOT)
        dst_gcunroot(dst_wrap_table(env));

    return env;
}

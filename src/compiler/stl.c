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

#include <dst/dst.h>
#include <dst/dstopcodes.h>
#include <dst/dstcorelib.h>
#include <dst/dstasm.h>
#include <dst/dstparse.h>
#include <dst/dstcompile.h>

/* Generated header */
#include "dststlbootstrap.gen.h"

static const DstReg cfuns[] = {
    {"native", dst_core_native},
    {"print", dst_core_print},
    {"describe", dst_core_describe},
    {"string", dst_core_string},
    {"symbol", dst_core_symbol},
    {"buffer", dst_core_buffer},
    {"format", dst_core_format},
    {"table", dst_core_table},
    {"array", dst_core_array},
    {"scan-number", dst_core_scannumber},
    {"scan-integer", dst_core_scaninteger},
    {"scan-real", dst_core_scanreal},
    {"tuple", dst_core_tuple},
    {"struct", dst_core_struct},
    {"buffer", dst_core_buffer},
    {"gensym", dst_core_gensym},
    {"get", dst_core_get},
    {"put", dst_core_put},
    {"length", dst_core_length},
    {"gccollect", dst_core_gccollect},
    {"gcsetinterval", dst_core_gcsetinterval},
    {"gcinterval", dst_core_gcinterval},
    {"type", dst_core_type},
    {"next", dst_core_next},
    {"hash", dst_core_hash},
    {"string.slice", dst_core_string_slice},
    {NULL, NULL}
};

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
    DstTable *env = dst_table(0);
    Dst ret = dst_wrap_table(env);

    /* Load main functions */
    dst_env_cfuns(env, cfuns);

    dst_env_def(env, "debug", dst_wrap_function(dst_quick_asm(0, 0, 1, debug_asm, sizeof(debug_asm))));
    dst_env_def(env, "error", dst_wrap_function(dst_quick_asm(1, 0, 1, error_asm, sizeof(error_asm))));
    dst_env_def(env, "apply1", dst_wrap_function(dst_quick_asm(2, 0, 2, apply_asm, sizeof(apply_asm))));

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
    }

    /* Allow references to the environment */
    dst_env_def(env, "_env", ret);

    /* Run bootstrap source */
    dst_dobytes(env, dst_stl_bootstrap_gen, sizeof(dst_stl_bootstrap_gen));

    if (flags & DST_STL_NOGCROOT)
        dst_gcunroot(dst_wrap_table(env));

    return env;
}

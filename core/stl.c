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
#include <dst/dststl.h>

int dst_stl_push(DstArgs args) {
    if (args.n != 2) {
        *args.ret = dst_cstringv("expected 2 arguments");
        return 1;
    }
    if (!dst_checktype(args.v[0], DST_ARRAY)) {
        *args.ret = dst_cstringv("expected array");
        return 1;
    }
    dst_array_push(dst_unwrap_array(args.v[0]), args.v[1]);
    *args.ret = args.v[0];
    return 0;
}

int dst_stl_parse(DstArgs args) {
    const uint8_t *src;
    int32_t len;
    DstParseResult res;
    const char *status_string = "ok";
    DstTable *t;
    if (args.n < 1) {
        *args.ret = dst_cstringv("expected at least on argument");
        return 1;
    }
    if (!dst_chararray_view(args.v[0], &src, &len)) {
        *args.ret = dst_cstringv("expected string/buffer");
        return 1;
    }
    res = dst_parse(src, len);
    t = dst_table(4);
    switch (res.status) {
        case DST_PARSE_OK:
            status_string = "ok";
            break;
        case DST_PARSE_ERROR:
            status_string = "error";
            break;
        case DST_PARSE_NODATA:
            status_string = "nodata";
            break;
        case DST_PARSE_UNEXPECTED_EOS:
            status_string = "eos";
            break;
    }
    dst_table_put(t, dst_cstringv("status"), dst_cstringv(status_string));
    if (res.status == DST_PARSE_OK) dst_table_put(t, dst_cstringv("map"), dst_wrap_tuple(res.map));
    if (res.status == DST_PARSE_OK) dst_table_put(t, dst_cstringv("value"), res.value);
    if (res.status == DST_PARSE_ERROR) dst_table_put(t, dst_cstringv("error"), dst_wrap_string(res.error));
    dst_table_put(t, dst_cstringv("bytes-read"), dst_wrap_integer(res.bytes_read));
    *args.ret = dst_wrap_table(t);
    return 0;
}

int dst_stl_compile(DstArgs args) {
    DstCompileOptions opts;
    DstCompileResult res;
    DstTable *t;
    if (args.n < 1) {
        *args.ret = dst_cstringv("expected at least on argument");
        return 1;
    }
    if (args.n >= 3 && !dst_checktype(args.v[2], DST_TUPLE)) {
        *args.ret = dst_cstringv("expected source map to be tuple");
        return 1;
    }
    opts.source = args.v[0];
    opts.env = args.n >= 2 ? args.v[1] : dst_loadstl(0);
    opts.sourcemap = args.n >= 3 ? dst_unwrap_tuple(args.v[2]) : NULL;
    opts.flags = 0;
    res = dst_compile(opts);
    if (res.status == DST_COMPILE_OK) {
        DstFunction *fun = dst_compile_func(res);
        *args.ret = dst_wrap_function(fun);
    } else {
        t = dst_table(2);
        dst_table_put(t, dst_cstringv("error"), dst_wrap_string(res.error));
        dst_table_put(t, dst_cstringv("error-start"), dst_wrap_integer(res.error_start));
        dst_table_put(t, dst_cstringv("error-end"), dst_wrap_integer(res.error_end));
        *args.ret = dst_wrap_table(t);
    }
    return 0;
    
}

int dst_stl_exit(DstArgs args) {
    int32_t exitcode = 0;
    if (args.n > 0) {
        exitcode = dst_hash(args.v[0]);
    }
    exit(exitcode);
    return 0;
}

int dst_stl_print(DstArgs args) {
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
    return 0;
}

int dst_stl_describe(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n; ++i) {
        int32_t j, len;
        const uint8_t *vstr = dst_description(args.v[i]);
        len = dst_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    return 0;
}

int dst_stl_string(DstArgs args) {
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

int dst_stl_asm(DstArgs args) {
    DstAssembleOptions opts;
    DstAssembleResult res;
    if (args.n < 1) {
        *args.ret = dst_cstringv("expected assembly source");
        return 1;
    }
    opts.source = args.v[0];
    opts.flags = 0;
    res = dst_asm(opts);
    if (res.status == DST_ASSEMBLE_OK) {
        *args.ret = dst_wrap_function(dst_asm_func(res));
        return 0;
    } else {
        *args.ret = dst_wrap_string(res.error);
        return 1;
    }
}

int dst_stl_disasm(DstArgs args) {
    DstFunction *f;
    if (args.n < 1 || !dst_checktype(args.v[0], DST_FUNCTION)) {
        *args.ret = dst_cstringv("expected function");
        return 1;
    }
    f = dst_unwrap_function(args.v[0]);
    *args.ret = dst_disasm(f->def);
    return 0;
}

int dst_stl_tuple(DstArgs args) {
    *args.ret = dst_wrap_tuple(dst_tuple_n(args.v, args.n));
    return 0;
}

int dst_stl_array(DstArgs args) {
    DstArray *array = dst_array(args.n);
    array->count = args.n;
    memcpy(array->data, args.v, args.n * sizeof(Dst));
    *args.ret = dst_wrap_array(array);
    return 0;
}

int dst_stl_table(DstArgs args) {
    int32_t i;
    DstTable *table = dst_table(args.n >> 1);
    if (args.n & 1) {
        *args.ret = dst_cstringv("expected even number of arguments");
        return 1;
    }
    for (i = 0; i < args.n; i += 2) {
        dst_table_put(table, args.v[i], args.v[i + 1]);
    }
    *args.ret = dst_wrap_table(table);
    return 0;
}

int dst_stl_struct(DstArgs args) {
    int32_t i;
    DstKV *st = dst_struct_begin(args.n >> 1);
    if (args.n & 1) {
        *args.ret = dst_cstringv("expected even number of arguments");
        return 1;
    }
    for (i = 0; i < args.n; i += 2) {
        dst_struct_put(st, args.v[i], args.v[i + 1]);
    }
    *args.ret = dst_wrap_struct(dst_struct_end(st));
    return 0;
}

int dst_stl_fiber(DstArgs args) {
    DstFiber *fiber;
    if (args.n < 1) {
        *args.ret = dst_cstringv("expected at least one argument");
        return 1;
    }
    if (!dst_checktype(args.v[0], DST_FUNCTION)) {
        *args.ret = dst_cstringv("expected a function");
        return 1;
    }
    fiber = dst_fiber(64);
    dst_fiber_funcframe(fiber, dst_unwrap_function(args.v[0]));
    fiber->parent = dst_vm_fiber;
    *args.ret = dst_wrap_fiber(fiber);
    return 0;
}

int dst_stl_buffer(DstArgs args) {
    DstBuffer *buffer = dst_buffer(10);
    int32_t i;
    for (i = 0; i < args.n; ++i) {
        const uint8_t *bytes = dst_to_string(args.v[i]);
        int32_t len = dst_string_length(bytes);
        dst_buffer_push_bytes(buffer, bytes, len);
    }
    *args.ret = dst_wrap_buffer(buffer);
    return 0;
}

int dst_stl_gensym(DstArgs args) {
    if (args.n > 1) {
        *args.ret = dst_cstringv("expected one argument");
        return 1;
    }
    if (args.n == 0) {
        *args.ret = dst_wrap_symbol(dst_symbol_gen(NULL, 0));
    } else {
        const uint8_t *s = dst_to_string(args.v[0]);
        *args.ret = dst_wrap_symbol(dst_symbol_gen(s, dst_string_length(s)));
    }
    return 0;
}

int dst_stl_length(DstArgs args) {
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected at least 1 argument");
        return 1;
    }
    *args.ret = dst_wrap_integer(dst_length(args.v[0]));
    return 0;
}

int dst_stl_get(DstArgs args) {
    int32_t i;
    Dst ds;
    if (args.n < 1) {
        *args.ret = dst_cstringv("expected at least 1 argument");
        return 1;
    }
    ds = args.v[0];
    for (i = 1; i < args.n; i++) {
        ds = dst_get(ds, args.v[i]);
        if (dst_checktype(ds, DST_NIL))
            break;
    }
    *args.ret = ds;
    return 0;
}

int dst_stl_status(DstArgs args) {
    const char *status;
    if (args.n != 1) {
        *args.ret = dst_cstringv("expected 1 argument");
        return 1;
    }
    if (!dst_checktype(args.v[0], DST_FIBER)) {
        *args.ret = dst_cstringv("expected fiber");
        return 1;
    }
    switch(dst_unwrap_fiber(args.v[0])->status) {
        case DST_FIBER_PENDING:
            status = "pending";
            break;
        case DST_FIBER_NEW:
            status = "new";
            break;
        case DST_FIBER_ALIVE:
            status = "alive";
            break;
        case DST_FIBER_DEAD:
            status = "dead";
            break;
        case DST_FIBER_ERROR:
            status = "error";
            break;
    }
    *args.ret = dst_cstringv(status);
    return 0;
}

int dst_stl_put(DstArgs args) {
    Dst ds, key, value;
    DstArgs subargs = args;
    if (args.n < 3) {
        *args.ret = dst_cstringv("expected at least 3 arguments");
        return 1;
    }
    subargs.n -= 2;
    if (dst_stl_get(subargs)) {
        return 1;
    }
    ds = *args.ret;
    key = args.v[args.n - 2];
    value = args.v[args.n - 1];
    dst_put(ds, key, value);
    return 0;
}

static int dst_stl_equal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (!dst_equals(args.v[i], args.v[i+1])) {
            *args.ret = dst_wrap_false();
            return 0;
        }
    }
    *args.ret = dst_wrap_true();
    return 0;
}

static int dst_stl_notequal(DstArgs args) {
    int32_t i;
    for (i = 0; i < args.n - 1; i++) {
        if (dst_equals(args.v[i], args.v[i+1])) {
            *args.ret = dst_wrap_false();
            return 0;
        }
    }
    *args.ret = dst_wrap_true();
    return 0;
}

static int dst_stl_not(DstArgs args) {
    *args.ret = dst_wrap_boolean(args.n == 0 || !dst_truthy(args.v[0]));
    return 0;
}

#define DST_DEFINE_COMPARATOR(name, pred)\
static int dst_stl_##name(DstArgs args) {\
    int32_t i;\
    for (i = 0; i < args.n - 1; i++) {\
        if (dst_compare(args.v[i], args.v[i+1]) pred) {\
            *args.ret = dst_wrap_false();\
            return 0;\
        }\
    }\
    *args.ret = dst_wrap_true();\
    return 0;\
}

DST_DEFINE_COMPARATOR(ascending, >= 0)
DST_DEFINE_COMPARATOR(descending, <= 0)
DST_DEFINE_COMPARATOR(notdescending, > 0)
DST_DEFINE_COMPARATOR(notascending, < 0)

static DstReg stl[] = {
    {"push", dst_stl_push},
    {"load-native", dst_load_native},
    {"parse", dst_stl_parse},
    {"compile", dst_stl_compile},
    {"int", dst_int},
    {"real", dst_real},
    {"print", dst_stl_print},
    {"describe", dst_stl_describe},
    {"string", dst_stl_string},
    {"table", dst_stl_table},
    {"array", dst_stl_array},
    {"tuple", dst_stl_tuple},
    {"struct", dst_stl_struct},
    {"fiber", dst_stl_fiber},
    {"status", dst_stl_status},
    {"buffer", dst_stl_buffer},
    {"gensym", dst_stl_gensym},
    {"asm", dst_stl_asm},
    {"disasm", dst_stl_disasm},
    {"get", dst_stl_get},
    {"put", dst_stl_put},
    {"length", dst_stl_length},
    {"+", dst_add},
    {"-", dst_subtract},
    {"*", dst_multiply},
    {"/", dst_divide},
    {"%", dst_modulo},
    {"cos", dst_cos},
    {"sin", dst_sin},
    {"tan", dst_tan},
    {"acos", dst_acos},
    {"asin", dst_asin},
    {"atan", dst_atan},
    {"exp", dst_exp},
    {"log", dst_log},
    {"log10", dst_log10},
    {"sqrt", dst_sqrt},
    {"floor", dst_floor},
    {"ceil", dst_ceil},
    {"pow", dst_pow},
    {"=", dst_stl_equal},
    {"not=", dst_stl_notequal},
    {"<", dst_stl_ascending},
    {">", dst_stl_descending},
    {"<=", dst_stl_notdescending},
    {">=", dst_stl_notascending},
    {"|", dst_bor},
    {"&", dst_band},
    {"^", dst_bxor},
    {">>", dst_lshift},
    {"<<", dst_rshift},
    {">>>", dst_lshiftu},
    {"not", dst_stl_not},
    {"fopen", dst_stl_fileopen},
    {"fclose", dst_stl_fileclose},
    {"fwrite", dst_stl_filewrite},
    {"fread", dst_stl_fileread},
    {"exit!", dst_stl_exit}
};

Dst dst_loadstl(int flags) {
    Dst ret = dst_loadreg(stl, sizeof(stl)/sizeof(DstReg));
    if (flags & DST_LOAD_ROOT) {
        dst_gcroot(ret);
    }
    if (dst_checktype(ret, DST_TABLE)) {
        DstTable *v = dst_table(1);
        dst_table_put(v, dst_csymbolv("value"), ret);
        dst_put(ret, dst_csymbolv("_env"), dst_wrap_table(v));
    }
    return ret;
}

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
#include <dst/dstcorelib.h>
#include "state.h"

int dst_core_print(DstArgs args) {
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

int dst_core_describe(DstArgs args) {
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

int dst_core_string(DstArgs args) {
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

int dst_core_symbol(DstArgs args) {
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

int dst_core_buffer(DstArgs args) {
    int32_t i;
    DstBuffer *b = dst_buffer(0);
    for (i = 0; i < args.n; ++i) {
        int32_t len;
        const uint8_t *str = dst_to_string(args.v[i]);
        len = dst_string_length(str);
        dst_buffer_push_bytes(b, str, len);
    }
    return dst_return(args, dst_wrap_buffer(b));
}

int dst_core_format(DstArgs args) {
    const uint8_t *format;
    int32_t i, len, n;
    DstBuffer buf;
    dst_minarity(args, 1);
    dst_arg_bytes(format, len, args, 0);
    n = 1;
    dst_buffer_init(&buf, len);
    for (i = 0; i < len; i++) {
        uint8_t c = format[i];
        if (c != '%') {
            dst_buffer_push_u8(&buf, c);
        } else {
            if (++i == len) break;
            c = format[i];
            switch (c) {
                default:
                    dst_buffer_push_u8(&buf, c);
                    break;
                case 's':
                {
                    if (n >= args.n) goto noarg;
                    dst_buffer_push_string(&buf, dst_to_string(args.v[n++]));
                    break;
                }
            }
        }
    }
    *args.ret = dst_wrap_string(dst_string(buf.data, buf.count));
    dst_buffer_deinit(&buf);
    return 0;
noarg:
    dst_buffer_deinit(&buf);
    return dst_throw(args, "not enough arguments to format");
}

int dst_core_scannumber(DstArgs args) {
    const uint8_t *data;
    Dst x;
    int32_t len;
    dst_fixarity(args, 1);
    dst_arg_bytes(data, len, args, 0);
    x = dst_scan_number(data, len);
    if (!dst_checktype(x, DST_INTEGER) && !dst_checktype(x, DST_REAL)) {
        return dst_throw(args, "error parsing number");
    }
    return dst_return(args, x);
}

int dst_core_scaninteger(DstArgs args) {
    const uint8_t *data;
    int32_t len, ret;
    int err = 0;
    dst_fixarity(args, 1);
    dst_arg_bytes(data, len, args, 0);
    ret = dst_scan_integer(data, len, &err);
    if (err) {
        return dst_throw(args, "error parsing integer");
    }
    return dst_return(args, dst_wrap_integer(ret));
}

int dst_core_scanreal(DstArgs args) {
    const uint8_t *data;
    int32_t len;
    double ret;
    int err = 0;
    dst_fixarity(args, 1);
    dst_arg_bytes(data, len, args, 0);
    ret = dst_scan_real(data, len, &err);
    if (err) {
        return dst_throw(args, "error parsing real");
    }
    return dst_return(args, dst_wrap_real(ret));
}

int dst_core_tuple(DstArgs args) {
    return dst_return(args, dst_wrap_tuple(dst_tuple_n(args.v, args.n)));
}

int dst_core_array(DstArgs args) {
    DstArray *array = dst_array(args.n);
    array->count = args.n;
    memcpy(array->data, args.v, args.n * sizeof(Dst));
    return dst_return(args, dst_wrap_array(array));
}

int dst_core_table(DstArgs args) {
    int32_t i;
    DstTable *table = dst_table(args.n >> 1);
    if (args.n & 1) return dst_throw(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_table_put(table, args.v[i], args.v[i + 1]);
    }
    return dst_return(args, dst_wrap_table(table));
}

int dst_core_struct(DstArgs args) {
    int32_t i;
    DstKV *st = dst_struct_begin(args.n >> 1);
    if (args.n & 1) return dst_throw(args, "expected even number of arguments");
    for (i = 0; i < args.n; i += 2) {
        dst_struct_put(st, args.v[i], args.v[i + 1]);
    }
    return dst_return(args, dst_wrap_struct(dst_struct_end(st)));
}

int dst_core_fiber(DstArgs args) {
    DstFiber *fiber;
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_FUNCTION);
    fiber = dst_fiber(dst_unwrap_function(args.v[0]), 64);
    return dst_return(args, dst_wrap_fiber(fiber));
}

int dst_core_gensym(DstArgs args) {
    dst_maxarity(args, 1);
    if (args.n == 0) {
        return dst_return(args, dst_wrap_symbol(dst_symbol_gen(NULL, 0)));
    } else {
        const uint8_t *s = dst_to_string(args.v[0]);
        return dst_return(args, dst_wrap_symbol(dst_symbol_gen(s, dst_string_length(s))));
    }
}

int dst_core_length(DstArgs args) {
    dst_fixarity(args, 1);
    return dst_return(args, dst_wrap_integer(dst_length(args.v[0])));
}

int dst_core_get(DstArgs args) {
    int32_t i;
    Dst ds;
    dst_minarity(args, 1);
    ds = args.v[0];
    for (i = 1; i < args.n; i++) {
        ds = dst_get(ds, args.v[i]);
        if (dst_checktype(ds, DST_NIL))
            break;
    }
    return dst_return(args, ds);
}

int dst_core_put(DstArgs args) {
    Dst ds, key, value;
    DstArgs subargs = args;
    dst_minarity(args, 3);
    subargs.n -= 2;
    if (dst_core_get(subargs)) return 1;
    ds = *args.ret;
    key = args.v[args.n - 2];
    value = args.v[args.n - 1];
    dst_put(ds, key, value);
    return 0;
}

int dst_core_gccollect(DstArgs args) {
    (void) args;
    dst_collect();
    return 0;
}

int dst_core_gcsetinterval(DstArgs args) {
    int32_t val;
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_INTEGER);
    val = dst_unwrap_integer(args.v[0]);
    if (val < 0)
        return dst_throw(args, "expected non-negative integer");
    dst_vm_gc_interval = val;
    return dst_return(args, args.v[0]); 
}

int dst_core_gcinterval(DstArgs args) {
    dst_fixarity(args, 0);
    return dst_return(args, dst_wrap_integer(dst_vm_gc_interval));
}

int dst_core_type(DstArgs args) {
    dst_fixarity(args, 1);
    if (dst_checktype(args.v[0], DST_ABSTRACT)) {
        return dst_return(args, dst_csymbolv(dst_abstract_type(dst_unwrap_abstract(args.v[0]))->name));
    } else {
        return dst_return(args, dst_csymbolv(dst_type_names[dst_type(args.v[0])]));
    }
}

int dst_core_next(DstArgs args) {
    Dst ds;
    const DstKV *kv;
    dst_fixarity(args, 2);
    dst_checkmany(args, 0, DST_TFLAG_DICTIONARY);
    ds = args.v[0];
    if (dst_checktype(ds, DST_TABLE)) {
        DstTable *t = dst_unwrap_table(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_table_find(t, args.v[1]);
    } else {
        const DstKV *st = dst_unwrap_struct(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_struct_find(st, args.v[1]);
    }
    kv = dst_next(ds, kv);
    if (kv) {
        return dst_return(args, kv->key);
    }
    return dst_return(args, dst_wrap_nil());
}

int dst_core_hash(DstArgs args) {
    dst_fixarity(args, 1);
    return dst_return(args, dst_wrap_integer(dst_hash(args.v[0])));
}

int dst_core_string_slice(DstArgs args) {
    const uint8_t *data;
    int32_t len, start, end;
    const uint8_t *ret;
    dst_minarity(args, 1);
    dst_maxarity(args, 3);
    if (!dst_chararray_view(args.v[0], &data, &len))
        return dst_throw(args, "expected buffer|string|symbol");
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (dst_checktype(args.v[1], DST_INTEGER)) {
        start = dst_unwrap_integer(args.v[1]);
    } else {
        return dst_throw(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (dst_checktype(args.v[2], DST_INTEGER)) {
        end = dst_unwrap_integer(args.v[2]);
    } else {
        return dst_throw(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = dst_string(data + start, end - start);
    } else {
        ret = dst_cstring("");
    }
    return dst_return(args, dst_wrap_string(ret));
}

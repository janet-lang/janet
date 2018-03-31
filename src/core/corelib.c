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

int dst_core_gensym(DstArgs args) {
    if (args.n > 1) return dst_throw(args, "expected one argument");
    if (args.n == 0) {
        return dst_return(args, dst_wrap_symbol(dst_symbol_gen(NULL, 0)));
    } else {
        const uint8_t *s = dst_to_string(args.v[0]);
        return dst_return(args, dst_wrap_symbol(dst_symbol_gen(s, dst_string_length(s))));
    }
}

int dst_core_length(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected at least 1 argument");
    return dst_return(args, dst_wrap_integer(dst_length(args.v[0])));
}

int dst_core_get(DstArgs args) {
    int32_t i;
    Dst ds;
    if (args.n < 1) return dst_throw(args, "expected at least 1 argument");
    ds = args.v[0];
    for (i = 1; i < args.n; i++) {
        ds = dst_get(ds, args.v[i]);
        if (dst_checktype(ds, DST_NIL))
            break;
    }
    return dst_return(args, ds);
}

int dst_core_rawget(DstArgs args) {
    if (args.n != 2) return dst_throw(args, "expected 2 arguments");
    if (!dst_checktype(args.v[0], DST_TABLE)) return dst_throw(args, "expected table");
    return dst_return(args, dst_table_rawget(dst_unwrap_table(args.v[0]), args.v[1]));
}

int dst_core_getproto(DstArgs args) {
    DstTable *t;
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    if (!dst_checktype(args.v[0], DST_TABLE)) return dst_throw(args, "expected table");
    t = dst_unwrap_table(args.v[0]);
    return dst_return(args, t->proto
            ? dst_wrap_table(t->proto)
            : dst_wrap_nil());
}

int dst_core_setproto(DstArgs args) {
    if (args.n != 2) return dst_throw(args, "expected 2 arguments");
    if (!dst_checktype(args.v[0], DST_TABLE)) return dst_throw(args, "expected table");
    if (!dst_checktype(args.v[1], DST_TABLE) && !dst_checktype(args.v[1], DST_NIL))
        return dst_throw(args, "expected table");
    dst_unwrap_table(args.v[0])->proto = dst_checktype(args.v[1], DST_TABLE)
        ? dst_unwrap_table(args.v[1])
        : NULL;
    return dst_return(args, args.v[0]);
}

int dst_core_put(DstArgs args) {
    Dst ds, key, value;
    DstArgs subargs = args;
    if (args.n < 3) return dst_throw(args, "expected at least 3 arguments");
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
    if (args.n < 1 ||
            !dst_checktype(args.v[0], DST_INTEGER) ||
            dst_unwrap_integer(args.v[0]) < 0)
        return dst_throw(args, "expected non-negative integer");
    dst_vm_gc_interval = dst_unwrap_integer(args.v[0]);
    return dst_return(args, dst_wrap_integer(dst_vm_gc_interval));
}

int dst_core_gcinterval(DstArgs args) {
    return dst_return(args, dst_wrap_integer(dst_vm_gc_interval));
}

int dst_core_type(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    if (dst_checktype(args.v[0], DST_ABSTRACT)) {
        return dst_return(args, dst_csymbolv(dst_abstract_type(dst_unwrap_abstract(args.v[0]))->name));
    } else {
        return dst_return(args, dst_csymbolv(dst_type_names[dst_type(args.v[0])]));
    }
}

int dst_core_next(DstArgs args) {
    Dst ds;
    const DstKV *kv;
    if (args.n != 2) return dst_throw(args, "expected 2 arguments");
    ds = args.v[0];
    if (dst_checktype(ds, DST_TABLE)) {
        DstTable *t = dst_unwrap_table(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_table_find(t, args.v[1]);
    } else if (dst_checktype(ds, DST_STRUCT)) {
        const DstKV *st = dst_unwrap_struct(ds);
        kv = dst_checktype(args.v[1], DST_NIL)
            ? NULL
            : dst_struct_find(st, args.v[1]);
    } else {
        return dst_throw(args, "expected table/struct");
    }
    kv = dst_next(ds, kv);
    if (kv) {
        return dst_return(args, kv->key);
    }
    return dst_return(args, dst_wrap_nil());
}

int dst_core_hash(DstArgs args) {
    if (args.n != 1) return dst_throw(args, "expected 1 argument");
    return dst_return(args, dst_wrap_integer(dst_hash(args.v[0])));
}

int dst_core_string_slice(DstArgs args) {
    const uint8_t *data;
    int32_t len, start, end;
    const uint8_t *ret;
    if (args.n < 1 || !dst_chararray_view(args.v[0], &data, &len))
        return dst_throw(args, "expected buffer/string");
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

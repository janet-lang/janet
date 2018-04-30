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
#include "util.h"
#include "gc.h"

/* Base 64 lookup table for digits */
const char dst_base64[65] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* The DST value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
const char *const dst_type_names[16] = {
    ":nil",
    ":false",
    ":true",
    ":fiber",
    ":integer",
    ":real",
    ":string",
    ":symbol",
    ":array",
    ":tuple",
    ":table",
    ":struct",
    ":buffer",
    ":function",
    ":cfunction",
    ":abstract"
};

/* Calculate hash for string */

int32_t dst_string_calchash(const uint8_t *str, int32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t dst_array_calchash(const Dst *array, int32_t len) {
    const Dst *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + dst_hash(*array++);
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t dst_kv_calchash(const DstKV *kvs, int32_t len) {
    const DstKV *end = kvs + len;
    uint32_t hash = 5381;
    while (kvs < end) {
        hash = (hash << 5) + hash + dst_hash(kvs->key);
        hash = (hash << 5) + hash + dst_hash(kvs->value);
        kvs++;
    }
    return (int32_t) hash;
}

/* Calculate next power of 2. May overflow. If n is 0,
 * will return 0. */
int32_t dst_tablen(int32_t n) {
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* Compare a dst string with a cstring. more efficient than loading
 * c string as a dst string. */
int dst_cstrcmp(const uint8_t *str, const char *other) {
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

/* Add a module definition */
void dst_env_def(DstTable *env, const char *name, Dst val) {
    DstTable *subt = dst_table(1);
    dst_table_put(subt, dst_csymbolv(":value"), val);
    dst_table_put(env, dst_csymbolv(name), dst_wrap_table(subt));
}

/* Add a var to the environment */
void dst_env_var(DstTable *env, const char *name, Dst val) {
    DstArray *array = dst_array(1);
    DstTable *subt = dst_table(1);
    dst_array_push(array, val);
    dst_table_put(subt, dst_csymbolv(":ref"), dst_wrap_array(array));
    dst_table_put(env, dst_csymbolv(name), dst_wrap_table(subt));
}

/* Load many cfunctions at once */
void dst_env_cfuns(DstTable *env, const DstReg *cfuns) {
    while (cfuns->name) {
        dst_env_def(env, cfuns->name, dst_wrap_cfunction(cfuns->cfun));
        cfuns++;
    }
}

/* Resolve a symbol in the environment. Undefined symbols will
 * resolve to nil */
Dst dst_env_resolve(DstTable *env, const char *name) {
    Dst ref;
    Dst entry = dst_table_get(env, dst_csymbolv(name));
    if (dst_checktype(entry, DST_NIL)) return dst_wrap_nil();
    ref = dst_get(entry, dst_csymbolv(":ref"));
    if (dst_checktype(ref, DST_ARRAY)) {
        return dst_getindex(ref, 0);
    }
    return dst_get(entry, dst_csymbolv(":value"));
}

/* Get module from the arguments passed to library */
DstTable *dst_env_arg(DstArgs args) {
    DstTable *module;
    if (args.n >= 1 && dst_checktype(args.v[0], DST_TABLE)) {
        module = dst_unwrap_table(args.v[0]);
    } else {
        module = dst_table(0);
    }
    *args.ret = dst_wrap_table(module);
    return module;
}

/* Read both tuples and arrays as c pointers + int32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int dst_seq_view(Dst seq, const Dst **data, int32_t *len) {
    if (dst_checktype(seq, DST_ARRAY)) {
        *data = dst_unwrap_array(seq)->data;
        *len = dst_unwrap_array(seq)->count;
        return 1;
    } else if (dst_checktype(seq, DST_TUPLE)) {
        *data = dst_unwrap_tuple(seq);
        *len = dst_tuple_length(dst_unwrap_struct(seq));
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + int32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int dst_chararray_view(Dst str, const uint8_t **data, int32_t *len) {
    if (dst_checktype(str, DST_STRING) || dst_checktype(str, DST_SYMBOL)) {
        *data = dst_unwrap_string(str);
        *len = dst_string_length(dst_unwrap_string(str));
        return 1;
    } else if (dst_checktype(str, DST_BUFFER)) {
        *data = dst_unwrap_buffer(str)->data;
        *len = dst_unwrap_buffer(str)->count;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int dst_hashtable_view(Dst tab, const DstKV **data, int32_t *len, int32_t *cap) {
    if (dst_checktype(tab, DST_TABLE)) {
        *data = dst_unwrap_table(tab)->data;
        *cap = dst_unwrap_table(tab)->capacity;
        *len = dst_unwrap_table(tab)->count;
        return 1;
    } else if (dst_checktype(tab, DST_STRUCT)) {
        *data = dst_unwrap_struct(tab);
        *cap = dst_struct_capacity(dst_unwrap_struct(tab));
        *len = dst_struct_length(dst_unwrap_struct(tab));
        return 1;
    }
    return 0;
}

/* Get actual type name of a value for debugging purposes */
static const char *typestr(DstArgs args, int32_t n) {
    DstType actual = n < args.n ? dst_type(args.v[n]) : DST_NIL;
    return (actual == DST_ABSTRACT)
        ? dst_abstract_type(dst_unwrap_abstract(args.v[n]))->name
        : dst_type_names[actual];
}

int dst_type_err(DstArgs args, int32_t n, DstType expected) {
    const uint8_t *message = dst_formatc(
            "bad argument #%d, expected %t, got %s",
            n,
            expected,
            typestr(args, n));
    return dst_throwv(args, dst_wrap_string(message));
}

int dst_typemany_err(DstArgs args, int32_t n, int expected) {
    int i;
    int first = 1;
    const uint8_t *message;
    DstBuffer buf;
    dst_buffer_init(&buf, 20);
    dst_buffer_push_string(&buf, dst_formatc("bad argument #%d, expected ", n));
    i = 0;
    while (expected) {
        if (1 & expected) {
            if (first) {
                first = 0;
            } else {
                dst_buffer_push_u8(&buf, '|');
            }
            dst_buffer_push_cstring(&buf, dst_type_names[i] + 1);
        }
        i++;
        expected >>= 1;
    }
    dst_buffer_push_cstring(&buf, ", got ");
    dst_buffer_push_cstring(&buf, typestr(args, n));
    message = dst_string(buf.data, buf.count);
    dst_buffer_deinit(&buf);
    return dst_throwv(args, dst_wrap_string(message));
}

int dst_arity_err(DstArgs args, int32_t n, const char *prefix) {
    return dst_throwv(args,
            dst_wrap_string(dst_formatc(
                    "expected %s%d argument%s, got %d", 
                    prefix, n, n == 1 ? "" : "s", args.n)));
}

int dst_typeabstract_err(DstArgs args, int32_t n, DstAbstractType *at) {
    return dst_throwv(args,
            dst_wrap_string(dst_formatc(
                    "bad argument #%d, expected %t, got %s", 
                    n, at->name, typestr(args, n)))); 
}

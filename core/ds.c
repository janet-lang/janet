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

#include "internal.h"
#include "cache.h"
#include <dst/dst.h>

/* Get a value out af an associated data structure.
 * Returns possible c error message, and NULL for no error. The
 * useful return value is written to out on success */
const char *dst_value_get(DstValue ds, DstValue key, DstValue *out) {
    int64_t index;
    DstValue ret;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.as.integer, ds.as.array->count);
        if (index < 0) return "invalid array access";
        ret = ds.as.array->data[index];
        break;
    case DST_TUPLE:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.as.integer, dst_tuple_length(ds.as.tuple));
        if (index < 0) return "invalid tuple access";
        ret = ds.as.tuple[index];
        break;
    case DST_BUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.as.integer, ds.as.buffer->count);
        if (index < 0) return "invalid buffer access";
        ret.type = DST_INTEGER;
        ret.as.integer = ds.as.buffer->data[index];
        break;
    case DST_STRING:
    case DST_SYMBOL:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.as.integer, dst_string_length(ds.as.string));
        if (index < 0) return "invalid string access";
        ret.type = DST_INTEGER;
        ret.as.integer = ds.as.string[index];
        break;
    case DST_STRUCT:
        ret = dst_struct_get(ds.as.st, key);
        break;
    case DST_TABLE:
        ret = dst_table_get(ds.as.table, key);
        break;
    default:
        return "cannot get";
    }
    *out = ret;
    return NULL;
}

void dst_get(Dst *vm) {
    DstValue ds = dst_popv(vm);
    DstValue key = dst_popv(vm);
    DstValue ret;
    const char *err = dst_value_get(ds, key, &ret);
    if (err) {
        vm->flags = 1;
        dst_cstring(vm, err);
    } else {
        dst_pushv(vm, ret);
    }
}

/* Set a value in an associative data structure. Returns possible
 * error message, and NULL if no error. */
const char *dst_value_set(Dst *vm, DstValue ds, DstValue key, DstValue value) {
    int64_t index;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.as.integer, ds.as.array->count);
        if (index < 0) return "invalid array access";
        ds.as.array->data[index] = value;
        break;
    case DST_BUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        if (value.type != DST_INTEGER) return "expected integer value";
        index = dst_startrange(key.as.integer, ds.as.buffer->count);
        if (index < 0) return "invalid buffer access";
        ds.as.buffer->data[index] = (uint8_t) value.as.integer;
        break;
    case DST_TABLE:
        dst_table_put(vm, ds.as.table, key, value);
        break;
    default:
        return "cannot set";
    }
    return NULL;
}

void dst_set(Dst *vm) {
    DstValue ds = dst_popv(vm);
    DstValue key = dst_popv(vm);
    DstValue value = dst_popv(vm);
    const char *err = dst_value_set(vm, ds, key, value);
    if (err) {
        vm->flags = 1;
        vm->ret = dst_string_cv(vm, err);
    }
}

/* Get the next key in an associative data structure. Used for iterating through an
 * associative data structure. */
int dst_next(Dst *vm) {
    DstValue dsv = dst_popv(vm);
    DstValue keyv = dst_popv(vm);
    DstValue ret = keyv;
    switch(dsv.type) {
        default:
            dst_cerr(vm, "expected table or struct");
            return 0;
        case DST_TABLE:
            ret = dst_table_next(dsv.as.table, keyv);
            break;
        case DST_STRUCT:
            ret = dst_struct_next(dsv.as.st, keyv);
            break;
    }
    dst_pushv(vm, ret);
    return ret.type != DST_NIL;
}

/* Ensure capacity in a datastructure */
void dst_ensure(Dst *vm, int64_t index, uint32_t capacity) {
    DstValue x = dst_getv(vm, index);
    switch (x.type) {
        default:
            dst_cerr(vm, "could not ensure capacity");
            break;
        case DST_ARRAY:
            dst_array_ensure_(vm, x.as.array, capacity);
            break;
        case DST_BUFFER:
            dst_buffer_ensure_(vm, x.as.buffer, capacity);
            break;
    }
}

/* Get the length of an object. Returns errors for invalid types */
uint32_t dst_length(Dst *vm, int64_t index) {
    DstValue x = dst_getv(vm, index);
    uint32_t length;
    switch (x.type) {
        default:
            dst_cerr(vm, "cannot get length");
            return 0;
        case DST_STRING:
            length = dst_string_length(x.as.string);
            break;
        case DST_ARRAY:
            length = x.as.array->count;
            break;
        case DST_BUFFER:
            length = x.as.buffer->count;
            break;
        case DST_TUPLE:
            length = dst_tuple_length(x.as.tuple);
            break;
        case DST_STRUCT:
            length = dst_struct_length(x.as.st);
            break;
        case DST_TABLE:
            length = x.as.table->count;
            break;
    }
    return length;
}

/* Get the capacity of an object. Returns errors for invalid types */
uint32_t dst_capacity(Dst *vm, int64_t index) {
    DstValue x = dst_getv(vm, index);
    uint32_t cap;
    switch (x.type) {
        default:
            dst_cerr(vm, "cannot get capacity");
            return 0;
        case DST_STRING:
            cap = dst_string_length(x.as.string);
            break;
        case DST_ARRAY:
            cap = x.as.array->capacity;
            break;
        case DST_BUFFER:
            cap = x.as.buffer->capacity;
            break;
        case DST_TUPLE:
            cap = dst_tuple_length(x.as.tuple);
            break;
        case DST_STRUCT:
            cap = dst_struct_length(x.as.st);
            break;
        case DST_TABLE:
            cap = x.as.table->capacity;
            break;
    }
    return cap;
}

/* Sequence functions */
const char *dst_getindex_value(DstValue ds, uint32_t index, DstValue *out) {
    switch (ds.type) {
        default:
            return "expected sequence type";
        case DST_STRING:
            if (index >= dst_string_length(ds.as.string)) return "index out of bounds";
            *out = dst_wrap_integer(ds.as.string[index]);
            break;
        case DST_ARRAY:
            if (index >= ds.as.array->count) return "index out of bounds";
            *out = ds.as.array->data[index];
            break;
        case DST_BUFFER:
            if (index >= ds.as.buffer->count) return "index out of bounds";
            *out = dst_wrap_integer(ds.as.buffer->data[index]);
            break;
        case DST_TUPLE:
            if (index >= dst_tuple_length(ds.as.tuple)) return "index out of bounds";
            *out = ds.as.tuple[index];
            break;
    }
    return NULL;
}
void dst_getindex(Dst *vm, uint32_t index) {
    DstValue out;
    DstValue ds = dst_popv(vm);
    const char *e = dst_getindex_value(ds, index, &out);
    if (e == NULL) {
        dst_pushv(vm, out);
    } else {
        dst_cstring(vm, e);
        vm->flags = 1;
    }
}

const char *dst_setindex_value(Dst *vm, DstValue ds, uint32_t index, DstValue value) {
    switch (ds.type) {
        default:
            return "expected mutable sequence type";
        case DST_ARRAY:
            if (index >= ds.as.array->count) {
                dst_array_ensure_(vm, ds.as.array, index + 1);
            }
            ds.as.array->data[index] = value;
            break;
        case DST_BUFFER:
            if (value.type != DST_INTEGER) return "expected integer type";
            if (index >= ds.as.buffer->count) {
                dst_buffer_ensure_(vm, ds.as.buffer, index + 1);
            }
            ds.as.buffer->data[index] = value.as.integer;
            break;
    }
    return NULL;
}
void dst_setindex(Dst *vm, uint32_t index) {
    DstValue ds = dst_popv(vm);
    DstValue value = dst_popv(vm);
    const char *e = dst_setindex_value(vm, ds, index, value);
    if (e != NULL) {
        dst_cstring(vm, e);
        vm->flags = 1;
    }
}

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

#include <gst/gst.h>
#include <stdio.h>

/* Boolean truth definition */
int gst_truthy(GstValue v) {
    return v.type != GST_NIL && !(v.type == GST_BOOLEAN && !v.data.boolean);
}

/* Check if two values are equal. This is strict equality with no conversion. */
int gst_equals(GstValue x, GstValue y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
        case GST_NIL:
            result = 1;
            break;
        case GST_BOOLEAN:
            result = (x.data.boolean == y.data.boolean);
            break;
        case GST_REAL:
            result = (x.data.real == y.data.real);
            break;
        case GST_INTEGER:
            result = (x.data.integer == y.data.integer);
            break;
        default:
            /* compare pointers */
            result = (x.data.pointer == y.data.pointer);
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t gst_hash(GstValue x) {
    uint32_t hash = 0;
    switch (x.type) {
    case GST_NIL:
        hash = 0;
        break;
    case GST_BOOLEAN:
        hash = x.data.boolean;
        break;
    case GST_STRING:
    case GST_SYMBOL:
        hash = gst_string_hash(x.data.string);
        break;
    case GST_TUPLE:
        hash = gst_tuple_hash(x.data.tuple);
        break;
    case GST_STRUCT:
        hash = gst_struct_hash(x.data.st);
        break;
    default:
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            hash = x.data.dwords[0] ^ x.data.dwords[1];
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (uint32_t) x.data.pointer;
        }
        break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int gst_compare(GstValue x, GstValue y) {
    if (x.type == y.type) {
        switch (x.type) {
            case GST_NIL:
                return 0;
            case GST_BOOLEAN:
                if (x.data.boolean == y.data.boolean) {
                    return 0;
                } else {
                    return x.data.boolean ? 1 : -1;
                }
            case GST_REAL:
                if (x.data.real == y.data.real) {
                    return 0;
                } else {
                    return x.data.real > y.data.real ? 1 : -1;
                }
            case GST_INTEGER:
                if (x.data.integer == y.data.integer) {
                    return 0;
                } else {
                    return x.data.integer > y.data.integer ? 1 : -1;
                }
            case GST_STRING:
                return gst_string_compare(x.data.string, y.data.string);
                /* Lower indices are most significant */
            case GST_TUPLE:
                {
                    uint32_t i;
                    uint32_t xlen = gst_tuple_length(x.data.tuple);
                    uint32_t ylen = gst_tuple_length(y.data.tuple);
                    uint32_t count = xlen < ylen ? xlen : ylen;
                    for (i = 0; i < count; ++i) {
                        int comp = gst_compare(x.data.tuple[i], y.data.tuple[i]);
                        if (comp != 0) return comp;
                    }
                    if (xlen < ylen)
                        return -1;
                    else if (xlen > ylen)
                        return 1;
                    return 0;
                }
                break;
            default:
                if (x.data.string == y.data.string) {
                    return 0;
                } else {
                    return x.data.string > y.data.string ? 1 : -1;
                }
        }
    } else if (x.type < y.type) {
        return -1;
    }
    return 1;
}

/* Get a value out af an associated data structure.
 * Returns possible c error message, and NULL for no error. The
 * useful return value is written to out on success */
const char *gst_get(GstValue ds, GstValue key, GstValue *out) {
    GstInteger index;
    GstValue ret;
    switch (ds.type) {
    case GST_ARRAY:
        if (key.type != GST_INTEGER) return "expected integer key";
        index = gst_startrange(key.data.integer, ds.data.array->count);
        if (index < 0) return "invalid array access";
        ret = ds.data.array->data[index];
        break;
    case GST_TUPLE:
        if (key.type != GST_INTEGER) return "expected integer key";
        index = gst_startrange(key.data.integer, gst_tuple_length(ds.data.tuple));
        if (index < 0) return "invalid tuple access";
        ret = ds.data.tuple[index];
        break;
    case GST_BYTEBUFFER:
        if (key.type != GST_INTEGER) return "expected integer key";
        index = gst_startrange(key.data.integer, ds.data.buffer->count);
        if (index < 0) return "invalid buffer access";
        ret.type = GST_INTEGER;
        ret.data.integer = ds.data.buffer->data[index];
        break;
    case GST_STRING:
    case GST_SYMBOL:
        if (key.type != GST_INTEGER) return "expected integer key";
        index = gst_startrange(key.data.integer, gst_string_length(ds.data.string));
        if (index < 0) return "invalid string access";
        ret.type = GST_INTEGER;
        ret.data.integer = ds.data.string[index];
        break;
    case GST_STRUCT:
        ret = gst_struct_get(ds.data.st, key);
        break;
    case GST_TABLE:
        ret = gst_table_get(ds.data.table, key);
        break;
    default:
       return "cannot get";
    }
    *out = ret;
    return NULL;
}

/* Set a value in an associative data structure. Returns possible
 * error message, and NULL if no error. */
const char *gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value) {
    GstInteger index;
    switch (ds.type) {
    case GST_ARRAY:
        if (key.type != GST_INTEGER) return "expected integer key";
        index = gst_startrange(key.data.integer, ds.data.array->count);
        if (index < 0) return "invalid array access";
        ds.data.array->data[index] = value;
        break;
    case GST_BYTEBUFFER:
        if (key.type != GST_INTEGER) return "expected integer key";
        if (value.type != GST_INTEGER) return "expected integer value";
        index = gst_startrange(key.data.integer, ds.data.buffer->count);
        if (index < 0) return "invalid buffer access";
        ds.data.buffer->data[index] = (uint8_t) value.data.integer;
        break;
    case GST_TABLE:
        gst_table_put(vm, ds.data.table, key, value);
        break;
    default:
        return "cannot set";
    }
    return NULL;
}

/* Get the length of an object. Returns errors for invalid types */
GstInteger gst_length(Gst *vm, GstValue x) {
    GstInteger length;
    switch (x.type) {
        default:
            vm->ret = gst_string_cv(vm, "cannot get length");
            return GST_RETURN_ERROR;
        case GST_STRING:
            length = gst_string_length(x.data.string);
            break;
        case GST_ARRAY:
            length = x.data.array->count;
            break;
        case GST_BYTEBUFFER:
            length = x.data.buffer->count;
            break;
        case GST_TUPLE:
            length = gst_tuple_length(x.data.tuple);
            break;
        case GST_STRUCT:
            length = gst_struct_length(x.data.st);
            break;
        case GST_TABLE:
            length = x.data.table->count;
            break;
    }
    return length;
}


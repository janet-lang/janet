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

/*
 * Define a number of functions that can be used internally on ANY DstValue.
 */

/* Boolean truth definition */
int dst_truthy(DstValue v) {
    return v.type != DST_NIL && !(v.type == DST_BOOLEAN && !v.as.boolean);
}

/* Check if two values are equal. This is strict equality with no conversion. */
int dst_equals(DstValue x, DstValue y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
        case DST_NIL:
            result = 1;
            break;
        case DST_BOOLEAN:
            result = (x.as.boolean == y.as.boolean);
            break;
        case DST_REAL:
            result = (x.as.real == y.as.real);
            break;
        case DST_INTEGER:
            result = (x.as.integer == y.as.integer);
            break;
        default:
            /* compare pointers */
            result = (x.as.pointer == y.as.pointer);
            break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t dst_hash(DstValue x) {
    uint32_t hash = 0;
    switch (x.type) {
    case DST_NIL:
        hash = 0;
        break;
    case DST_BOOLEAN:
        hash = x.as.boolean;
        break;
    case DST_STRING:
    case DST_SYMBOL:
        hash = dst_string_hash(x.as.string);
        break;
    case DST_TUPLE:
        hash = dst_tuple_hash(x.as.tuple);
        break;
    case DST_STRUCT:
        hash = dst_struct_hash(x.as.st);
        break;
    default:
        if (sizeof(double) == sizeof(void *)) {
            /* Assuming 8 byte pointer */
            uint64_t i = x.as.integer;
            hash = (i >> 32) ^ (i & 0xFFFFFFFF);
        } else {
            /* Assuming 4 byte pointer (or smaller) */
            hash = (uint32_t) x.as.pointer;
        }
        break;
    }
    return hash;
}

/* Computes hash of an array of values */
uint32_t dst_calchash_array(const DstValue *array, uint32_t len) {
    const DstValue *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + dst_hash(*array++);
    return hash;
}

/* Compare two strings */
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = dst_string_length(lhs);
    uint32_t ylen = dst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int dst_compare(DstValue x, DstValue y) {
    if (x.type == y.type) {
        switch (x.type) {
            case DST_NIL:
                return 0;
            case DST_BOOLEAN:
                if (x.as.boolean == y.as.boolean) {
                    return 0;
                } else {
                    return x.as.boolean ? 1 : -1;
                }
            case DST_REAL:
                
                /* Check for nans to ensure total order */
                if (x.as.real != x.as.real)
                    return y.as.real != y.as.real
                        ? 0
                        : -1;
                if (y.as.real != y.as.real)
                    return 1;

                if (x.as.real == y.as.real) {
                    return 0;
                } else {
                    return x.as.real > y.as.real ? 1 : -1;
                }
            case DST_INTEGER:
                if (x.as.integer == y.as.integer) {
                    return 0;
                } else {
                    return x.as.integer > y.as.integer ? 1 : -1;
                }
            case DST_STRING:
                return dst_string_compare(x.as.string, y.as.string);
                /* Lower indices are most significant */
            case DST_TUPLE:
                {
                    uint32_t i;
                    uint32_t xlen = dst_tuple_length(x.as.tuple);
                    uint32_t ylen = dst_tuple_length(y.as.tuple);
                    uint32_t count = xlen < ylen ? xlen : ylen;
                    for (i = 0; i < count; ++i) {
                        int comp = dst_compare(x.as.tuple[i], y.as.tuple[i]);
                        if (comp != 0) return comp;
                    }
                    if (xlen < ylen)
                        return -1;
                    else if (xlen > ylen)
                        return 1;
                    return 0;
                }
                break;
                /* TODO - how should structs compare by default? For now, just use pointers. */
            default:
                if (x.as.string == y.as.string) {
                    return 0;
                } else {
                    return x.as.string > y.as.string ? 1 : -1;
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
const char *dst_try_get(DstValue ds, DstValue key, DstValue *out) {
    int64_t index;
    DstValue ret;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (index < 0 || index >= ds.as.array->count)
            return "invalid array access";
        ret = ds.as.array->data[index];
        break;
    case DST_TUPLE:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (index < 0 || index >= dst_tuple_length(ds.as.tuple))
            return "invalid tuple access";
        ret = ds.as.tuple[index];
        break;
    case DST_BUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (index < 0 || index >= ds.as.buffer->count)
            return "invalid buffer access";
        ret.type = DST_INTEGER;
        ret.as.integer = ds.as.buffer->data[index];
        break;
    case DST_STRING:
    case DST_SYMBOL:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (index < 0 || index >= dst_string_length(ds.as.string))
            return "invalid string access";
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

/* Set a value in an associative data structure. Returns possible
 * error message, and NULL if no error. */
const char *dst_try_put(DstValue ds, DstValue key, DstValue value) {
    int64_t index;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (index < 0 || index >= ds.as.array->count)
            return "invalid array access";
        ds.as.array->data[index] = value;
        break;
    case DST_BUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = key.as.integer;
        if (value.type != DST_INTEGER) return "expected integer value";
        if (index < 0 || index >= ds.as.buffer->count)
            return "invalid buffer access";
        ds.as.buffer->data[index] = (uint8_t) value.as.integer;
        break;
    case DST_TABLE:
        dst_table_put(ds.as.table, key, value);
        break;
    default:
        return "cannot set";
    }
    return NULL;
}

/* Get the next key in an associative data structure. Used for iterating through an
 * associative data structure. */
const char *dst_try_next(DstValue ds, DstValue key, DstValue *out) {
    switch(ds.type) {
        default:
            return "expected table or struct";
        case DST_TABLE:
            *out = dst_table_next(ds.as.table, key);
            return NULL;
        case DST_STRUCT:
            *out = dst_struct_next(ds.as.st, key);
            return NULL;
    }
}

/* Get the length of an object. Returns errors for invalid types */
uint32_t dst_length(DstValue x) {
    switch (x.type) {
        default:
            return 0;
        case DST_STRING:
            return dst_string_length(x.as.string);
        case DST_ARRAY:
            return x.as.array->count;
        case DST_BUFFER:
            return x.as.buffer->count;
        case DST_TUPLE:
            return dst_tuple_length(x.as.tuple);
        case DST_STRUCT:
            return dst_struct_length(x.as.st);
        case DST_TABLE:
            return x.as.table->count;
    }
}

/* Get the capacity of an object. Returns 0 for invalid types */
uint32_t dst_capacity(DstValue x) {
    switch (x.type) {
        default:
            return 0;
        case DST_STRING:
            return dst_string_length(x.as.string);
        case DST_ARRAY:
            return x.as.array->capacity;
        case DST_BUFFER:
            return x.as.buffer->capacity;
        case DST_TUPLE:
            return dst_tuple_length(x.as.tuple);
        case DST_STRUCT:
            return dst_struct_length(x.as.st);
        case DST_TABLE:
            return x.as.table->capacity;
    }
}

/* Index into a data structure. Returns nil for out of bounds or invliad data structure */
DstValue dst_getindex(DstValue ds, uint32_t index) {
    switch (ds.type) {
        default:
            return dst_wrap_nil();
        case DST_STRING:
            if (index >= dst_string_length(ds.as.string)) return dst_wrap_nil();
            return dst_wrap_integer(ds.as.string[index]);
        case DST_ARRAY:
            if (index >= ds.as.array->count) return dst_wrap_nil();
            return ds.as.array->data[index];
        case DST_BUFFER:
            if (index >= ds.as.buffer->count) return dst_wrap_nil();
            return dst_wrap_integer(ds.as.buffer->data[index]);
        case DST_TUPLE:
            if (index >= dst_tuple_length(ds.as.tuple)) return dst_wrap_nil();
            return ds.as.tuple[index];
    }
}

/* Set an index in a linear data structure. Does nothing if data structure
 * is invalid */
void dst_setindex(DstValue ds, DstValue value, uint32_t index) {
    switch (ds.type) {
        default:
            return;
        case DST_ARRAY:
            if (index >= ds.as.array->count) {
                dst_array_ensure(ds.as.array, 2 * index);
                ds.as.array->count = index + 1;
            }
            ds.as.array->data[index] = value;
            return;
        case DST_BUFFER:
            if (value.type != DST_INTEGER) return;
            if (index >= ds.as.buffer->count) {
                dst_buffer_ensure(ds.as.buffer, 2 * index);
                ds.as.buffer->count = index + 1;
            }
            ds.as.buffer->data[index] = value.as.integer;
            return;
    }
}

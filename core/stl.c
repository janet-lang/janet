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

static const char GST_EXPECTED_INTEGER[] = "expected integer";
static const char GST_EXPECTED_STRING[] = "expected string";

/***/
/* Arithmetic */
/***/

#define MAKE_BINOP(name, op)\
GstValue gst_stl_binop_##name(GstValue lhs, GstValue rhs) {\
    if (lhs.type == GST_INTEGER)\
        if (rhs.type == GST_INTEGER)\
            return gst_wrap_integer(lhs.data.integer op rhs.data.integer);\
        else if (rhs.type == GST_REAL)\
            return gst_wrap_real(lhs.data.integer op rhs.data.real);\
        else\
            return gst_wrap_nil();\
    else if (lhs.type == GST_REAL)\
        if (rhs.type == GST_INTEGER)\
            return gst_wrap_real(lhs.data.real op rhs.data.integer);\
        else if (rhs.type == GST_REAL)\
            return gst_wrap_real(lhs.data.real op rhs.data.real);\
        else\
            return gst_wrap_nil();\
    else\
        return gst_wrap_nil();\
}

#define SIMPLE_ACCUM_FUNCTION(name, op)\
MAKE_BINOP(name, op)\
int gst_stl_##name(Gst* vm) {\
    GstValue lhs, rhs;\
    uint32_t j, count;\
    count = gst_count_args(vm);\
    lhs = gst_arg(vm, 0);\
    for (j = 1; j < count; ++j) {\
        rhs = gst_arg(vm, j);\
        lhs = gst_stl_binop_##name(lhs, rhs);\
    }\
    if (lhs.type == GST_NIL)\
        gst_c_throwc(vm, "expected integer/real");\
    gst_c_return(vm, lhs);\
}

SIMPLE_ACCUM_FUNCTION(add, +)
SIMPLE_ACCUM_FUNCTION(mul, *)
SIMPLE_ACCUM_FUNCTION(sub, -)

/* Detect division by zero */
MAKE_BINOP(div, /)
int gst_stl_div(Gst *vm) {
    GstValue lhs, rhs;
    uint32_t j, count;
    count = gst_count_args(vm);
    lhs = gst_arg(vm, 0);
    for (j = 1; j < count; ++j) {
        rhs = gst_arg(vm, j);
        if (lhs.type == GST_INTEGER && rhs.type == GST_INTEGER && rhs.data.integer == 0)
            gst_c_throwc(vm, "cannot integer divide by 0");
        lhs = gst_stl_binop_div(lhs, rhs);
    }
    if (lhs.type == GST_NIL)
        gst_c_throwc(vm, "expected integer/real");
    gst_c_return(vm, lhs);
}

#undef SIMPLE_ACCUM_FUNCTION

#define BITWISE_FUNCTION(name, op) \
int gst_stl_##name(Gst *vm) {\
    GstValue ret;\
    uint32_t i, count;\
    count = gst_count_args(vm);\
    ret = gst_arg(vm, 0);\
    if (ret.type != GST_INTEGER) {\
        gst_c_throwc(vm, "expected integer");\
    }\
    if (count < 2) {\
        gst_c_return(vm, ret);\
    }\
    for (i = 1; i < count; ++i) {\
        GstValue next = gst_arg(vm, i);\
        if (next.type != GST_INTEGER) {\
            gst_c_throwc(vm, "expected integer");\
        }\
        ret.data.integer = ret.data.integer op next.data.integer;\
    }\
    gst_c_return(vm, ret);\
}

BITWISE_FUNCTION(band, &)
BITWISE_FUNCTION(bor, |)
BITWISE_FUNCTION(bxor, ^)
BITWISE_FUNCTION(blshift, <<)
BITWISE_FUNCTION(brshift, >>)

#undef BITWISE_FUNCTION

int gst_stl_bnot(Gst *vm) {
    GstValue in = gst_arg(vm, 0);
    uint32_t count = gst_count_args(vm);
    if (count != 1 || in.type != GST_INTEGER) {
        gst_c_throwc(vm, "expected 1 integer argument");
    }
    in.data.integer = ~in.data.integer;
    gst_c_return(vm, in);
}

#define COMPARE_FUNCTION(name, check)\
int gst_stl_##name(Gst *vm) {\
    GstValue ret;\
    uint32_t i, count;\
    count = gst_count_args(vm);\
    ret.data.boolean = 1;\
    ret.type = GST_BOOLEAN;\
    if (count < 2) {\
        gst_c_return(vm, ret);\
    }\
    for (i = 1; i < count; ++i) {\
        GstValue lhs = gst_arg(vm, i - 1);\
        GstValue rhs = gst_arg(vm, i);\
        if (!(check)) {\
            ret.data.boolean = 0;\
            break;\
        }\
    }\
    gst_c_return(vm, ret);\
}

COMPARE_FUNCTION(lessthan, gst_compare(lhs, rhs) < 0)
COMPARE_FUNCTION(greaterthan, gst_compare(lhs, rhs) > 0)
COMPARE_FUNCTION(equal, gst_equals(lhs, rhs))
COMPARE_FUNCTION(notequal, !gst_equals(lhs, rhs))
COMPARE_FUNCTION(lessthaneq, gst_compare(lhs, rhs) <= 0)
COMPARE_FUNCTION(greaterthaneq, gst_compare(lhs, rhs) >= 0)

#undef COMPARE_FUNCTION

/* Boolean not */
int gst_stl_not(Gst *vm) {
    gst_c_return(vm, gst_wrap_boolean(!gst_truthy(gst_arg(vm, 0))));
}

/****/
/* Core */
/****/

/* Empty a mutable datastructure */
int gst_stl_clear(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    switch (x.type) {
    default:
        gst_c_throwc(vm, "cannot get length");
    case GST_ARRAY:
        x.data.array->count = 0;
        break;
    case GST_BYTEBUFFER:
        x.data.buffer->count = 0;
        break;
    case GST_TABLE:
        gst_table_clear(x.data.table);
        break;
    }
    gst_c_return(vm, x);
}

/* Get length of object */
int gst_stl_length(Gst *vm) {
    uint32_t count = gst_count_args(vm);
    if (count == 0) {
        gst_c_return(vm, gst_wrap_nil());
    } else {
        GstValue ret;
        ret.type = GST_INTEGER;
        GstValue x = gst_arg(vm, 0);
        switch (x.type) {
        default:
            gst_c_throwc(vm, "cannot get length");
        case GST_STRING:
        case GST_SYMBOL:
            ret.data.integer = gst_string_length(x.data.string);
            break;
        case GST_ARRAY:
            ret.data.integer = x.data.array->count;
            break;
        case GST_BYTEBUFFER:
            ret.data.integer = x.data.buffer->count;
            break;
        case GST_TUPLE:
            ret.data.integer = gst_tuple_length(x.data.tuple);
            break;
        case GST_TABLE:
            ret.data.integer = x.data.table->count;
            break;
        case GST_STRUCT:
            ret.data.integer = gst_struct_length(x.data.st);
            break;
        case GST_FUNCDEF:
            ret.data.integer = x.data.def->byteCodeLen;
            break;
        }
        gst_c_return(vm, ret);
    }
}

/* Get hash of a value */
int gst_stl_hash(Gst *vm) {
    GstInteger h = gst_hash(gst_arg(vm, 0));
    gst_c_return(vm, gst_wrap_integer(h));
}

/* Convert to integer */
int gst_stl_to_int(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    if (x.type == GST_INTEGER) gst_c_return(vm, x);
    if (x.type == GST_REAL)
        gst_c_return(vm, gst_wrap_integer((GstInteger) x.data.real));
    else
       gst_c_throwc(vm, "expected number");
}

/* Convert to integer */
int gst_stl_to_real(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    if (x.type == GST_REAL) gst_c_return(vm, x);
    if (x.type == GST_INTEGER)
        gst_c_return(vm, gst_wrap_real((GstReal) x.data.integer));
    else
       gst_c_throwc(vm, "expected number");
}

/* Get a slice of a sequence */
int gst_stl_slice(Gst *vm) {
    uint32_t count = gst_count_args(vm);
    int32_t from, to;
    GstValue x;
    const GstValue *data;
    const uint8_t *cdata;
    uint32_t length;
    uint32_t newlength;
    GstInteger num;

    /* Get data */
    x = gst_arg(vm, 0);
    if (!gst_seq_view(x, &data, &length) &&
            !gst_chararray_view(x, &cdata, &length))  {
        gst_c_throwc(vm, "expected array or tuple");
    }

    /* Get from index */
    if (count < 2) {
        from = 0;
    } else {
        if (!gst_check_integer(vm, 1, &num))
            gst_c_throwc(vm, GST_EXPECTED_INTEGER);
        from = gst_startrange(num, length);
    }

    /* Get to index */
    if (count < 3) {
        to = length;
    } else {
        if (!gst_check_integer(vm, 2, &num))
            gst_c_throwc(vm, GST_EXPECTED_INTEGER);
        to = gst_endrange(num, length);
    }

    /* Check from bad bounds */
    if (from < 0 || to < 0 || to < from)
        gst_c_throwc(vm, "index out of bounds");

    /* Build slice */
    newlength = to - from;
    if (x.type == GST_TUPLE) {
        GstValue *tup = gst_tuple_begin(vm, newlength);
        gst_memcpy(tup, data + from, newlength * sizeof(GstValue));
        gst_c_return(vm, gst_wrap_tuple(gst_tuple_end(vm, tup)));
    } else if (x.type == GST_ARRAY) {
        GstArray *arr = gst_array(vm, newlength);
        arr->count = newlength;
        gst_memcpy(arr->data, data + from, newlength * sizeof(GstValue));
        gst_c_return(vm, gst_wrap_array(arr));
    } else if (x.type == GST_STRING) {
        gst_c_return(vm, gst_wrap_string(gst_string_b(vm, x.data.string + from, newlength)));
    } else if (x.type == GST_SYMBOL) {
        gst_c_return(vm, gst_wrap_symbol(gst_string_b(vm, x.data.string + from, newlength)));
    } else { /* buffer */
        GstBuffer *b = gst_buffer(vm, newlength);
        gst_memcpy(b->data, x.data.buffer->data, newlength);
        b->count = newlength;
        gst_c_return(vm, gst_wrap_buffer(b));
    }
}

/* Get type of object */
int gst_stl_type(Gst *vm) {
    GstValue x;
    const char *typestr = "nil";
    uint32_t count = gst_count_args(vm);
    if (count == 0)
        gst_c_throwc(vm, "expected at least 1 argument");
    x = gst_arg(vm, 0);
    switch (x.type) {
    default:
        break;
    case GST_REAL:
        typestr = "real";
        break;
    case GST_INTEGER:
        typestr = "integer";
        break;
    case GST_BOOLEAN:
        typestr = "boolean";
        break;
    case GST_STRING:
        typestr = "string";
        break;
    case GST_SYMBOL:
        typestr = "symbol";
        break;
    case GST_ARRAY:
        typestr = "array";
        break;
    case GST_TUPLE:
        typestr = "tuple";
        break;
    case GST_THREAD:
        typestr = "thread";
        break;
    case GST_BYTEBUFFER:
        typestr = "buffer";
        break;
    case GST_FUNCTION:
        typestr = "function";
        break;
    case GST_CFUNCTION:
        typestr = "cfunction";
        break;
    case GST_TABLE:
        typestr = "table";
        break;
    case GST_USERDATA:
        typestr = "userdata";
        break;
    case GST_FUNCENV:
        typestr = "funcenv";
        break;
    case GST_FUNCDEF:
        typestr = "funcdef";
        break;
    }
    gst_c_return(vm, gst_string_cv(vm, typestr));
}

/* Create array */
int gst_stl_array(Gst *vm) {
    uint32_t i;
    uint32_t count = gst_count_args(vm);
    GstArray *array = gst_array(vm, count);
    for (i = 0; i < count; ++i)
        array->data[i] = gst_arg(vm, i);
    gst_c_return(vm, gst_wrap_array(array));
}

/* Create tuple */
int gst_stl_tuple(Gst *vm) {
    uint32_t i;
    uint32_t count = gst_count_args(vm);
    GstValue *tuple= gst_tuple_begin(vm, count);
    for (i = 0; i < count; ++i)
        tuple[i] = gst_arg(vm, i);
    gst_c_return(vm, gst_wrap_tuple(gst_tuple_end(vm, tuple)));
}

/* Create object */
int gst_stl_table(Gst *vm) {
    uint32_t i;
    uint32_t count = gst_count_args(vm);
    GstTable *table;
    if (count % 2 != 0)
        gst_c_throwc(vm, "expected even number of arguments");
    table = gst_table(vm, 4 * count);
    for (i = 0; i < count; i += 2)
        gst_table_put(vm, table, gst_arg(vm, i), gst_arg(vm, i + 1));
    gst_c_return(vm, gst_wrap_table(table));
}

/* Create struct */
int gst_stl_struct(Gst *vm) {
    uint32_t i;
    uint32_t count = gst_count_args(vm);
    GstValue *st;
    if (count % 2 != 0)
        gst_c_throwc(vm, "expected even number of arguments");
    st = gst_struct_begin(vm, count / 2);
    for (i = 0; i < count; i += 2)
        gst_struct_put(st, gst_arg(vm, i), gst_arg(vm, i + 1));
    gst_c_return(vm, gst_wrap_struct(gst_struct_end(vm, st)));
}

/* Create a buffer */
int gst_stl_buffer(Gst *vm) {
    uint32_t i, count;
    const uint8_t *dat;
    uint32_t slen;
    GstBuffer *buf = gst_buffer(vm, 10);
    count = gst_count_args(vm);
    for (i = 0; i < count; ++i) {
        if (gst_chararray_view(gst_arg(vm, i), &dat, &slen))
            gst_buffer_append(vm, buf, dat, slen);
        else
            gst_c_throwc(vm, GST_EXPECTED_STRING);
    }
    gst_c_return(vm, gst_wrap_buffer(buf));
}

/* Create a string */
int gst_stl_string(Gst *vm) {
    uint32_t j;
    uint32_t count = gst_count_args(vm);
    uint32_t length = 0;
    uint32_t index = 0;
    uint8_t *str;
    const uint8_t *dat;
    uint32_t slen;
    /* Find length and assert string arguments */
    for (j = 0; j < count; ++j) {
        if (!gst_chararray_view(gst_arg(vm, j), &dat, &slen)) {
            GstValue newarg;
            dat = gst_to_string(vm, gst_arg(vm, j));
            slen = gst_string_length(dat);
            newarg.type = GST_STRING;
            newarg.data.string = dat;
            gst_set_arg(vm, j, newarg);
        }
        length += slen;
    }
    /* Make string */
    str = gst_string_begin(vm, length);
    for (j = 0; j < count; ++j) {
        gst_chararray_view(gst_arg(vm, j), &dat, &slen);
        gst_memcpy(str + index, dat, slen);
        index += slen;
    }
    gst_c_return(vm, gst_wrap_string(gst_string_end(vm, str)));
}

/* Create a symbol */
int gst_stl_symbol(Gst *vm) {
    int ret = gst_stl_string(vm);
    if (ret == GST_RETURN_OK) {
        vm->ret.type = GST_SYMBOL;
    }
    return ret;
}

/* Create a thread */
int gst_stl_thread(Gst *vm) {
    GstThread *t;
    GstValue callee = gst_arg(vm, 0);
    GstValue parent = gst_arg(vm, 1);
    GstValue errorParent = gst_arg(vm, 2);
    t = gst_thread(vm, callee, 10);
    if (callee.type != GST_FUNCTION && callee.type != GST_CFUNCTION)
        gst_c_throwc(vm, "expected function in thread constructor");
    if (parent.type == GST_THREAD) {
        t->parent = parent.data.thread;
    } else if (parent.type != GST_NIL) {
        gst_c_throwc(vm, "expected thread/nil as parent");
    } else {
        t->parent = vm->thread;
    }
    if (errorParent.type == GST_THREAD) {
        t->errorParent = errorParent.data.thread;
    } else if (errorParent.type != GST_NIL) {
        gst_c_throwc(vm, "expected thread/nil as error parent");
    } else {
        t->errorParent = vm->thread;
    }
    gst_c_return(vm, gst_wrap_thread(t));
}

/* Get current thread */
int gst_stl_current(Gst *vm) {
    gst_c_return(vm, gst_wrap_thread(vm->thread));
}

/* Get parent of a thread */
/* TODO - consider implications of this function
 * for sandboxing */
int gst_stl_parent(Gst *vm) {
    GstThread *t;
    if (!gst_check_thread(vm, 0, &t))
        gst_c_throwc(vm, "expected thread");
    if (t->parent == NULL)
        gst_c_return(vm, gst_wrap_nil());
    gst_c_return(vm, gst_wrap_thread(t->parent));
}

/* Get the status of a thread */
int gst_stl_status(Gst *vm) {
    GstThread *t;
    const char *cstr;
    if (!gst_check_thread(vm, 0, &t))
        gst_c_throwc(vm, "expected thread");
    switch (t->status) {
        case GST_THREAD_PENDING:
            cstr = "pending";
            break;
        case GST_THREAD_ALIVE:
            cstr = "alive";
            break;
        case GST_THREAD_DEAD:
            cstr = "dead";
            break;
        case GST_THREAD_ERROR:
            cstr = "error";
            break;
    }
    gst_c_return(vm, gst_string_cv(vm, cstr));
}

/* Associative get */
int gst_stl_get(Gst *vm) {
    GstValue ret;
    uint32_t count;
    const char *err;
    count = gst_count_args(vm);
    if (count != 2)
        gst_c_throwc(vm, "expects 2 arguments");
    err = gst_get(gst_arg(vm, 0), gst_arg(vm, 1), &ret);
    if (err != NULL)
        gst_c_throwc(vm, err);
    else
        gst_c_return(vm, ret);
}

/* Associative set */
int gst_stl_set(Gst *vm) {
    uint32_t count;
    const char *err;
    count = gst_count_args(vm);
    if (count != 3)
        gst_c_throwc(vm, "expects 3 arguments");
    err = gst_set(vm, gst_arg(vm, 0), gst_arg(vm, 1), gst_arg(vm, 2));
    if (err != NULL)
        gst_c_throwc(vm, err);
    else
        gst_c_return(vm, gst_arg(vm, 0));
}

/* Push to end of array */
int gst_stl_push(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    if (ds.type != GST_ARRAY)
        gst_c_throwc(vm, "expected array");
    gst_array_push(vm, ds.data.array, gst_arg(vm, 1));
    gst_c_return(vm, ds);
}

/* Pop from end of array */
int gst_stl_pop(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    if (ds.type != GST_ARRAY)
        gst_c_throwc(vm, "expected array");
    gst_c_return(vm, gst_array_pop(ds.data.array));
}

/* Peek at end of array */
int gst_stl_peek(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    if (ds.type != GST_ARRAY)
        gst_c_throwc(vm, "expected array");
    gst_c_return(vm, gst_array_peek(ds.data.array));
}

/* Ensure array capacity */
int gst_stl_ensure(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    GstValue cap = gst_arg(vm, 1);
    if (ds.type != GST_ARRAY)
        gst_c_throwc(vm, "expected array");
    if (cap.type != GST_INTEGER)
        gst_c_throwc(vm, GST_EXPECTED_INTEGER);
    gst_array_ensure(vm, ds.data.array, (uint32_t) cap.data.integer);
    gst_c_return(vm, ds);
}

/* Get next key in struct or table */
int gst_stl_next(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    GstValue key = gst_arg(vm, 1);
    if (ds.type == GST_TABLE) {
        gst_c_return(vm, gst_table_next(ds.data.table, key));
    } else if (ds.type == GST_STRUCT) {
        gst_c_return(vm, gst_struct_next(ds.data.st, key));
    } else {
        gst_c_throwc(vm, "expected table or struct");
    }
}

/* Print values for inspection */
int gst_stl_print(Gst *vm) {
    uint32_t j, count;
    count = gst_count_args(vm);
    for (j = 0; j < count; ++j) {
        uint32_t i;
        const uint8_t *string = gst_to_string(vm, gst_arg(vm, j));
        uint32_t len = gst_string_length(string);
        for (i = 0; i < len; ++i)
            fputc(string[i], stdout);
    }
    fputc('\n', stdout);
    return GST_RETURN_OK;
}

/* Long description */
int gst_stl_description(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    const uint8_t *buf = gst_description(vm, x);
    gst_c_return(vm, gst_wrap_string(buf));
}

/* Short description */
int gst_stl_short_description(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    const uint8_t *buf = gst_short_description(vm, x);
    gst_c_return(vm, gst_wrap_string(buf));
}

/* Exit */
int gst_stl_exit(Gst *vm) {
    int ret;
    GstValue x = gst_arg(vm, 0);
    ret = x.type == GST_INTEGER ? x.data.integer : (x.type == GST_REAL ? x.data.real : 0);
    exit(ret);
    return GST_RETURN_OK;
}

/* Throw error */
int gst_stl_error(Gst *vm) {
    gst_c_throw(vm, gst_arg(vm, 0));
}

/****/
/* Serialization */
/****/

/* Serialize data into buffer */
int gst_stl_serialize(Gst *vm) {
    const char *err;
    GstValue buffer = gst_arg(vm, 1);
    if (buffer.type != GST_BYTEBUFFER)
        buffer = gst_wrap_buffer(gst_buffer(vm, 10));
    err = gst_serialize(vm, buffer.data.buffer, gst_arg(vm, 0));
    if (err != NULL)
        gst_c_throwc(vm, err);
    gst_c_return(vm, buffer);
}

/* Deserialize data from a buffer */
int gst_stl_deserialize(Gst *vm) {
    GstValue ret;
    uint32_t len;
    const uint8_t *data;
    const char *err;
    if (!gst_chararray_view(gst_arg(vm, 0), &data, &len))
        gst_c_throwc(vm, "expected string/buffer/symbol");
    err = gst_deserialize(vm, data, len, &ret, &data);
    if (err != NULL)
        gst_c_throwc(vm, err);
    gst_c_return(vm, ret);
}

/***/
/* Function reflection */
/***/

int gst_stl_funcenv(Gst *vm) {
    GstFunction *fn;
    if (!gst_check_function(vm, 0, &fn))
        gst_c_throwc(vm, "expected function");
    if (fn->env)
        gst_c_return(vm, gst_wrap_funcenv(fn->env));
    else
        return GST_RETURN_OK;
}

int gst_stl_funcdef(Gst *vm) {
    GstFunction *fn;
    if (!gst_check_function(vm, 0, &fn))
        gst_c_throwc(vm, "expected function");
    gst_c_return(vm, gst_wrap_funcdef(fn->def));
}

int gst_stl_funcparent(Gst *vm) {
    GstFunction *fn;
    if (!gst_check_function(vm, 0, &fn))
        gst_c_throwc(vm, "expected function");
    if (fn->parent)
        gst_c_return(vm, gst_wrap_function(fn->parent));
    else
        return GST_RETURN_OK;
}

int gst_stl_def(Gst *vm) {
    GstValue key = gst_arg(vm, 0);
    if (gst_count_args(vm) != 2) {
        gst_c_throwc(vm, "expected 2 arguments to global-def");
    }
    if (key.type != GST_STRING && key.type != GST_SYMBOL) {
        gst_c_throwc(vm, "expected string/symbol as first argument");
    }
    key.type = GST_SYMBOL;
    gst_env_put(vm, vm->env, key, gst_arg(vm, 1));
    gst_c_return(vm, gst_arg(vm, 1));
}

int gst_stl_var(Gst *vm) {
    GstValue key = gst_arg(vm, 0);
    if (gst_count_args(vm) != 2) {
        gst_c_throwc(vm, "expected 2 arguments to global-var");
    }
    if (key.type != GST_STRING && key.type != GST_SYMBOL) {
        gst_c_throwc(vm, "expected string as first argument");
    }
    key.type = GST_SYMBOL;
    gst_env_putvar(vm, vm->env, key, gst_arg(vm, 1));
    gst_c_return(vm, gst_arg(vm, 1));
}

/****/
/* IO */
/****/

/* File type definition */
static GstUserType gst_stl_filetype = {
    "std.file",
    NULL,
    NULL,
    NULL,
    NULL
};

/* Open a a file and return a userdata wrapper arounf the C file API. */
int gst_stl_open(Gst *vm) {
    const uint8_t *fname = gst_to_string(vm, gst_arg(vm, 0));
    const uint8_t *fmode = gst_to_string(vm, gst_arg(vm, 1));
    FILE *f;
    FILE **fp;
    if (gst_count_args(vm) < 2 || gst_arg(vm, 0).type != GST_STRING
            || gst_arg(vm, 1).type != GST_STRING)
        gst_c_throwc(vm, "expected filename and filemode");
    f = fopen((const char *)fname, (const char *)fmode);
    if (!f)
        gst_c_throwc(vm, "could not open file");
    fp = gst_userdata(vm, sizeof(FILE *), &gst_stl_filetype);
    *fp = f;
    gst_c_return(vm, gst_wrap_userdata(fp));
}

/* Read an entire file into memory */
int gst_stl_slurp(Gst *vm) {
    GstBuffer *b;
    long fsize;
    FILE *f;
    FILE **fp = gst_check_userdata(vm, 0, &gst_stl_filetype);
    if (fp == NULL) gst_c_throwc(vm, "expected file");
    if (!gst_check_buffer(vm, 1, &b)) b = gst_buffer(vm, 10);
    f = *fp;
    /* Read whole file */
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Ensure buffer size */
    gst_buffer_ensure(vm, b, b->count + fsize);
    fread((char *)(b->data + b->count), fsize, 1, f);
    b->count += fsize;
    gst_c_return(vm, gst_wrap_buffer(b));
}

/* Read a certain number of bytes into memory */
int gst_stl_read(Gst *vm) {
    GstBuffer *b;
    FILE *f;
    int64_t len;
    FILE **fp = gst_check_userdata(vm, 0, &gst_stl_filetype);
    if (fp == NULL) gst_c_throwc(vm, "expected file");
    if (!(gst_check_integer(vm, 1, &len))) gst_c_throwc(vm, "expected integer");
    if (!gst_check_buffer(vm, 2, &b)) b = gst_buffer(vm, 10);
    f = *fp;
    /* Ensure buffer size */
    gst_buffer_ensure(vm, b, b->count + len);
    b->count += fread((char *)(b->data + b->count), len, 1, f) * len;
    gst_c_return(vm, gst_wrap_buffer(b));
}

/* Write bytes to a file */
int gst_stl_write(Gst *vm) {
    FILE *f;
    const uint8_t *data;
    uint32_t len;
    FILE **fp = gst_check_userdata(vm, 0, &gst_stl_filetype);
    if (fp == NULL) gst_c_throwc(vm, "expected file");
    if (!gst_chararray_view(gst_arg(vm, 1), &data, &len)) gst_c_throwc(vm, "expected string|buffer");
    f = *fp;
    fwrite(data, len, 1, f);
    return GST_RETURN_OK;
}

/* Close a file */
int gst_stl_close(Gst *vm) {
    FILE **fp = gst_check_userdata(vm, 0, &gst_stl_filetype);
    if (fp == NULL) gst_c_throwc(vm, "expected file");
    fclose(*fp);
    gst_c_return(vm, gst_wrap_nil());
}

/****/
/* Temporary */
/****/

/* Force garbage collection */
int gst_stl_gcollect(Gst *vm) {
	gst_collect(vm);
	return GST_RETURN_OK;
}

/***/
/* Parsing */
/***/

/* GC mark a parser */
static void gst_stl_parser_mark(Gst *vm, void *data, uint32_t len) {
    uint32_t i;
    GstParser *p = (GstParser *) data;
    if (len != sizeof(GstParser))
        return;
    gst_mark_mem(vm, p->data);
    gst_mark_value(vm, p->value);
    for (i = 0; i < p->count; ++i) {
        GstParseState *ps = p->data + i;
        switch (ps->type) {
            case PTYPE_FORM:
                gst_mark_value(vm, gst_wrap_array(ps->buf.form.array));
                break;
            case PTYPE_STRING:
            case PTYPE_TOKEN:
                gst_mark_value(vm, gst_wrap_buffer(ps->buf.string.buffer));
                break;
        }
    }
}

/* Parse filetype */
static const GstUserType gst_stl_parsetype = {
    "std.parser",
    NULL,
    NULL,
    NULL,
    &gst_stl_parser_mark
};

/* Create a parser */
static int gst_stl_parser(Gst *vm) {
    GstParser *p = gst_userdata(vm, sizeof(GstParser), &gst_stl_parsetype);
    gst_parser(p, vm);
    gst_c_return(vm, gst_wrap_userdata(p));
}

/* Consume a value from the parser */
static int gst_stl_parser_consume(Gst *vm) {
    GstParser *p = gst_check_userdata(vm, 0, &gst_stl_parsetype);
    if (p == NULL)
        gst_c_throwc(vm, "expected parser");
    if (p->status == GST_PARSER_ERROR)
        gst_c_return(vm, gst_string_cv(vm, p->error));
    if (!gst_parse_hasvalue(p))
        gst_c_throwc(vm, "parser has no pending value");
    gst_c_return(vm, gst_parse_consume(p));
}

/* Check if the parser has a value to consume */
static int gst_stl_parser_hasvalue(Gst *vm) {
    GstParser *p = gst_check_userdata(vm, 0, &gst_stl_parsetype);
    if (p == NULL)
        gst_c_throwc(vm, "expected parser");
    gst_c_return(vm, gst_wrap_boolean(gst_parse_hasvalue(p)));
}

/* Parse a single byte. Returns if the byte was successfully parsed. */
static int gst_stl_parser_byte(Gst *vm) {
    GstInteger b;
    GstParser *p = gst_check_userdata(vm, 0, &gst_stl_parsetype);
    if (p == NULL)
        gst_c_throwc(vm, "expected parser");
    if (!gst_check_integer(vm, 1, &b))
        gst_c_throwc(vm, "expected integer");
    if (p->status == GST_PARSER_PENDING || p->status == GST_PARSER_ROOT) {
        gst_parse_byte(p, b);
        gst_c_return(vm, gst_wrap_boolean(1));
    } else {
        gst_c_return(vm, gst_wrap_boolean(0));
    }
}

/* Parse a string or buffer. Returns nil if the entire char array is parsed,
* otherwise returns the remainder of what could not be parsed. */
static int gst_stl_parser_charseq(Gst *vm) {
    uint32_t i;
    uint32_t len;
    const uint8_t *data;
    GstParser *p = gst_check_userdata(vm, 0, &gst_stl_parsetype);
    if (p == NULL)
        gst_c_throwc(vm, "expected parser");
    if (!gst_chararray_view(gst_arg(vm, 1), &data, &len))
        gst_c_throwc(vm, "expected string/buffer/symbol");
    for (i = 0; i < len; ++i) {
        if (p->status != GST_PARSER_PENDING && p->status != GST_PARSER_ROOT) break;
        gst_parse_byte(p, data[i]);
    }
    if (i == len) {
        /* No remainder */
        gst_c_return(vm, gst_wrap_nil());
    } else {
        /* We have remaining characters */
        gst_c_return(vm, gst_wrap_string(gst_string_b(vm, data + i, len - i)));
    }
}

/* Get status of parser */
static int gst_stl_parser_status(Gst *vm) {
    GstParser *p = gst_check_userdata(vm, 0, &gst_stl_parsetype);
    const char *cstr;
    if (p == NULL)
        gst_c_throwc(vm, "expected parser");
    switch (p->status) {
        case GST_PARSER_ERROR:
            cstr = "error";
            break;
        case GST_PARSER_FULL:
            cstr = "full";
            break;
        case GST_PARSER_PENDING:
            cstr = "pending";
            break;
        case GST_PARSER_ROOT:
            cstr = "root";
            break;
        default:
            cstr = "unknown";
            break;
    }
    gst_c_return(vm, gst_string_cv(vm, cstr));
}

/* Parse a string */
static int gst_stl_parse(Gst *vm) {
    uint32_t len, i;
    GstParser p;
    const uint8_t *data;
    if (!gst_chararray_view(gst_arg(vm, 0), &data, &len))
        gst_c_throwc(vm, "expected string/buffer/symbol to parse");
    gst_parser(&p, vm);
    for (i = 0; i < len; ++i) {
        if (p.status != GST_PARSER_PENDING && p.status != GST_PARSER_ROOT) break;
        gst_parse_byte(&p, data[i]);
    }
    switch (p.status) {
        case GST_PARSER_ERROR:
            gst_c_throwc(vm, p.error);
            break;
        case GST_PARSER_FULL:
            gst_c_return(vm, p.value);
            break;
        case GST_PARSER_PENDING:
        case GST_PARSER_ROOT:
            gst_c_throwc(vm, "unexpected end of source");
            break;
        default:
            gst_c_throwc(vm, "unknown error parsing");
            break;
    }
    return 0;
}

/***/
/* Compilation */
/***/

/* Compile a value */
static int gst_stl_compile(Gst *vm) {
    GstTable *env = vm->env;
    if (gst_arg(vm, 1).type == GST_TABLE) {
        env = gst_arg(vm, 1).data.table;
    }
    gst_c_return(vm, gst_compile(vm, env, gst_arg(vm, 0)));
}

/* Get vm->env */
static int gst_stl_getenv(Gst *vm) {
    gst_c_return(vm, gst_wrap_table(vm->env));
}

/* Set vm->env */
static int gst_stl_setenv(Gst *vm) {
    GstValue newEnv = gst_arg(vm, 0);
    if (newEnv.type != GST_TABLE) {
        gst_c_throwc(vm, "expected table");
    }
    vm->env = newEnv.data.table;
    return GST_RETURN_OK;
}

/****/
/* Bootstraping */
/****/

static const GstModuleItem std_module[] = {
    /* Arithmetic */
    {"+", gst_stl_add},
    {"*", gst_stl_mul},
    {"-", gst_stl_sub},
    {"/", gst_stl_div},
    /* Comparisons */
    {"<", gst_stl_lessthan},
    {">", gst_stl_greaterthan},
    {"=", gst_stl_equal},
    {"not=", gst_stl_notequal},
    {"<=", gst_stl_lessthaneq},
    {">=", gst_stl_greaterthaneq},
    /* Bitwise arithmetic */
    {"band", gst_stl_band},
    {"bor", gst_stl_bor},
    {"bxor", gst_stl_bxor},
    {"blshift", gst_stl_blshift},
    {"brshift", gst_stl_brshift},
    {"bnot", gst_stl_bnot},
    /* IO */
    {"open", gst_stl_open},
    {"slurp", gst_stl_slurp},
    {"read", gst_stl_read},
    {"write", gst_stl_write},
    /* Parsing */
    {"parser", gst_stl_parser},
    {"parse-byte", gst_stl_parser_byte},
    {"parse-consume", gst_stl_parser_consume},
    {"parse-hasvalue", gst_stl_parser_hasvalue},
    {"parse-charseq", gst_stl_parser_charseq},
    {"parse-status", gst_stl_parser_status},
    {"parse", gst_stl_parse},
    /* Compile */
    {"getenv", gst_stl_getenv},
    {"setenv", gst_stl_setenv},
    {"compile", gst_stl_compile},
    /* Other */
    {"not", gst_stl_not},
    {"clear", gst_stl_clear},
    {"length", gst_stl_length},
    {"hash", gst_stl_hash},
    {"integer", gst_stl_to_int},
    {"real", gst_stl_to_real},
    {"type", gst_stl_type},
    {"slice", gst_stl_slice},
    {"array", gst_stl_array},
    {"tuple", gst_stl_tuple},
    {"table", gst_stl_table},
    {"struct", gst_stl_struct},
    {"buffer", gst_stl_buffer},
    {"string", gst_stl_string},
    {"symbol", gst_stl_symbol},
    {"thread", gst_stl_thread},
    {"status", gst_stl_status},
    {"current", gst_stl_current},
    {"parent", gst_stl_parent},
    {"print", gst_stl_print},
    {"description", gst_stl_description},
    {"short-description", gst_stl_short_description},
    {"exit!", gst_stl_exit},
    {"get", gst_stl_get},
    {"set!", gst_stl_set},
    {"next", gst_stl_next},
    {"error", gst_stl_error},
    {"serialize", gst_stl_serialize},
    {"deserialize", gst_stl_deserialize},
    {"push!", gst_stl_push},
    {"pop!", gst_stl_pop},
    {"peek", gst_stl_peek},
    {"ensure!", gst_stl_ensure},
    {"open", gst_stl_open},
    {"slurp", gst_stl_slurp},
    {"read", gst_stl_read},
    {"write", gst_stl_write},
    {"close", gst_stl_close},
    {"funcenv", gst_stl_funcenv},
    {"funcdef", gst_stl_funcdef},
    {"funcparent", gst_stl_funcparent},
    {"gcollect", gst_stl_gcollect},
    {"global-def", gst_stl_def},
    {"global-var", gst_stl_var},
    {NULL, NULL}
};

/* Load stl library into the current environment. Create stl module object
 * only if it is not yet created. */
void gst_stl_load(Gst *vm) {
    GstValue maybeEnv = gst_table_get(vm->modules, gst_string_cvs(vm, "std"));
    if (maybeEnv.type == GST_TABLE) {
        /* Module already created, so merge into main vm. */
        gst_env_merge(vm, vm->env, maybeEnv.data.table);
    } else {
        /* Module not yet created */
        /* Load the normal c functions */
        gst_module_mutable(vm, "std", std_module);
        /* Wrap stdin and stdout */
        FILE **inp = gst_userdata(vm, sizeof(FILE *), &gst_stl_filetype);
        FILE **outp = gst_userdata(vm, sizeof(FILE *), &gst_stl_filetype);
        FILE **errp = gst_userdata(vm, sizeof(FILE *), &gst_stl_filetype);
        *inp = stdin;
        *outp = stdout;
        *errp = stderr;
        gst_module_put(vm, "std", "stdin", gst_wrap_userdata(inp));
        gst_module_put(vm, "std", "stdout", gst_wrap_userdata(outp));
        gst_module_put(vm, "std", "stderr", gst_wrap_userdata(outp));
        // Now merge
        maybeEnv = gst_table_get(vm->modules, gst_string_cvs(vm, "std"));
        gst_env_merge(vm, vm->env, maybeEnv.data.table);
    }
}

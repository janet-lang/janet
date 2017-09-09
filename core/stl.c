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
#include "internal.h"

static const char DST_EXPECTED_INTEGER[] = "expected integer";
static const char DST_EXPECTED_STRING[] = "expected string";

static const char *types[] = {
    "nil",
    "real",
    "integer",
    "boolean",
    "string",
    "symbol",
    "array",
    "tuple",
    "table",
    "struct",
    "thread",
    "buffer",
    "function",
    "cfunction",
    "userdata",
    "funcenv",
    "funcdef"
};

static DstValue nil() {
    DstValue n;
    n.type = DST_NIL;
    n.data.integer = 0;
    return n;
}

static DstValue integer(DstInteger i) {
    DstValue n;
    n.type = DST_INTEGER;
    n.data.integer = i;
    return n;
}

static DstValue real(DstReal r) {
    DstValue n;
    n.type = DST_REAL;
    n.data.real = r;
    return n;
}

static DstValue boolean(int b) {
    DstValue n;
    n.type = DST_BOOLEAN;
    n.data.boolean = b;
    return n;
}

/***/
/* Arithmetic */
/***/

#define MAKE_BINOP(name, op)\
static DstValue dst_stl_binop_##name(DstValue lhs, DstValue rhs) {\
    if (lhs.type == DST_INTEGER)\
        if (rhs.type == DST_INTEGER)\
            return integer(lhs.data.integer op rhs.data.integer);\
        else if (rhs.type == DST_REAL)\
            return real(lhs.data.integer op rhs.data.real);\
        else\
            return nil();\
    else if (lhs.type == DST_REAL)\
        if (rhs.type == DST_INTEGER)\
            return real(lhs.data.real op rhs.data.integer);\
        else if (rhs.type == DST_REAL)\
            return real(lhs.data.real op rhs.data.real);\
        else\
            return nil();\
    else\
        return nil();\
}

#define SIMPLE_ACCUM_FUNCTION(name, op)\
MAKE_BINOP(name, op)\
int dst_stl_##name(Dst* vm) {\
    DstValue lhs, rhs;\
    uint32_t j, count;\
    count = dst_args(vm);\
    lhs = dst_arg(vm, 0);\
    for (j = 1; j < count; ++j) {\
        rhs = dst_arg(vm, j);\
        lhs = dst_stl_binop_##name(lhs, rhs);\
    }\
    if (lhs.type == DST_NIL)\
        dst_c_throwc(vm, "expected integer/real");\
    dst_c_return(vm, lhs);\
}

SIMPLE_ACCUM_FUNCTION(add, +)
SIMPLE_ACCUM_FUNCTION(mul, *)
SIMPLE_ACCUM_FUNCTION(sub, -)

/* Detect division by zero */
MAKE_BINOP(div, /)
int dst_stl_div(Dst *vm) {
    DstValue lhs, rhs;
    uint32_t j, count;
    count = dst_args(vm);
    lhs = dst_arg(vm, 0);
    for (j = 1; j < count; ++j) {
        rhs = dst_arg(vm, j);
        if (lhs.type == DST_INTEGER && rhs.type == DST_INTEGER && rhs.data.integer == 0)
            dst_c_throwc(vm, "cannot integer divide by 0");
        lhs = dst_stl_binop_div(lhs, rhs);
    }
    if (lhs.type == DST_NIL)
        dst_c_throwc(vm, "expected integer/real");
    dst_c_return(vm, lhs);
}

#undef SIMPLE_ACCUM_FUNCTION

#define BITWISE_FUNCTION(name, op) \
int dst_stl_##name(Dst *vm) {\
    DstValue ret;\
    uint32_t i, count;\
    count = dst_args(vm);\
    ret = dst_arg(vm, 0);\
    if (ret.type != DST_INTEGER) {\
        dst_c_throwc(vm, "expected integer");\
    }\
    if (count < 2) {\
        dst_c_return(vm, ret);\
    }\
    for (i = 1; i < count; ++i) {\
        DstValue next = dst_arg(vm, i);\
        if (next.type != DST_INTEGER) {\
            dst_c_throwc(vm, "expected integer");\
        }\
        ret.data.integer = ret.data.integer op next.data.integer;\
    }\
    dst_c_return(vm, ret);\
}

BITWISE_FUNCTION(band, &)
BITWISE_FUNCTION(bor, |)
BITWISE_FUNCTION(bxor, ^)
BITWISE_FUNCTION(blshift, <<)
BITWISE_FUNCTION(brshift, >>)

#undef BITWISE_FUNCTION

int dst_stl_bnot(Dst *vm) {
    DstValue in = dst_arg(vm, 0);
    uint32_t count = dst_args(vm);
    if (count != 1 || in.type != DST_INTEGER) {
        dst_c_throwc(vm, "expected 1 integer argument");
    }
    in.data.integer = ~in.data.integer;
    dst_c_return(vm, in);
}

#define COMPARE_FUNCTION(name, check)\
int dst_stl_##name(Dst *vm) {\
    DstValue ret;\
    uint32_t i, count;\
    count = dst_args(vm);\
    ret.data.boolean = 1;\
    ret.type = DST_BOOLEAN;\
    if (count < 2) {\
        dst_c_return(vm, ret);\
    }\
    for (i = 1; i < count; ++i) {\
        DstValue lhs = dst_arg(vm, i - 1);\
        DstValue rhs = dst_arg(vm, i);\
        if (!(check)) {\
            ret.data.boolean = 0;\
            break;\
        }\
    }\
    dst_c_return(vm, ret);\
}

COMPARE_FUNCTION(lessthan, dst_compare(lhs, rhs) < 0)
COMPARE_FUNCTION(greaterthan, dst_compare(lhs, rhs) > 0)
COMPARE_FUNCTION(equal, dst_equals(lhs, rhs))
COMPARE_FUNCTION(notequal, !dst_equals(lhs, rhs))
COMPARE_FUNCTION(lessthaneq, dst_compare(lhs, rhs) <= 0)
COMPARE_FUNCTION(greaterthaneq, dst_compare(lhs, rhs) >= 0)

#undef COMPARE_FUNCTION

/* Boolean not */
int dst_stl_not(Dst *vm) {
    dst_c_return(vm, boolean(!dst_truthy(dst_arg(vm, 0))));
}

/****/
/* Core */
/****/

/* Empty a mutable datastructure */
int dst_stl_clear(Dst *vm) {
    DstValue x = dst_arg(vm, 0);
    switch (x.type) {
    default:
        dst_c_throwc(vm, "cannot clear");
    case DST_ARRAY:
        x.data.array->count = 0;
        break;
    case DST_BYTEBUFFER:
        x.data.buffer->count = 0;
        break;
    case DST_TABLE:
        dst_table_clear(x.data.table);
        break;
    }
    dst_c_return(vm, x);
}

/* Get length of object */
int dst_stl_length(Dst *vm) {
    dst_set_integer(vm, 0, dst_length(vm, 0));
    dst_return(vm, 0);
    return 0;
}

/* Get hash of a value */
int dst_stl_hash(Dst *vm) {
    dst_set_integer(vm, 0, dst_hash(vm, 0););
    dst_return(vm, 0);
    return 0;
}

/* Convert to integer */
int dst_stl_to_int(Dst *vm) {
    DstValue x = dst_arg(vm, 0);
    if (x.type == DST_INTEGER) dst_c_return(vm, x);
    if (x.type == DST_REAL)
        dst_c_return(vm, integer((DstInteger) x.data.real));
    else
       dst_c_throwc(vm, "expected number");
}

/* Convert to integer */
int dst_stl_to_real(Dst *vm) {
    DstValue x = dst_arg(vm, 0);
    if (x.type == DST_REAL) dst_c_return(vm, x);
    if (x.type == DST_INTEGER)
        dst_c_return(vm, dst_wrap_real((DstReal) x.data.integer));
    else
       dst_c_throwc(vm, "expected number");
}

/* Get a slice of a sequence */
int dst_stl_slice(Dst *vm) {
    uint32_t count = dst_args(vm);
    int32_t from, to;
    DstValue x;
    const DstValue *data;
    const uint8_t *cdata;
    uint32_t length;
    uint32_t newlength;
    DstInteger num;

    /* Get data */
    x = dst_arg(vm, 0);
    if (!dst_seq_view(x, &data, &length) &&
            !dst_chararray_view(x, &cdata, &length))  {
        dst_c_throwc(vm, "expected array/tuple/buffer/symbol/string");
    }

    /* Get from index */
    if (count < 2) {
        from = 0;
    } else {
        if (!dst_check_integer(vm, 1, &num))
            dst_c_throwc(vm, DST_EXPECTED_INTEGER);
        from = dst_startrange(num, length);
    }

    /* Get to index */
    if (count < 3) {
        to = length;
    } else {
        if (!dst_check_integer(vm, 2, &num))
            dst_c_throwc(vm, DST_EXPECTED_INTEGER);
        to = dst_endrange(num, length);
    }

    /* Check from bad bounds */
    if (from < 0 || to < 0 || to < from)
        dst_c_throwc(vm, "index out of bounds");

    /* Build slice */
    newlength = to - from;
    if (x.type == DST_TUPLE) {
        DstValue *tup = dst_tuple_begin(vm, newlength);
        dst_memcpy(tup, data + from, newlength * sizeof(DstValue));
        dst_c_return(vm, dst_wrap_tuple(dst_tuple_end(vm, tup)));
    } else if (x.type == DST_ARRAY) {
        DstArray *arr = dst_array(vm, newlength);
        arr->count = newlength;
        dst_memcpy(arr->data, data + from, newlength * sizeof(DstValue));
        dst_c_return(vm, dst_wrap_array(arr));
    } else if (x.type == DST_STRING) {
        dst_c_return(vm, dst_wrap_string(dst_string_b(vm, x.data.string + from, newlength)));
    } else if (x.type == DST_SYMBOL) {
        dst_c_return(vm, dst_wrap_symbol(dst_string_b(vm, x.data.string + from, newlength)));
    } else { /* buffer */
        DstBuffer *b = dst_buffer(vm, newlength);
        dst_memcpy(b->data, x.data.buffer->data, newlength);
        b->count = newlength;
        dst_c_return(vm, dst_wrap_buffer(b));
    }
}

/* Get type of object */
int dst_stl_type(Dst *vm) {
    DstValue x;
    const char *typestr = "nil";
    uint32_t count = dst_args(vm);
    if (count == 0)
        dst_c_throwc(vm, "expected at least 1 argument");
    x = dst_arg(vm, 0);
    switch (x.type) {
    default:
        break;
    case DST_REAL:
        typestr = "real";
        break;
    case DST_INTEGER:
        typestr = "integer";
        break;
    case DST_BOOLEAN:
        typestr = "boolean";
        break;
    case DST_STRING:
        typestr = "string";
        break;
    case DST_SYMBOL:
        typestr = "symbol";
        break;
    case DST_ARRAY:
        typestr = "array";
        break;
    case DST_TUPLE:
        typestr = "tuple";
        break;
    case DST_THREAD:
        typestr = "thread";
        break;
    case DST_BYTEBUFFER:
        typestr = "buffer";
        break;
    case DST_FUNCTION:
        typestr = "function";
        break;
    case DST_CFUNCTION:
        typestr = "cfunction";
        break;
    case DST_TABLE:
        typestr = "table";
        break;
    case DST_USERDATA:
        typestr = "userdata";
        break;
    case DST_FUNCENV:
        typestr = "funcenv";
        break;
    case DST_FUNCDEF:
        typestr = "funcdef";
        break;
    }
    dst_c_return(vm, dst_string_cv(vm, typestr));
}

/* Create array */
int dst_stl_array(Dst *vm) {
    uint32_t i;
    uint32_t count = dst_args(vm);
    DstArray *array = dst_array(vm, count);
    for (i = 0; i < count; ++i)
        array->data[i] = dst_arg(vm, i);
    dst_c_return(vm, dst_wrap_array(array));
}

/* Create tuple */
int dst_stl_tuple(Dst *vm) {
    uint32_t i;
    uint32_t count = dst_args(vm);
    DstValue *tuple= dst_tuple_begin(vm, count);
    for (i = 0; i < count; ++i)
        tuple[i] = dst_arg(vm, i);
    dst_c_return(vm, dst_wrap_tuple(dst_tuple_end(vm, tuple)));
}

/* Create object */
int dst_stl_table(Dst *vm) {
    uint32_t i;
    uint32_t count = dst_args(vm);
    DstTable *table;
    if (count % 2 != 0)
        dst_c_throwc(vm, "expected even number of arguments");
    table = dst_table(vm, 4 * count);
    for (i = 0; i < count; i += 2)
        dst_table_put(vm, table, dst_arg(vm, i), dst_arg(vm, i + 1));
    dst_c_return(vm, dst_wrap_table(table));
}

/* Create struct */
int dst_stl_struct(Dst *vm) {
    uint32_t i;
    uint32_t count = dst_args(vm);
    DstValue *st;
    if (count % 2 != 0)
        dst_c_throwc(vm, "expected even number of arguments");
    st = dst_struct_begin(vm, count / 2);
    for (i = 0; i < count; i += 2)
        dst_struct_put(st, dst_arg(vm, i), dst_arg(vm, i + 1));
    dst_c_return(vm, dst_wrap_struct(dst_struct_end(vm, st)));
}

/* Create a buffer */
int dst_stl_buffer(Dst *vm) {
    uint32_t i, count;
    const uint8_t *dat;
    uint32_t slen;
    DstBuffer *buf = dst_buffer(vm, 10);
    count = dst_args(vm);
    for (i = 0; i < count; ++i) {
        if (dst_chararray_view(dst_arg(vm, i), &dat, &slen))
            dst_buffer_append(vm, buf, dat, slen);
        else
            dst_c_throwc(vm, DST_EXPECTED_STRING);
    }
    dst_c_return(vm, dst_wrap_buffer(buf));
}

/* Create a string */
int dst_stl_string(Dst *vm) {
    uint32_t j;
    uint32_t count = dst_args(vm);
    uint32_t length = 0;
    uint32_t index = 0;
    uint8_t *str;
    const uint8_t *dat;
    uint32_t slen;
    /* Find length and assert string arguments */
    for (j = 0; j < count; ++j) {
        if (!dst_chararray_view(dst_arg(vm, j), &dat, &slen)) {
            DstValue newarg;
            dat = dst_to_string(vm, dst_arg(vm, j));
            slen = dst_string_length(dat);
            newarg.type = DST_STRING;
            newarg.data.string = dat;
            dst_set_arg(vm, j, newarg);
        }
        length += slen;
    }
    /* Make string */
    str = dst_string_begin(vm, length);
    for (j = 0; j < count; ++j) {
        dst_chararray_view(dst_arg(vm, j), &dat, &slen);
        dst_memcpy(str + index, dat, slen);
        index += slen;
    }
    dst_c_return(vm, dst_wrap_string(dst_string_end(vm, str)));
}

/* Create a symbol */
int dst_stl_symbol(Dst *vm) {
    int ret = dst_stl_string(vm);
    if (ret == DST_RETURN_OK) {
        vm->ret.type = DST_SYMBOL;
    }
    return ret;
}

/* Create a thread */
int dst_stl_thread(Dst *vm) {
    DstThread *t;
    DstValue callee = dst_arg(vm, 0);
    DstValue parent = dst_arg(vm, 1);
    DstValue errorParent = dst_arg(vm, 2);
    t = dst_thread(vm, callee, 10);
    if (callee.type != DST_FUNCTION && callee.type != DST_CFUNCTION)
        dst_c_throwc(vm, "expected function in thread constructor");
    if (parent.type == DST_THREAD) {
        t->parent = parent.data.thread;
    } else if (parent.type != DST_NIL) {
        dst_c_throwc(vm, "expected thread/nil as parent");
    } else {
        t->parent = vm->thread;
    }
    dst_c_return(vm, dst_wrap_thread(t));
}

/* Get current thread */
int dst_stl_current(Dst *vm) {
    dst_c_return(vm, dst_wrap_thread(vm->thread));
}

/* Get parent of a thread */
/* TODO - consider implications of this function
 * for sandboxing */
int dst_stl_parent(Dst *vm) {
    DstThread *t;
    if (!dst_check_thread(vm, 0, &t))
        dst_c_throwc(vm, "expected thread");
    if (t->parent == NULL)
        dst_c_return(vm, dst_wrap_nil());
    dst_c_return(vm, dst_wrap_thread(t->parent));
}

/* Get the status of a thread */
int dst_stl_status(Dst *vm) {
    DstThread *t;
    const char *cstr;
    if (!dst_check_thread(vm, 0, &t))
        dst_c_throwc(vm, "expected thread");
    switch (t->status) {
        case DST_THREAD_PENDING:
            cstr = "pending";
            break;
        case DST_THREAD_ALIVE:
            cstr = "alive";
            break;
        case DST_THREAD_DEAD:
            cstr = "dead";
            break;
        case DST_THREAD_ERROR:
            cstr = "error";
            break;
    }
    dst_c_return(vm, dst_string_cv(vm, cstr));
}

/* Associative get */
int dst_stl_get(Dst *vm) {
    DstValue ret;
    uint32_t count;
    const char *err;
    count = dst_args(vm);
    if (count != 2)
        dst_c_throwc(vm, "expects 2 arguments");
    err = dst_get(dst_arg(vm, 0), dst_arg(vm, 1), &ret);
    if (err != NULL)
        dst_c_throwc(vm, err);
    else
        dst_c_return(vm, ret);
}

/* Associative set */
int dst_stl_set(Dst *vm) {
    uint32_t count;
    const char *err;
    count = dst_args(vm);
    if (count != 3)
        dst_c_throwc(vm, "expects 3 arguments");
    err = dst_set(vm, dst_arg(vm, 0), dst_arg(vm, 1), dst_arg(vm, 2));
    if (err != NULL)
        dst_c_throwc(vm, err);
    else
        dst_c_return(vm, dst_arg(vm, 0));
}

/* Push to end of array */
int dst_stl_push(Dst *vm) {
    DstValue ds = dst_arg(vm, 0);
    if (ds.type != DST_ARRAY)
        dst_c_throwc(vm, "expected array");
    dst_array_push(vm, ds.data.array, dst_arg(vm, 1));
    dst_c_return(vm, ds);
}

/* Pop from end of array */
int dst_stl_pop(Dst *vm) {
    DstValue ds = dst_arg(vm, 0);
    if (ds.type != DST_ARRAY)
        dst_c_throwc(vm, "expected array");
    dst_c_return(vm, dst_array_pop(ds.data.array));
}

/* Peek at end of array */
int dst_stl_peek(Dst *vm) {
    DstValue ds = dst_arg(vm, 0);
    if (ds.type != DST_ARRAY)
        dst_c_throwc(vm, "expected array");
    dst_c_return(vm, dst_array_peek(ds.data.array));
}

/* Ensure array capacity */
int dst_stl_ensure(Dst *vm) {
    DstValue ds = dst_arg(vm, 0);
    DstValue cap = dst_arg(vm, 1);
    if (ds.type != DST_ARRAY)
        dst_c_throwc(vm, "expected array");
    if (cap.type != DST_INTEGER)
        dst_c_throwc(vm, DST_EXPECTED_INTEGER);
    dst_array_ensure(vm, ds.data.array, (uint32_t) cap.data.integer);
    dst_c_return(vm, ds);
}

/* Get next key in struct or table */
int dst_stl_next(Dst *vm) {
    DstValue ds = dst_arg(vm, 0);
    DstValue key = dst_arg(vm, 1);
    if (ds.type == DST_TABLE) {
        dst_c_return(vm, dst_table_next(ds.data.table, key));
    } else if (ds.type == DST_STRUCT) {
        dst_c_return(vm, dst_struct_next(ds.data.st, key));
    } else {
        dst_c_throwc(vm, "expected table or struct");
    }
}

/* Print values for inspection */
int dst_stl_print(Dst *vm) {
    uint32_t j, count;
    count = dst_args(vm);
    for (j = 0; j < count; ++j) {
        uint32_t i;
        const uint8_t *string = dst_to_string(vm, dst_arg(vm, j));
        uint32_t len = dst_string_length(string);
        for (i = 0; i < len; ++i)
            fputc(string[i], stdout);
    }
    fputc('\n', stdout);
    return DST_RETURN_OK;
}

/* Long description */
int dst_stl_description(Dst *vm) {
    DstValue x = dst_arg(vm, 0);
    const uint8_t *buf = dst_description(vm, x);
    dst_c_return(vm, dst_wrap_string(buf));
}

/* Short description */
int dst_stl_short_description(Dst *vm) {
    DstValue x = dst_arg(vm, 0);
    const uint8_t *buf = dst_short_description(vm, x);
    dst_c_return(vm, dst_wrap_string(buf));
}

/* Exit */
int dst_stl_exit(Dst *vm) {
    int ret;
    DstValue x = dst_arg(vm, 0);
    ret = x.type == DST_INTEGER ? x.data.integer : (x.type == DST_REAL ? x.data.real : 0);
    exit(ret);
    return DST_RETURN_OK;
}

/* Throw error */
int dst_stl_error(Dst *vm) {
    dst_c_throw(vm, dst_arg(vm, 0));
}

/****/
/* Serialization */
/****/

/* Serialize data into buffer */
int dst_stl_serialize(Dst *vm) {
    const char *err;
    DstValue buffer = dst_arg(vm, 1);
    if (buffer.type != DST_BYTEBUFFER)
        buffer = dst_wrap_buffer(dst_buffer(vm, 10));
    err = dst_serialize(vm, buffer.data.buffer, dst_arg(vm, 0));
    if (err != NULL)
        dst_c_throwc(vm, err);
    dst_c_return(vm, buffer);
}

/* Deserialize data from a buffer */
int dst_stl_deserialize(Dst *vm) {
    DstValue ret;
    uint32_t len;
    const uint8_t *data;
    const char *err;
    if (!dst_chararray_view(dst_arg(vm, 0), &data, &len))
        dst_c_throwc(vm, "expected string/buffer/symbol");
    err = dst_deserialize(vm, data, len, &ret, &data);
    if (err != NULL)
        dst_c_throwc(vm, err);
    dst_c_return(vm, ret);
}

/***/
/* Function reflection */
/***/

int dst_stl_funcenv(Dst *vm) {
    DstFunction *fn;
    if (!dst_check_function(vm, 0, &fn))
        dst_c_throwc(vm, "expected function");
    if (fn->env)
        dst_c_return(vm, dst_wrap_funcenv(fn->env));
    else
        return DST_RETURN_OK;
}

int dst_stl_funcdef(Dst *vm) {
    DstFunction *fn;
    if (!dst_check_function(vm, 0, &fn))
        dst_c_throwc(vm, "expected function");
    dst_c_return(vm, dst_wrap_funcdef(fn->def));
}

int dst_stl_funcparent(Dst *vm) {
    DstFunction *fn;
    if (!dst_check_function(vm, 0, &fn))
        dst_c_throwc(vm, "expected function");
    if (fn->parent)
        dst_c_return(vm, dst_wrap_function(fn->parent));
    else
        return DST_RETURN_OK;
}

int dst_stl_def(Dst *vm) {
    DstValue key = dst_arg(vm, 0);
    if (dst_args(vm) != 2) {
        dst_c_throwc(vm, "expected 2 arguments to global-def");
    }
    if (key.type != DST_STRING && key.type != DST_SYMBOL) {
        dst_c_throwc(vm, "expected string/symbol as first argument");
    }
    key.type = DST_SYMBOL;
    dst_env_put(vm, vm->env, key, dst_arg(vm, 1));
    dst_c_return(vm, dst_arg(vm, 1));
}

int dst_stl_var(Dst *vm) {
    DstValue key = dst_arg(vm, 0);
    if (dst_args(vm) != 2) {
        dst_c_throwc(vm, "expected 2 arguments to global-var");
    }
    if (key.type != DST_STRING && key.type != DST_SYMBOL) {
        dst_c_throwc(vm, "expected string as first argument");
    }
    key.type = DST_SYMBOL;
    dst_env_putvar(vm, vm->env, key, dst_arg(vm, 1));
    dst_c_return(vm, dst_arg(vm, 1));
}

/****/
/* IO */
/****/

/* File type definition */
static DstUserType dst_stl_filetype = {
    "std.file",
    NULL,
    NULL,
    NULL,
    NULL
};

/* Open a a file and return a userdata wrapper arounf the C file API. */
int dst_stl_open(Dst *vm) {
    const uint8_t *fname = dst_to_string(vm, dst_arg(vm, 0));
    const uint8_t *fmode = dst_to_string(vm, dst_arg(vm, 1));
    FILE *f;
    FILE **fp;
    if (dst_args(vm) < 2 || dst_arg(vm, 0).type != DST_STRING
            || dst_arg(vm, 1).type != DST_STRING)
        dst_c_throwc(vm, "expected filename and filemode");
    f = fopen((const char *)fname, (const char *)fmode);
    if (!f)
        dst_c_throwc(vm, "could not open file");
    fp = dst_userdata(vm, sizeof(FILE *), &dst_stl_filetype);
    *fp = f;
    dst_c_return(vm, dst_wrap_userdata(fp));
}

/* Read an entire file into memory */
int dst_stl_slurp(Dst *vm) {
    DstBuffer *b;
    long fsize;
    FILE *f;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!dst_check_buffer(vm, 1, &b)) b = dst_buffer(vm, 10);
    f = *fp;
    /* Read whole file */
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Ensure buffer size */
    dst_buffer_ensure(vm, b, b->count + fsize);
    fread((char *)(b->data + b->count), fsize, 1, f);
    b->count += fsize;
    dst_c_return(vm, dst_wrap_buffer(b));
}

/* Read a certain number of bytes into memory */
int dst_stl_read(Dst *vm) {
    DstBuffer *b;
    FILE *f;
    int64_t len;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!(dst_check_integer(vm, 1, &len))) dst_c_throwc(vm, "expected integer");
    if (!dst_check_buffer(vm, 2, &b)) b = dst_buffer(vm, 10);
    f = *fp;
    /* Ensure buffer size */
    dst_buffer_ensure(vm, b, b->count + len);
    b->count += fread((char *)(b->data + b->count), len, 1, f) * len;
    dst_c_return(vm, dst_wrap_buffer(b));
}

/* Write bytes to a file */
int dst_stl_write(Dst *vm) {
    FILE *f;
    const uint8_t *data;
    uint32_t len;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!dst_chararray_view(dst_arg(vm, 1), &data, &len)) dst_c_throwc(vm, "expected string|buffer");
    f = *fp;
    fwrite(data, len, 1, f);
    return DST_RETURN_OK;
}

/* Close a file */
int dst_stl_close(Dst *vm) {
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    fclose(*fp);
    dst_c_return(vm, dst_wrap_nil());
}

/****/
/* Temporary */
/****/

/* Force garbage collection */
int dst_stl_gcollect(Dst *vm) {
	dst_collect(vm);
	return DST_RETURN_OK;
}

/***/
/* Compilation */
/***/

/* Generate a unique symbol */
static int dst_stl_gensym(Dst *vm) {
    DstValue source = dst_arg(vm, 0);
    const uint8_t *sym = NULL;
    uint32_t len;
    const uint8_t *data;
    if (source.type == DST_NIL) {
        sym = dst_string_cu(vm, "");
    } else if (dst_chararray_view(source, &data, &len)) {
        sym = dst_string_bu(vm, data, len);
    } else {
        dst_c_throwc(vm, "exepcted string/buffer/symbol/nil");
    }
    dst_c_return(vm, dst_wrap_symbol(sym));
}

/* Compile a value */
static int dst_stl_compile(Dst *vm) {
    DstTable *env = vm->env;
    if (dst_arg(vm, 1).type == DST_TABLE) {
        env = dst_arg(vm, 1).data.table;
    }
    dst_c_return(vm, dst_compile(vm, env, dst_arg(vm, 0)));
}

/* Get vm->env */
static int dst_stl_getenv(Dst *vm) {
    dst_c_return(vm, dst_wrap_table(vm->env));
}

/* Set vm->env */
static int dst_stl_setenv(Dst *vm) {
    DstValue newEnv = dst_arg(vm, 0);
    if (newEnv.type != DST_TABLE) {
        dst_c_throwc(vm, "expected table");
    }
    vm->env = newEnv.data.table;
    return DST_RETURN_OK;
}

/****/
/* Bootstraping */
/****/

static const DstModuleItem std_module[] = {
    /* Arithmetic */
    {"+", dst_stl_add},
    {"*", dst_stl_mul},
    {"-", dst_stl_sub},
    {"/", dst_stl_div},
    /* Comparisons */
    {"<", dst_stl_lessthan},
    {">", dst_stl_greaterthan},
    {"=", dst_stl_equal},
    {"not=", dst_stl_notequal},
    {"<=", dst_stl_lessthaneq},
    {">=", dst_stl_greaterthaneq},
    /* Bitwise arithmetic */
    {"band", dst_stl_band},
    {"bor", dst_stl_bor},
    {"bxor", dst_stl_bxor},
    {"blshift", dst_stl_blshift},
    {"brshift", dst_stl_brshift},
    {"bnot", dst_stl_bnot},
    /* IO */
    {"open", dst_stl_open},
    {"slurp", dst_stl_slurp},
    {"read", dst_stl_read},
    {"write", dst_stl_write},
    /* Compile */
    {"gensym", dst_stl_gensym},
    {"getenv", dst_stl_getenv},
    {"setenv", dst_stl_setenv},
    {"compile", dst_stl_compile},
    /* Other */
    {"not", dst_stl_not},
    {"clear", dst_stl_clear},
    {"length", dst_stl_length},
    {"hash", dst_stl_hash},
    {"integer", dst_stl_to_int},
    {"real", dst_stl_to_real},
    {"type", dst_stl_type},
    {"slice", dst_stl_slice},
    {"array", dst_stl_array},
    {"tuple", dst_stl_tuple},
    {"table", dst_stl_table},
    {"struct", dst_stl_struct},
    {"buffer", dst_stl_buffer},
    {"string", dst_stl_string},
    {"symbol", dst_stl_symbol},
    {"thread", dst_stl_thread},
    {"status", dst_stl_status},
    {"current", dst_stl_current},
    {"parent", dst_stl_parent},
    {"print", dst_stl_print},
    {"description", dst_stl_description},
    {"short-description", dst_stl_short_description},
    {"exit!", dst_stl_exit},
    {"get", dst_stl_get},
    {"set!", dst_stl_set},
    {"next", dst_stl_next},
    {"error", dst_stl_error},
    {"serialize", dst_stl_serialize},
    {"deserialize", dst_stl_deserialize},
    {"push!", dst_stl_push},
    {"pop!", dst_stl_pop},
    {"peek", dst_stl_peek},
    {"ensure!", dst_stl_ensure},
    {"open", dst_stl_open},
    {"slurp", dst_stl_slurp},
    {"read", dst_stl_read},
    {"write", dst_stl_write},
    {"close", dst_stl_close},
    {"funcenv", dst_stl_funcenv},
    {"funcdef", dst_stl_funcdef},
    {"funcparent", dst_stl_funcparent},
    {"gcollect", dst_stl_gcollect},
    {"global-def", dst_stl_def},
    {"global-var", dst_stl_var},
    {NULL, NULL}
};

/* Load stl library into the current environment. Create stl module object
 * only if it is not yet created. */
void dst_stl_load(Dst *vm) {
    DstValue maybeEnv = dst_table_get(vm->modules, dst_string_cvs(vm, "std"));
    if (maybeEnv.type == DST_TABLE) {
        /* Module already created, so merge into main vm. */
        dst_env_merge(vm, vm->env, maybeEnv.data.table);
    } else {
        /* Module not yet created */
        /* Load the normal c functions */
        dst_module_mutable(vm, "std", std_module);
        /* Wrap stdin and stdout */
        FILE **inp = dst_userdata(vm, sizeof(FILE *), &dst_stl_filetype);
        FILE **outp = dst_userdata(vm, sizeof(FILE *), &dst_stl_filetype);
        FILE **errp = dst_userdata(vm, sizeof(FILE *), &dst_stl_filetype);
        *inp = stdin;
        *outp = stdout;
        *errp = stderr;
        dst_module_put(vm, "std", "stdin", dst_wrap_userdata(inp));
        dst_module_put(vm, "std", "stdout", dst_wrap_userdata(outp));
        dst_module_put(vm, "std", "stderr", dst_wrap_userdata(outp));
        /* Now merge */
        maybeEnv = dst_table_get(vm->modules, dst_string_cvs(vm, "std"));
        dst_env_merge(vm, vm->env, maybeEnv.data.table);
    }
}

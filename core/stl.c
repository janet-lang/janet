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
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>

#include <gst/disasm.h>

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

#define SIMPLE_ACCUM_FUNCTION(name, start, op)\
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
    gst_c_return(vm, lhs);\
}

SIMPLE_ACCUM_FUNCTION(add, 0, +)
SIMPLE_ACCUM_FUNCTION(mul, 1, *)
SIMPLE_ACCUM_FUNCTION(sub, 0, -)
SIMPLE_ACCUM_FUNCTION(div, 1, /)

#undef SIMPLE_ACCUM_FUNCTION

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
    uint32_t length;
    uint32_t newlength;
    GstInteger num;

    /* Get data */
    x = gst_arg(vm, 0);
    if (!gst_seq_view(x, &data, &length)) 
        gst_c_throwc(vm, "expected array or tuple");

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
    if (from < 0 || to < 0)
        gst_c_throwc(vm, "index out of bounds");

    /* Build slice */
    newlength = to - from;
    if (x.type == GST_TUPLE) {
        GstValue *tup = gst_tuple_begin(vm, newlength);
        gst_memcpy(tup, data + from, newlength * sizeof(GstValue));
        gst_c_return(vm, gst_wrap_tuple(gst_tuple_end(vm, tup)));
    } else {
        GstArray *arr = gst_array(vm, newlength);
        arr->count = newlength;
        gst_memcpy(arr->data, data + from, newlength * sizeof(GstValue));
        gst_c_return(vm, gst_wrap_array(arr));
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
    st = gst_struct_begin(vm, count * 2);
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
        if (gst_chararray_view(gst_arg(vm, j), &dat, &slen))
            length += slen;
        else
            gst_c_throwc(vm, GST_EXPECTED_STRING);
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

/* Associative rawget */
int gst_stl_rawget(Gst *vm) {
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

/* Associative rawset */
int gst_stl_rawset(Gst *vm) {
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
        if (j < count - 1)
            fputc(' ', stdout);
    }
    fputc('\n', stdout);
    return GST_RETURN_OK;
}

/* To string */
int gst_stl_tostring(Gst *vm) {
    const uint8_t *string = gst_to_string(vm, gst_arg(vm, 0));
    gst_c_return(vm, gst_wrap_string(string));
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
    uint32_t i;
    GstValue buffer = gst_arg(vm, 0);
    if (buffer.type != GST_BYTEBUFFER)
        gst_c_throwc(vm, "expected buffer");
    for (i = 1; i < gst_count_args(vm); ++i) {
        err = gst_serialize(vm, buffer.data.buffer, gst_arg(vm, i));
        if (err != NULL)
            gst_c_throwc(vm, err);
    }
    gst_c_return(vm, buffer);
}

/****/
/* Registry */
/****/

int gst_stl_global(Gst *vm) {
    gst_c_return(vm, gst_table_get(vm->registry, gst_arg(vm, 0)));
}

int gst_stl_setglobal(Gst *vm) {
    gst_table_put(vm, vm->registry, gst_arg(vm, 0), gst_arg(vm, 1));
    gst_c_return(vm, gst_wrap_nil());
}

/****/
/* IO */
/****/

/* File type definition */
static GstUserType gst_stl_filetype = {
    "io.file",
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

/* These functions should definitely be moved to a different module, removed, or
 * rewritten in gst when the language is complete enough. This is not to say
 * that functions in other section need not be moved. */

/* Print disassembly for a function */
int gst_stl_dasm(Gst *vm) {
    GstValue x = gst_arg(vm, 0);
    if (x.type == GST_FUNCTION) {
        printf("%c[31m===== Begin Disassembly =====\n", 27);
        gst_dasm_function(stdout, x.data.function);
        printf("=====  End Disassembly  =====%c[0m\n", 27);
    } else {
        gst_c_throwc(vm, "expected function");
    }
    return GST_RETURN_OK;
}

/****/
/* Bootstraping */
/****/

static const GstModuleItem const std_module[] = {
    {"+", gst_stl_add},
    {"*", gst_stl_mul},
    {"-", gst_stl_sub},
    {"/", gst_stl_div},
    {"<", gst_stl_lessthan},
    {">", gst_stl_greaterthan},
    {"=", gst_stl_equal},
    {"<=", gst_stl_lessthaneq},
    {">=", gst_stl_greaterthaneq},
    {"not", gst_stl_not},
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
    {"print", gst_stl_print},
    {"tostring", gst_stl_tostring},
    {"exit", gst_stl_exit},
    {"rawget", gst_stl_rawget},
    {"rawset", gst_stl_rawset},
    {"next", gst_stl_next},
    {"error", gst_stl_error},
    {"serialize", gst_stl_serialize},
    {"global", gst_stl_global},
    {"setglobal", gst_stl_setglobal},
    {"push", gst_stl_push},
    {"pop", gst_stl_pop},
    {"peek", gst_stl_peek},
    {"ensure", gst_stl_ensure},
    {"open", gst_stl_open},
    {"slurp", gst_stl_slurp},
    {"close", gst_stl_close},
    {"dasm", gst_stl_dasm},
    {NULL, NULL}
};

/* Load all libraries */
void gst_stl_load(Gst *vm) {
    gst_module_put(vm, "std", gst_cmodule_struct(vm, std_module));
}

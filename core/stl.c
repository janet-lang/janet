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

static const char GST_EXPECTED_NUMBER_OP[] = "expected operand to be number";
static const char GST_EXPECTED_STRING[] = "expected string";

/***/
/* Arithmetic */
/***/

#define SIMPLE_ACCUM_FUNCTION(name, start, op)\
int gst_stl_##name(Gst* vm) {\
    GstValue ret;\
    uint32_t j, count;\
    ret.type = GST_NUMBER;\
    ret.data.number = start;\
    count = gst_count_args(vm);\
    for (j = 0; j < count; ++j) {\
        GstValue operand = gst_arg(vm, j);\
        if (operand.type != GST_NUMBER)\
            gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);\
        ret.data.number op operand.data.number;\
    }\
    gst_c_return(vm, ret);\
}

SIMPLE_ACCUM_FUNCTION(add, 0, +=)
SIMPLE_ACCUM_FUNCTION(mul, 1, *=)

#undef SIMPLE_ACCUM_FUNCTION

#define UNARY_ACCUM_FUNCTION(name, zeroval, unaryop, op)\
int gst_stl_##name(Gst* vm) {\
    GstValue ret;\
    GstValue operand;\
    uint32_t j, count;\
    ret.type = GST_NUMBER;\
    count = gst_count_args(vm);\
    if (count == 0) {\
        ret.data.number = zeroval;\
        gst_c_return(vm, ret);\
    }\
    operand = gst_arg(vm, 0);\
    if (operand.type != GST_NUMBER)\
        gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);\
    if (count == 1) {\
        ret.data.number = unaryop operand.data.number;\
        gst_c_return(vm, ret);\
    } else {\
        ret.data.number = operand.data.number;\
    }\
    for (j = 1; j < count; ++j) {\
        operand = gst_arg(vm, j);\
        if (operand.type != GST_NUMBER)\
            gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);\
        ret.data.number op operand.data.number;\
    }\
    gst_c_return(vm, ret);\
}

UNARY_ACCUM_FUNCTION(sub, 0, -, -=)
UNARY_ACCUM_FUNCTION(div, 1, 1/, /=)

#undef UNARY_ACCUM_FUNCTION

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

/****/
/* Core */
/****/

/* Get length of object */
int gst_stl_length(Gst *vm) {
    GstValue ret;
    uint32_t count = gst_count_args(vm);
    if (count == 0) {
        ret.type = GST_NIL;
        gst_c_return(vm, ret);
    }
    if (count == 1) {
        ret.type = GST_NUMBER;
        GstValue x = gst_arg(vm, 0);
        switch (x.type) {
        default:
            gst_c_throwc(vm, "cannot get length");
        case GST_STRING:
            ret.data.number = gst_string_length(x.data.string);
            break;
        case GST_ARRAY:
            ret.data.number = x.data.array->count;
            break;
        case GST_BYTEBUFFER:
            ret.data.number = x.data.buffer->count;
            break;
        case GST_TUPLE:
            ret.data.number = gst_tuple_length(x.data.tuple);
            break;
        case GST_OBJECT:
            ret.data.number = x.data.object->count;
            break;
        }
    }
    gst_c_return(vm, ret);
}

/* Get nth argument, not including first argument */
int gst_stl_select(Gst *vm) {
    GstValue selector;
    uint32_t count, n;
    count = gst_count_args(vm);
    if (count == 0)
        gst_c_throwc(vm, "select takes at least one argument");
    selector = gst_arg(vm, 0);
    if (selector.type != GST_NUMBER)
        gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);
    n = selector.data.number;
    gst_c_return(vm, gst_arg(vm, n + 1));
}

/* Get a slice of a sequence */
int gst_stl_slice(Gst *vm) {
    uint32_t count = gst_count_args(vm);
    int32_t from, to;
    GstValue x;
    const GstValue *data;
    uint32_t length;
    uint32_t newlength;
    GstNumber num;

    /* Check args */
    if (count < 1)
        gst_c_throwc(vm, "slice takes at least one argument");
    x = gst_arg(vm, 0);

    /* Get data */
    if (x.type == GST_TUPLE) {
        data = x.data.st;
        length = gst_tuple_length(x.data.st);
    } else if (x.type == GST_ARRAY) {
        data = x.data.array->data;
        length = x.data.array->count;
    } else {
        gst_c_throwc(vm, "expected array or tuple");
    }

    /* Get from index */
    if (count < 2) {
        from = 0;
    } else {
        if (!gst_check_number(vm, 1, &num))
            gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);
        from = gst_to_index(num, length);
    }

    /* Get to index */
    if (count < 3) {
        to = length;
    } else {
        if (!gst_check_number(vm, 2, &num))
            gst_c_throwc(vm, GST_EXPECTED_NUMBER_OP);
        to = gst_to_endrange(num, length);
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
    case GST_NUMBER:
        typestr = "number";
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
    case GST_OBJECT:
        typestr = "object";
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
int gst_stl_object(Gst *vm) {
    uint32_t i;
    uint32_t count = gst_count_args(vm);
    GstObject *object;
    if (count % 2 != 0)
        gst_c_throwc(vm, "expected even number of arguments");
    object = gst_object(vm, 4 * count);
    for (i = 0; i < count; i += 2)
        gst_object_put(vm, object, gst_arg(vm, i), gst_arg(vm, i + 1));
    gst_c_return(vm, gst_wrap_object(object));
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
    GstBuffer *buf = gst_buffer(vm, 10);
    count = gst_count_args(vm);
    for (i = 0; i < count; ++i) {
        const uint8_t *string = gst_to_string(vm, gst_arg(vm, i));
        gst_buffer_append(vm, buf, string, gst_string_length(string));
    }
    gst_c_return(vm, gst_wrap_buffer(buf));
}

/* Concatenate string */
int gst_stl_strcat(Gst *vm) {
    uint32_t j, count, length, index;
    uint8_t *str;
    count = gst_count_args(vm);
    length = 0;
    index = 0;
    /* Find length and assert string arguments */
    for (j = 0; j < count; ++j) {
        GstValue arg = gst_arg(vm, j);
        if (arg.type != GST_STRING)
            gst_c_throwc(vm, GST_EXPECTED_STRING);
        length += gst_string_length(arg.data.string);
    }
    /* Make string */
    str = gst_string_begin(vm, length);
    for (j = 0; j < count; ++j) {
        GstValue arg = gst_arg(vm, j);
        uint32_t slen = gst_string_length(arg.data.string);
        gst_memcpy(str + index, arg.data.string, slen);
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
    if (count != 2) {
        gst_c_throwc(vm, "expects 2 arguments");
    }
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
    if (count != 3) {
        gst_c_throwc(vm, "expects 3 arguments");
    }
    err = gst_set(vm, gst_arg(vm, 0), gst_arg(vm, 1), gst_arg(vm, 2));
    if (err != NULL) {
        gst_c_throwc(vm, err);
    } else {
        gst_c_return(vm, gst_arg(vm, 0));
    }
}

/* Get next key in struct or object */
int gst_stl_next(Gst *vm) {
    GstValue ds = gst_arg(vm, 0);
    GstValue key = gst_arg(vm, 1);
    if (ds.type == GST_OBJECT) {
       gst_c_return(vm, gst_object_next(ds.data.object, key));    
    } else if (ds.type == GST_STRUCT) {
       gst_c_return(vm, gst_struct_next(ds.data.st, key));    
    } else {
        gst_c_throwc(vm, "expected object or struct");
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
    GstValue exitValue = gst_arg(vm, 0);
    ret = (exitValue.type == GST_NUMBER) ? exitValue.data.number : 0;
    exit(ret);
    return GST_RETURN_OK;
}

/* Throw error */
int gst_stl_error(Gst *vm) {
    GstValue errval = gst_arg(vm, 0);
    gst_c_throw(vm, errval);
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
/* IO */
/****/

/* TODO - add userdata to allow for manipulation of FILE pointers. */

int gst_stl_open(Gst *vm) {
    const uint8_t *fname = gst_to_string(vm, gst_arg(vm, 0));
    const uint8_t *fmode = gst_to_string(vm, gst_arg(vm, 1));
    FILE *f;
    FILE **fp;
    GstValue *st;
    if (gst_count_args(vm) < 2)
        gst_c_throwc(vm, "expected filename and filemode");
    f = fopen((const char *)fname, (const char *)fmode);
    if (!f)
        gst_c_throwc(vm, "could not open file");
    st = gst_struct_begin(vm, 0);
    fp = gst_userdata(vm, sizeof(FILE *), gst_struct_end(vm, st));
    gst_c_return(vm, gst_wrap_userdata(fp));
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
    {"length", gst_stl_length},
    {"type", gst_stl_type},
    {"select", gst_stl_select},
    {"slice", gst_stl_slice},
    {"array", gst_stl_array},
    {"tuple", gst_stl_tuple},
    {"object", gst_stl_object},
    {"struct", gst_stl_struct},
    {"buffer", gst_stl_buffer},
    {"strcat", gst_stl_strcat},
    {"print", gst_stl_print},
    {"tostring", gst_stl_tostring},
    {"exit", gst_stl_exit},
    {"rawget", gst_stl_rawget},
    {"rawset", gst_stl_rawset},
    {"next", gst_stl_next},
    {"error", gst_stl_error},
    {"serialize", gst_stl_serialize},
    {"open", gst_stl_open},
    {NULL, NULL}
};

/* Load all libraries */
void gst_stl_load(Gst *vm) {
    gst_module_put(vm, "std", gst_cmodule_struct(vm, std_module));
}

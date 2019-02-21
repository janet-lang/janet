/*
* Copyright (c) 2019 Calvin Rose & contributors
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

/* Compiler feature test macros for things */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

typedef uint8_t   ta_uint8_t;
typedef int8_t    ta_int8_t;
typedef uint16_t  ta_uint16_t;
typedef int16_t   ta_int16_t;
typedef uint32_t  ta_uint32_t;
typedef int32_t   ta_int32_t;
typedef uint64_t  ta_uint64_t;
typedef int64_t   ta_int64_t;
typedef float     ta_float32_t;
typedef double    ta_float64_t;

typedef enum TA_Type {
    TA_TYPE_uint8,
    TA_TYPE_int8,
    TA_TYPE_uint16,
    TA_TYPE_int16,
    TA_TYPE_uint32,
    TA_TYPE_int32,
    TA_TYPE_uint64,
    TA_TYPE_int64,
    TA_TYPE_float32,
    TA_TYPE_float64,
} TA_Type;


static  char *ta_type_names[] = {
    "uint8",
    "int8",
    "uint16",
    "int16",
    "uint32",
    "int32",
    "uint64",
    "int64",
    "float32",
    "float64",
};

static  size_t ta_type_sizes[] = {
    sizeof(ta_uint8_t),
    sizeof(ta_int8_t),
    sizeof(ta_uint16_t),
    sizeof(ta_int16_t),
    sizeof(ta_uint32_t),
    sizeof(ta_int32_t),
    sizeof(ta_uint64_t),
    sizeof(ta_int64_t),
    sizeof(ta_float32_t),
    sizeof(ta_float64_t),
};
#define TA_COUNT_TYPES (TA_TYPE_float64 + 1)
#define TA_ATOM_MAXSIZE 8;

#define TA_FLAG_BIG_ENDIAN 1

static TA_Type get_ta_type_by_name(const uint8_t *name) {
    size_t nt = sizeof(ta_type_names) / sizeof(char *);
    for (size_t i = 0; i < nt; i++) {
        if (!janet_cstrcmp(name, ta_type_names[i]))
            return i;
    }
    return 0;
}




typedef struct {
    uint8_t *data;
    size_t size;
    int32_t flags;
} TA_Buffer;

static TA_Buffer *ta_buffer_init(TA_Buffer *buf, size_t size) {
    buf->data = (uint8_t *)calloc(size, sizeof(uint8_t));
    buf->size = size;
#ifdef JANET_BIG_ENDIAN
    buf->flags = TA_FLAG_BIG_ENDIAN;
#else
    buf->flags = 0;
#endif
    return buf;
}

static int ta_buffer_gc(void *p, size_t s) {
    (void) s;
    TA_Buffer *buf = (TA_Buffer *)p;
    free(buf->data);
    return 0;
}

static void ta_buffer_marshal(void *p, JanetMarshalContext *ctx) {
  TA_Buffer *buf = (TA_Buffer *)p;
  janet_marshal_int(ctx,buf->size);
  janet_marshal_int(ctx,buf->flags);
  janet_marshal_bytes(ctx,buf->data,buf->size);
}


static const JanetAbstractType ta_buffer_type = {"ta/buffer", ta_buffer_gc, NULL, NULL, NULL};


typedef struct {
    TA_Buffer *buffer;
    void *data;   /* pointer inside buffer->data */
    size_t size;
    size_t stride;
    TA_Type type;
} TA_View;


static int ta_mark(void *p, size_t s) {
    (void) s;
    TA_View *view = (TA_View *)p;
    janet_mark(janet_wrap_abstract(view->buffer));
    return 0;
}

#define DEFINE_VIEW_TYPE(type) \
  typedef struct {         \
    TA_Buffer * buffer;        \
    ta_##type##_t * data;      \
    size_t size;           \
    size_t stride;         \
    TA_Type type;          \
  } TA_View_##type ;


#define  DEFINE_VIEW_GETTER(type) \
static Janet ta_get_##type(void *p, Janet key) { \
  Janet value;  \
  size_t index; \
  if (!janet_checkint(key))      \
    janet_panic("expected integer key");     \
  index = (size_t)janet_unwrap_integer(key);\
  TA_View_##type * array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    value = janet_wrap_nil(); \
  } else { \
    value = janet_wrap_number(array->data[index*array->stride]); \
  } \
  return value; \
}

#define DEFINE_VIEW_SETTER(type) \
void ta_put_##type(void *p, Janet key,Janet value) { \
  size_t index;\
  if (!janet_checkint(key))\
    janet_panic("expected integer key"); \
  if (!janet_checktype(value,JANET_NUMBER)) \
    janet_panic("expected number value"); \
  index = (size_t)janet_unwrap_integer(key); \
  TA_View_##type * array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    janet_panic("typed array out of bound"); \
  } \
  array->data[index*array->stride]=(ta_##type##_t)janet_unwrap_number(value); \
}

#define DEFINE_VIEW_INITIALIZER(type) \
  static TA_View * ta_init_##type(TA_View * view,TA_Buffer * buf,size_t size,size_t offset,size_t stride) { \
  TA_View_##type * tview=(TA_View_##type *) view; \
  size_t buf_size=offset+(size-1)*(sizeof(ta_##type##_t))*stride+1; \
  if (buf==NULL) {  \
    buf=(TA_Buffer *)janet_abstract(&ta_buffer_type,sizeof(TA_Buffer)); \
    ta_buffer_init(buf,buf_size); \
  } \
  if (buf->size<buf_size) { \
    janet_panic("bad buffer size"); \
  } \
  tview->buffer=buf; \
  tview->stride=stride; \
  tview->size=size; \
  tview->data=(ta_##type##_t *)(buf->data+offset);  \
  tview->type=TA_TYPE_##type; \
  return view; \
}

#define BUILD_TYPE(type) \
DEFINE_VIEW_TYPE(type)   \
DEFINE_VIEW_GETTER(type)  \
DEFINE_VIEW_SETTER(type) \
DEFINE_VIEW_INITIALIZER(type)

BUILD_TYPE(uint8)
BUILD_TYPE(int8)
BUILD_TYPE(uint16)
BUILD_TYPE(int16)
BUILD_TYPE(uint32)
BUILD_TYPE(int32)
BUILD_TYPE(uint64)
BUILD_TYPE(int64)
BUILD_TYPE(float32)
BUILD_TYPE(float64)

#undef DEFINE_VIEW_TYPE
#undef DEFINE_VIEW_GETTER
#undef DEFINE_VIEW_SETTER
#undef DEFINE_VIEW_INITIALIZER


#define VIEW_ABSTRACT_DEFINE(type) {"ta/"#type,NULL,ta_mark,ta_get_##type,ta_put_##type}

static const JanetAbstractType ta_array_types[] = {
    VIEW_ABSTRACT_DEFINE(uint8),
    VIEW_ABSTRACT_DEFINE(int8),
    VIEW_ABSTRACT_DEFINE(uint16),
    VIEW_ABSTRACT_DEFINE(int16),
    VIEW_ABSTRACT_DEFINE(uint32),
    VIEW_ABSTRACT_DEFINE(int32),
    VIEW_ABSTRACT_DEFINE(uint64),
    VIEW_ABSTRACT_DEFINE(int64),
    VIEW_ABSTRACT_DEFINE(float32),
    VIEW_ABSTRACT_DEFINE(float64),
};

static int is_ta_type(Janet x) {
    if (janet_checktype(x, JANET_ABSTRACT)) {
        const JanetAbstractType *at = janet_abstract_type(janet_unwrap_abstract(x));
        for (size_t i = 0; i < TA_COUNT_TYPES; i++) {
            if (at == ta_array_types + i) return 1;
        }
    }
    return 0;
}

#undef VIEW_ABSTRACT_DEFINE


#define CASE_TYPE_INITIALIZE(type)  case  TA_TYPE_##type :  ta_init_##type(view,buffer,size,offset,stride); break

static Janet cfun_typed_array_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 5);
    size_t offset = 0;
    size_t stride = 1;
    TA_Buffer *buffer = NULL;
    const uint8_t *keyw = janet_getkeyword(argv, 0);
    TA_Type type = get_ta_type_by_name(keyw);
    size_t size = (size_t)janet_getinteger(argv, 1);
    if (argc > 2)
        stride = (size_t)janet_getinteger(argv, 2);
    if (argc > 3)
        offset = (size_t)janet_getinteger(argv, 3);
    if (argc > 4) {
        if (is_ta_type(argv[4])) {
            TA_View *view = (TA_View *)janet_unwrap_abstract(argv[4]);
            offset = (view->buffer->data - (uint8_t *)(view->data)) + offset * ta_type_sizes[view->type];
            stride *= view->stride;
            buffer = view->buffer;
        } else {
            buffer = (TA_Buffer *)janet_getabstract(argv, 4, &ta_buffer_type);
        }
    }
    TA_View *view = janet_abstract(&ta_array_types[type], sizeof(TA_View));
    switch (type) {
            CASE_TYPE_INITIALIZE(uint8);
            CASE_TYPE_INITIALIZE(int8);
            CASE_TYPE_INITIALIZE(uint16);
            CASE_TYPE_INITIALIZE(int16);
            CASE_TYPE_INITIALIZE(uint32);
            CASE_TYPE_INITIALIZE(int32);
            CASE_TYPE_INITIALIZE(uint64);
            CASE_TYPE_INITIALIZE(int64);
            CASE_TYPE_INITIALIZE(float32);
            CASE_TYPE_INITIALIZE(float64);
    }
    return janet_wrap_abstract(view);
}

#undef CASE_TYPE_INITIALIZE

static Janet cfun_typed_array_buffer(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (is_ta_type(argv[0])) {
        TA_View *view = (TA_View *)janet_unwrap_abstract(argv[0]);
        return janet_wrap_abstract(view->buffer);
    }
    size_t size = (size_t)janet_getinteger(argv, 0);
    TA_Buffer *buf = (TA_Buffer *)janet_abstract(&ta_buffer_type, sizeof(TA_Buffer));
    ta_buffer_init(buf, size);
    return janet_wrap_abstract(buf);
}

static Janet cfun_typed_array_size(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (is_ta_type(argv[0])) {
        TA_View *view = (TA_View *)janet_unwrap_abstract(argv[0]);
        return janet_wrap_number(view->size);
    }
    TA_Buffer *buf = (TA_Buffer *)janet_getabstract(argv, 0, &ta_buffer_type);
    return janet_wrap_number(buf->size);
}

static Janet cfun_typed_array_properties(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (!is_ta_type(argv[0]))
        janet_panic("expected typed array");
    TA_View *view = (TA_View *)janet_unwrap_abstract(argv[0]);
    JanetKV *props = janet_struct_begin(6);
    janet_struct_put(props, janet_ckeywordv("size"), janet_wrap_number(view->size));
    janet_struct_put(props, janet_ckeywordv("byte-offset"), janet_wrap_number((uint8_t *)(view->data) - view->buffer->data));
    janet_struct_put(props, janet_ckeywordv("stride"), janet_wrap_number(view->stride));
    janet_struct_put(props, janet_ckeywordv("type"), janet_ckeywordv(ta_type_names[view->type]));
    janet_struct_put(props, janet_ckeywordv("type-size"), janet_wrap_number(ta_type_sizes[view->type]));
    janet_struct_put(props, janet_ckeywordv("buffer"), janet_wrap_abstract(view->buffer));
    return janet_wrap_struct(janet_struct_end(props));
}

static Janet cfun_typed_array_copy_bytes(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 4);
    if (is_ta_type(argv[0]) && is_ta_type(argv[2])) {
      TA_View *src = (TA_View *)janet_unwrap_abstract(argv[0]);
      size_t index_src=(size_t)janet_getinteger(argv, 1);
      TA_View *dst = (TA_View *)janet_unwrap_abstract(argv[2]);
      size_t index_dst=(size_t)janet_getinteger(argv, 3);
      size_t src_atom_size=ta_type_sizes[src->type];
      size_t dst_atom_size=ta_type_sizes[dst->type];
      size_t pos_src=((uint8_t *)(src->data) - src->buffer->data)+(index_src*src->stride*src_atom_size);
      size_t pos_dst=((uint8_t *)(dst->data) - dst->buffer->data)+(index_dst*dst->stride*dst_atom_size);
      if (pos_dst+src_atom_size <= dst->buffer->size)
	memmove(dst->buffer->data+pos_dst,src->buffer->data+pos_src,src_atom_size);
      else
	janet_panic("typed array out of bound");
    } else {
      janet_panic("expected typed array");
    }
    return janet_wrap_nil();
}

static Janet cfun_typed_array_swap_bytes(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 4);
    if (is_ta_type(argv[0]) && is_ta_type(argv[2])) {
      TA_View *src = (TA_View *)janet_unwrap_abstract(argv[0]);
      size_t index_src=(size_t)janet_getinteger(argv, 1);
      TA_View *dst = (TA_View *)janet_unwrap_abstract(argv[2]);
      size_t index_dst=(size_t)janet_getinteger(argv, 3);
      size_t src_atom_size=ta_type_sizes[src->type];
      size_t dst_atom_size=ta_type_sizes[dst->type];
      size_t pos_src=((uint8_t *)(src->data) - src->buffer->data)+(index_src*src->stride*src_atom_size);
      size_t pos_dst=((uint8_t *)(dst->data) - dst->buffer->data)+(index_dst*dst->stride*dst_atom_size);
      uint8_t temp[TA_ATOM_MAXSIZE];
      if (pos_dst+src_atom_size <= dst->buffer->size) {
	uint8_t * src_ptr=src->buffer->data+pos_src;
	uint8_t * dst_ptr=dst->buffer->data+pos_dst;
	memcpy(temp,src_ptr,src_atom_size);
	memcpy(src_ptr,dst_ptr,src_atom_size);
	memcpy(dst_ptr,temp,src_atom_size);
      }
      else
	janet_panic("typed array buffer out of bound");
    } else {
      janet_panic("expected typed array");
    }
    return janet_wrap_nil();
}


/* static int marshal_push_ta_buffer(MarshalState *st, Janet x, int flags) { */
/*   TA_Buffer *buffer =(TA_Buffer *)janet_unwrap_abstract(x); */
/*   /\* Record reference *\/ */
/*   janet_table_put(&st->seen, x, janet_wrap_integer(st->nextid++)) */
/*   pushbyte(st, LB_TA_BUFFER); */
/*   pushint(st, buffer->size); */
/*   pushint(st, buffer->flags); */
/*   pushbytes(st, buffer->data, buffer->size); */
/* } */





static const JanetReg ta_cfuns[] = {
    {
        "tarray/new", cfun_typed_array_new,
        JDOC("(tarray/new type size [stride = 1 [offset = 0 [tarray | buffer]]] )\n\n"
             "Create new typed array")
    },
    {
        "tarray/buffer", cfun_typed_array_buffer,
        JDOC("(tarray/buffer (array | size) )\n\n"
             "return typed array buffer or create a new buffer ")
    },
    {
        "tarray/length", cfun_typed_array_size,
        JDOC("(tarray/length (array | buffer) )\n\n"
             "return typed array or buffer size ")
    },
    {
        "tarray/properties", cfun_typed_array_properties,
        JDOC("(tarray/properties array )\n\n"
             "return typed array properties as a struct")
    },

    {NULL, NULL, NULL}
};


/* Module entry point */
void janet_lib_typed_array(JanetTable *env) {
    janet_core_cfuns(env, NULL, ta_cfuns);
}

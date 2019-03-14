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

typedef uint8_t ta_uint8_t;
typedef int8_t ta_int8_t;
typedef uint16_t ta_uint16_t;
typedef int16_t ta_int16_t;
typedef uint32_t ta_uint32_t;
typedef int32_t ta_int32_t;
typedef float ta_float32_t;
typedef double ta_float64_t;
#ifdef JANET_BIGINT
typedef uint64_t ta_uint64_t;
typedef int64_t ta_int64_t;
#endif

static char *ta_type_names[] = {
    "uint8",
    "int8",
    "uint16",
    "int16",
    "uint32",
    "int32",
#ifdef JANET_BIGINT
    "uint64",
    "int64",
#endif
    "float32",
    "float64",
    "any"
};

static size_t ta_type_sizes[] = {
    sizeof(ta_uint8_t),
    sizeof(ta_int8_t),
    sizeof(ta_uint16_t),
    sizeof(ta_int16_t),
    sizeof(ta_uint32_t),
    sizeof(ta_int32_t),
#ifdef JANET_BIGINT
    sizeof(ta_uint64_t),
    sizeof(ta_int64_t),
#endif
    sizeof(ta_float32_t),
    sizeof(ta_float64_t),
    0
};

#define TA_COUNT_TYPES (JANET_TARRAY_TYPE_float64 + 1)
#define TA_ATOM_MAXSIZE 8
#define TA_FLAG_BIG_ENDIAN 1

static JanetTArrayType get_ta_type_by_name(const uint8_t *name) {
    for (int i = 0; i < TA_COUNT_TYPES; i++) {
        if (!janet_cstrcmp(name, ta_type_names[i]))
            return i;
    }
    janet_panicf("invalid typed array type %S", name);
    return 0;
}

static JanetTArrayBuffer *ta_buffer_init(JanetTArrayBuffer *buf, size_t size) {
    buf->data = NULL;
    if (size > 0) {
        buf->data = (uint8_t *)calloc(size, sizeof(uint8_t));
        if (buf->data == NULL) {
            JANET_OUT_OF_MEMORY;
        }
    }
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
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)p;
    free(buf->data);
    return 0;
}

static void ta_buffer_marshal(void *p, JanetMarshalContext *ctx) {
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)p;
    janet_marshal_size(ctx, buf->size);
    janet_marshal_int(ctx, buf->flags);
    janet_marshal_bytes(ctx, buf->data, buf->size);
}

static void ta_buffer_unmarshal(void *p, JanetMarshalContext *ctx) {
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)p;
    size_t size;
    janet_unmarshal_size(ctx, &size);
    ta_buffer_init(buf, size);
    janet_unmarshal_int(ctx, &(buf->flags));
    janet_unmarshal_bytes(ctx, buf->data, size);
}

static const JanetAbstractType ta_buffer_type = {
    "ta/buffer",
    ta_buffer_gc,
    NULL,
    NULL,
    NULL,
    ta_buffer_marshal,
    ta_buffer_unmarshal,
};

static int ta_mark(void *p, size_t s) {
    (void) s;
    JanetTArrayView *view = (JanetTArrayView *)p;
    janet_mark(janet_wrap_abstract(view->buffer));
    return 0;
}

static void ta_view_marshal(void *p, JanetMarshalContext *ctx) {
    JanetTArrayView *view = (JanetTArrayView *)p;
    size_t offset = (view->buffer->data - (uint8_t *)(view->data));
    janet_marshal_size(ctx, view->size);
    janet_marshal_size(ctx, view->stride);
    janet_marshal_int(ctx, view->type);
    janet_marshal_size(ctx, offset);
    janet_marshal_janet(ctx, janet_wrap_abstract(view->buffer));
}

static void ta_view_unmarshal(void *p, JanetMarshalContext *ctx) {
    JanetTArrayView *view = (JanetTArrayView *)p;
    size_t offset;
    int32_t atype;
    Janet buffer;
    janet_unmarshal_size(ctx, &(view->size));
    janet_unmarshal_size(ctx, &(view->stride));
    janet_unmarshal_int(ctx, &atype);
    if (atype < 0 || atype >= TA_COUNT_TYPES)
        janet_panic("bad typed array type");
    view->type = atype;
    janet_unmarshal_size(ctx, &offset);
    janet_unmarshal_janet(ctx, &buffer);
    if (!janet_checktype(buffer, JANET_ABSTRACT) ||
            (janet_abstract_type(janet_unwrap_abstract(buffer)) != &ta_buffer_type)) {
        janet_panicf("expected typed array buffer");
    }
    view->buffer = (JanetTArrayBuffer *)janet_unwrap_abstract(buffer);
    size_t buf_need_size = offset + (janet_tarray_type_size(view->type)) * ((view->size - 1) * view->stride + 1);
    if (view->buffer->size < buf_need_size)
        janet_panic("bad typed array offset in marshalled data");
    view->data = view->buffer->data + offset;
}

#define DEFINE_VIEW_TYPE(thetype) \
  typedef struct { \
    JanetTArrayBuffer *buffer; \
    ta_##thetype##_t *data; \
    size_t size; \
    size_t stride; \
    JanetTArrayType type; \
  } TA_View_##thetype ;

#define DEFINE_VIEW_GETTER(type) \
static Janet ta_get_##type(void *p, Janet key) { \
  Janet value;  \
  size_t index; \
  if (!janet_checksize(key))      \
    janet_panic("expected size as key");     \
  index = (size_t)janet_unwrap_number(key);\
  TA_View_##type *array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    value = janet_wrap_nil(); \
  } else { \
    value = janet_wrap_number(array->data[index*array->stride]); \
  } \
  return value; \
}

#define DEFINE_VIEW_GETTER_BIGINT(type) \
static Janet ta_get_##type(void *p, Janet key) { \
  Janet value;  \
  size_t index; \
  if (!janet_checksize(key))      \
    janet_panic("expected size as key");     \
  index = (size_t)janet_unwrap_number(key);\
  TA_View_##type *array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    value = janet_wrap_nil(); \
  } else { \
    value = janet_bigint_##type(array->data[index*array->stride]); \
  } \
  return value; \
}


#define DEFINE_VIEW_SETTER(type) \
void ta_put_##type(void *p, Janet key,Janet value) { \
  size_t index;\
  if (!janet_checksize(key))\
    janet_panic("expected size as key"); \
  if (!janet_checktype(value,JANET_NUMBER)) \
    janet_panic("expected number value"); \
  index = (size_t)janet_unwrap_number(key); \
  TA_View_##type *array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    janet_panic("index out of bounds"); \
  } \
  array->data[index*array->stride]=(ta_##type##_t)janet_unwrap_number(value); \
}

#define DEFINE_VIEW_SETTER_BIGINT(type) \
void ta_put_##type(void *p, Janet key,Janet value) { \
  size_t index;\
  if (!janet_checksize(key))\
    janet_panic("expected size as key"); \
  index = (size_t)janet_unwrap_number(key); \
  TA_View_##type *array=(TA_View_##type *)p; \
  if (index >= array->size) { \
    janet_panic("index out of bounds"); \
  } \
  array->data[index*array->stride]=(ta_##type##_t)janet_checkbigint_##type(value); \
}


#define DEFINE_VIEW_INITIALIZER(thetype) \
  static JanetTArrayView *ta_init_##thetype(JanetTArrayView *view, \
          JanetTArrayBuffer *buf, size_t size, \
          size_t offset, size_t stride) { \
  if ((stride<1) || (size <1)) {                    \
      janet_panic("stride and size should be > 0");     \
  }; \
  TA_View_##thetype * tview=(TA_View_##thetype *) view; \
  size_t buf_size=offset+(sizeof(ta_##thetype##_t))*((size-1)*stride+1);    \
  if (buf==NULL) {  \
    buf=(JanetTArrayBuffer *)janet_abstract(&ta_buffer_type,sizeof(JanetTArrayBuffer)); \
    ta_buffer_init(buf,buf_size); \
  } \
  if (buf->size<buf_size) { \
    janet_panicf("bad buffer size, %i bytes allocated < %i required",buf->size,buf_size); \
  } \
  tview->buffer=buf; \
  tview->stride=stride; \
  tview->size=size; \
  tview->data=(ta_##thetype##_t *)(buf->data+offset);  \
  tview->type=JANET_TARRAY_TYPE_##thetype; \
  return view; \
};

#define BUILD_TYPE(type) \
DEFINE_VIEW_TYPE(type)   \
DEFINE_VIEW_GETTER(type)  \
DEFINE_VIEW_SETTER(type) \
DEFINE_VIEW_INITIALIZER(type)

#define BUILD_TYPE_BIGINT(type) \
DEFINE_VIEW_TYPE(type)   \
DEFINE_VIEW_GETTER_BIGINT(type)  \
DEFINE_VIEW_SETTER_BIGINT(type) \
DEFINE_VIEW_INITIALIZER(type)

BUILD_TYPE(uint8)
BUILD_TYPE(int8)
BUILD_TYPE(uint16)
BUILD_TYPE(int16)
BUILD_TYPE(uint32)
BUILD_TYPE(int32)
#ifdef JANET_BIGINT
BUILD_TYPE_BIGINT(uint64)
BUILD_TYPE_BIGINT(int64)
#endif
BUILD_TYPE(float32)
BUILD_TYPE(float64)

#undef DEFINE_VIEW_TYPE
#undef DEFINE_VIEW_GETTER
#undef DEFINE_VIEW_SETTER
#undef DEFINE_VIEW_GETTER_BIGINT
#undef DEFINE_VIEW_SETTER_BIGINT
#undef DEFINE_VIEW_INITIALIZER

#define DEFINE_VIEW_ABSTRACT_TYPE(type) \
{ \
  "ta/"#type, \
  NULL, \
  ta_mark, \
  ta_get_##type, \
  ta_put_##type, \
  ta_view_marshal, \
  ta_view_unmarshal \
}

static const JanetAbstractType ta_array_types[] = {
    DEFINE_VIEW_ABSTRACT_TYPE(uint8),
    DEFINE_VIEW_ABSTRACT_TYPE(int8),
    DEFINE_VIEW_ABSTRACT_TYPE(uint16),
    DEFINE_VIEW_ABSTRACT_TYPE(int16),
    DEFINE_VIEW_ABSTRACT_TYPE(uint32),
    DEFINE_VIEW_ABSTRACT_TYPE(int32),
#ifdef JANET_BIGINT
    DEFINE_VIEW_ABSTRACT_TYPE(uint64),
    DEFINE_VIEW_ABSTRACT_TYPE(int64),
#endif
    DEFINE_VIEW_ABSTRACT_TYPE(float32),
    DEFINE_VIEW_ABSTRACT_TYPE(float64)
};

#undef DEFINE_VIEW_ABSTRACT_TYPE

static int is_ta_anytype(Janet x) {
    if (janet_checktype(x, JANET_ABSTRACT)) {
        const JanetAbstractType *at = janet_abstract_type(janet_unwrap_abstract(x));
        for (size_t i = 0; i < TA_COUNT_TYPES; i++) {
            if (at == ta_array_types + i) return 1;
        }
    }
    return 0;
}

static int is_ta_type(Janet x, JanetTArrayType type) {
    return janet_checktype(x, JANET_ABSTRACT) &&
           (type < TA_COUNT_TYPES) &&
           (janet_abstract_type(janet_unwrap_abstract(x)) == &ta_array_types[type]);
}

#define CASE_TYPE_INITIALIZE(type) case JANET_TARRAY_TYPE_##type: \
    ta_init_##type(view,buffer,size,offset,stride); break

JanetTArrayBuffer *janet_tarray_buffer(size_t size) {
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)janet_abstract(&ta_buffer_type, sizeof(JanetTArrayBuffer));
    ta_buffer_init(buf, size);
    return buf;
}

JanetTArrayView *janet_tarray_view(JanetTArrayType type, size_t size, size_t stride, size_t offset, JanetTArrayBuffer *buffer) {
    JanetTArrayView *view = janet_abstract(&ta_array_types[type], sizeof(JanetTArrayView));
    switch (type) {
            CASE_TYPE_INITIALIZE(uint8);
            CASE_TYPE_INITIALIZE(int8);
            CASE_TYPE_INITIALIZE(uint16);
            CASE_TYPE_INITIALIZE(int16);
            CASE_TYPE_INITIALIZE(uint32);
            CASE_TYPE_INITIALIZE(int32);
#ifdef JANET_BIGINT
            CASE_TYPE_INITIALIZE(uint64);
            CASE_TYPE_INITIALIZE(int64);
#endif
            CASE_TYPE_INITIALIZE(float32);
            CASE_TYPE_INITIALIZE(float64);
        default :
            janet_panic("bad typed array type");
    }
    return view;
}

#undef CASE_TYPE_INITIALIZE

JanetTArrayBuffer *janet_gettarray_buffer(const Janet *argv, int32_t n) {
    return (JanetTArrayBuffer *)janet_getabstract(argv, n, &ta_buffer_type);
}

int janet_is_tarray_view(Janet x, JanetTArrayType type) {
    return (type == JANET_TARRAY_TYPE_any) ? is_ta_anytype(x) : is_ta_type(x, type);
}

size_t janet_tarray_type_size(JanetTArrayType type) {
    return (type < TA_COUNT_TYPES) ? ta_type_sizes[type] : 0;
}

JanetTArrayView *janet_gettarray_view(const Janet *argv, int32_t n, JanetTArrayType type) {
    if (janet_is_tarray_view(argv[n], type)) {
        return (JanetTArrayView *)janet_unwrap_abstract(argv[n]);
    } else {
        janet_panicf("bad slot #%d, expected typed array of type %s, got %v",
                     n, (type <= JANET_TARRAY_TYPE_any) ? ta_type_names[type] : "?", argv[n]);
        return NULL;
    }
}

static Janet cfun_typed_array_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 5);
    size_t offset = 0;
    size_t stride = 1;
    JanetTArrayBuffer *buffer = NULL;
    const uint8_t *keyw = janet_getkeyword(argv, 0);
    JanetTArrayType type = get_ta_type_by_name(keyw);
    size_t size = janet_getsize(argv, 1);
    if (argc > 2)
        stride = janet_getsize(argv, 2);
    if (argc > 3)
        offset = janet_getsize(argv, 3);
    if (argc > 4) {
        if (is_ta_anytype(argv[4])) {
            JanetTArrayView *view = (JanetTArrayView *)janet_unwrap_abstract(argv[4]);
            offset = (view->buffer->data - (uint8_t *)(view->data)) + offset * ta_type_sizes[view->type];
            stride *= view->stride;
            buffer = view->buffer;
        } else {
            buffer = (JanetTArrayBuffer *)janet_getabstract(argv, 4, &ta_buffer_type);
        }
    }
    JanetTArrayView *view = janet_tarray_view(type, size, stride, offset, buffer);
    return janet_wrap_abstract(view);
}

static Janet cfun_typed_array_buffer(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (is_ta_anytype(argv[0])) {
        JanetTArrayView *view = (JanetTArrayView *)janet_unwrap_abstract(argv[0]);
        return janet_wrap_abstract(view->buffer);
    }
    size_t size = janet_getsize(argv, 0);
    JanetTArrayBuffer *buf = janet_tarray_buffer(size);
    return janet_wrap_abstract(buf);
}

static Janet cfun_typed_array_size(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (is_ta_anytype(argv[0])) {
        JanetTArrayView *view = (JanetTArrayView *)janet_unwrap_abstract(argv[0]);
        return janet_wrap_number((double) view->size);
    }
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)janet_getabstract(argv, 0, &ta_buffer_type);
    return janet_wrap_number((double) buf->size);
}

static Janet cfun_typed_array_properties(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    if (is_ta_anytype(argv[0])) {
        JanetTArrayView *view = (JanetTArrayView *)janet_unwrap_abstract(argv[0]);
        JanetKV *props = janet_struct_begin(6);
        ptrdiff_t boffset = (uint8_t *)(view->data) - view->buffer->data;
        janet_struct_put(props, janet_ckeywordv("size"),
                         janet_wrap_number((double) view->size));
        janet_struct_put(props, janet_ckeywordv("byte-offset"),
                         janet_wrap_number((double) boffset));
        janet_struct_put(props, janet_ckeywordv("stride"),
                         janet_wrap_number((double) view->stride));
        janet_struct_put(props, janet_ckeywordv("type"),
                         janet_ckeywordv(ta_type_names[view->type]));
        janet_struct_put(props, janet_ckeywordv("type-size"),
                         janet_wrap_number((double) ta_type_sizes[view->type]));
        janet_struct_put(props, janet_ckeywordv("buffer"),
                         janet_wrap_abstract(view->buffer));
        return janet_wrap_struct(janet_struct_end(props));
    } else {
        JanetTArrayBuffer *buffer = janet_gettarray_buffer(argv, 0);
        JanetKV *props = janet_struct_begin(2);
        janet_struct_put(props, janet_ckeywordv("size"),
                         janet_wrap_number((double) buffer->size));
        janet_struct_put(props, janet_ckeywordv("big-endian"),
                         janet_wrap_boolean(buffer->flags & TA_FLAG_BIG_ENDIAN));
        return janet_wrap_struct(janet_struct_end(props));
    }
}

static Janet cfun_typed_array_slice(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetTArrayView *src = janet_gettarray_view(argv, 0, JANET_TARRAY_TYPE_any);
    const JanetAbstractType *at = janet_abstract_type(janet_unwrap_abstract(argv[0]));
    JanetRange range;
    int32_t length = (int32_t)src->size;
    if (argc == 1) {
        range.start = 0;
        range.end = length;
    } else if (argc == 2) {
        range.start = janet_gethalfrange(argv, 1, length, "start");
        range.end = length;
    } else {
        range.start = janet_gethalfrange(argv, 1, length, "start");
        range.end = janet_gethalfrange(argv, 2, length, "end");
        if (range.end < range.start)
            range.end = range.start;
    }
    JanetArray *array = janet_array(range.end - range.start);
    if (array->data) {
        for (int32_t i = range.start; i < range.end; i++) {
            array->data[i - range.start] = at->get(src, janet_wrap_number(i));
        }
    }
    array->count = range.end - range.start;
    return janet_wrap_array(array);
}

static Janet cfun_typed_array_copy_bytes(int32_t argc, Janet *argv) {
    janet_arity(argc, 4, 5);
    JanetTArrayView *src = janet_gettarray_view(argv, 0, JANET_TARRAY_TYPE_any);
    size_t index_src = janet_getsize(argv, 1);
    JanetTArrayView *dst = janet_gettarray_view(argv, 2, JANET_TARRAY_TYPE_any);
    size_t index_dst = janet_getsize(argv, 3);
    size_t count = (argc == 5) ? janet_getsize(argv, 4) : 1;
    size_t src_atom_size = ta_type_sizes[src->type];
    size_t dst_atom_size = ta_type_sizes[dst->type];
    size_t step_src = src->stride * src_atom_size;
    size_t step_dst = dst->stride * dst_atom_size;
    size_t pos_src = ((uint8_t *)(src->data) - src->buffer->data) + (index_src * step_src);
    size_t pos_dst = ((uint8_t *)(dst->data) - dst->buffer->data) + (index_dst * step_dst);
    uint8_t *ps = src->buffer->data + pos_src, * pd = dst->buffer->data + pos_dst;
    if ((pos_dst + (count - 1)*step_dst + src_atom_size <= dst->buffer->size) &&
            (pos_src + (count - 1)*step_src + src_atom_size <= src->buffer->size)) {
        for (size_t i = 0; i < count; i++) {
            memmove(pd, ps, src_atom_size);
            pd += step_dst;
            ps += step_src;
        }
    } else {
        janet_panic("typed array copy out of bounds");
    }
    return janet_wrap_nil();
}

static Janet cfun_typed_array_swap_bytes(int32_t argc, Janet *argv) {
    janet_arity(argc, 4, 5);
    JanetTArrayView *src = janet_gettarray_view(argv, 0, JANET_TARRAY_TYPE_any);
    size_t index_src = janet_getsize(argv, 1);
    JanetTArrayView *dst = janet_gettarray_view(argv, 2, JANET_TARRAY_TYPE_any);
    size_t index_dst = janet_getsize(argv, 3);
    size_t count = (argc == 5) ? janet_getsize(argv, 4) : 1;
    size_t src_atom_size = ta_type_sizes[src->type];
    size_t dst_atom_size = ta_type_sizes[dst->type];
    size_t step_src = src->stride * src_atom_size;
    size_t step_dst = dst->stride * dst_atom_size;
    size_t pos_src = ((uint8_t *)(src->data) - src->buffer->data) + (index_src * step_src);
    size_t pos_dst = ((uint8_t *)(dst->data) - dst->buffer->data) + (index_dst * step_dst);
    uint8_t *ps = src->buffer->data + pos_src, * pd = dst->buffer->data + pos_dst;
    uint8_t temp[TA_ATOM_MAXSIZE];
    if ((pos_dst + (count - 1)*step_dst + src_atom_size <= dst->buffer->size) &&
            (pos_src + (count - 1)*step_src + src_atom_size <= src->buffer->size)) {
        for (size_t i = 0; i < count; i++) {
            memcpy(temp, ps, src_atom_size);
            memcpy(ps, pd, src_atom_size);
            memcpy(pd, temp, src_atom_size);
            pd += step_dst;
            ps += step_src;
        }
    } else {
        janet_panic("typed array swap out of bounds");
    }
    return janet_wrap_nil();
}

static const JanetReg ta_cfuns[] = {
    {
        "tarray/new", cfun_typed_array_new,
        JDOC("(tarray/new type size [stride = 1 [offset = 0 [tarray | buffer]]] )\n\n"
             "Create new typed array.")
    },
    {
        "tarray/buffer", cfun_typed_array_buffer,
        JDOC("(tarray/buffer (array | size) )\n\n"
             "Return typed array buffer or create a new buffer.")
    },
    {
        "tarray/length", cfun_typed_array_size,
        JDOC("(tarray/length (array | buffer) )\n\n"
             "Return typed array or buffer size.")
    },
    {
        "tarray/properties", cfun_typed_array_properties,
        JDOC("(tarray/properties array )\n\n"
             "Return typed array properties as a struct.")
    },
    {
        "tarray/copy-bytes", cfun_typed_array_copy_bytes,
        JDOC("(tarray/copy-bytes src sindex dst dindex [count=1])\n\n"
             "Copy count elements of src array from index sindex "
             "to dst array at position dindex "
             "memory can overlap.")
    },
    {
        "tarray/swap-bytes", cfun_typed_array_swap_bytes,
        JDOC("(tarray/swap-bytes src sindex dst dindex [count=1])\n\n"
             "Swap count elements between src array from index sindex "
             "and dst array at position dindex "
             "memory can overlap.")
    },
    {
        "tarray/slice", cfun_typed_array_slice,
        JDOC("(tarray/slice tarr [, start=0 [, end=(size tarr)]])\n\n"
             "Takes a slice of a typed array from start to end. The range is half "
             "open, [start, end). Indexes can also be negative, indicating indexing "
             "from the end of the end of the typed array. By default, start is 0 and end is "
             "the size of the typed array. Returns a new janet array.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_typed_array(JanetTable *env) {
    janet_core_cfuns(env, NULL, ta_cfuns);
    janet_register_abstract_type(&ta_buffer_type);
    for (int i = 0; i < TA_COUNT_TYPES; i++) {
        janet_register_abstract_type(ta_array_types + i);
    }
}

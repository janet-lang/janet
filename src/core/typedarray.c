/*
* Copyright (c) 2020 Calvin Rose & contributors
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#endif

#ifdef JANET_TYPED_ARRAY

static char *ta_type_names[] = {
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
    "?"
};

static size_t ta_type_sizes[] = {
    sizeof(uint8_t),
    sizeof(int8_t),
    sizeof(uint16_t),
    sizeof(int16_t),
    sizeof(uint32_t),
    sizeof(int32_t),
    sizeof(uint64_t),
    sizeof(int64_t),
    sizeof(float),
    sizeof(double),
    0
};

#define TA_COUNT_TYPES (JANET_TARRAY_TYPE_F64 + 1)
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
    janet_marshal_abstract(ctx, p);
    janet_marshal_size(ctx, buf->size);
    janet_marshal_int(ctx, buf->flags);
    janet_marshal_bytes(ctx, buf->data, buf->size);
}

static void *ta_buffer_unmarshal(JanetMarshalContext *ctx) {
    JanetTArrayBuffer *buf = janet_unmarshal_abstract(ctx, sizeof(JanetTArrayBuffer));
    size_t size = janet_unmarshal_size(ctx);
    int32_t flags = janet_unmarshal_int(ctx);
    ta_buffer_init(buf, size);
    buf->flags = flags;
    janet_unmarshal_bytes(ctx, buf->data, size);
    return buf;
}

const JanetAbstractType janet_ta_buffer_type = {
    "ta/buffer",
    ta_buffer_gc,
    NULL,
    NULL,
    NULL,
    ta_buffer_marshal,
    ta_buffer_unmarshal,
    JANET_ATEND_UNMARSHAL
};

static int ta_mark(void *p, size_t s) {
    (void) s;
    JanetTArrayView *view = (JanetTArrayView *)p;
    janet_mark(janet_wrap_abstract(view->buffer));
    return 0;
}

static void ta_view_marshal(void *p, JanetMarshalContext *ctx) {
    JanetTArrayView *view = (JanetTArrayView *)p;
    size_t offset = (view->buffer->data - view->as.u8);
    janet_marshal_abstract(ctx, p);
    janet_marshal_size(ctx, view->size);
    janet_marshal_size(ctx, view->stride);
    janet_marshal_int(ctx, view->type);
    janet_marshal_size(ctx, offset);
    janet_marshal_janet(ctx, janet_wrap_abstract(view->buffer));
}

static void *ta_view_unmarshal(JanetMarshalContext *ctx) {
    size_t offset;
    int32_t atype;
    Janet buffer;
    JanetTArrayView *view = janet_unmarshal_abstract(ctx, sizeof(JanetTArrayView));
    view->size = janet_unmarshal_size(ctx);
    view->stride = janet_unmarshal_size(ctx);
    atype = janet_unmarshal_int(ctx);
    if (atype < 0 || atype >= TA_COUNT_TYPES)
        janet_panic("bad typed array type");
    view->type = atype;
    offset = janet_unmarshal_size(ctx);
    buffer = janet_unmarshal_janet(ctx);
    if (!janet_checktype(buffer, JANET_ABSTRACT) ||
            (janet_abstract_type(janet_unwrap_abstract(buffer)) != &janet_ta_buffer_type)) {
        janet_panicf("expected typed array buffer");
    }
    view->buffer = (JanetTArrayBuffer *)janet_unwrap_abstract(buffer);
    size_t buf_need_size = offset + (ta_type_sizes[view->type]) * ((view->size - 1) * view->stride + 1);
    if (view->buffer->size < buf_need_size)
        janet_panic("bad typed array offset in marshalled data");
    view->as.u8 = view->buffer->data + offset;
    return view;
}

static JanetMethod tarray_view_methods[6];

static int ta_getter(void *p, Janet key, Janet *out) {
    size_t index, i;
    JanetTArrayView *array = p;
    if (janet_checktype(key, JANET_KEYWORD)) {
        return janet_getmethod(janet_unwrap_keyword(key), tarray_view_methods, out);
    }
    if (!janet_checksize(key)) janet_panic("expected size as key");
    index = (size_t) janet_unwrap_number(key);
    i = index * array->stride;
    if (index >= array->size) {
        return 0;
    } else {
        switch (array->type) {
            case JANET_TARRAY_TYPE_U8:
                *out = janet_wrap_number(array->as.u8[i]);
                break;
            case JANET_TARRAY_TYPE_S8:
                *out = janet_wrap_number(array->as.s8[i]);
                break;
            case JANET_TARRAY_TYPE_U16:
                *out = janet_wrap_number(array->as.u16[i]);
                break;
            case JANET_TARRAY_TYPE_S16:
                *out = janet_wrap_number(array->as.s16[i]);
                break;
            case JANET_TARRAY_TYPE_U32:
                *out = janet_wrap_number(array->as.u32[i]);
                break;
            case JANET_TARRAY_TYPE_S32:
                *out = janet_wrap_number(array->as.s32[i]);
                break;
#ifdef JANET_INT_TYPES
            case JANET_TARRAY_TYPE_U64:
                *out = janet_wrap_u64(array->as.u64[i]);
                break;
            case JANET_TARRAY_TYPE_S64:
                *out = janet_wrap_s64(array->as.s64[i]);
                break;
#endif
            case JANET_TARRAY_TYPE_F32:
                *out = janet_wrap_number_safe(array->as.f32[i]);
                break;
            case JANET_TARRAY_TYPE_F64:
                *out = janet_wrap_number_safe(array->as.f64[i]);
                break;
            default:
                janet_panicf("cannot get from typed array of type %s",
                             ta_type_names[array->type]);
                break;
        }
    }
    return 1;
}

static void ta_setter(void *p, Janet key, Janet value) {
    size_t index, i;
    if (!janet_checksize(key)) janet_panic("expected size as key");
    index = (size_t) janet_unwrap_number(key);
    JanetTArrayView *array = p;
    i = index * array->stride;
    if (index >= array->size) {
        janet_panic("index out of bounds");
    }
    if (!janet_checktype(value, JANET_NUMBER) &&
            array->type != JANET_TARRAY_TYPE_U64 &&
            array->type != JANET_TARRAY_TYPE_S64) {
        janet_panic("expected number value");
    }
    switch (array->type) {
        case JANET_TARRAY_TYPE_U8:
            array->as.u8[i] = (uint8_t) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_S8:
            array->as.s8[i] = (int8_t) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_U16:
            array->as.u16[i] = (uint16_t) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_S16:
            array->as.s16[i] = (int16_t) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_U32:
            array->as.u32[i] = (uint32_t) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_S32:
            array->as.s32[i] = (int32_t) janet_unwrap_number(value);
            break;
#ifdef JANET_INT_TYPES
        case JANET_TARRAY_TYPE_U64:
            array->as.u64[i] = janet_unwrap_u64(value);
            break;
        case JANET_TARRAY_TYPE_S64:
            array->as.s64[i] = janet_unwrap_s64(value);
            break;
#endif
        case JANET_TARRAY_TYPE_F32:
            array->as.f32[i] = (float) janet_unwrap_number(value);
            break;
        case JANET_TARRAY_TYPE_F64:
            array->as.f64[i] = janet_unwrap_number(value);
            break;
        default:
            janet_panicf("cannot set typed array of type %s",
                         ta_type_names[array->type]);
            break;
    }
}

static Janet ta_view_next(void *p, Janet key) {
    JanetTArrayView *view = p;
    if (janet_checktype(key, JANET_NIL)) {
        if (view->size > 0) {
            return janet_wrap_number(0);
        } else {
            return janet_wrap_nil();
        }
    }
    if (!janet_checksize(key)) janet_panic("expected size as key");
    size_t index = (size_t) janet_unwrap_number(key);
    index++;
    if (index < view->size) {
        return janet_wrap_number((double) index);
    }
    return janet_wrap_nil();
}

const JanetAbstractType janet_ta_view_type = {
    "ta/view",
    NULL,
    ta_mark,
    ta_getter,
    ta_setter,
    ta_view_marshal,
    ta_view_unmarshal,
    NULL,
    NULL,
    NULL,
    ta_view_next,
    JANET_ATEND_NEXT
};

JanetTArrayBuffer *janet_tarray_buffer(size_t size) {
    JanetTArrayBuffer *buf = janet_abstract(&janet_ta_buffer_type, sizeof(JanetTArrayBuffer));
    ta_buffer_init(buf, size);
    return buf;
}

JanetTArrayView *janet_tarray_view(
    JanetTArrayType type,
    size_t size,
    size_t stride,
    size_t offset,
    JanetTArrayBuffer *buffer) {

    JanetTArrayView *view = janet_abstract(&janet_ta_view_type, sizeof(JanetTArrayView));

    if ((stride < 1) || (size < 1)) janet_panic("stride and size should be > 0");
    size_t buf_size = offset + ta_type_sizes[type] * ((size - 1) * stride + 1);

    if (NULL == buffer) {
        buffer = janet_abstract(&janet_ta_buffer_type, sizeof(JanetTArrayBuffer));
        ta_buffer_init(buffer, buf_size);
    }

    if (buffer->size < buf_size) {
        janet_panicf("bad buffer size, %i bytes allocated < %i required",
                     buffer->size,
                     buf_size);
    }

    view->buffer = buffer;
    view->stride = stride;
    view->size = size;
    view->as.u8 = buffer->data + offset;
    view->type = type;

    return view;
}

JanetTArrayBuffer *janet_gettarray_buffer(const Janet *argv, int32_t n) {
    return janet_getabstract(argv, n, &janet_ta_buffer_type);
}

JanetTArrayView *janet_gettarray_any(const Janet *argv, int32_t n) {
    return janet_getabstract(argv, n, &janet_ta_view_type);
}

JanetTArrayView *janet_gettarray_view(const Janet *argv, int32_t n, JanetTArrayType type) {
    JanetTArrayView *view = janet_getabstract(argv, n, &janet_ta_view_type);
    if (view->type != type) {
        janet_panicf("bad slot #%d, expected typed array of type %s, got %v",
                     n, ta_type_names[type], argv[n]);
    }
    return view;
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
        if (!janet_checktype(argv[4], JANET_ABSTRACT)) {
            janet_panicf("bad slot #%d, expected ta/view|ta/buffer, got %v",
                         4, argv[4]);
        }
        void *p = janet_unwrap_abstract(argv[4]);
        if (janet_abstract_type(p) == &janet_ta_view_type) {
            JanetTArrayView *view = (JanetTArrayView *)p;
            offset = (view->buffer->data - view->as.u8) + offset * ta_type_sizes[view->type];
            stride *= view->stride;
            buffer = view->buffer;
        } else if (janet_abstract_type(p) == &janet_ta_buffer_type) {
            buffer = p;
        } else {
            janet_panicf("bad slot #%d, expected ta/view|ta/buffer, got %v",
                         4, argv[4]);
        }
    }
    JanetTArrayView *view = janet_tarray_view(type, size, stride, offset, buffer);
    return janet_wrap_abstract(view);
}

static JanetTArrayView *ta_is_view(Janet x) {
    if (!janet_checktype(x, JANET_ABSTRACT)) return NULL;
    void *abst = janet_unwrap_abstract(x);
    if (janet_abstract_type(abst) != &janet_ta_view_type) return NULL;
    return (JanetTArrayView *)abst;
}

static Janet cfun_typed_array_buffer(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTArrayView *view;
    if ((view = ta_is_view(argv[0]))) {
        return janet_wrap_abstract(view->buffer);
    }
    size_t size = janet_getsize(argv, 0);
    JanetTArrayBuffer *buf = janet_tarray_buffer(size);
    return janet_wrap_abstract(buf);
}

static Janet cfun_typed_array_size(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTArrayView *view;
    if ((view = ta_is_view(argv[0]))) {
        return janet_wrap_number((double) view->size);
    }
    JanetTArrayBuffer *buf = (JanetTArrayBuffer *)janet_getabstract(argv, 0, &janet_ta_buffer_type);
    return janet_wrap_number((double) buf->size);
}

static Janet cfun_typed_array_properties(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTArrayView *view;
    if ((view = ta_is_view(argv[0]))) {
        JanetTArrayView *view = janet_unwrap_abstract(argv[0]);
        JanetKV *props = janet_struct_begin(6);
        ptrdiff_t boffset = view->as.u8 - view->buffer->data;
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
    JanetTArrayView *src = janet_getabstract(argv, 0, &janet_ta_view_type);
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
            if (!ta_getter(src, janet_wrap_number(i), &array->data[i - range.start]))
                array->data[i - range.start] = janet_wrap_nil();
        }
    }
    array->count = range.end - range.start;
    return janet_wrap_array(array);
}

static Janet cfun_typed_array_copy_bytes(int32_t argc, Janet *argv) {
    janet_arity(argc, 4, 5);
    JanetTArrayView *src = janet_getabstract(argv, 0, &janet_ta_view_type);
    size_t index_src = janet_getsize(argv, 1);
    JanetTArrayView *dst = janet_getabstract(argv, 2, &janet_ta_view_type);
    size_t index_dst = janet_getsize(argv, 3);
    size_t count = (argc == 5) ? janet_getsize(argv, 4) : 1;
    size_t src_atom_size = ta_type_sizes[src->type];
    size_t dst_atom_size = ta_type_sizes[dst->type];
    size_t step_src = src->stride * src_atom_size;
    size_t step_dst = dst->stride * dst_atom_size;
    size_t pos_src = (src->as.u8 - src->buffer->data) + (index_src * step_src);
    size_t pos_dst = (dst->as.u8 - dst->buffer->data) + (index_dst * step_dst);
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
    JanetTArrayView *src = janet_getabstract(argv, 0, &janet_ta_view_type);
    size_t index_src = janet_getsize(argv, 1);
    JanetTArrayView *dst = janet_getabstract(argv, 2, &janet_ta_view_type);
    size_t index_dst = janet_getsize(argv, 3);
    size_t count = (argc == 5) ? janet_getsize(argv, 4) : 1;
    size_t src_atom_size = ta_type_sizes[src->type];
    size_t dst_atom_size = ta_type_sizes[dst->type];
    size_t step_src = src->stride * src_atom_size;
    size_t step_dst = dst->stride * dst_atom_size;
    size_t pos_src = (src->as.u8 - src->buffer->data) + (index_src * step_src);
    size_t pos_dst = (dst->as.u8 - dst->buffer->data) + (index_dst * step_dst);
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
        JDOC("(tarray/new type size &opt stride offset tarray|buffer)\n\n"
             "Create new typed array.")
    },
    {
        "tarray/buffer", cfun_typed_array_buffer,
        JDOC("(tarray/buffer array|size)\n\n"
             "Return typed array buffer or create a new buffer.")
    },
    {
        "tarray/length", cfun_typed_array_size,
        JDOC("(tarray/length array|buffer)\n\n"
             "Return typed array or buffer size.")
    },
    {
        "tarray/properties", cfun_typed_array_properties,
        JDOC("(tarray/properties array)\n\n"
             "Return typed array properties as a struct.")
    },
    {
        "tarray/copy-bytes", cfun_typed_array_copy_bytes,
        JDOC("(tarray/copy-bytes src sindex dst dindex &opt count)\n\n"
             "Copy count elements (default 1) of src array from index sindex "
             "to dst array at position dindex "
             "memory can overlap.")
    },
    {
        "tarray/swap-bytes", cfun_typed_array_swap_bytes,
        JDOC("(tarray/swap-bytes src sindex dst dindex &opt count)\n\n"
             "Swap count elements (default 1) between src array from index sindex "
             "and dst array at position dindex "
             "memory can overlap.")
    },
    {
        "tarray/slice", cfun_typed_array_slice,
        JDOC("(tarray/slice tarr &opt start end)\n\n"
             "Takes a slice of a typed array from start to end. The range is half "
             "open, [start, end). Indexes can also be negative, indicating indexing "
             "from the end of the end of the typed array. By default, start is 0 and end is "
             "the size of the typed array. Returns a new janet array.")
    },
    {NULL, NULL, NULL}
};

static JanetMethod tarray_view_methods[] = {
    {"length", cfun_typed_array_size},
    {"properties", cfun_typed_array_properties},
    {"copy-bytes", cfun_typed_array_copy_bytes},
    {"swap-bytes", cfun_typed_array_swap_bytes},
    {"slice", cfun_typed_array_slice},
    {NULL, NULL}
};

/* Module entry point */
void janet_lib_typed_array(JanetTable *env) {
    janet_core_cfuns(env, NULL, ta_cfuns);
    janet_register_abstract_type(&janet_ta_buffer_type);
    janet_register_abstract_type(&janet_ta_view_type);
}

#endif

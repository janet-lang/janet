/*
* Copyright (c) 2022 Calvin Rose
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

#ifdef JANET_FFI

#ifdef _MSC_VER
#define alloca _alloca
#elif defined(JANET_LINUX)
#include <alloca.h>
#elif !defined(alloca)
/* Last ditch effort to get alloca - works for gcc and clang */
#define alloca __builtin_alloca
#endif

#define JANET_FFI_MAX_RECUR 64

/* Compiler, OS, and arch detection. Used
 * to enable a set of calling conventions. The
 * :none calling convention is always enabled. */
#if defined(JANET_WINDOWS) && (defined(__x86_64__) || defined(_M_X64))
#define JANET_FFI_WIN64_ENABLED
#endif
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(JANET_WINDOWS)
#define JANET_FFI_SYSV64_ENABLED
#endif

typedef struct JanetFFIType JanetFFIType;
typedef struct JanetFFIStruct JanetFFIStruct;

typedef enum {
    JANET_FFI_TYPE_VOID,
    JANET_FFI_TYPE_BOOL,
    JANET_FFI_TYPE_PTR,
    JANET_FFI_TYPE_STRING,
    JANET_FFI_TYPE_FLOAT,
    JANET_FFI_TYPE_DOUBLE,
    JANET_FFI_TYPE_INT8,
    JANET_FFI_TYPE_UINT8,
    JANET_FFI_TYPE_INT16,
    JANET_FFI_TYPE_UINT16,
    JANET_FFI_TYPE_INT32,
    JANET_FFI_TYPE_UINT32,
    JANET_FFI_TYPE_INT64,
    JANET_FFI_TYPE_UINT64,
    JANET_FFI_TYPE_STRUCT
} JanetFFIPrimType;

/* Custom alignof since alignof not in c99 standard */
#define ALIGNOF(type) offsetof(struct { char c; type member; }, member)

typedef struct {
    size_t size;
    size_t align;
} JanetFFIPrimInfo;

static const JanetFFIPrimInfo janet_ffi_type_info[] = {
    {0, 0}, /* JANET_FFI_TYPE_VOID */
    {sizeof(char), ALIGNOF(char)}, /* JANET_FFI_TYPE_BOOL */
    {sizeof(void *), ALIGNOF(void *)}, /* JANET_FFI_TYPE_PTR */
    {sizeof(char *), ALIGNOF(char *)}, /* JANET_FFI_TYPE_STRING */
    {sizeof(float), ALIGNOF(float)}, /* JANET_FFI_TYPE_FLOAT */
    {sizeof(double), ALIGNOF(double)}, /* JANET_FFI_TYPE_DOUBLE */
    {sizeof(int8_t), ALIGNOF(int8_t)}, /* JANET_FFI_TYPE_INT8 */
    {sizeof(uint8_t), ALIGNOF(uint8_t)}, /* JANET_FFI_TYPE_UINT8 */
    {sizeof(int16_t), ALIGNOF(int16_t)}, /* JANET_FFI_TYPE_INT16 */
    {sizeof(uint16_t), ALIGNOF(uint16_t)}, /* JANET_FFI_TYPE_UINT16 */
    {sizeof(int32_t), ALIGNOF(int32_t)}, /* JANET_FFI_TYPE_INT32 */
    {sizeof(uint32_t), ALIGNOF(uint32_t)}, /* JANET_FFI_TYPE_UINT32 */
    {sizeof(int64_t), ALIGNOF(int64_t)}, /* JANET_FFI_TYPE_INT64 */
    {sizeof(uint64_t), ALIGNOF(uint64_t)}, /* JANET_FFI_TYPE_UINT64 */
    {0, ALIGNOF(uint64_t)} /* JANET_FFI_TYPE_STRUCT */
};

struct JanetFFIType {
    JanetFFIStruct *st;
    JanetFFIPrimType prim;
    int32_t array_count;
};

typedef struct {
    JanetFFIType type;
    size_t offset;
} JanetFFIStructMember;

/* Also used to store array types */
struct JanetFFIStruct {
    uint32_t size;
    uint32_t align;
    uint32_t field_count;
    uint32_t is_aligned;
    JanetFFIStructMember fields[];
};

/* Specifies how the registers are classified. This is used
 * to determine if a certain argument should be passed in a register,
 * on the stack, special floating pointer register, etc. */
typedef enum {
    JANET_SYSV64_INTEGER,
    JANET_SYSV64_SSE,
    JANET_SYSV64_SSEUP,
    JANET_SYSV64_X87,
    JANET_SYSV64_X87UP,
    JANET_SYSV64_COMPLEX_X87,
    JANET_SYSV64_NO_CLASS,
    JANET_SYSV64_MEMORY,
    JANET_WIN64_REGISTER,
    JANET_WIN64_STACK,
    JANET_WIN64_REGISTER_REF,
    JANET_WIN64_STACK_REF
} JanetFFIWordSpec;

/* Describe how each Janet argument is interpreted in terms of machine words
 * that will be mapped to registers/stack. */
typedef struct {
    JanetFFIType type;
    JanetFFIWordSpec spec;
    uint32_t offset; /* point to the exact register / stack offset depending on spec. */
    uint32_t offset2; /* for reference passing apis (windows), use to allocate reference */
} JanetFFIMapping;

typedef enum {
    JANET_FFI_CC_NONE,
    JANET_FFI_CC_SYSV_64,
    JANET_FFI_CC_WIN_64
} JanetFFICallingConvention;

#ifdef JANET_FFI_WIN64_ENABLED
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_WIN_64
#elif defined(JANET_FFI_SYSV64_ENABLED)
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_SYSV_64
#else
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_NONE
#endif

#define JANET_FFI_MAX_ARGS 32

typedef struct {
    uint32_t frame_size;
    uint32_t arg_count;
    uint32_t word_count;
    uint32_t variant;
    uint32_t stack_count;
    JanetFFICallingConvention cc;
    JanetFFIMapping ret;
    JanetFFIMapping args[JANET_FFI_MAX_ARGS];
} JanetFFISignature;

int signature_mark(void *p, size_t s) {
    (void) s;
    JanetFFISignature *sig = p;
    for (uint32_t i = 0; i < sig->arg_count; i++) {
        JanetFFIType t = sig->args[i].type;
        if (t.prim == JANET_FFI_TYPE_STRUCT) {
            janet_mark(janet_wrap_abstract(t.st));
        }
    }
    return 0;
}

static const JanetAbstractType janet_signature_type = {
    "core/ffi-signature",
    NULL,
    signature_mark,
    JANET_ATEND_GCMARK
};

int struct_mark(void *p, size_t s) {
    (void) s;
    JanetFFIStruct *st = p;
    for (uint32_t i = 0; i < st->field_count; i++) {
        JanetFFIType t = st->fields[i].type;
        if (t.prim == JANET_FFI_TYPE_STRUCT) {
            janet_mark(janet_wrap_abstract(t.st));
        }
    }
    return 0;
}

static const JanetAbstractType janet_struct_type = {
    "core/ffi-struct",
    NULL,
    struct_mark,
    JANET_ATEND_GCMARK
};

typedef struct {
    Clib clib;
    int closed;
    int is_self;
} JanetAbstractNative;

static const JanetAbstractType janet_native_type = {
    "core/ffi-native",
    JANET_ATEND_NAME
};

static JanetFFIType prim_type(JanetFFIPrimType pt) {
    JanetFFIType t;
    t.prim = pt;
    t.st = NULL;
    t.array_count = -1;
    return t;
}

static size_t type_size(JanetFFIType t) {
    size_t count = t.array_count < 0 ? 1 : (size_t) t.array_count;
    if (t.prim == JANET_FFI_TYPE_STRUCT) {
        return t.st->size * count;
    } else {
        return janet_ffi_type_info[t.prim].size * count;
    }
}

static size_t type_align(JanetFFIType t) {
    if (t.prim == JANET_FFI_TYPE_STRUCT) {
        return t.st->align;
    } else {
        return janet_ffi_type_info[t.prim].align;
    }
}

static JanetFFICallingConvention decode_ffi_cc(const uint8_t *name) {
    if (!janet_cstrcmp(name, "none")) return JANET_FFI_CC_NONE;
#ifdef JANET_FFI_WIN64_ENABLED
    if (!janet_cstrcmp(name, "win64")) return JANET_FFI_CC_WIN_64;
#endif
#ifdef JANET_FFI_SYSV64_ENABLED
    if (!janet_cstrcmp(name, "sysv64")) return JANET_FFI_CC_SYSV_64;
#endif
    if (!janet_cstrcmp(name, "default")) return JANET_FFI_CC_DEFAULT;
    janet_panicf("unknown calling convention %s", name);
}

static JanetFFIPrimType decode_ffi_prim(const uint8_t *name) {
    if (!janet_cstrcmp(name, "void")) return JANET_FFI_TYPE_VOID;
    if (!janet_cstrcmp(name, "bool")) return JANET_FFI_TYPE_BOOL;
    if (!janet_cstrcmp(name, "ptr")) return JANET_FFI_TYPE_PTR;
    if (!janet_cstrcmp(name, "string")) return JANET_FFI_TYPE_STRING;
    if (!janet_cstrcmp(name, "float")) return JANET_FFI_TYPE_FLOAT;
    if (!janet_cstrcmp(name, "double")) return JANET_FFI_TYPE_DOUBLE;
    if (!janet_cstrcmp(name, "int8")) return JANET_FFI_TYPE_INT8;
    if (!janet_cstrcmp(name, "uint8")) return JANET_FFI_TYPE_UINT8;
    if (!janet_cstrcmp(name, "int16")) return JANET_FFI_TYPE_INT16;
    if (!janet_cstrcmp(name, "uint16")) return JANET_FFI_TYPE_UINT16;
    if (!janet_cstrcmp(name, "int32")) return JANET_FFI_TYPE_INT32;
    if (!janet_cstrcmp(name, "uint32")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "int64")) return JANET_FFI_TYPE_INT64;
    if (!janet_cstrcmp(name, "uint64")) return JANET_FFI_TYPE_UINT64;
#ifdef JANET_64
    if (!janet_cstrcmp(name, "size")) return JANET_FFI_TYPE_UINT64;
    if (!janet_cstrcmp(name, "ssize")) return JANET_FFI_TYPE_INT64;
#else
    if (!janet_cstrcmp(name, "size")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "ssize")) return JANET_FFI_TYPE_INT32;
#endif
    /* aliases */
    if (!janet_cstrcmp(name, "r32")) return JANET_FFI_TYPE_FLOAT;
    if (!janet_cstrcmp(name, "r64")) return JANET_FFI_TYPE_DOUBLE;
    if (!janet_cstrcmp(name, "s8")) return JANET_FFI_TYPE_INT8;
    if (!janet_cstrcmp(name, "u8")) return JANET_FFI_TYPE_UINT8;
    if (!janet_cstrcmp(name, "s16")) return JANET_FFI_TYPE_INT16;
    if (!janet_cstrcmp(name, "u16")) return JANET_FFI_TYPE_UINT16;
    if (!janet_cstrcmp(name, "s32")) return JANET_FFI_TYPE_INT32;
    if (!janet_cstrcmp(name, "u32")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "s64")) return JANET_FFI_TYPE_INT64;
    if (!janet_cstrcmp(name, "u64")) return JANET_FFI_TYPE_UINT64;
    if (!janet_cstrcmp(name, "char")) return JANET_FFI_TYPE_INT8;
    if (!janet_cstrcmp(name, "short")) return JANET_FFI_TYPE_INT16;
    if (!janet_cstrcmp(name, "int")) return JANET_FFI_TYPE_INT32;
    if (!janet_cstrcmp(name, "long")) return JANET_FFI_TYPE_INT64;
    if (!janet_cstrcmp(name, "byte")) return JANET_FFI_TYPE_UINT8;
    if (!janet_cstrcmp(name, "uchar")) return JANET_FFI_TYPE_UINT8;
    if (!janet_cstrcmp(name, "ushort")) return JANET_FFI_TYPE_UINT16;
    if (!janet_cstrcmp(name, "uint")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "ulong")) return JANET_FFI_TYPE_UINT64;
    janet_panicf("unknown machine type %s", name);
}

/* A common callback function signature. To avoid runtime code generation, which is prohibited
 * on many platforms, often buggy (see libffi), and generally complicated, instead provide
 * a single (or small set of commonly used function signatures). All callbacks should
 * eventually call this. */
void janet_ffi_trampoline(void *ctx, void *userdata) {
    if (NULL == userdata) {
        /* Userdata not set. */
        janet_eprintf("no userdata found for janet callback");
        return;
    }
    Janet context = janet_wrap_pointer(ctx);
    JanetFunction *fun = userdata;
    janet_call(fun, 1, &context);
}

static JanetFFIType decode_ffi_type(Janet x);

static JanetFFIStruct *build_struct_type(int32_t argc, const Janet *argv) {
    /* Use :pack to indicate a single packed struct member and :pack-all
     * to pack the remaining members */
    int32_t member_count = argc;
    int all_packed = 0;
    for (int32_t i = 0; i < argc; i++) {
        if (janet_keyeq(argv[i], "pack")) {
            member_count--;
        } else if (janet_keyeq(argv[i], "pack-all")) {
            member_count--;
            all_packed = 1;
        }
    }

    JanetFFIStruct *st = janet_abstract(&janet_struct_type,
                                        sizeof(JanetFFIStruct) + argc * sizeof(JanetFFIStructMember));
    st->field_count = member_count;
    st->size = 0;
    st->align = 1;
    if (argc == 0) {
        janet_panic("invalid empty struct");
    }
    uint32_t is_aligned = 1;
    int32_t i = 0;
    for (int32_t j = 0; j < argc; j++) {
        int pack_one = 0;
        if (janet_keyeq(argv[j], "pack") || janet_keyeq(argv[j], "pack-all")) {
            pack_one = 1;
            j++;
            if (j == argc) break;
        }
        st->fields[i].type = decode_ffi_type(argv[j]);
        size_t el_size = type_size(st->fields[i].type);
        size_t el_align = type_align(st->fields[i].type);
        if (all_packed || pack_one) {
            if (st->size % el_align != 0) is_aligned = 0;
            st->fields[i].offset = st->size;
            st->size += (uint32_t) el_size;
        } else {
            if (el_align > st->align) st->align = (uint32_t) el_align;
            st->fields[i].offset = (uint32_t)(((st->size + el_align - 1) / el_align) * el_align);
            st->size = (uint32_t)(el_size + st->fields[i].offset);
        }
        i++;
    }
    st->is_aligned = is_aligned;
    st->size += (st->align - 1);
    st->size /= st->align;
    st->size *= st->align;
    return st;
}

static JanetFFIType decode_ffi_type(Janet x) {
    if (janet_checktype(x, JANET_KEYWORD)) {
        return prim_type(decode_ffi_prim(janet_unwrap_keyword(x)));
    }
    JanetFFIType ret;
    ret.array_count = -1;
    ret.prim = JANET_FFI_TYPE_STRUCT;
    if (janet_checkabstract(x, &janet_struct_type)) {
        ret.st = janet_unwrap_abstract(x);
        return ret;
    }
    int32_t len;
    const Janet *els;
    if (janet_indexed_view(x, &els, &len)) {
        if (janet_checktype(x, JANET_ARRAY)) {
            if (len != 2 && len != 1) janet_panicf("array type must be of form @[type count], got %v", x);
            ret = decode_ffi_type(els[0]);
            int32_t array_count = len == 1 ? 0 : janet_getnat(els, 1);
            ret.array_count = array_count;
        } else {
            ret.st = build_struct_type(len, els);
        }
        return ret;
    } else {
        janet_panicf("bad native type %v", x);
    }
}

JANET_CORE_FN(cfun_ffi_struct,
              "(ffi/struct & types)",
              "Create a struct type definition that can be used to pass structs into native functions. ") {
    janet_arity(argc, 1, -1);
    return janet_wrap_abstract(build_struct_type(argc, argv));
}

JANET_CORE_FN(cfun_ffi_size,
              "(ffi/size type)",
              "Get the size of an ffi type in bytes.") {
    janet_fixarity(argc, 1);
    size_t size = type_size(decode_ffi_type(argv[0]));
    return janet_wrap_number((double) size);
}

JANET_CORE_FN(cfun_ffi_align,
              "(ffi/align type)",
              "Get the align of an ffi type in bytes.") {
    janet_fixarity(argc, 1);
    size_t size = type_align(decode_ffi_type(argv[0]));
    return janet_wrap_number((double) size);
}

static void *janet_ffi_getpointer(const Janet *argv, int32_t n) {
    switch (janet_type(argv[n])) {
        default:
            janet_panicf("bad slot #%d, expected ffi pointer convertable type, got %v", argv[n]);
        case JANET_POINTER:
        case JANET_STRING:
        case JANET_KEYWORD:
        case JANET_SYMBOL:
        case JANET_ABSTRACT:
            return janet_unwrap_pointer(argv[n]);
        case JANET_BUFFER:
            return janet_unwrap_buffer(argv[n])->data;
        case JANET_FUNCTION:
            /* Users may pass in a function. Any function passed is almost certainly
             * being used as a callback, so we add it to the root set. */
            janet_gcroot(argv[n]);
            return janet_unwrap_pointer(argv[n]);
        case JANET_NIL:
            return NULL;
    }
}

/* Write a value given by some Janet values and an FFI type as it would appear in memory.
 * The alignment and space available is assumed to already be sufficient */
static void janet_ffi_write_one(void *to, const Janet *argv, int32_t n, JanetFFIType type, int recur) {
    if (recur == 0) janet_panic("recursion too deep");
    if (type.array_count >= 0) {
        JanetFFIType el_type = type;
        el_type.array_count = -1;
        size_t el_size = type_size(el_type);
        JanetView els = janet_getindexed(argv, n);
        if (els.len != type.array_count) {
            janet_panicf("bad array length, expected %d, got %d", type.array_count, els.len);
        }
        char *cursor = to;
        for (int32_t i = 0; i < els.len; i++) {
            janet_ffi_write_one(cursor, els.items, i, el_type, recur - 1);
            cursor += el_size;
        }
        return;
    }
    switch (type.prim) {
        case JANET_FFI_TYPE_VOID:
            if (!janet_checktype(argv[n], JANET_NIL)) {
                janet_panicf("expected nil, got %v", argv[n]);
            }
            break;
        case JANET_FFI_TYPE_STRUCT: {
            JanetView els = janet_getindexed(argv, n);
            JanetFFIStruct *st = type.st;
            if ((uint32_t) els.len != st->field_count) {
                janet_panicf("wrong number of fields in struct, expected %d, got %d",
                             (int32_t) st->field_count, els.len);
            }
            for (int32_t i = 0; i < els.len; i++) {
                JanetFFIType tp = st->fields[i].type;
                janet_ffi_write_one((char *) to + st->fields[i].offset, els.items, i, tp, recur - 1);
            }
        }
        break;
        case JANET_FFI_TYPE_DOUBLE:
            ((double *)(to))[0] = janet_getnumber(argv, n);
            break;
        case JANET_FFI_TYPE_FLOAT:
            ((float *)(to))[0] = (float) janet_getnumber(argv, n);
            break;
        case JANET_FFI_TYPE_PTR:
            ((void **)(to))[0] = janet_ffi_getpointer(argv, n);
            break;
        case JANET_FFI_TYPE_STRING:
            ((const char **)(to))[0] = janet_getcstring(argv, n);
            break;
        case JANET_FFI_TYPE_BOOL:
            ((bool *)(to))[0] = janet_getboolean(argv, n);
            break;
        case JANET_FFI_TYPE_INT8:
            ((int8_t *)(to))[0] = janet_getinteger(argv, n);
            break;
        case JANET_FFI_TYPE_INT16:
            ((int16_t *)(to))[0] = janet_getinteger(argv, n);
            break;
        case JANET_FFI_TYPE_INT32:
            ((int32_t *)(to))[0] = janet_getinteger(argv, n);
            break;
        case JANET_FFI_TYPE_INT64:
            ((int64_t *)(to))[0] = janet_getinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT8:
            ((uint8_t *)(to))[0] = (uint8_t) janet_getuinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT16:
            ((uint16_t *)(to))[0] = (uint16_t) janet_getuinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT32:
            ((uint32_t *)(to))[0] = (uint32_t) janet_getuinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT64:
            ((uint64_t *)(to))[0] = janet_getuinteger64(argv, n);
            break;
    }
}

/* Read a value from memory and construct a Janet data structure that can be passed back into
 * the interpreter. This should be the inverse to janet_ffi_write_one. It is assumed that the
 * size of the data is correct. */
static Janet janet_ffi_read_one(const uint8_t *from, JanetFFIType type, int recur) {
    if (recur == 0) janet_panic("recursion too deep");
    if (type.array_count >= 0) {
        JanetFFIType el_type = type;
        el_type.array_count = -1;
        size_t el_size = type_size(el_type);
        JanetArray *array = janet_array(type.array_count);
        for (int32_t i = 0; i < type.array_count; i++) {
            janet_array_push(array, janet_ffi_read_one(from, el_type, recur - 1));
            from += el_size;
        }
        return janet_wrap_array(array);
    }
    switch (type.prim) {
        default:
        case JANET_FFI_TYPE_VOID:
            return janet_wrap_nil();
        case JANET_FFI_TYPE_STRUCT: {
            JanetFFIStruct *st = type.st;
            Janet *tup = janet_tuple_begin(st->field_count);
            for (uint32_t i = 0; i < st->field_count; i++) {
                JanetFFIType tp = st->fields[i].type;
                tup[i] = janet_ffi_read_one(from + st->fields[i].offset, tp, recur - 1);
            }
            return janet_wrap_tuple(janet_tuple_end(tup));
        }
        case JANET_FFI_TYPE_DOUBLE:
            return janet_wrap_number(((double *)(from))[0]);
        case JANET_FFI_TYPE_FLOAT:
            return janet_wrap_number(((float *)(from))[0]);
        case JANET_FFI_TYPE_PTR: {
            void *ptr = ((void **)(from))[0];
            return (NULL == ptr) ? janet_wrap_nil() : janet_wrap_pointer(ptr);
        }
        case JANET_FFI_TYPE_STRING:
            return janet_cstringv(((char **)(from))[0]);
        case JANET_FFI_TYPE_BOOL:
            return janet_wrap_boolean(((bool *)(from))[0]);
        case JANET_FFI_TYPE_INT8:
            return janet_wrap_number(((int8_t *)(from))[0]);
        case JANET_FFI_TYPE_INT16:
            return janet_wrap_number(((int16_t *)(from))[0]);
        case JANET_FFI_TYPE_INT32:
            return janet_wrap_number(((int32_t *)(from))[0]);
        case JANET_FFI_TYPE_UINT8:
            return janet_wrap_number(((uint8_t *)(from))[0]);
        case JANET_FFI_TYPE_UINT16:
            return janet_wrap_number(((uint16_t *)(from))[0]);
        case JANET_FFI_TYPE_UINT32:
            return janet_wrap_number(((uint32_t *)(from))[0]);
#ifdef JANET_INT_TYPES
        case JANET_FFI_TYPE_INT64:
            return janet_wrap_s64(((int64_t *)(from))[0]);
        case JANET_FFI_TYPE_UINT64:
            return janet_wrap_u64(((uint64_t *)(from))[0]);
#else
        case JANET_FFI_TYPE_INT64:
            return janet_wrap_number(((int64_t *)(from))[0]);
        case JANET_FFI_TYPE_UINT64:
            return janet_wrap_number(((uint64_t *)(from))[0]);
#endif
    }
}

static JanetFFIMapping void_mapping(void) {
    JanetFFIMapping m;
    m.type = prim_type(JANET_FFI_TYPE_VOID);
    m.spec = JANET_SYSV64_NO_CLASS;
    m.offset = 0;
    return m;
}

#ifdef JANET_FFI_SYSV64_ENABLED
/* AMD64 ABI Draft 0.99.7 – November 17, 2014 – 15:08
 * See section 3.2.3 Parameter Passing */
static JanetFFIWordSpec sysv64_classify(JanetFFIType type) {
    switch (type.prim) {
        case JANET_FFI_TYPE_PTR:
        case JANET_FFI_TYPE_STRING:
        case JANET_FFI_TYPE_BOOL:
        case JANET_FFI_TYPE_INT8:
        case JANET_FFI_TYPE_INT16:
        case JANET_FFI_TYPE_INT32:
        case JANET_FFI_TYPE_INT64:
        case JANET_FFI_TYPE_UINT8:
        case JANET_FFI_TYPE_UINT16:
        case JANET_FFI_TYPE_UINT32:
        case JANET_FFI_TYPE_UINT64:
            return JANET_SYSV64_INTEGER;
        case JANET_FFI_TYPE_DOUBLE:
        case JANET_FFI_TYPE_FLOAT:
            return JANET_SYSV64_SSE;
        case JANET_FFI_TYPE_STRUCT: {
            JanetFFIStruct *st = type.st;
            if (st->size > 16) return JANET_SYSV64_MEMORY;
            if (!st->is_aligned) return JANET_SYSV64_MEMORY;
            JanetFFIWordSpec clazz = JANET_SYSV64_NO_CLASS;
            for (uint32_t i = 0; i < st->field_count; i++) {
                JanetFFIWordSpec next_class = sysv64_classify(st->fields[i].type);
                if (next_class != clazz) {
                    if (clazz == JANET_SYSV64_NO_CLASS) {
                        clazz = next_class;
                    } else if (clazz == JANET_SYSV64_MEMORY || next_class == JANET_SYSV64_MEMORY) {
                        clazz = JANET_SYSV64_MEMORY;
                    } else if (clazz == JANET_SYSV64_INTEGER || next_class == JANET_SYSV64_INTEGER) {
                        clazz = JANET_SYSV64_INTEGER;
                    } else if (next_class == JANET_SYSV64_X87 || next_class == JANET_SYSV64_X87UP
                               || next_class == JANET_SYSV64_COMPLEX_X87) {
                        clazz = JANET_SYSV64_MEMORY;
                    } else {
                        clazz = JANET_SYSV64_SSE;
                    }
                }
            }
            return clazz;
        }
        case JANET_FFI_TYPE_VOID:
            return JANET_SYSV64_NO_CLASS;
        default:
            janet_panic("nyi");
            return JANET_SYSV64_NO_CLASS;
    }
}
#endif

JANET_CORE_FN(cfun_ffi_signature,
              "(ffi/signature calling-convention ret-type & arg-types)",
              "Create a function signature object that can be used to make calls "
              "with raw function pointers.") {
    janet_arity(argc, 2, -1);
    uint32_t frame_size = 0;
    uint32_t variant = 0;
    uint32_t arg_count = argc - 2;
    uint32_t stack_count = 0;
    JanetFFICallingConvention cc = decode_ffi_cc(janet_getkeyword(argv, 0));
    JanetFFIType ret_type = decode_ffi_type(argv[1]);
    JanetFFIMapping ret = {
        ret_type,
        JANET_SYSV64_NO_CLASS,
        0,
        0
    };
    JanetFFIMapping mappings[JANET_FFI_MAX_ARGS];
    for (int i = 0; i < JANET_FFI_MAX_ARGS; i++) mappings[i] = void_mapping();
    switch (cc) {
        default:
        case JANET_FFI_CC_NONE: {
            /* Even if unsupported, we can check that the signature is valid
             * and error at runtime */
            for (uint32_t i = 0; i < arg_count; i++) {
                decode_ffi_type(argv[i + 2]);
            }
        }
        break;

#ifdef JANET_FFI_WIN64_ENABLED
        case JANET_FFI_CC_WIN_64: {
            size_t ret_size = type_size(ret.type);
            uint32_t ref_stack_count = 0;
            ret.spec = JANET_WIN64_REGISTER;
            uint32_t next_register = 0;
            if (ret_size != 1 && ret_size != 2 && ret_size != 4 && ret_size != 8) {
                ret.spec = JANET_WIN64_REGISTER_REF;
                next_register++;
            } else if (ret.type.prim == JANET_FFI_TYPE_FLOAT ||
                       ret.type.prim == JANET_FFI_TYPE_DOUBLE) {
                variant += 16;
            }
            for (uint32_t i = 0; i < arg_count; i++) {
                mappings[i].type = decode_ffi_type(argv[i + 2]);
                size_t el_size = type_size(mappings[i].type);
                int is_register_sized = (el_size == 1 || el_size == 2 || el_size == 4 || el_size == 8);
                if (next_register < 4) {
                    mappings[i].offset = next_register;
                    if (is_register_sized) {
                        mappings[i].spec = JANET_WIN64_REGISTER;
                        if (mappings[i].type.prim == JANET_FFI_TYPE_FLOAT ||
                                mappings[i].type.prim == JANET_FFI_TYPE_DOUBLE) {
                            variant += 1 << (3 - next_register);
                        }
                    } else {
                        mappings[i].spec = JANET_WIN64_REGISTER_REF;
                        mappings[i].offset2 = ref_stack_count;
                        ref_stack_count += (uint32_t)((el_size + 15) / 16);
                    }
                    next_register++;
                } else {
                    if (is_register_sized) {
                        mappings[i].spec = JANET_WIN64_STACK;
                        mappings[i].offset = stack_count;
                        stack_count++;
                    } else {
                        mappings[i].spec = JANET_WIN64_STACK_REF;
                        mappings[i].offset = stack_count;
                        stack_count++;
                        mappings[i].offset2 = ref_stack_count;
                        ref_stack_count += (uint32_t)((el_size + 15) / 16);
                    }
                }
            }

            /* Add reference items */
            size_t old_stack_count = stack_count;
            stack_count += 2 * ref_stack_count;
            if (stack_count & 0x1) {
                stack_count++;
            }

            /* Invert stack
             * Offsets are in units of 8-bytes */
            for (uint32_t i = 0; i < arg_count; i++) {
                if (mappings[i].spec == JANET_WIN64_STACK_REF || mappings[i].spec == JANET_WIN64_REGISTER_REF) {
                    /* Align size to 16 bytes */
                    size_t size = (type_size(mappings[i].type) + 15) & ~0xFUL;
                    mappings[i].offset2 = (uint32_t)(stack_count - mappings[i].offset2 - (size / 8));
                }
            }

        }
        break;
#endif

#ifdef JANET_FFI_SYSV64_ENABLED
        case JANET_FFI_CC_SYSV_64: {
            JanetFFIWordSpec ret_spec = sysv64_classify(ret.type);
            ret.spec = ret_spec;
            if (ret_spec == JANET_SYSV64_SSE) variant = 1;
            /* Spill register overflow to memory */
            uint32_t next_register = 0;
            uint32_t next_fp_register = 0;
            const uint32_t max_regs = 6;
            const uint32_t max_fp_regs = 8;
            if (ret_spec == JANET_SYSV64_MEMORY) {
                /* First integer reg is pointer. */
                next_register = 1;
            }
            for (uint32_t i = 0; i < arg_count; i++) {
                mappings[i].type = decode_ffi_type(argv[i + 2]);
                mappings[i].offset = 0;
                mappings[i].spec = sysv64_classify(mappings[i].type);
                if (mappings[i].spec == JANET_SYSV64_NO_CLASS) {
                    janet_panic("unexpected void parameter");
                }
                size_t el_size = (type_size(mappings[i].type) + 7) / 8;
                switch (mappings[i].spec) {
                    default:
                        janet_panicf("nyi: %d", mappings[i].spec);
                    case JANET_SYSV64_INTEGER: {
                        if (next_register < max_regs) {
                            mappings[i].offset = next_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                        break;
                    }
                    case JANET_SYSV64_SSE: {
                        if (next_fp_register < max_fp_regs) {
                            mappings[i].offset = next_fp_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                        break;
                    }
                    case JANET_SYSV64_MEMORY: {
                        mappings[i].offset = stack_count;
                        stack_count += el_size;
                    }
                }
            }
        }
        break;
#endif
    }

    /* Create signature abstract value */
    JanetFFISignature *abst = janet_abstract(&janet_signature_type, sizeof(JanetFFISignature));
    abst->frame_size = frame_size;
    abst->cc = cc;
    abst->ret = ret;
    abst->arg_count = arg_count;
    abst->variant = variant;
    abst->stack_count = stack_count;
    memcpy(abst->args, mappings, sizeof(JanetFFIMapping) * JANET_FFI_MAX_ARGS);
    return janet_wrap_abstract(abst);
}

#ifdef JANET_FFI_SYSV64_ENABLED

static void janet_ffi_sysv64_standard_callback(void *ctx, void *userdata) {
    janet_ffi_trampoline(ctx, userdata);
}

/* Functions that set all argument registers. Two variants - one to read rax and rdx returns, another
 * to read xmm0 and xmm1 returns. */
typedef struct {
    uint64_t x;
    uint64_t y;
} sysv64_int_return;
typedef struct {
    double x;
    double y;
} sysv64_sse_return;
typedef sysv64_int_return janet_sysv64_variant_1(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);
typedef sysv64_sse_return janet_sysv64_variant_2(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);

static Janet janet_ffi_sysv64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    sysv64_int_return int_return;
    sysv64_sse_return sse_return;
    uint64_t regs[6];
    double fp_regs[8];
    JanetFFIWordSpec ret_spec = signature->ret.spec;
    void *ret_mem = &int_return;
    if (ret_spec == JANET_SYSV64_MEMORY) {
        ret_mem = alloca(type_size(signature->ret.type));
        regs[0] = (uint64_t) ret_mem;
    } else if (ret_spec == JANET_SYSV64_SSE) {
        ret_mem = &sse_return;
    }
    uint64_t *stack = alloca(sizeof(uint64_t) * signature->stack_count);
    for (uint32_t i = 0; i < signature->arg_count; i++) {
        uint64_t *to;
        int32_t n = i + 2;
        JanetFFIMapping arg = signature->args[i];
        switch (arg.spec) {
            default:
                janet_panic("nyi");
            case JANET_SYSV64_INTEGER:
                to = regs + arg.offset;
                break;
            case JANET_SYSV64_SSE:
                to = (uint64_t *)(fp_regs + arg.offset);
                break;
            case JANET_SYSV64_MEMORY:
                to = stack + arg.offset;
                break;
        }
        janet_ffi_write_one(to, argv, n, arg.type, JANET_FFI_MAX_RECUR);
    }

    if (signature->variant) {
        sse_return = ((janet_sysv64_variant_2 *)(function_pointer))(
                         regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                         fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                         fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
    } else {
        int_return = ((janet_sysv64_variant_1 *)(function_pointer))(
                         regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                         fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                         fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);

    }

    return janet_ffi_read_one(ret_mem, signature->ret.type, JANET_FFI_MAX_RECUR);
}

#endif

#ifdef JANET_FFI_WIN64_ENABLED

static void janet_ffi_win64_standard_callback(void *ctx, void *userdata) {
    janet_ffi_trampoline(ctx, userdata);
}

/* Variants that allow setting all required registers for 64 bit windows calling convention.
 * win64 calling convention has up to 4 arguments on registers, and one register for returns.
 * Each register can either be an integer or floating point register, resulting in
 * 2^5 = 32 variants. Unlike sysv, there are no function signatures that will fill
 * all of the possible registers which is why we have so many variants. If you were using
 * assembly, you could manually fill all of the registers and only have a single variant.
 * And msvc does not support inline assembly on 64 bit targets, so yeah, we have this hackery. */
typedef uint64_t (win64_variant_i_iiii)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (win64_variant_i_iiif)(uint64_t, uint64_t, uint64_t, double);
typedef uint64_t (win64_variant_i_iifi)(uint64_t, uint64_t, double, uint64_t);
typedef uint64_t (win64_variant_i_iiff)(uint64_t, uint64_t, double, double);
typedef uint64_t (win64_variant_i_ifii)(uint64_t, double, uint64_t, uint64_t);
typedef uint64_t (win64_variant_i_ifif)(uint64_t, double, uint64_t, double);
typedef uint64_t (win64_variant_i_iffi)(uint64_t, double, double, uint64_t);
typedef uint64_t (win64_variant_i_ifff)(uint64_t, double, double, double);
typedef uint64_t (win64_variant_i_fiii)(double, uint64_t, uint64_t, uint64_t);
typedef uint64_t (win64_variant_i_fiif)(double, uint64_t, uint64_t, double);
typedef uint64_t (win64_variant_i_fifi)(double, uint64_t, double, uint64_t);
typedef uint64_t (win64_variant_i_fiff)(double, uint64_t, double, double);
typedef uint64_t (win64_variant_i_ffii)(double, double, uint64_t, uint64_t);
typedef uint64_t (win64_variant_i_ffif)(double, double, uint64_t, double);
typedef uint64_t (win64_variant_i_fffi)(double, double, double, uint64_t);
typedef uint64_t (win64_variant_i_ffff)(double, double, double, double);
typedef double (win64_variant_f_iiii)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef double (win64_variant_f_iiif)(uint64_t, uint64_t, uint64_t, double);
typedef double (win64_variant_f_iifi)(uint64_t, uint64_t, double, uint64_t);
typedef double (win64_variant_f_iiff)(uint64_t, uint64_t, double, double);
typedef double (win64_variant_f_ifii)(uint64_t, double, uint64_t, uint64_t);
typedef double (win64_variant_f_ifif)(uint64_t, double, uint64_t, double);
typedef double (win64_variant_f_iffi)(uint64_t, double, double, uint64_t);
typedef double (win64_variant_f_ifff)(uint64_t, double, double, double);
typedef double (win64_variant_f_fiii)(double, uint64_t, uint64_t, uint64_t);
typedef double (win64_variant_f_fiif)(double, uint64_t, uint64_t, double);
typedef double (win64_variant_f_fifi)(double, uint64_t, double, uint64_t);
typedef double (win64_variant_f_fiff)(double, uint64_t, double, double);
typedef double (win64_variant_f_ffii)(double, double, uint64_t, uint64_t);
typedef double (win64_variant_f_ffif)(double, double, uint64_t, double);
typedef double (win64_variant_f_fffi)(double, double, double, uint64_t);
typedef double (win64_variant_f_ffff)(double, double, double, double);

static Janet janet_ffi_win64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    union {
        uint64_t integer;
        double real;
    } regs[4];
    union {
        uint64_t integer;
        double real;
    } ret_reg;
    JanetFFIWordSpec ret_spec = signature->ret.spec;
    void *ret_mem = &ret_reg.integer;
    if (ret_spec == JANET_WIN64_REGISTER_REF) {
        ret_mem = alloca(type_size(signature->ret.type));
        regs[0].integer = (uint64_t) ret_mem;
    }
    uint64_t *stack = alloca(signature->stack_count * 8);
    stack -= 2; /* hack to get proper stack placement */
    for (uint32_t i = 0; i < signature->arg_count; i++) {
        int32_t n = i + 2;
        JanetFFIMapping arg = signature->args[i];
        if (arg.spec == JANET_WIN64_STACK) {
            janet_ffi_write_one(stack + arg.offset, argv, n, arg.type, JANET_FFI_MAX_RECUR);
        } else if (arg.spec == JANET_WIN64_STACK_REF) {
            uint8_t *ptr = (uint8_t *)(stack + arg.offset2);
            janet_ffi_write_one(ptr, argv, n, arg.type, JANET_FFI_MAX_RECUR);
            stack[arg.offset] = (uint64_t) ptr;
        } else if (arg.spec == JANET_WIN64_REGISTER_REF) {
            uint8_t *ptr = (uint8_t *)(stack + arg.offset2);
            janet_ffi_write_one(ptr, argv, n, arg.type, JANET_FFI_MAX_RECUR);
            regs[arg.offset].integer = (uint64_t) ptr;
        } else {
            janet_ffi_write_one((uint8_t *) &regs[arg.offset].integer, argv, n, arg.type, JANET_FFI_MAX_RECUR);
        }
    }

    switch (signature->variant) {
        default:
            janet_panicf("unknown variant %d", signature->variant);
        case 0:
            ret_reg.integer = ((win64_variant_i_iiii *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].integer, regs[3].integer);
            break;
        case 1:
            ret_reg.integer = ((win64_variant_i_iiif *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].integer, regs[3].real);
            break;
        case 2:
            ret_reg.integer = ((win64_variant_i_iifi *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].real, regs[3].integer);
            break;
        case 3:
            ret_reg.integer = ((win64_variant_i_iiff *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].real, regs[3].real);
            break;
        case 4:
            ret_reg.integer = ((win64_variant_i_ifii *) function_pointer)(regs[0].integer, regs[1].real, regs[2].integer, regs[3].integer);
            break;
        case 5:
            ret_reg.integer = ((win64_variant_i_ifif *) function_pointer)(regs[0].integer, regs[1].real, regs[2].integer, regs[3].real);
            break;
        case 6:
            ret_reg.integer = ((win64_variant_i_iffi *) function_pointer)(regs[0].integer, regs[1].real, regs[2].real, regs[3].integer);
            break;
        case 7:
            ret_reg.integer = ((win64_variant_i_ifff *) function_pointer)(regs[0].integer, regs[1].real, regs[2].real, regs[3].real);
            break;
        case 8:
            ret_reg.integer = ((win64_variant_i_fiii *) function_pointer)(regs[0].real, regs[1].integer, regs[2].integer, regs[3].integer);
            break;
        case 9:
            ret_reg.integer = ((win64_variant_i_fiif *) function_pointer)(regs[0].real, regs[1].integer, regs[2].integer, regs[3].real);
            break;
        case 10:
            ret_reg.integer = ((win64_variant_i_fifi *) function_pointer)(regs[0].real, regs[1].integer, regs[2].real, regs[3].integer);
            break;
        case 11:
            ret_reg.integer = ((win64_variant_i_fiff *) function_pointer)(regs[0].real, regs[1].integer, regs[2].real, regs[3].real);
            break;
        case 12:
            ret_reg.integer = ((win64_variant_i_ffii *) function_pointer)(regs[0].real, regs[1].real, regs[2].integer, regs[3].integer);
            break;
        case 13:
            ret_reg.integer = ((win64_variant_i_ffif *) function_pointer)(regs[0].real, regs[1].real, regs[2].integer, regs[3].real);
            break;
        case 14:
            ret_reg.integer = ((win64_variant_i_fffi *) function_pointer)(regs[0].real, regs[1].real, regs[2].real, regs[3].integer);
            break;
        case 15:
            ret_reg.integer = ((win64_variant_i_ffff *) function_pointer)(regs[0].real, regs[1].real, regs[2].real, regs[3].real);
            break;
        case 16:
            ret_reg.real = ((win64_variant_f_iiii *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].integer, regs[3].integer);
            break;
        case 17:
            ret_reg.real = ((win64_variant_f_iiif *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].integer, regs[3].real);
            break;
        case 18:
            ret_reg.real = ((win64_variant_f_iifi *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].real, regs[3].integer);
            break;
        case 19:
            ret_reg.real = ((win64_variant_f_iiff *) function_pointer)(regs[0].integer, regs[1].integer, regs[2].real, regs[3].real);
            break;
        case 20:
            ret_reg.real = ((win64_variant_f_ifii *) function_pointer)(regs[0].integer, regs[1].real, regs[2].integer, regs[3].integer);
            break;
        case 21:
            ret_reg.real = ((win64_variant_f_ifif *) function_pointer)(regs[0].integer, regs[1].real, regs[2].integer, regs[3].real);
            break;
        case 22:
            ret_reg.real = ((win64_variant_f_iffi *) function_pointer)(regs[0].integer, regs[1].real, regs[2].real, regs[3].integer);
            break;
        case 23:
            ret_reg.real = ((win64_variant_f_ifff *) function_pointer)(regs[0].integer, regs[1].real, regs[2].real, regs[3].real);
            break;
        case 24:
            ret_reg.real = ((win64_variant_f_fiii *) function_pointer)(regs[0].real, regs[1].integer, regs[2].integer, regs[3].integer);
            break;
        case 25:
            ret_reg.real = ((win64_variant_f_fiif *) function_pointer)(regs[0].real, regs[1].integer, regs[2].integer, regs[3].real);
            break;
        case 26:
            ret_reg.real = ((win64_variant_f_fifi *) function_pointer)(regs[0].real, regs[1].integer, regs[2].real, regs[3].integer);
            break;
        case 27:
            ret_reg.real = ((win64_variant_f_fiff *) function_pointer)(regs[0].real, regs[1].integer, regs[2].real, regs[3].real);
            break;
        case 28:
            ret_reg.real = ((win64_variant_f_ffii *) function_pointer)(regs[0].real, regs[1].real, regs[2].integer, regs[3].integer);
            break;
        case 29:
            ret_reg.real = ((win64_variant_f_ffif *) function_pointer)(regs[0].real, regs[1].real, regs[2].integer, regs[3].real);
            break;
        case 30:
            ret_reg.real = ((win64_variant_f_fffi *) function_pointer)(regs[0].real, regs[1].real, regs[2].real, regs[3].integer);
            break;
        case 31:
            ret_reg.real = ((win64_variant_f_ffff *) function_pointer)(regs[0].real, regs[1].real, regs[2].real, regs[3].real);
            break;
    }

    return janet_ffi_read_one(ret_mem, signature->ret.type, JANET_FFI_MAX_RECUR);
}

#endif

JANET_CORE_FN(cfun_ffi_call,
              "(ffi/call pointer signature & args)",
              "Call a raw pointer as a function pointer. The function signature specifies "
              "how Janet values in `args` are converted to native machine types.") {
    janet_arity(argc, 2, -1);
    void *function_pointer = janet_getpointer(argv, 0);
    JanetFFISignature *signature = janet_getabstract(argv, 1, &janet_signature_type);
    janet_fixarity(argc - 2, signature->arg_count);
    switch (signature->cc) {
        default:
        case JANET_FFI_CC_NONE:
            janet_panic("calling convention not supported");
#ifdef JANET_FFI_WIN64_ENABLED
        case JANET_FFI_CC_WIN_64:
            return janet_ffi_win64(signature, function_pointer, argv);
#endif
#ifdef JANET_FFI_SYSV64_ENABLED
        case JANET_FFI_CC_SYSV_64:
            return janet_ffi_sysv64(signature, function_pointer, argv);
#endif
    }
}

JANET_CORE_FN(cfun_ffi_buffer_write,
              "(ffi/write ffi-type data &opt buffer)",
              "Append a native tyep to a buffer such as it would appear in memory. This can be used "
              "to pass pointers to structs in the ffi, or send C/C++/native structs over the network "
              "or to files. Returns a modifed buffer or a new buffer if one is not supplied.") {
    janet_arity(argc, 2, 3);
    JanetFFIType type = decode_ffi_type(argv[0]);
    uint32_t el_size = (uint32_t) type_size(type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, el_size);
    janet_buffer_extra(buffer, el_size);
    memset(buffer->data, 0, el_size);
    janet_ffi_write_one(buffer->data, argv, 1, type, JANET_FFI_MAX_RECUR);
    buffer->count += el_size;
    return janet_wrap_buffer(buffer);
}

JANET_CORE_FN(cfun_ffi_buffer_read,
              "(ffi/read ffi-type bytes &opt offset)",
              "Parse a native struct out of a buffer and convert it to normal Janet data structures. "
              "This function is the inverse of `ffi/write`. `bytes` can also be a raw pointer, although "
              "this is unsafe.") {
    janet_arity(argc, 2, 3);
    JanetFFIType type = decode_ffi_type(argv[0]);
    size_t offset = (size_t) janet_optnat(argv, argc, 2, 0);
    if (janet_checktype(argv[1], JANET_POINTER)) {
        uint8_t *ptr = janet_unwrap_pointer(argv[1]);
        return janet_ffi_read_one(ptr + offset, type, JANET_FFI_MAX_RECUR);
    } else {
        size_t el_size = type_size(type);
        JanetByteView bytes = janet_getbytes(argv, 1);
        if ((size_t) bytes.len < offset + el_size) janet_panic("read out of range");
        return janet_ffi_read_one(bytes.bytes + offset, type, JANET_FFI_MAX_RECUR);
    }
}

JANET_CORE_FN(cfun_ffi_get_callback_trampoline,
              "(ffi/trampoline cc)",
              "Get a native function pointer that can be used as a callback and passed to C libraries. "
              "This callback trampoline has the signature `void trampoline(void \\*ctx, void \\*userdata)` in "
              "the given calling convention. This is the only function signature supported. "
              "It is up to the programmer to ensure that the `userdata` argument contains a janet function "
              "the will be called with one argument, `ctx` which is an opaque pointer. This pointer can "
              "be further inspected with `ffi/read`.") {
    janet_arity(argc, 0, 1);
    JanetFFICallingConvention cc = JANET_FFI_CC_DEFAULT;
    if (argc >= 1) cc = decode_ffi_cc(janet_getkeyword(argv, 0));
    switch (cc) {
        default:
        case JANET_FFI_CC_NONE:
            janet_panic("calling convention not supported");
#ifdef JANET_FFI_WIN64_ENABLED
        case JANET_FFI_CC_WIN_64:
            return janet_wrap_pointer(janet_ffi_win64_standard_callback);
#endif
#ifdef JANET_FFI_SYSV64_ENABLED
        case JANET_FFI_CC_SYSV_64:
            return janet_wrap_pointer(janet_ffi_sysv64_standard_callback);
#endif
    }
}

JANET_CORE_FN(janet_core_raw_native,
              "(ffi/native &opt path)",
              "Load a shared object or dll from the given path, and do not extract"
              " or run any code from it. This is different than `native`, which will "
              "run initialization code to get a module table. If `path` is nil, opens the current running binary. "
              "Returns a `core/native`.") {
    janet_arity(argc, 0, 1);
    const char *path = janet_optcstring(argv, argc, 0, NULL);
    Clib lib = load_clib(path);
    if (!lib) janet_panic(error_clib());
    JanetAbstractNative *anative = janet_abstract(&janet_native_type, sizeof(JanetAbstractNative));
    anative->clib = lib;
    anative->closed = 0;
    anative->is_self = path == NULL;
    return janet_wrap_abstract(anative);
}

JANET_CORE_FN(janet_core_native_lookup,
              "(ffi/lookup native symbol-name)",
              "Lookup a symbol from a native object. All symbol lookups will return a raw pointer "
              "if the symbol is found, else nil.") {
    janet_fixarity(argc, 2);
    JanetAbstractNative *anative = janet_getabstract(argv, 0, &janet_native_type);
    const char *sym = janet_getcstring(argv, 1);
    if (anative->closed) janet_panic("native object already closed");
    void *value = symbol_clib(anative->clib, sym);
    if (NULL == value) return janet_wrap_nil();
    return janet_wrap_pointer(value);
}

JANET_CORE_FN(janet_core_native_close,
              "(ffi/close native)",
              "Free a native object. Dereferencing pointers to symbols in the object will have undefined "
              "behavior after freeing.") {
    janet_fixarity(argc, 1);
    JanetAbstractNative *anative = janet_getabstract(argv, 0, &janet_native_type);
    if (anative->closed) janet_panic("native object already closed");
    if (anative->is_self) janet_panic("cannot close self");
    anative->closed = 1;
    free_clib(anative->clib);
    return janet_wrap_nil();
}

void janet_lib_ffi(JanetTable *env) {
    JanetRegExt ffi_cfuns[] = {
        JANET_CORE_REG("ffi/native", janet_core_raw_native),
        JANET_CORE_REG("ffi/lookup", janet_core_native_lookup),
        JANET_CORE_REG("ffi/close", janet_core_native_close),
        JANET_CORE_REG("ffi/signature", cfun_ffi_signature),
        JANET_CORE_REG("ffi/call", cfun_ffi_call),
        JANET_CORE_REG("ffi/struct", cfun_ffi_struct),
        JANET_CORE_REG("ffi/write", cfun_ffi_buffer_write),
        JANET_CORE_REG("ffi/read", cfun_ffi_buffer_read),
        JANET_CORE_REG("ffi/size", cfun_ffi_size),
        JANET_CORE_REG("ffi/align", cfun_ffi_align),
        JANET_CORE_REG("ffi/trampoline", cfun_ffi_get_callback_trampoline),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, ffi_cfuns);
}

#endif

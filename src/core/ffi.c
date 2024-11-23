/*
* Copyright (c) 2024 Calvin Rose
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
#include "gc.h"
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

/* FFI jit includes */
#ifdef JANET_FFI_JIT
#ifndef JANET_WINDOWS
#include <sys/mman.h>
#endif
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
#if (defined(__aarch64__) || defined(_M_ARM64)) && !defined(JANET_WINDOWS)
#define JANET_FFI_AAPCS64_ENABLED
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
    JANET_SYSV64_PAIR_INTINT,
    JANET_SYSV64_PAIR_INTSSE,
    JANET_SYSV64_PAIR_SSEINT,
    JANET_SYSV64_PAIR_SSESSE,
    JANET_SYSV64_NO_CLASS,
    JANET_SYSV64_MEMORY,
    JANET_WIN64_REGISTER,
    JANET_WIN64_STACK,
    JANET_WIN64_REGISTER_REF,
    JANET_WIN64_STACK_REF,
    JANET_AAPCS64_GENERAL,
    JANET_AAPCS64_SSE,
    JANET_AAPCS64_GENERAL_REF,
    JANET_AAPCS64_STACK,
    JANET_AAPCS64_STACK_REF,
    JANET_AAPCS64_NONE
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
    JANET_FFI_CC_WIN_64,
    JANET_FFI_CC_AAPCS64
} JanetFFICallingConvention;

#ifdef JANET_FFI_WIN64_ENABLED
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_WIN_64
#elif defined(JANET_FFI_SYSV64_ENABLED)
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_SYSV_64
#elif defined(JANET_FFI_AAPCS64_ENABLED)
#define JANET_FFI_CC_DEFAULT JANET_FFI_CC_AAPCS64
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

typedef struct {
    void *function_pointer;
    size_t size;
} JanetFFIJittedFn;

static const JanetAbstractType janet_struct_type = {
    "core/ffi-struct",
    NULL,
    struct_mark,
    JANET_ATEND_GCMARK
};

static int janet_ffijit_gc(void *p, size_t s) {
    (void) s;
    JanetFFIJittedFn *fn = p;
    if (fn->function_pointer == NULL) return 0;
#ifdef JANET_FFI_JIT
#ifdef JANET_WINDOWS
    VirtualFree(fn->function_pointer, fn->size, MEM_RELEASE);
#else
    munmap(fn->function_pointer, fn->size);
#endif
#endif
    return 0;
}

static JanetByteView janet_ffijit_getbytes(void *p, size_t s) {
    (void) s;
    JanetFFIJittedFn *fn = p;
    JanetByteView bytes;
    bytes.bytes = fn->function_pointer;
    bytes.len = (int32_t) fn->size;
    return bytes;
}

static size_t janet_ffijit_length(void *p, size_t s) {
    (void) s;
    JanetFFIJittedFn *fn = p;
    return fn->size;
}

const JanetAbstractType janet_type_ffijit = {
    .name = "ffi/jitfn",
    .gc = janet_ffijit_gc,
    .bytes = janet_ffijit_getbytes,
    .length = janet_ffijit_length
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
#ifdef JANET_FFI_AAPCS64_ENABLED
    if (!janet_cstrcmp(name, "aapcs64")) return JANET_FFI_CC_AAPCS64;
#endif
    if (!janet_cstrcmp(name, "default")) return JANET_FFI_CC_DEFAULT;
    janet_panicf("unknown calling convention %s", name);
}

static JanetFFIPrimType decode_ffi_prim(const uint8_t *name) {
    if (!janet_cstrcmp(name, "void")) return JANET_FFI_TYPE_VOID;
    if (!janet_cstrcmp(name, "bool")) return JANET_FFI_TYPE_BOOL;
    if (!janet_cstrcmp(name, "ptr")) return JANET_FFI_TYPE_PTR;
    if (!janet_cstrcmp(name, "pointer")) return JANET_FFI_TYPE_PTR;
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
    st->field_count = 0;
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
        if (el_align <= 0) janet_panicf("bad field type %V", argv[j]);
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
    st->field_count = member_count;
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
            janet_panicf("bad slot #%d, expected ffi pointer convertible type, got %v", n, argv[n]);
        case JANET_POINTER:
        case JANET_STRING:
        case JANET_KEYWORD:
        case JANET_SYMBOL:
        case JANET_CFUNCTION:
            return janet_unwrap_pointer(argv[n]);
        case JANET_ABSTRACT:
            return (void *) janet_getbytes(argv, n).bytes;
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

static void *janet_ffi_get_callable_pointer(const Janet *argv, int32_t n) {
    switch (janet_type(argv[n])) {
        default:
            break;
        case JANET_POINTER:
            return janet_unwrap_pointer(argv[n]);
        case JANET_ABSTRACT:
            if (!janet_checkabstract(argv[n], &janet_type_ffijit)) break;
            return ((JanetFFIJittedFn *)janet_unwrap_abstract(argv[n]))->function_pointer;
    }
    janet_panicf("bad slot #%d, expected ffi callable pointer type, got %v", n, argv[n]);
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
static JanetFFIWordSpec sysv64_classify_ext(JanetFFIType type, size_t shift) {
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
            if (st->size > 8 && st->size <= 16) {
                /* map to pair classification */
                int has_int_lo = 0;
                int has_int_hi = 0;
                for (uint32_t i = 0; i < st->field_count; i++) {
                    JanetFFIWordSpec next_class = sysv64_classify_ext(st->fields[i].type, shift + st->fields[i].offset);
                    switch (next_class) {
                        default:
                            break;
                        case JANET_SYSV64_INTEGER:
                            if (shift + st->fields[i].offset + type_size(st->fields[i].type) <= 8) {
                                has_int_lo = 1;
                            } else {
                                has_int_hi = 2;
                            }
                            break;
                        case JANET_SYSV64_PAIR_INTINT:
                            has_int_lo = 1;
                            has_int_hi = 2;
                            break;
                        case JANET_SYSV64_PAIR_INTSSE:
                            has_int_lo = 1;
                            break;
                        case JANET_SYSV64_PAIR_SSEINT:
                            has_int_hi = 2;
                            break;
                            break;
                    }
                }
                switch (has_int_hi + has_int_lo) {
                    case 0:
                        clazz = JANET_SYSV64_PAIR_SSESSE;
                        break;
                    case 1:
                        clazz = JANET_SYSV64_PAIR_INTSSE;
                        break;
                    case 2:
                        clazz = JANET_SYSV64_PAIR_SSEINT;
                        break;
                    case 3:
                        clazz = JANET_SYSV64_PAIR_INTINT;
                        break;
                }
            } else {
                /* Normal struct classification */
                for (uint32_t i = 0; i < st->field_count; i++) {
                    JanetFFIWordSpec next_class = sysv64_classify_ext(st->fields[i].type, shift + st->fields[i].offset);
                    if (next_class != clazz) {
                        if (clazz == JANET_SYSV64_NO_CLASS) {
                            clazz = next_class;
                        } else if (clazz == JANET_SYSV64_MEMORY || next_class == JANET_SYSV64_MEMORY) {
                            clazz = JANET_SYSV64_MEMORY;
                        } else if (clazz == JANET_SYSV64_INTEGER || next_class == JANET_SYSV64_INTEGER) {
                            clazz = JANET_SYSV64_INTEGER;
                        } else {
                            clazz = JANET_SYSV64_SSE;
                        }
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
static JanetFFIWordSpec sysv64_classify(JanetFFIType type) {
    return sysv64_classify_ext(type, 0);
}
#endif

#ifdef JANET_FFI_AAPCS64_ENABLED
/* Procedure Call Standard for the Arm® 64-bit Architecture (AArch64) 2023Q3 – October 6, 2023
 * See section 6.8.2 Parameter passing rules.
 * https://github.com/ARM-software/abi-aa/releases/download/2023Q3/aapcs64.pdf
 *
 * Additional documentation needed for Apple platforms.
 * https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms */

#define JANET_FFI_AAPCS64_FORCE_STACK_ALIGN(ptr, alignment) (ptr = ((ptr) + ((alignment) - 1)) & ~((alignment) - 1))
#if !defined(JANET_APPLE)
#define JANET_FFI_AAPCS64_STACK_ALIGN(ptr, alignment) ((void) alignment, JANET_FFI_AAPCS64_FORCE_STACK_ALIGN(ptr, 8))
#else
#define JANET_FFI_AAPCS64_STACK_ALIGN(ptr, alignment) JANET_FFI_AAPCS64_FORCE_STACK_ALIGN(ptr, alignment)
#endif

typedef struct {
    uint64_t a;
    uint64_t b;
} Aapcs64Variant1ReturnGeneral;

typedef struct {
    double a;
    double b;
    double c;
    double d;
} Aapcs64Variant2ReturnSse;

/* Workaround for passing a return value pointer through x8.
 * Limits struct returns to 128 bytes. */
typedef struct {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    uint64_t e;
    uint64_t f;
    uint64_t g;
    uint64_t h;
    uint64_t i;
    uint64_t j;
    uint64_t k;
    uint64_t l;
    uint64_t m;
    uint64_t n;
    uint64_t o;
    uint64_t p;
} Aapcs64Variant3ReturnPointer;

static JanetFFIWordSpec aapcs64_classify(JanetFFIType type) {
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
            return JANET_AAPCS64_GENERAL;
        case JANET_FFI_TYPE_DOUBLE:
        case JANET_FFI_TYPE_FLOAT:
            return JANET_AAPCS64_SSE;
        case JANET_FFI_TYPE_STRUCT: {
            JanetFFIStruct *st = type.st;
            if (st->field_count <= 4 && aapcs64_classify(st->fields[0].type) == JANET_AAPCS64_SSE) {
                bool is_hfa = true;
                for (uint32_t i = 1; i < st->field_count; i++) {
                    if (st->fields[0].type.prim != st->fields[i].type.prim) {
                        is_hfa = false;
                        break;
                    }
                }
                if (is_hfa) {
                    return JANET_AAPCS64_SSE;
                }
            }

            if (type_size(type) > 16) {
                return JANET_AAPCS64_GENERAL_REF;
            }

            return JANET_AAPCS64_GENERAL;
        }
        case JANET_FFI_TYPE_VOID:
            return JANET_AAPCS64_NONE;
        default:
            janet_panic("nyi");
            return JANET_AAPCS64_NONE;
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
            if (ret_size != 0 && ret_size != 1 && ret_size != 2 && ret_size != 4 && ret_size != 8) {
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
            if (ret_spec == JANET_SYSV64_PAIR_INTSSE) variant = 2;
            if (ret_spec == JANET_SYSV64_PAIR_SSEINT) variant = 3;
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
                    }
                    break;
                    case JANET_SYSV64_SSE: {
                        if (next_fp_register < max_fp_regs) {
                            mappings[i].offset = next_fp_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                    }
                    break;
                    case JANET_SYSV64_MEMORY: {
                        mappings[i].offset = stack_count;
                        stack_count += el_size;
                    }
                    break;
                    case JANET_SYSV64_PAIR_INTINT: {
                        if (next_register + 1 < max_regs) {
                            mappings[i].offset = next_register++;
                            mappings[i].offset2 = next_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                    }
                    break;
                    case JANET_SYSV64_PAIR_INTSSE: {
                        if (next_register < max_regs && next_fp_register < max_fp_regs) {
                            mappings[i].offset = next_register++;
                            mappings[i].offset2 = next_fp_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                    }
                    break;
                    case JANET_SYSV64_PAIR_SSEINT: {
                        if (next_register < max_regs && next_fp_register < max_fp_regs) {
                            mappings[i].offset = next_fp_register++;
                            mappings[i].offset2 = next_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                    }
                    break;
                    case JANET_SYSV64_PAIR_SSESSE: {
                        if (next_fp_register < max_fp_regs) {
                            mappings[i].offset = next_fp_register++;
                            mappings[i].offset2 = next_fp_register++;
                        } else {
                            mappings[i].spec = JANET_SYSV64_MEMORY;
                            mappings[i].offset = stack_count;
                            stack_count += el_size;
                        }
                    }
                    break;
                }
            }
        }
        break;
#endif

#ifdef JANET_FFI_AAPCS64_ENABLED
        case JANET_FFI_CC_AAPCS64: {
            uint32_t next_general_reg = 0;
            uint32_t next_fp_reg = 0;
            uint32_t stack_offset = 0;
            uint32_t ref_stack_offset = 0;

            JanetFFIWordSpec ret_spec = aapcs64_classify(ret_type);
            ret.spec = ret_spec;
            if (ret_spec == JANET_AAPCS64_SSE) {
                variant = 1;
            } else if (ret_spec == JANET_AAPCS64_GENERAL_REF) {
                if (type_size(ret_type) > sizeof(Aapcs64Variant3ReturnPointer)) {
                    janet_panic("return value bigger than supported");
                }
                variant = 2;
            } else {
                variant = 0;
            }

            for (uint32_t i = 0; i < arg_count; i++) {
                mappings[i].type = decode_ffi_type(argv[i + 2]);
                mappings[i].spec = aapcs64_classify(mappings[i].type);
                size_t arg_size = type_size(mappings[i].type);

                switch (mappings[i].spec) {
                    case JANET_AAPCS64_GENERAL: {
                        bool arg_is_struct = mappings[i].type.prim == JANET_FFI_TYPE_STRUCT;
                        uint32_t needed_registers = (arg_size + 7) / 8;
                        if (next_general_reg + needed_registers <= 8) {
                            mappings[i].offset = next_general_reg;
                            next_general_reg += needed_registers;
                        } else {
                            size_t arg_align = arg_is_struct ? 8 : type_align(mappings[i].type);
                            mappings[i].spec = JANET_AAPCS64_STACK;
                            mappings[i].offset = JANET_FFI_AAPCS64_STACK_ALIGN(stack_offset, arg_align);
#if !defined(JANET_APPLE)
                            stack_offset += arg_size > 8 ? arg_size : 8;
#else
                            stack_offset += arg_size;
#endif
                            next_general_reg = 8;
                        }
                        break;
                    }
                    case JANET_AAPCS64_GENERAL_REF:
                        if (next_general_reg < 8) {
                            mappings[i].offset = next_general_reg++;
                        } else {
                            mappings[i].spec = JANET_AAPCS64_STACK_REF;
                            mappings[i].offset = JANET_FFI_AAPCS64_STACK_ALIGN(stack_offset, 8);
                            stack_offset += 8;
                        }
                        mappings[i].offset2 = JANET_FFI_AAPCS64_FORCE_STACK_ALIGN(ref_stack_offset, 8);
                        ref_stack_offset += arg_size;
                        break;
                    case JANET_AAPCS64_SSE: {
                        uint32_t needed_registers = (arg_size + 7) / 8;
                        if (next_fp_reg + needed_registers <= 8) {
                            mappings[i].offset = next_fp_reg;
                            next_fp_reg += needed_registers;
                        } else {
                            mappings[i].spec = JANET_AAPCS64_STACK;
                            mappings[i].offset = JANET_FFI_AAPCS64_STACK_ALIGN(stack_offset, 8);
#if !defined(JANET_APPLE)
                            stack_offset += 8;
#else
                            stack_offset += arg_size;
#endif
                        }
                        break;
                    }
                    default:
                        janet_panic("nyi");
                }
            }

            stack_offset = (stack_offset + 15) & ~0xFUL;
            ref_stack_offset = (ref_stack_offset + 15) & ~0xFUL;
            stack_count = stack_offset + ref_stack_offset;

            for (uint32_t i = 0; i < arg_count; i++) {
                if (mappings[i].spec == JANET_AAPCS64_GENERAL_REF || mappings[i].spec == JANET_AAPCS64_STACK_REF) {
                    mappings[i].offset2 = stack_offset + mappings[i].offset2;
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
typedef struct {
    uint64_t x;
    double y;
} sysv64_intsse_return;
typedef struct {
    double y;
    uint64_t x;
} sysv64_sseint_return;
typedef sysv64_int_return janet_sysv64_variant_1(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);
typedef sysv64_sse_return janet_sysv64_variant_2(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);
typedef sysv64_intsse_return janet_sysv64_variant_3(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);
typedef sysv64_sseint_return janet_sysv64_variant_4(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
        double r1, double r2, double r3, double r4, double r5, double r6, double r7, double r8);

static Janet janet_ffi_sysv64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    union {
        sysv64_int_return int_return;
        sysv64_sse_return sse_return;
        sysv64_sseint_return sseint_return;
        sysv64_intsse_return intsse_return;
    } retu;
    uint64_t pair[2];
    uint64_t regs[6];
    double fp_regs[8];
    JanetFFIWordSpec ret_spec = signature->ret.spec;
    void *ret_mem = &retu.int_return;
    if (ret_spec == JANET_SYSV64_MEMORY) {
        ret_mem = alloca(type_size(signature->ret.type));
        regs[0] = (uint64_t) ret_mem;
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
            case JANET_SYSV64_PAIR_INTINT:
                janet_ffi_write_one(pair, argv, n, arg.type, JANET_FFI_MAX_RECUR);
                regs[arg.offset] = pair[0];
                regs[arg.offset2] = pair[1];
                continue;
            case JANET_SYSV64_PAIR_INTSSE:
                janet_ffi_write_one(pair, argv, n, arg.type, JANET_FFI_MAX_RECUR);
                regs[arg.offset] = pair[0];
                ((uint64_t *) fp_regs)[arg.offset2] = pair[1];
                continue;
            case JANET_SYSV64_PAIR_SSEINT:
                janet_ffi_write_one(pair, argv, n, arg.type, JANET_FFI_MAX_RECUR);
                ((uint64_t *) fp_regs)[arg.offset] = pair[0];
                regs[arg.offset2] = pair[1];
                continue;
            case JANET_SYSV64_PAIR_SSESSE:
                janet_ffi_write_one(pair, argv, n, arg.type, JANET_FFI_MAX_RECUR);
                ((uint64_t *) fp_regs)[arg.offset] = pair[0];
                ((uint64_t *) fp_regs)[arg.offset2] = pair[1];
                continue;
        }
        janet_ffi_write_one(to, argv, n, arg.type, JANET_FFI_MAX_RECUR);
    }

    switch (signature->variant) {
        case 0:
            retu.int_return = ((janet_sysv64_variant_1 *)(function_pointer))(
                                  regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                                  fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                  fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
        case 1:
            retu.sse_return = ((janet_sysv64_variant_2 *)(function_pointer))(
                                  regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                                  fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                  fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
        case 2:
            retu.intsse_return = ((janet_sysv64_variant_3 *)(function_pointer))(
                                     regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                                     fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                     fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
        case 3:
            retu.sseint_return = ((janet_sysv64_variant_4 *)(function_pointer))(
                                     regs[0], regs[1], regs[2], regs[3], regs[4], regs[5],
                                     fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                     fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
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
    size_t stack_size = signature->stack_count * 8;
    size_t stack_shift = 2;
    uint64_t *stack = alloca(stack_size);
    for (uint32_t i = 0; i < signature->arg_count; i++) {
        int32_t n = i + 2;
        JanetFFIMapping arg = signature->args[i];
        if (arg.spec == JANET_WIN64_STACK) {
            janet_ffi_write_one(stack + arg.offset, argv, n, arg.type, JANET_FFI_MAX_RECUR);
        } else if (arg.spec == JANET_WIN64_STACK_REF) {
            uint8_t *ptr = (uint8_t *)(stack + arg.offset2);
            janet_ffi_write_one(ptr, argv, n, arg.type, JANET_FFI_MAX_RECUR);
            stack[arg.offset] = (uint64_t)(ptr - stack_shift * sizeof(uint64_t));
        } else if (arg.spec == JANET_WIN64_REGISTER_REF) {
            uint8_t *ptr = (uint8_t *)(stack + arg.offset2);
            janet_ffi_write_one(ptr, argv, n, arg.type, JANET_FFI_MAX_RECUR);
            regs[arg.offset].integer = (uint64_t)(ptr - stack_shift * sizeof(uint64_t));
        } else {
            janet_ffi_write_one((uint8_t *) &regs[arg.offset].integer, argv, n, arg.type, JANET_FFI_MAX_RECUR);
        }
    }

    /* hack to get proper stack placement and avoid clobbering from logic above - shift stack down, otherwise we have issues.
     * Technically, this writes into 16 bytes of unallocated stack memory */
#ifdef JANET_MINGW
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    if (stack_size) memmove(stack - stack_shift, stack, stack_size);
#ifdef JANET_MINGW
#pragma GCC diagnostic pop
#endif

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

#ifdef JANET_FFI_AAPCS64_ENABLED

static void janet_ffi_aapcs64_standard_callback(void *ctx, void *userdata) {
    janet_ffi_trampoline(ctx, userdata);
}

typedef Aapcs64Variant1ReturnGeneral janet_aapcs64_variant_1(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7,
        double v0, double v1, double v2, double v3, double v4, double v5, double v6, double v7);
typedef Aapcs64Variant2ReturnSse janet_aapcs64_variant_2(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7,
        double v0, double v1, double v2, double v3, double v4, double v5, double v6, double v7);
typedef Aapcs64Variant3ReturnPointer janet_aapcs64_variant_3(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7,
        double v0, double v1, double v2, double v3, double v4, double v5, double v6, double v7);


static Janet janet_ffi_aapcs64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    union {
        Aapcs64Variant1ReturnGeneral general_return;
        Aapcs64Variant2ReturnSse sse_return;
        Aapcs64Variant3ReturnPointer pointer_return;
    } retu;
    uint64_t regs[8];
    double fp_regs[8];
    void *ret_mem = &retu.general_return;

    /* Apple's stack values do not need to be 8-byte aligned,
     * thus all stack offsets refer to actual byte positions. */
    uint8_t *stack = alloca(signature->stack_count);
#if defined(JANET_APPLE)
    /* Values must be zero-extended by the caller instead of the callee. */
    memset(stack, 0, signature->stack_count);
#endif
    for (uint32_t i = 0; i < signature->arg_count; i++) {
        int32_t n = i + 2;
        JanetFFIMapping arg = signature->args[i];
        void *to = NULL;

        switch (arg.spec) {
            case JANET_AAPCS64_GENERAL:
                to = regs + arg.offset;
                break;
            case JANET_AAPCS64_GENERAL_REF:
                to = stack + arg.offset2;
                regs[arg.offset] = (uint64_t) to;
                break;
            case JANET_AAPCS64_SSE:
                to = fp_regs + arg.offset;
                break;
            case JANET_AAPCS64_STACK:
                to = stack + arg.offset;
                break;
            case JANET_AAPCS64_STACK_REF:
                to = stack + arg.offset2;
                uint64_t *ptr = (uint64_t *) stack + arg.offset;
                *ptr = (uint64_t) to;
                break;
            default:
                janet_panic("nyi");
        }

        if (to) {
            janet_ffi_write_one(to, argv, n, arg.type, JANET_FFI_MAX_RECUR);
        }
    }

    switch (signature->variant) {
        case 0:
            retu.general_return = ((janet_aapcs64_variant_1 *)(function_pointer))(
                                      regs[0], regs[1], regs[2], regs[3],
                                      regs[4], regs[5], regs[6], regs[7],
                                      fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                      fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
        case 1:
            retu.sse_return = ((janet_aapcs64_variant_2 *)(function_pointer))(
                                  regs[0], regs[1], regs[2], regs[3],
                                  regs[4], regs[5], regs[6], regs[7],
                                  fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                  fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
            break;
        case 2: {
            retu.pointer_return = ((janet_aapcs64_variant_3 *)(function_pointer))(
                                      regs[0], regs[1], regs[2], regs[3],
                                      regs[4], regs[5], regs[6], regs[7],
                                      fp_regs[0], fp_regs[1], fp_regs[2], fp_regs[3],
                                      fp_regs[4], fp_regs[5], fp_regs[6], fp_regs[7]);
        }
    }

    return janet_ffi_read_one(ret_mem, signature->ret.type, JANET_FFI_MAX_RECUR);
}

#endif

/* Allocate executable memory chunks in sizes of a page. Ideally we would keep
 * an allocator around so that multiple JIT allocations would point to the same
 * region but it isn't really worth it. */
#define FFI_PAGE_MASK 0xFFF

JANET_CORE_FN(cfun_ffi_jitfn,
              "(ffi/jitfn bytes)",
              "Create an abstract type that can be used as the pointer argument to `ffi/call`. The content "
              "of `bytes` is architecture specific machine code that will be copied into executable memory.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_JIT);
    janet_fixarity(argc, 1);
    JanetByteView bytes = janet_getbytes(argv, 0);

    /* Quick hack to align to page boundary, we should query OS. FIXME */
    size_t alloc_size = ((size_t) bytes.len + FFI_PAGE_MASK) & ~FFI_PAGE_MASK;

#ifdef JANET_FFI_JIT
#ifdef JANET_EV
    JanetFFIJittedFn *fn = janet_abstract_threaded(&janet_type_ffijit, sizeof(JanetFFIJittedFn));
#else
    JanetFFIJittedFn *fn = janet_abstract(&janet_type_ffijit, sizeof(JanetFFIJittedFn));
#endif
    fn->function_pointer = NULL;
    fn->size = 0;
#ifdef JANET_WINDOWS
    void *ptr = VirtualAlloc(NULL, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(MAP_ANONYMOUS)
    void *ptr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#elif defined(MAP_ANON)
    /* macos doesn't have MAP_ANONYMOUS */
    void *ptr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
    /* -std=c99 gets in the way */
    /* #define MAP_ANONYMOUS 0x20 should work, though. */
    void *ptr = mmap(0, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
#endif
    if (!ptr) {
        janet_panic("failed to memory map writable memory");
    }
    memcpy(ptr, bytes.bytes, bytes.len);
#ifdef JANET_WINDOWS
    DWORD old = 0;
    if (!VirtualProtect(ptr, alloc_size, PAGE_EXECUTE_READ, &old)) {
        janet_panic("failed to make mapped memory executable");
    }
#else
    if (mprotect(ptr, alloc_size, PROT_READ | PROT_EXEC) == -1) {
        janet_panic("failed to make mapped memory executable");
    }
#endif
    fn->size = alloc_size;
    fn->function_pointer = ptr;
    return janet_wrap_abstract(fn);
#else
    janet_panic("ffi/jitfn not available on this platform");
#endif
}

JANET_CORE_FN(cfun_ffi_call,
              "(ffi/call pointer signature & args)",
              "Call a raw pointer as a function pointer. The function signature specifies "
              "how Janet values in `args` are converted to native machine types.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_arity(argc, 2, -1);
    void *function_pointer = janet_ffi_get_callable_pointer(argv, 0);
    JanetFFISignature *signature = janet_getabstract(argv, 1, &janet_signature_type);
    janet_fixarity(argc - 2, signature->arg_count);
    switch (signature->cc) {
        default:
        case JANET_FFI_CC_NONE:
            (void) function_pointer;
            janet_panic("calling convention not supported");
#ifdef JANET_FFI_WIN64_ENABLED
        case JANET_FFI_CC_WIN_64:
            return janet_ffi_win64(signature, function_pointer, argv);
#endif
#ifdef JANET_FFI_SYSV64_ENABLED
        case JANET_FFI_CC_SYSV_64:
            return janet_ffi_sysv64(signature, function_pointer, argv);
#endif
#ifdef JANET_FFI_AAPCS64_ENABLED
        case JANET_FFI_CC_AAPCS64:
            return janet_ffi_aapcs64(signature, function_pointer, argv);
#endif
    }
}

JANET_CORE_FN(cfun_ffi_buffer_write,
              "(ffi/write ffi-type data &opt buffer index)",
              "Append a native type to a buffer such as it would appear in memory. This can be used "
              "to pass pointers to structs in the ffi, or send C/C++/native structs over the network "
              "or to files. Returns a modified buffer or a new buffer if one is not supplied.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_arity(argc, 2, 4);
    JanetFFIType type = decode_ffi_type(argv[0]);
    uint32_t el_size = (uint32_t) type_size(type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, el_size);
    int32_t index = janet_optnat(argv, argc, 3, 0);
    int32_t old_count = buffer->count;
    if (index > old_count) janet_panic("index out of bounds");
    buffer->count = index;
    janet_buffer_extra(buffer, el_size);
    buffer->count = old_count;
    memset(buffer->data + index, 0, el_size);
    janet_ffi_write_one(buffer->data + index, argv, 1, type, JANET_FFI_MAX_RECUR);
    index += el_size;
    if (buffer->count < index) buffer->count = index;
    return janet_wrap_buffer(buffer);
}

JANET_CORE_FN(cfun_ffi_buffer_read,
              "(ffi/read ffi-type bytes &opt offset)",
              "Parse a native struct out of a buffer and convert it to normal Janet data structures. "
              "This function is the inverse of `ffi/write`. `bytes` can also be a raw pointer, although "
              "this is unsafe.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
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
#ifdef JANET_FFI_AAPCS64_ENABLED
        case JANET_FFI_CC_AAPCS64:
            return janet_wrap_pointer(janet_ffi_aapcs64_standard_callback);
#endif
    }
}

JANET_CORE_FN(janet_core_raw_native,
              "(ffi/native &opt path)",
              "Load a shared object or dll from the given path, and do not extract"
              " or run any code from it. This is different than `native`, which will "
              "run initialization code to get a module table. If `path` is nil, opens the current running binary. "
              "Returns a `core/native`.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_DEFINE);
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
    janet_sandbox_assert(JANET_SANDBOX_FFI_DEFINE);
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
    janet_sandbox_assert(JANET_SANDBOX_FFI_DEFINE);
    janet_fixarity(argc, 1);
    JanetAbstractNative *anative = janet_getabstract(argv, 0, &janet_native_type);
    if (anative->closed) janet_panic("native object already closed");
    if (anative->is_self) janet_panic("cannot close self");
    anative->closed = 1;
    free_clib(anative->clib);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_ffi_malloc,
              "(ffi/malloc size)",
              "Allocates memory directly using the janet memory allocator. Memory allocated in this way must be freed manually! Returns a raw pointer, or nil if size = 0.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_fixarity(argc, 1);
    size_t size = janet_getsize(argv, 0);
    if (size == 0) return janet_wrap_nil();
    return janet_wrap_pointer(janet_malloc(size));
}

JANET_CORE_FN(cfun_ffi_free,
              "(ffi/free pointer)",
              "Free memory allocated with `ffi/malloc`. Returns nil.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_fixarity(argc, 1);
    if (janet_checktype(argv[0], JANET_NIL)) return janet_wrap_nil();
    void *pointer = janet_getpointer(argv, 0);
    janet_free(pointer);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_ffi_pointer_buffer,
              "(ffi/pointer-buffer pointer capacity &opt count offset)",
              "Create a buffer from a pointer. The underlying memory of the buffer will not be "
              "reallocated or freed by the garbage collector, allowing unmanaged, mutable memory "
              "to be manipulated with buffer functions. Attempts to resize or extend the buffer "
              "beyond its initial capacity will raise an error. As with many FFI functions, this is memory "
              "unsafe and can potentially allow out of bounds memory access. Returns a new buffer.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_arity(argc, 2, 4);
    void *pointer = janet_getpointer(argv, 0);
    int32_t capacity = janet_getnat(argv, 1);
    int32_t count = janet_optnat(argv, argc, 2, 0);
    int64_t offset = janet_optinteger64(argv, argc, 3, 0);
    uint8_t *offset_pointer = ((uint8_t *) pointer) + offset;
    return janet_wrap_buffer(janet_pointer_buffer_unsafe(offset_pointer, capacity, count));
}

JANET_CORE_FN(cfun_ffi_pointer_cfunction,
              "(ffi/pointer-cfunction pointer &opt name source-file source-line)",
              "Create a C Function from a raw pointer. Optionally give the cfunction a name and "
              "source location for stack traces and debugging.") {
    janet_sandbox_assert(JANET_SANDBOX_FFI_USE);
    janet_arity(argc, 1, 4);
    void *pointer = janet_getpointer(argv, 0);
    const char *name = janet_optcstring(argv, argc, 1, NULL);
    const char *source = janet_optcstring(argv, argc, 2, NULL);
    int32_t line = janet_optinteger(argv, argc, 3, -1);
    if ((name != NULL) || (source != NULL) || (line != -1)) {
        janet_registry_put((JanetCFunction) pointer, name, NULL, source, line);
    }
    return janet_wrap_cfunction((JanetCFunction) pointer);
}

JANET_CORE_FN(cfun_ffi_supported_calling_conventions,
              "(ffi/calling-conventions)",
              "Get an array of all supported calling conventions on the current architecture. Some architectures may have some FFI "
              "functionality (ffi/malloc, ffi/free, ffi/read, ffi/write, etc.) but not support "
              "any calling conventions. This function can be used to get all supported calling conventions "
              "that can be used on this architecture. All architectures support the :none calling "
              "convention which is a placeholder that cannot be used at runtime.") {
    janet_fixarity(argc, 0);
    (void) argv;
    JanetArray *array = janet_array(4);
#ifdef JANET_FFI_WIN64_ENABLED
    janet_array_push(array, janet_ckeywordv("win64"));
#endif
#ifdef JANET_FFI_SYSV64_ENABLED
    janet_array_push(array, janet_ckeywordv("sysv64"));
#endif
#ifdef JANET_FFI_AAPCS64_ENABLED
    janet_array_push(array, janet_ckeywordv("aapcs64"));
#endif
    janet_array_push(array, janet_ckeywordv("none"));
    return janet_wrap_array(array);
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
        JANET_CORE_REG("ffi/jitfn", cfun_ffi_jitfn),
        JANET_CORE_REG("ffi/malloc", cfun_ffi_malloc),
        JANET_CORE_REG("ffi/free", cfun_ffi_free),
        JANET_CORE_REG("ffi/pointer-buffer", cfun_ffi_pointer_buffer),
        JANET_CORE_REG("ffi/pointer-cfunction", cfun_ffi_pointer_cfunction),
        JANET_CORE_REG("ffi/calling-conventions", cfun_ffi_supported_calling_conventions),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, ffi_cfuns);
}

#endif

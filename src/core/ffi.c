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
#endif

#define JANET_FFI_MAX_RECUR 64

typedef struct JanetFFIType JanetFFIType;
typedef struct JanetFFIStruct JanetFFIStruct;

typedef enum {
    JANET_FFI_TYPE_VOID,
    JANET_FFI_TYPE_BOOL,
    JANET_FFI_TYPE_PTR,
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
};

typedef struct {
    JanetFFIType type;
    size_t offset;
} JanetFFIStructMember;

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
    JANET_SYSV64_MEMORY
} JanetFFIWordSpec;

/* Describe how each Janet argument is interpreted in terms of machine words
 * that will be mapped to registers/stack. */
typedef struct {
    JanetFFIType type;
    JanetFFIWordSpec spec;
    uint32_t offset; /* point to the exact register / stack offset depending on spec. */
} JanetFFIMapping;

typedef enum {
    JANET_FFI_CC_SYSV_64
} JanetFFICallingConvention;

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

static JanetFFIType prim_type(JanetFFIPrimType pt) {
    JanetFFIType t;
    t.prim = pt;
    t.st = NULL;
    return t;
}

static size_t type_size(JanetFFIType t) {
    if (t.prim == JANET_FFI_TYPE_STRUCT) {
        return t.st->size;
    } else {
        return janet_ffi_type_info[t.prim].size;
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
    if (!janet_cstrcmp(name, "sysv64")) return JANET_FFI_CC_SYSV_64;
    if (!janet_cstrcmp(name, "default")) {
        return JANET_FFI_CC_SYSV_64;
    }
    janet_panicf("unknown calling convention %s", name);
}

static JanetFFIPrimType decode_ffi_prim(const uint8_t *name) {
    if (!janet_cstrcmp(name, "void")) return JANET_FFI_TYPE_VOID;
    if (!janet_cstrcmp(name, "bool")) return JANET_FFI_TYPE_BOOL;
    if (!janet_cstrcmp(name, "ptr")) return JANET_FFI_TYPE_PTR;
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
    if (!janet_cstrcmp(name, "ushort")) return JANET_FFI_TYPE_UINT16;
    if (!janet_cstrcmp(name, "uint")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "ulong")) return JANET_FFI_TYPE_UINT64;
    janet_panicf("unknown machine type %s", name);
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
        }
        st->fields[i].type = decode_ffi_type(argv[j]);
        size_t el_size = type_size(st->fields[i].type);
        size_t el_align = type_align(st->fields[i].type);
        if (all_packed || pack_one) {
            if (st->size % el_align != 0) is_aligned = 0;
            st->fields[i].offset = st->size;
            st->size += el_size;
        } else {
            if (el_align > st->align) st->align = el_align;
            st->fields[i].offset = (((st->size + el_align - 1) / el_align) * el_align);
            st->size = el_size + st->fields[i].offset;
        }
        i++;
    }
    st->is_aligned = is_aligned;
    return st;
}

static JanetFFIType decode_ffi_type(Janet x) {
    if (janet_checktype(x, JANET_KEYWORD)) {
        return prim_type(decode_ffi_prim(janet_unwrap_keyword(x)));
    }
    JanetFFIType ret;
    ret.prim = JANET_FFI_TYPE_STRUCT;
    if (janet_checkabstract(x, &janet_struct_type)) {
        ret.st = janet_unwrap_abstract(x);
        return ret;
    }
    int32_t len;
    const Janet *els;
    if (janet_indexed_view(x, &els, &len)) {
        ret.st = build_struct_type(len, els);
        return ret;
    } else {
        janet_panicf("bad native type %v", x);
    }
}

JANET_CORE_FN(cfun_ffi_struct,
              "(native-struct & types)",
              "Create a struct type descriptor that can be used to pass structs into native functions. ") {
    janet_arity(argc, 1, -1);
    return janet_wrap_abstract(build_struct_type(argc, argv));
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
                janet_ffi_write_one(to + st->fields[i].offset, els.items, i, tp, recur - 1);
            }
        }
        break;
        case JANET_FFI_TYPE_DOUBLE:
            ((double *)(to))[0] = janet_getnumber(argv, n);
            break;
        case JANET_FFI_TYPE_FLOAT:
            ((float *)(to))[0] = janet_getnumber(argv, n);
            break;
        case JANET_FFI_TYPE_PTR:
            ((void **)(to))[0] = janet_ffi_getpointer(argv, n);
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
            ((uint8_t *)(to))[0] = janet_getuinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT16:
            ((uint16_t *)(to))[0] = janet_getuinteger64(argv, n);
            break;
        case JANET_FFI_TYPE_UINT32:
            ((uint32_t *)(to))[0] = janet_getuinteger64(argv, n);
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
        case JANET_FFI_TYPE_PTR:
            return janet_wrap_pointer(((void **)(from))[0]);
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

/* AMD64 ABI Draft 0.99.7 – November 17, 2014 – 15:08
 * See section 3.2.3 Parameter Passing */
static JanetFFIWordSpec sysv64_classify(JanetFFIType type) {
    switch (type.prim) {
        case JANET_FFI_TYPE_PTR:
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

JANET_CORE_FN(cfun_ffi_signature,
              "(native-signature calling-convention ret-type & arg-types)",
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
        0
    };
    JanetFFIMapping mappings[JANET_FFI_MAX_ARGS];
    for (int i = 0; i < JANET_FFI_MAX_ARGS; i++) mappings[i] = void_mapping();
    switch (cc) {
        default:
            janet_panicf("calling convention %v unsupported", argv[0]);
            break;
        case JANET_FFI_CC_SYSV_64: {
            JanetFFIWordSpec ret_spec = sysv64_classify(ret.type);
            if (ret_spec == JANET_SYSV64_SSE) variant = 1;
            ret.spec = ret_spec;

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

                /* Invert stack */
                for (uint32_t i = 0; i < arg_count; i++) {
                    if (mappings[i].spec == JANET_SYSV64_MEMORY) {
                        uint32_t old_offset = mappings[i].offset;
                        size_t el_size = type_size(mappings[i].type);
                        mappings[i].offset = stack_count - ((el_size + 7) / 8) - old_offset;
                    }
                }
            }
        }
        break;
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

static void janet_ffi_sysv64_standard_callback(void *ctx, void *userdata) {
    janet_ffi_trampoline(ctx, userdata);
}

static Janet janet_ffi_sysv64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    uint64_t ret[2];
    uint64_t regs[6];
    uint64_t fp_regs[8];
    JanetFFIWordSpec ret_spec = signature->ret.spec;
    void *ret_mem = ret;
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
                to = fp_regs + arg.offset;
                break;
            case JANET_SYSV64_MEMORY:
                to = stack + arg.offset;
                break;
        }
        janet_ffi_write_one(to, argv, n, arg.type, JANET_FFI_MAX_RECUR);
    }

    /* !!ACHTUNG!! */

#define FFI_ASM_PRELUDE \
    "mov %3, %%rdi\n\t" \
    "mov %4, %%rsi\n\t" \
    "mov %5, %%rdx\n\t" \
    "mov %6, %%rcx\n\t" \
    "mov %7, %%r8\n\t" \
    "mov %8, %%r9\n\t" \
    "movq %9, %%xmm0\n\t" \
    "movq %10, %%xmm1\n\t" \
    "movq %11, %%xmm2\n\t" \
    "movq %12, %%xmm3\n\t" \
    "movq %13, %%xmm4\n\t" \
    "movq %14, %%xmm5\n\t" \
    "movq %15, %%xmm6\n\t" \
    "movq %16, %%xmm7\n\t"
#define FFI_ASM_OUTPUTS "=g" (ret[0]), "=g" (ret[1])
#define FFI_ASM_INPUTS \
    "g"(function_pointer), \
    "g"(regs[0]), \
    "g"(regs[1]), \
    "g"(regs[2]), \
    "g"(regs[3]), \
    "g"(regs[4]), \
    "g"(regs[5]), \
    "g"(fp_regs[0]), \
    "g"(fp_regs[1]), \
    "g"(fp_regs[2]), \
    "g"(fp_regs[3]), \
    "g"(fp_regs[4]), \
    "g"(fp_regs[5]), \
    "g"(fp_regs[6]), \
    "g"(fp_regs[7])

    switch (signature->variant) {
        default:
        /* fallthrough */
        case 0:
            __asm__(FFI_ASM_PRELUDE
                    "call *%2\n\t"
                    "mov %%rax, %0\n\t"
                    "mov %%rdx, %1"
                    : FFI_ASM_OUTPUTS
                    : FFI_ASM_INPUTS
                    : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11");
            break;
        case 1:
            __asm__(FFI_ASM_PRELUDE
                    "call *%2\n\t"
                    "movq %%xmm0, %0\n\t"
                    "movq %%xmm1, %1"
                    : FFI_ASM_OUTPUTS
                    : FFI_ASM_INPUTS
                    : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11");
            break;
    }

#undef FFI_ASM_PRELUDE
#undef FFI_ASM_OUTPUTS
#undef FFI_ASM_INPUTS

    return janet_ffi_read_one(ret_mem, signature->ret.type, JANET_FFI_MAX_RECUR);
}

JANET_CORE_FN(cfun_ffi_call,
              "(native-call pointer signature & args)",
              "Call a raw pointer as a function pointer. The function signature specifies "
              "how Janet values in `args` are converted to native machine types.") {
    janet_arity(argc, 2, -1);
    void *function_pointer = janet_getpointer(argv, 0);
    JanetFFISignature *signature = janet_getabstract(argv, 1, &janet_signature_type);
    janet_fixarity(argc - 2, signature->arg_count);
    switch (signature->cc) {
        default:
            janet_panic("unsupported calling convention");
        case JANET_FFI_CC_SYSV_64:
            return janet_ffi_sysv64(signature, function_pointer, argv);
    }
}

JANET_CORE_FN(cfun_ffi_buffer_write,
              "(native-write ffi-type data &opt buffer)",
              "Append a native tyep to a buffer such as it would appear in memory. This can be used "
              "to pass pointers to structs in the ffi, or send C/C++/native structs over the network "
              "or to files. Returns a modifed buffer or a new buffer if one is not supplied.") {
    janet_arity(argc, 2, 3);
    JanetFFIType type = decode_ffi_type(argv[0]);
    size_t el_size = type_size(type);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, el_size);
    janet_buffer_extra(buffer, el_size);
    memset(buffer->data, 0, el_size);
    janet_ffi_write_one(buffer->data, argv, 1, type, JANET_FFI_MAX_RECUR);
    buffer->count += el_size;
    return janet_wrap_buffer(buffer);
}


JANET_CORE_FN(cfun_ffi_buffer_read,
              "(native-read ffi-type bytes &opt offset)",
              "Parse a native struct out of a buffer and convert it to normal Janet data structures. "
              "This function is the inverse of `native-write`. `bytes` can also be a raw pointer, although "
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
              "(native-trampoline cc)",
              "Get a native function pointer that can be used as a callback and passed to C libraries. "
              "This callback trampoline has the signature `void trampoline(void *ctx, void *userdata)` in "
              "the given calling convention. This is the only function signature supported. "
              "It is up to the programmer to ensure that the `userdata` argument contains a janet function "
              "the will be called with one argument, `ctx` which is an opaque pointer. This pointer can "
              "be further inspected with `native-read`.") {
    janet_fixarity(argc, 1);
    JanetFFICallingConvention cc = decode_ffi_cc(janet_getkeyword(argv, 0));
    switch (cc) {
        default:
        case JANET_FFI_CC_SYSV_64:
            return janet_wrap_pointer(janet_ffi_sysv64_standard_callback);
    }
}

void janet_lib_ffi(JanetTable *env) {
    JanetRegExt ffi_cfuns[] = {
        JANET_CORE_REG("native-signature", cfun_ffi_signature),
        JANET_CORE_REG("native-call", cfun_ffi_call),
        JANET_CORE_REG("native-struct", cfun_ffi_struct),
        JANET_CORE_REG("native-write", cfun_ffi_buffer_write),
        JANET_CORE_REG("native-read", cfun_ffi_buffer_read),
        JANET_CORE_REG("native-trampoline", cfun_ffi_get_callback_trampoline),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, ffi_cfuns);
}

#endif

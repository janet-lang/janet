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

static uint64_t test_function(int32_t a, int32_t b, const char *s) {
    printf("a = %d\n", a);
    printf("b = %d\n", b);
    uint64_t ret = a + b;
    printf("string: %s\n", s);
    printf("hello from test function. Returning %lu.\n", ret);
    return ret;
}

JANET_CORE_FN(cfun_ffi_get_test_pointer,
        "(ffi/get-test-pointer)",
        "Get a test pointer to call using ffi.") {
    janet_fixarity(argc, 0);
    (void) argv;
    return janet_wrap_pointer(test_function);
}

typedef enum {
    JANET_FFI_TYPE_VOID,
    JANET_FFI_TYPE_SHORT,
    JANET_FFI_TYPE_INT,
    JANET_FFI_TYPE_LONG,
    JANET_FFI_TYPE_USHORT,
    JANET_FFI_TYPE_UINT,
    JANET_FFI_TYPE_ULONG,
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
} JanetFFIPrimType;

static const size_t janet_ffi_type_sizes[] = {
    0, /* JANET_FFI_TYPE_VOID */
    sizeof(short), /* JANET_FFI_TYPE_SHORT */
    sizeof(int), /* JANET_FFI_TYPE_INT */
    sizeof(long), /* JANET_FFI_TYPE_LONG */
    sizeof(unsigned short), /* JANET_FFI_TYPE_USHORT */
    sizeof(unsigned), /* JANET_FFI_TYPE_UINT */
    sizeof(unsigned long), /* JANET_FFI_TYPE_ULONG */
    sizeof(char), /* JANET_FFI_TYPE_BOOL */
    sizeof(void *), /* JANET_FFI_TYPE_PTR */
    sizeof(float), /* JANET_FFI_TYPE_FLOAT */
    sizeof(double), /* JANET_FFI_TYPE_DOUBLE */
    sizeof(int8_t), /* JANET_FFI_TYPE_INT8 */
    sizeof(uint8_t), /* JANET_FFI_TYPE_UINT8 */
    sizeof(int16_t), /* JANET_FFI_TYPE_INT16 */
    sizeof(uint16_t), /* JANET_FFI_TYPE_UINT16 */
    sizeof(int32_t), /* JANET_FFI_TYPE_INT32 */
    sizeof(uint32_t), /* JANET_FFI_TYPE_UINT32 */
    sizeof(int64_t), /* JANET_FFI_TYPE_INT64 */
    sizeof(uint64_t) /* JANET_FFI_TYPE_UINT64 */
};

typedef enum {
    JANET_FFI_CC_SYSV_64
} JanetFFICallingConvention;

#define JANET_FFI_MAX_REGS 16
#define JANET_FFI_MAX_STACK 32

typedef struct {
    uint32_t frame_size;
    uint32_t reg_count;
    uint32_t stack_count;
    uint32_t arg_count;
    JanetFFICallingConvention cc;
    JanetFFIPrimType ret_type;
    JanetFFIPrimType regs[JANET_FFI_MAX_REGS];
    JanetFFIPrimType stack[JANET_FFI_MAX_STACK];
} JanetFFISignature;

static const JanetAbstractType janet_signature_type = {
    "core/ffi-signature",
    JANET_ATEND_NAME
};

static JanetFFICallingConvention decode_ffi_cc(const uint8_t *name) {
    /* TODO */
    (void) name;
    return JANET_FFI_CC_SYSV_64;
}

static JanetFFIPrimType decode_ffi_prim(const uint8_t *name) {
    if (!janet_cstrcmp(name, "void")) return JANET_FFI_TYPE_VOID;
    if (!janet_cstrcmp(name, "short")) return JANET_FFI_TYPE_SHORT;
    if (!janet_cstrcmp(name, "int")) return JANET_FFI_TYPE_INT;
    if (!janet_cstrcmp(name, "long")) return JANET_FFI_TYPE_LONG;
    if (!janet_cstrcmp(name, "ushort")) return JANET_FFI_TYPE_USHORT;
    if (!janet_cstrcmp(name, "uint")) return JANET_FFI_TYPE_UINT;
    if (!janet_cstrcmp(name, "ulong")) return JANET_FFI_TYPE_ULONG;
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
    janet_panicf("unknown machine type %s", name);
}

JANET_CORE_FN(cfun_ffi_signature,
        "(ffi/signature calling-convention ret-type & arg-types)",
        "Create a function signature object that can be used to make calls "
        "with raw function pointers.") {
    janet_arity(argc, 2, -1);
    uint32_t frame_size = 0;
    uint32_t reg_count = 0;
    uint32_t stack_count = 0;
    JanetFFICallingConvention cc = decode_ffi_cc(janet_getkeyword(argv, 0));
    JanetFFIPrimType ret_type = decode_ffi_prim(janet_getkeyword(argv, 1));
    uint32_t max_regs = JANET_FFI_MAX_REGS;
    JanetFFIPrimType regs[JANET_FFI_MAX_REGS];
    JanetFFIPrimType stack[JANET_FFI_MAX_STACK];
    for (int i = 0; i < JANET_FFI_MAX_REGS; i++) regs[i] = JANET_FFI_TYPE_VOID;
    for (int i = 0; i < JANET_FFI_MAX_STACK; i++) stack[i] = JANET_FFI_TYPE_VOID;
    switch (cc) {
        default:
            break;
        case JANET_FFI_CC_SYSV_64:
            max_regs = 6;
            break;
    }
    for (int32_t i = 2; i < argc; i++) {
        JanetFFIPrimType ptype = decode_ffi_prim(janet_getkeyword(argv, i));
        if (reg_count < max_regs) {
            regs[reg_count++] = ptype;
        } else {
            stack[stack_count++] = ptype;
            frame_size += janet_ffi_type_sizes[ptype];
        }
    }
    JanetFFISignature *abst = janet_abstract(&janet_signature_type, sizeof(JanetFFISignature));
    abst->frame_size = frame_size;
    abst->reg_count = reg_count;
    abst->stack_count = stack_count;
    abst->cc = cc;
    abst->ret_type = ret_type;
    abst->arg_count = stack_count + reg_count;
    memcpy(abst->regs, regs, sizeof(JanetFFIPrimType) * JANET_FFI_MAX_REGS);
    memcpy(abst->stack, stack, sizeof(JanetFFIPrimType) * JANET_FFI_MAX_STACK);
    return janet_wrap_abstract(abst);
}

static void *janet_ffi_getpointer(const Janet *argv, int32_t n) {
    switch(janet_type(argv[n])) {
        default:
            janet_panicf("bad slot #%d, expected pointer convertable type, got %v", argv[n]);
        case JANET_POINTER:
        case JANET_STRING:
        case JANET_KEYWORD:
        case JANET_SYMBOL:
            return janet_unwrap_pointer(argv[n]);
        case JANET_BUFFER:
            return janet_unwrap_buffer(argv[n])->data;
    }
}

JANET_CORE_FN(cfun_ffi_call,
        "(ffi/call pointer signature & args)",
        "Call a raw pointer as a function pointer. The function signature specifies "
        "how Janet values in `args` are converted to native machine types.") {
    janet_arity(argc, 2, -1);
    void *function_pointer = janet_getpointer(argv, 0);
    JanetFFISignature *signature = janet_getabstract(argv, 1, &janet_signature_type);
    janet_fixarity(argc - 2, signature->arg_count);

    uint64_t regs[6];
    for (uint32_t i = 0; i < signature->reg_count; i++) {
        switch (signature->regs[i]) {
            case JANET_FFI_TYPE_FLOAT:
            case JANET_FFI_TYPE_DOUBLE:
                janet_panic("nyi");
                break;
            case JANET_FFI_TYPE_VOID:
                regs[i] = 0;
                continue;
            case JANET_FFI_TYPE_PTR:
                regs[i] = (uint64_t) janet_ffi_getpointer(argv, i + 2);
                break;
            case JANET_FFI_TYPE_BOOL:
                regs[i] = (uint64_t) janet_getboolean(argv, i + 2);
                break;
            case JANET_FFI_TYPE_SHORT:
            case JANET_FFI_TYPE_INT:
            case JANET_FFI_TYPE_INT8:
            case JANET_FFI_TYPE_INT16:
            case JANET_FFI_TYPE_INT32:
            case JANET_FFI_TYPE_INT64:
            case JANET_FFI_TYPE_LONG:
                regs[i] = (uint64_t) janet_getinteger64(argv, i + 2);
                break;
            case JANET_FFI_TYPE_USHORT:
            case JANET_FFI_TYPE_UINT:
            case JANET_FFI_TYPE_UINT8:
            case JANET_FFI_TYPE_UINT16:
            case JANET_FFI_TYPE_UINT32:
            case JANET_FFI_TYPE_UINT64:
            case JANET_FFI_TYPE_ULONG:
                regs[i] = janet_getuinteger64(argv, i + 2);
                break;
        }
    }

    /* Danger zone */
    uint64_t ret, rethi;
    __asm__("mov %3, %%rdi\n\t"
            "mov %4, %%rsi\n\t"
            "mov %5, %%rdx\n\t"
            "mov %6, %%rcx\n\t"
            "mov %7, %%r8\n\t"
            "mov %8, %%r9\n\t"
            "call *%2\n\t"
            "mov %%rax, %0\n\t"
            "mov %%rdx, %1"
            : "=g" (ret), "=g" (rethi)
            : "g"(function_pointer),
                "g"(regs[0]),
                "g"(regs[1]),
                "g"(regs[2]),
                "g"(regs[3]),
                "g"(regs[4]),
                "g"(regs[5])
            : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11"); 

    (void) rethi; /* at some point we will support more complex return types */
    switch (signature->ret_type) {
        case JANET_FFI_TYPE_FLOAT:
        case JANET_FFI_TYPE_DOUBLE:
            janet_panic("nyi");
            break;
        case JANET_FFI_TYPE_VOID:
            break;
        case JANET_FFI_TYPE_PTR:
            return janet_wrap_pointer((void *) ret);
        case JANET_FFI_TYPE_BOOL:
            return janet_wrap_boolean(ret);
        case JANET_FFI_TYPE_SHORT:
        case JANET_FFI_TYPE_INT:
        case JANET_FFI_TYPE_INT8:
        case JANET_FFI_TYPE_INT16:
        case JANET_FFI_TYPE_INT32:
            return janet_wrap_integer((int32_t) ret);
        case JANET_FFI_TYPE_INT64:
        case JANET_FFI_TYPE_LONG:
            return janet_wrap_integer((int64_t) ret);
        case JANET_FFI_TYPE_USHORT:
        case JANET_FFI_TYPE_UINT:
        case JANET_FFI_TYPE_UINT8:
        case JANET_FFI_TYPE_UINT16:
        case JANET_FFI_TYPE_UINT32:
        case JANET_FFI_TYPE_UINT64:
        case JANET_FFI_TYPE_ULONG:
            return janet_wrap_number(ret);
    }

    return janet_wrap_nil();
}

void janet_lib_ffi(JanetTable *env) {
    JanetRegExt ffi_cfuns[] = {
        JANET_CORE_REG("ffi/get-test-pointer", cfun_ffi_get_test_pointer),
        JANET_CORE_REG("ffi/signature", cfun_ffi_signature),
        JANET_CORE_REG("ffi/call", cfun_ffi_call),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, ffi_cfuns);
}

#endif

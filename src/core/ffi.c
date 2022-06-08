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

/* Custom alignof since alignof not in c99 standard */
#define ALIGNOF(type) offsetof(struct { char c; type member; }, member)

typedef struct {
    size_t size;
    size_t align;
} JanetFFIPrimInfo;

static const JanetFFIPrimInfo janet_ffi_type_info[] = {
    {0, 0}, /* JANET_FFI_TYPE_VOID */
    {sizeof(short), ALIGNOF(short)},/* JANET_FFI_TYPE_SHORT */
    {sizeof(int), ALIGNOF(int)}, /* JANET_FFI_TYPE_INT */
    {sizeof(long), ALIGNOF(long)}, /* JANET_FFI_TYPE_LONG */
    {sizeof(unsigned short), ALIGNOF(unsigned short)}, /* JANET_FFI_TYPE_USHORT */
    {sizeof(unsigned), ALIGNOF(unsigned)}, /* JANET_FFI_TYPE_UINT */
    {sizeof(unsigned long), ALIGNOF(unsigned long)}, /* JANET_FFI_TYPE_ULONG */
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
};

typedef struct {
    uint32_t size;
    uint32_t align;
    uint32_t field_count;
    JanetFFIPrimType fields[];
} JanetFFIStruct;

typedef struct {
    JanetFFIPrimType prim;
    int32_t argn;
} JanetFFIMapping;

typedef enum {
    JANET_FFI_CC_SYSV_64
} JanetFFICallingConvention;

#define JANET_FFI_MAX_REGS 16
#define JANET_FFI_MAX_FP_REGS 8
#define JANET_FFI_MAX_STACK 32

typedef struct {
    uint32_t frame_size;
    uint32_t reg_count;
    uint32_t fp_reg_count;
    uint32_t stack_count;
    uint32_t arg_count;
    uint32_t variant;
    JanetFFICallingConvention cc;
    JanetFFIPrimType ret_type;
    JanetFFIMapping regs[JANET_FFI_MAX_REGS];
    JanetFFIMapping fp_regs[JANET_FFI_MAX_FP_REGS];
    JanetFFIMapping stack[JANET_FFI_MAX_STACK];
} JanetFFISignature;

static const JanetAbstractType janet_signature_type = {
    "core/ffi-signature",
    JANET_ATEND_NAME
};

static JanetFFICallingConvention decode_ffi_cc(const uint8_t *name) {
    if (!janet_cstrcmp(name, "sysv64")) return JANET_FFI_CC_SYSV_64;
    if (!janet_cstrcmp(name, "default")) {
        return JANET_FFI_CC_SYSV_64;
    }
    janet_panicf("unknown calling convention %s", name);
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
#ifdef JANET_64
    if (!janet_cstrcmp(name, "size")) return JANET_FFI_TYPE_UINT64;
    if (!janet_cstrcmp(name, "ssize")) return JANET_FFI_TYPE_INT64;
#else
    if (!janet_cstrcmp(name, "size")) return JANET_FFI_TYPE_UINT32;
    if (!janet_cstrcmp(name, "ssize")) return JANET_FFI_TYPE_INT32;
#endif
    janet_panicf("unknown machine type %s", name);
}

static int is_fp_type(JanetFFIPrimType prim) {
    return prim == JANET_FFI_TYPE_DOUBLE || prim == JANET_FFI_TYPE_FLOAT;
}

JANET_CORE_FN(cfun_ffi_signature,
              "(native-signature calling-convention ret-type & arg-types)",
              "Create a function signature object that can be used to make calls "
              "with raw function pointers.") {
    janet_arity(argc, 2, -1);
    uint32_t frame_size = 0;
    uint32_t reg_count = 0;
    uint32_t fp_reg_count = 0;
    uint32_t stack_count = 0;
    uint32_t variant = 0;
    JanetFFICallingConvention cc = decode_ffi_cc(janet_getkeyword(argv, 0));
    JanetFFIPrimType ret_type = decode_ffi_prim(janet_getkeyword(argv, 1));
    uint32_t max_regs = JANET_FFI_MAX_REGS;
    uint32_t max_fp_regs = JANET_FFI_MAX_FP_REGS;
    JanetFFIMapping regs[JANET_FFI_MAX_REGS];
    JanetFFIMapping stack[JANET_FFI_MAX_STACK];
    JanetFFIMapping fp_regs[JANET_FFI_MAX_FP_REGS];
    for (int i = 0; i < JANET_FFI_MAX_REGS; i++) {
        regs[i].prim = JANET_FFI_TYPE_VOID;
        regs[i].argn = 0;
    }
    for (int i = 0; i < JANET_FFI_MAX_FP_REGS; i++) {
        fp_regs[i].prim = JANET_FFI_TYPE_VOID;
        fp_regs[i].argn = 0;
    }
    for (int i = 0; i < JANET_FFI_MAX_STACK; i++) {
        stack[i].prim = JANET_FFI_TYPE_VOID;
        stack[i].argn = 0;
    }
    switch (cc) {
        default:
            break;
        case JANET_FFI_CC_SYSV_64:
            max_regs = 6;
            max_fp_regs = 8;
            if (is_fp_type(ret_type)) variant = 1;
            break;
    }
    for (int32_t i = 2; i < argc; i++) {
        JanetFFIPrimType ptype = decode_ffi_prim(janet_getkeyword(argv, i));
        int is_fp = is_fp_type(ptype);
        if (is_fp && fp_reg_count < max_fp_regs) {
            fp_regs[fp_reg_count].argn = i;
            fp_regs[fp_reg_count++].prim = ptype;
        } else if (!is_fp && reg_count < max_regs) {
            regs[reg_count].argn = i;
            regs[reg_count++].prim = ptype;
        } else {
            stack[stack_count].argn = i;
            stack[stack_count++].prim = ptype;
            frame_size += janet_ffi_type_info[ptype].size;
        }
    }

    /* Create signature abstract value */
    JanetFFISignature *abst = janet_abstract(&janet_signature_type, sizeof(JanetFFISignature));
    abst->frame_size = frame_size;
    abst->reg_count = reg_count;
    abst->fp_reg_count = fp_reg_count;
    abst->stack_count = stack_count;
    abst->cc = cc;
    abst->ret_type = ret_type;
    abst->arg_count = stack_count + reg_count + fp_reg_count;
    abst->variant = variant;
    memcpy(abst->regs, regs, sizeof(JanetFFIMapping) * JANET_FFI_MAX_REGS);
    memcpy(abst->fp_regs, fp_regs, sizeof(JanetFFIMapping) * JANET_FFI_MAX_FP_REGS);
    memcpy(abst->stack, stack, sizeof(JanetFFIMapping) * JANET_FFI_MAX_STACK);
    return janet_wrap_abstract(abst);
}

static void *janet_ffi_getpointer(const Janet *argv, int32_t n) {
    switch (janet_type(argv[n])) {
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

static uint64_t janet_ffi_reg64(const Janet *argv, JanetFFIMapping mapping) {
    JanetFFIPrimType ptype = mapping.prim;
    int32_t n = mapping.argn;
    union {
        float f;
        double d;
        uint64_t reg;
    } u;
    switch (ptype) {
        default:
            janet_panic("nyi");
            return 0;
        case JANET_FFI_TYPE_DOUBLE:
            u.d = janet_getnumber(argv, n);
            return u.reg;
        case JANET_FFI_TYPE_FLOAT:
            u.f = janet_getnumber(argv, n);
            return u.reg;
        case JANET_FFI_TYPE_VOID:
            return 0;
        case JANET_FFI_TYPE_PTR:
            return (uint64_t) janet_ffi_getpointer(argv, n);
        case JANET_FFI_TYPE_BOOL:
            return (uint64_t) janet_getboolean(argv, n);
        case JANET_FFI_TYPE_SHORT:
        case JANET_FFI_TYPE_INT:
        case JANET_FFI_TYPE_INT8:
        case JANET_FFI_TYPE_INT16:
        case JANET_FFI_TYPE_INT32:
        case JANET_FFI_TYPE_INT64:
        case JANET_FFI_TYPE_LONG:
            return (uint64_t) janet_getinteger64(argv, n);
        case JANET_FFI_TYPE_USHORT:
        case JANET_FFI_TYPE_UINT:
        case JANET_FFI_TYPE_UINT8:
        case JANET_FFI_TYPE_UINT16:
        case JANET_FFI_TYPE_UINT32:
        case JANET_FFI_TYPE_UINT64:
        case JANET_FFI_TYPE_ULONG:
            return janet_getuinteger64(argv, n);
    }
}

static Janet janet_ffi_from64(uint64_t ret, JanetFFIPrimType ret_type) {
    union {
        float f;
        double d;
        uint64_t reg;
    } u;
    switch (ret_type) {
        default:
            janet_panic("nyi");
            return janet_wrap_nil();
        case JANET_FFI_TYPE_FLOAT:
            u.reg = ret;
            return janet_wrap_number(u.f);
        case JANET_FFI_TYPE_DOUBLE:
            u.reg = ret;
            return janet_wrap_number(u.d);
        case JANET_FFI_TYPE_VOID:
            return janet_wrap_nil();
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
}

static Janet janet_ffi_sysv64(JanetFFISignature *signature, void *function_pointer, const Janet *argv) {
    uint64_t ret, rethi;
    (void) rethi; /* at some point we will support more complex return types */
    uint64_t regs[6];
    uint64_t fp_regs[8];
    for (uint32_t i = 0; i < signature->reg_count; i++) {
        regs[i] = janet_ffi_reg64(argv, signature->regs[i]);
    }
    for (uint32_t i = 0; i < signature->fp_reg_count; i++) {
        fp_regs[i] = janet_ffi_reg64(argv, signature->fp_regs[i]);
    }
    uint64_t *stack = alloca(sizeof(uint64_t) * signature->stack_count);
    for (uint32_t i = 0; i < signature->stack_count; i++) {
        stack[signature->stack_count - 1 - i] = janet_ffi_reg64(argv, signature->stack[i]);
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
#define FFI_ASM_OUTPUTS "=g" (ret), "=g" (rethi)
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
            return janet_ffi_from64(ret, signature->ret_type);
        case 1:
            __asm__(FFI_ASM_PRELUDE
                    "call *%2\n\t"
                    "movq %%xmm0, %0\n\t"
                    "movq %%xmm1, %1"
                    : FFI_ASM_OUTPUTS
                    : FFI_ASM_INPUTS
                    : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11");
            return janet_ffi_from64(ret, signature->ret_type);
    }

#undef FFI_ASM_PRELUDE
#undef FFI_ASM_OUTPUTS
#undef FFI_ASM_INPUTS

    return janet_wrap_nil();
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

void janet_lib_ffi(JanetTable *env) {
    JanetRegExt ffi_cfuns[] = {
        JANET_CORE_REG("native-signature", cfun_ffi_signature),
        JANET_CORE_REG("native-call", cfun_ffi_call),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, ffi_cfuns);
}

#endif

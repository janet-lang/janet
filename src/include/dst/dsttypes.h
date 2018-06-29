/*
* Copyright (c) 2018 Calvin Rose
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

#ifndef DST_TYPES_H_defined
#define DST_TYPES_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dstconfig.h"

/* Names of all of the types */
extern const char *const dst_type_names[16];

/* Fiber signals */
typedef enum {
    DST_SIGNAL_OK,
    DST_SIGNAL_ERROR,
    DST_SIGNAL_DEBUG,
    DST_SIGNAL_YIELD,
    DST_SIGNAL_USER0,
    DST_SIGNAL_USER1,
    DST_SIGNAL_USER2,
    DST_SIGNAL_USER3,
    DST_SIGNAL_USER4,
    DST_SIGNAL_USER5,
    DST_SIGNAL_USER6,
    DST_SIGNAL_USER7,
    DST_SIGNAL_USER8,
    DST_SIGNAL_USER9
} DstSignal;

/* Fiber statuses - mostly corresponds to signals. */
typedef enum {
    DST_STATUS_DEAD,
    DST_STATUS_ERROR,
    DST_STATUS_DEBUG,
    DST_STATUS_PENDING,
    DST_STATUS_USER0,
    DST_STATUS_USER1,
    DST_STATUS_USER2,
    DST_STATUS_USER3,
    DST_STATUS_USER4,
    DST_STATUS_USER5,
    DST_STATUS_USER6,
    DST_STATUS_USER7,
    DST_STATUS_USER8,
    DST_STATUS_USER9,
    DST_STATUS_NEW,
    DST_STATUS_ALIVE
} DstFiberStatus;

#ifdef DST_NANBOX
typedef union Dst Dst;
#else
typedef struct Dst Dst;
#endif

/* All of the dst types */
typedef struct DstFunction DstFunction;
typedef struct DstArray DstArray;
typedef struct DstBuffer DstBuffer;
typedef struct DstTable DstTable;
typedef struct DstFiber DstFiber;

/* Other structs */
typedef struct DstAbstractHeader DstAbstractHeader;
typedef struct DstFuncDef DstFuncDef;
typedef struct DstFuncEnv DstFuncEnv;
typedef struct DstKV DstKV;
typedef struct DstStackFrame DstStackFrame;
typedef struct DstAbstractType DstAbstractType;
typedef struct DstArgs DstArgs;
typedef struct DstReg DstReg;
typedef struct DstSourceMapping DstSourceMapping;
typedef int (*DstCFunction)(DstArgs args);

/* Basic types for all Dst Values */
typedef enum DstType {
    DST_NIL,
    DST_FALSE,
    DST_TRUE,
    DST_FIBER,
    DST_INTEGER,
    DST_REAL,
    DST_STRING,
    DST_SYMBOL,
    DST_ARRAY,
    DST_TUPLE,
    DST_TABLE,
    DST_STRUCT,
    DST_BUFFER,
    DST_FUNCTION,
    DST_CFUNCTION,
    DST_ABSTRACT
} DstType;

#define DST_COUNT_TYPES (DST_ABSTRACT + 1)

/* Type flags */
#define DST_TFLAG_NIL (1 << DST_NIL)
#define DST_TFLAG_FALSE (1 << DST_FALSE)
#define DST_TFLAG_TRUE (1 << DST_TRUE)
#define DST_TFLAG_FIBER (1 << DST_FIBER)
#define DST_TFLAG_INTEGER (1 << DST_INTEGER)
#define DST_TFLAG_REAL (1 << DST_REAL)
#define DST_TFLAG_STRING (1 << DST_STRING)
#define DST_TFLAG_SYMBOL (1 << DST_SYMBOL)
#define DST_TFLAG_ARRAY (1 << DST_ARRAY)
#define DST_TFLAG_TUPLE (1 << DST_TUPLE)
#define DST_TFLAG_TABLE (1 << DST_TABLE)
#define DST_TFLAG_STRUCT (1 << DST_STRUCT)
#define DST_TFLAG_BUFFER (1 << DST_BUFFER)
#define DST_TFLAG_FUNCTION (1 << DST_FUNCTION)
#define DST_TFLAG_CFUNCTION (1 << DST_CFUNCTION)
#define DST_TFLAG_ABSTRACT (1 << DST_ABSTRACT)

/* Some abstractions */
#define DST_TFLAG_BOOLEAN (DST_TFLAG_TRUE | DST_TFLAG_FALSE)
#define DST_TFLAG_NUMBER (DST_TFLAG_REAL | DST_TFLAG_INTEGER)
#define DST_TFLAG_CALLABLE (DST_TFLAG_FUNCTION | DST_TFLAG_CFUNCTION)
#define DST_TFLAG_BYTES (DST_TFLAG_STRING | DST_TFLAG_SYMBOL | DST_TFLAG_BUFFER)
#define DST_TFLAG_INDEXED (DST_TFLAG_ARRAY | DST_TFLAG_TUPLE)
#define DST_TFLAG_DICTIONARY (DST_TFLAG_TABLE | DST_TFLAG_STRUCT)
#define DST_TFLAG_LENGTHABLE (DST_TFLAG_CHARS | DST_TFLAG_INDEXED | DST_TFLAG_DICTIONARY)

/* We provide two possible implemenations of Dsts. The preferred
 * nanboxing approach, and the standard C version. Code in the rest of the
 * application must interact through exposed interface. */

/* Required interface for Dst */
/* wrap and unwrap for all types */
/* Get type quickly */
/* Check against type quickly */
/* Small footprint */
/* 32 bit integer support */

/* dst_type(x)
 * dst_checktype(x, t)
 * dst_wrap_##TYPE(x)
 * dst_unwrap_##TYPE(x)
 * dst_truthy(x)
 * dst_memclear(p, n) - clear memory for hash tables to nils
 * dst_u64(x) - get 64 bits of payload for hashing
 */

#ifdef DST_NANBOX

#include <math.h>

union Dst {
    uint64_t u64;
    int64_t i64;
    double real;
};

#define dst_u64(x) ((x).u64)

/* This representation uses 48 bit pointers. The trade off vs. the LuaJIT style
 * 47 bit payload representaion is that the type bits are no long contiguous. Type
 * checking can still be fast, but typewise polymorphism takes a bit longer. However, 
 * hopefully we can avoid some annoying problems that occur when trying to use 47 bit pointers
 * in a 48 bit address space (Linux on ARM). If DST_NANBOX_47 is set, use 47 bit tagged pointers. */

/*                    |.......Tag.......|.......................Payload..................| */
/* Non-double:        t|11111111111|1ttt|xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */
/* Types of NIL, TRUE, and FALSE must have payload set to all 1s. */

/* Double (no NaNs):   x xxxxxxxxxxx xxxx xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx */

#if defined (DST_NANBOX_47) || defined (DST_32)

#define DST_NANBOX_TAGBITS     0xFFFF800000000000llu
#define DST_NANBOX_PAYLOADBITS 0x00007FFFFFFFFFFFllu


#define dst_nanbox_lowtag(type) \
    ((uint64_t)(type) | 0x1FFF0)

#define dst_nanbox_tag(type) \
    (dst_nanbox_lowtag(type) << 47)

#define dst_type(x) \
    (isnan((x).real) \
        ? (((x).u64 >> 47) & 0xF) \
        : DST_REAL)

#else /* defined (DST_NANBOX_47) || defined (DST_32) */

#define DST_NANBOX_TAGBITS     0xFFFF000000000000llu
#define DST_NANBOX_PAYLOADBITS 0x0000FFFFFFFFFFFFllu

#define dst_nanbox_lowtag(type) \
    ((((uint64_t)(type) & 0x1) << 15) | 0x7FF8 | ((type) >> 1))

#define dst_nanbox_tag(type) \
    (dst_nanbox_lowtag(type) << 48)

#define dst_type(x) \
    (isnan((x).real) \
        ? (((x).u64 >> 47) & 0xE) | ((x).u64 >> 63) \
        : DST_REAL)

#endif /* defined (DST_NANBOX_47) || defined (DST_32) */

/* 32 bit mode will not use the full payload for pointers. */
#ifdef DST_32

#define DST_NANBOX_POINTERBITS 0xFFFFFFFFllu
#else
#define DST_NANBOX_POINTERBITS DST_NANBOX_PAYLOADBITS
#endif

#define dst_nanbox_checkauxtype(x, type) \
    (((x).u64 & DST_NANBOX_TAGBITS) == dst_nanbox_tag((type)))

#define dst_nanbox_isreal(x) \
    (!isnan((x).real) || dst_nanbox_checkauxtype((x), DST_REAL))

#define dst_checktype(x, t) \
    (((t) == DST_REAL) \
        ? dst_nanbox_isreal(x) \
        : dst_nanbox_checkauxtype((x), (t)))

void *dst_nanbox_to_pointer(Dst x);
void dst_nanbox_memempty(DstKV *mem, int32_t count);
void *dst_nanbox_memalloc_empty(int32_t count);
Dst dst_nanbox_from_pointer(void *p, uint64_t tagmask);
Dst dst_nanbox_from_cpointer(const void *p, uint64_t tagmask);
Dst dst_nanbox_from_double(double d);
Dst dst_nanbox_from_bits(uint64_t bits);

#define dst_memempty(mem, len) dst_nanbox_memempty((mem), (len))
#define dst_memalloc_empty(count) dst_nanbox_memalloc_empty(count)

/* Todo - check for single mask operation */
#define dst_truthy(x) \
    (!(dst_checktype((x), DST_NIL) || dst_checktype((x), DST_FALSE)))

#define dst_nanbox_from_payload(t, p) \
    dst_nanbox_from_bits(dst_nanbox_tag(t) | (p))

#define dst_nanbox_wrap_(p, t) \
    dst_nanbox_from_pointer((p), dst_nanbox_tag(t))

#define dst_nanbox_wrap_c(p, t) \
    dst_nanbox_from_cpointer((p), dst_nanbox_tag(t))

/* Wrap the simple types */
#define dst_wrap_nil() dst_nanbox_from_payload(DST_NIL, 1)
#define dst_wrap_true() dst_nanbox_from_payload(DST_TRUE, 1)
#define dst_wrap_false() dst_nanbox_from_payload(DST_FALSE, 1)
#define dst_wrap_boolean(b) dst_nanbox_from_payload((b) ? DST_TRUE : DST_FALSE, 1)
#define dst_wrap_integer(i) dst_nanbox_from_payload(DST_INTEGER, (uint32_t)(i))
#define dst_wrap_real(r) dst_nanbox_from_double(r)

/* Unwrap the simple types */
#define dst_unwrap_boolean(x) \
    (dst_checktype(x, DST_TRUE))
#define dst_unwrap_integer(x) \
    ((int32_t)((x).u64 & 0xFFFFFFFFlu))
#define dst_unwrap_real(x) ((x).real)

/* Wrap the pointer types */
#define dst_wrap_struct(s) dst_nanbox_wrap_c((s), DST_STRUCT)
#define dst_wrap_tuple(s) dst_nanbox_wrap_c((s), DST_TUPLE)
#define dst_wrap_fiber(s) dst_nanbox_wrap_((s), DST_FIBER)
#define dst_wrap_array(s) dst_nanbox_wrap_((s), DST_ARRAY)
#define dst_wrap_table(s) dst_nanbox_wrap_((s), DST_TABLE)
#define dst_wrap_buffer(s) dst_nanbox_wrap_((s), DST_BUFFER)
#define dst_wrap_string(s) dst_nanbox_wrap_c((s), DST_STRING)
#define dst_wrap_symbol(s) dst_nanbox_wrap_c((s), DST_SYMBOL)
#define dst_wrap_abstract(s) dst_nanbox_wrap_((s), DST_ABSTRACT)
#define dst_wrap_function(s) dst_nanbox_wrap_((s), DST_FUNCTION)
#define dst_wrap_cfunction(s) dst_nanbox_wrap_((s), DST_CFUNCTION)

/* Unwrap the pointer types */
#define dst_unwrap_struct(x) ((const DstKV *)dst_nanbox_to_pointer(x))
#define dst_unwrap_tuple(x) ((const Dst *)dst_nanbox_to_pointer(x))
#define dst_unwrap_fiber(x) ((DstFiber *)dst_nanbox_to_pointer(x))
#define dst_unwrap_array(x) ((DstArray *)dst_nanbox_to_pointer(x))
#define dst_unwrap_table(x) ((DstTable *)dst_nanbox_to_pointer(x))
#define dst_unwrap_buffer(x) ((DstBuffer *)dst_nanbox_to_pointer(x))
#define dst_unwrap_string(x) ((const uint8_t *)dst_nanbox_to_pointer(x))
#define dst_unwrap_symbol(x) ((const uint8_t *)dst_nanbox_to_pointer(x))
#define dst_unwrap_abstract(x) (dst_nanbox_to_pointer(x))
#define dst_unwrap_pointer(x) (dst_nanbox_to_pointer(x))
#define dst_unwrap_function(x) ((DstFunction *)dst_nanbox_to_pointer(x))
#define dst_unwrap_cfunction(x) ((DstCFunction)dst_nanbox_to_pointer(x))

/* End of [#ifdef DST_NANBOX] */
#else

/* A general dst value type */
struct Dst {
    union {
        uint64_t u64;
        double real;
        int32_t integer;
        void *pointer;
        const void *cpointer;
    } as;
    DstType type;
};

#define dst_u64(x) ((x).as.u64)
#define dst_memempty(mem, count) memset((mem), 0, sizeof(DstKV) * (count))
#define dst_memalloc_empty(count) calloc((count), sizeof(DstKV))
#define dst_type(x) ((x).type)
#define dst_checktype(x, t) ((x).type == (t))
#define dst_truthy(x) \
    ((x).type != DST_NIL && (x).type != DST_FALSE)

#define dst_unwrap_struct(x) ((const DstKV *)(x).as.pointer)
#define dst_unwrap_tuple(x) ((const Dst *)(x).as.pointer)
#define dst_unwrap_fiber(x) ((DstFiber *)(x).as.pointer)
#define dst_unwrap_array(x) ((DstArray *)(x).as.pointer)
#define dst_unwrap_table(x) ((DstTable *)(x).as.pointer)
#define dst_unwrap_buffer(x) ((DstBuffer *)(x).as.pointer)
#define dst_unwrap_string(x) ((const uint8_t *)(x).as.pointer)
#define dst_unwrap_symbol(x) ((const uint8_t *)(x).as.pointer)
#define dst_unwrap_abstract(x) ((x).as.pointer)
#define dst_unwrap_pointer(x) ((x).as.pointer)
#define dst_unwrap_function(x) ((DstFunction *)(x).as.pointer)
#define dst_unwrap_cfunction(x) ((DstCFunction)(x).as.pointer)
#define dst_unwrap_boolean(x) ((x).type == DST_TRUE)
#define dst_unwrap_integer(x) ((x).as.integer)
#define dst_unwrap_real(x) ((x).as.real)

Dst dst_wrap_nil(void);
Dst dst_wrap_real(double x);
Dst dst_wrap_integer(int32_t x);
Dst dst_wrap_true(void);
Dst dst_wrap_false(void);
Dst dst_wrap_boolean(int x);
Dst dst_wrap_string(const uint8_t *x);
Dst dst_wrap_symbol(const uint8_t *x);
Dst dst_wrap_array(DstArray *x);
Dst dst_wrap_tuple(const Dst *x);
Dst dst_wrap_struct(const DstKV *x);
Dst dst_wrap_fiber(DstFiber *x);
Dst dst_wrap_buffer(DstBuffer *x);
Dst dst_wrap_function(DstFunction *x);
Dst dst_wrap_cfunction(DstCFunction x);
Dst dst_wrap_table(DstTable *x);
Dst dst_wrap_abstract(void *x);

/* End of tagged union implementation */
#endif

/* Hold components of arguments passed to DstCFunction. */
struct DstArgs {
    int32_t n;
    Dst *v;
    Dst *ret;
};

/* Fiber flags */
#define DST_FIBER_FLAG_SIGNAL_WAITING (1 << 30)

/* Fiber signal masks. */
#define DST_FIBER_MASK_ERROR 2
#define DST_FIBER_MASK_DEBUG 4
#define DST_FIBER_MASK_YIELD 8

#define DST_FIBER_MASK_USER0 (16 << 0)
#define DST_FIBER_MASK_USER1 (16 << 1)
#define DST_FIBER_MASK_USER2 (16 << 2)
#define DST_FIBER_MASK_USER3 (16 << 3)
#define DST_FIBER_MASK_USER4 (16 << 4)
#define DST_FIBER_MASK_USER5 (16 << 5)
#define DST_FIBER_MASK_USER6 (16 << 6)
#define DST_FIBER_MASK_USER7 (16 << 7)
#define DST_FIBER_MASK_USER8 (16 << 8)
#define DST_FIBER_MASK_USER9 (16 << 9)

#define DST_FIBER_MASK_USERN(N) (16 << (N))
#define DST_FIBER_MASK_USER 0x3FF0

#define DST_FIBER_STATUS_MASK 0xFF0000
#define DST_FIBER_STATUS_OFFSET 16

/* A lightweight green thread in dst. Does not correspond to
 * operating system threads. */
struct DstFiber {
    Dst *data;
    DstFiber *child; /* Keep linked list of fibers for restarting pending fibers */
    DstFunction *root; /* First value */
    int32_t frame; /* Index of the stack frame */
    int32_t stackstart; /* Beginning of next args */
    int32_t stacktop; /* Top of stack. Where values are pushed and popped from. */
    int32_t capacity;
    int32_t maxstack; /* Arbitrary defined limit for stack overflow */
    uint32_t flags; /* Various flags */
};

/* Mark if a stack frame is a tail call for debugging */
#define DST_STACKFRAME_TAILCALL 1

/* A stack frame on the fiber. Is stored along with the stack values. */
struct DstStackFrame {
    DstFunction *func;
    uint32_t *pc;
    DstFuncEnv *env;
    int32_t prevframe;
    uint32_t flags;
};

/* Number of Dsts a frame takes up in the stack */
#define DST_FRAME_SIZE ((sizeof(DstStackFrame) + sizeof(Dst) - 1) / sizeof(Dst))

/* A dynamic array type. */
struct DstArray {
    Dst *data;
    int32_t count;
    int32_t capacity;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct DstBuffer {
    uint8_t *data;
    int32_t count;
    int32_t capacity;
};

/* A mutable associative data type. Backed by a hashtable. */
struct DstTable {
    DstKV *data;
    DstTable *proto;
    int32_t count;
    int32_t capacity;
    int32_t deleted;
};

/* A key value pair in a struct or table */
struct DstKV {
    Dst key;
    Dst value;
};

/* Some function defintion flags */
#define DST_FUNCDEF_FLAG_VARARG 1
#define DST_FUNCDEF_FLAG_NEEDSENV 4

struct DstSourceMapping {
    int32_t start;
    int32_t end;
};

/* A function definition. Contains information needed to instantiate closures. */
struct DstFuncDef {
    int32_t *environments; /* Which environments to capture from parent. */
    Dst *constants;
    DstFuncDef **defs;
    uint32_t *bytecode;

    /* Various debug information */
    DstSourceMapping *sourcemap;
    const uint8_t *source;
    const uint8_t *name;

    uint32_t flags;
    int32_t slotcount; /* The amount of stack space required for the function */
    int32_t arity; /* Not including varargs */
    int32_t constants_length;
    int32_t bytecode_length;
    int32_t environments_length; 
    int32_t defs_length; 
};

#define DST_PARSEFLAG_SOURCEMAP 1

/* A fuction environment */
struct DstFuncEnv {
    union {
        DstFiber *fiber;
        Dst *values;
    } as;
    int32_t length; /* Size of environment */
    int32_t offset; /* Stack offset when values still on stack. If offset is <= 0, then
        environment is no longer on the stack. */
};

/* A function */
struct DstFunction {
    DstFuncDef *def;
    DstFuncEnv *envs[];
};

typedef struct DstParseState DstParseState;
typedef struct DstParser DstParser;

enum DstParserStatus {
    DST_PARSE_ROOT,
    DST_PARSE_ERROR,
    DST_PARSE_FULL,
    DST_PARSE_PENDING
};

/* A dst parser */
struct DstParser {
    Dst* args;
    const char *error;
    DstParseState *states;
    uint8_t *buf;
    size_t argcount;
    size_t argcap;
    size_t statecount;
    size_t statecap;
    size_t bufcount;
    size_t bufcap;
    size_t index;
    int lookback;
};

/* Defines an abstract type */
struct DstAbstractType {
    const char *name;
    int (*gc)(void *data, size_t len);
    int (*gcmark)(void *data, size_t len);
};

/* Contains information about userdata */
struct DstAbstractHeader {
    const DstAbstractType *type;
    size_t size;
};

struct DstReg {
    const char *name;
    DstCFunction cfun;
};

#ifdef __cplusplus
}
#endif

#endif /* DST_TYPES_H_defined */

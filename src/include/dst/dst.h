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

#ifndef DST_H_defined
#define DST_H_defined

#ifdef __cplusplus
extern "C" {
#endif

/***** START SECTION CONFIG *****/

#define DST_VERSION "0.0.0 alpha"

/*
 * Detect OS and endianess.
 * From webkit source. There is likely some extreneous
 * detection for unsupported platforms
 */

/* Check Unix */
#if defined(_AIX) \
    || defined(__APPLE__) /* Darwin */ \
    || defined(__FreeBSD__) || defined(__DragonFly__) \
    || defined(__FreeBSD_kernel__) \
    || defined(__GNU__) /* GNU/Hurd */ \
    || defined(__linux__) \
    || defined(__NetBSD__) \
    || defined(__OpenBSD__) \
    || defined(__QNXNTO__) \
    || defined(sun) || defined(__sun) /* Solaris */ \
    || defined(unix) || defined(__unix) || defined(__unix__)
#define DST_UNIX 1
/* Enable certain posix features */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#elif defined(__EMSCRIPTEN__)
#define DST_WEB 1
#elif defined(WIN32) || defined(_WIN32)
#define DST_WINDOWS 1
#endif

/* Check 64-bit vs 32-bit */
#if ((defined(__x86_64__) || defined(_M_X64)) \
     && (defined(DST_UNIX) || defined(DST_WINDOWS))) \
	|| (defined(_WIN64)) /* Windows 64 bit */ \
    || (defined(__ia64__) && defined(__LP64__)) /* Itanium in LP64 mode */ \
    || defined(__alpha__) /* DEC Alpha */ \
    || (defined(__sparc__) && defined(__arch64__) || defined (__sparcv9)) /* BE */ \
    || defined(__s390x__) /* S390 64-bit (BE) */ \
    || (defined(__ppc64__) || defined(__PPC64__)) \
    || defined(__aarch64__) /* ARM 64-bit */
#define DST_64 1
#else
#define DST_32 1
#endif

/* Check big endian */
#if defined(__MIPSEB__) /* MIPS 32-bit */ \
    || defined(__ppc__) || defined(__PPC__) /* CPU(PPC) - PowerPC 32-bit */ \
    || defined(__powerpc__) || defined(__powerpc) || defined(__POWERPC__) \
    || defined(_M_PPC) || defined(__PPC) \
    || defined(__ppc64__) || defined(__PPC64__) /* PowerPC 64-bit */ \
    || defined(__sparc)   /* Sparc 32bit */  \
    || defined(__sparc__) /* Sparc 64-bit */ \
    || defined(__s390x__) /* S390 64-bit */ \
    || defined(__s390__)  /* S390 32-bit */ \
    || defined(__ARMEB__) /* ARM big endian */ \
    || ((defined(__CC_ARM) || defined(__ARMCC__)) /* ARM RealView compiler */ \
        && defined(__BIG_ENDIAN))
#define DST_BIG_ENDIAN 1
#else
#define DST_LITTLE_ENDIAN 1
#endif

/* Define how global dst state is declared */
#ifdef DST_SINGLE_THREADED
#define DST_THREAD_LOCAL
#elif defined(__GNUC__)
#define DST_THREAD_LOCAL __thread
#elif defined(_MSC_BUILD)
#define DST_THREAD_LOCAL __declspec(thread)
#else
#define DST_THREAD_LOCAL
#endif

/* Enable or disbale dynamic module loading. Enabled by default. */
#ifndef DST_NO_DYNAMIC_MODULES
#define DST_DYNAMIC_MODULES
#endif

/* Handle runtime errors */
#ifndef dst_exit
#include <stdio.h>
#define dst_exit(m) do { \
    printf("C runtime error at line %d in file %s: %s\n",\
        __LINE__,\
        __FILE__,\
        (m));\
    exit(1);\
} while (0)
#endif

#define dst_assert(c, m) do { \
    if (!(c)) dst_exit((m)); \
} while (0)

/* What to do when out of memory */
#ifndef DST_OUT_OF_MEMORY
#include <stdio.h>
#define DST_OUT_OF_MEMORY do { printf("dst out of memory\n"); exit(1); } while (0)
#endif

/* Helper for debugging */
#define dst_trace(x) dst_puts(dst_formatc("DST TRACE %s, %d: %v\n", __FILE__, __LINE__, x))

/* Prevent some recursive functions from recursing too deeply
 * ands crashing (the parser). Instead, error out. */
#define DST_RECURSION_GUARD 1024

/* Maximum depth to follow table prototypes before giving up and returning nil. */
#define DST_MAX_PROTO_DEPTH 200

/* Maximum depth to follow table prototypes before giving up and returning nil. */
#define DST_MAX_MACRO_EXPAND 200

/* Define max stack size for stacks before raising a stack overflow error.
 * If this is not defined, fiber stacks can grow without limit (until memory
 * runs out) */
#define DST_STACK_MAX 8192

/* Use nanboxed values - uses 8 bytes per value instead of 12 or 16.
 * To turn of nanboxing, for debugging purposes or for certain
 * architectures (Nanboxing only tested on x86 and x64), comment out
 * the DST_NANBOX define.*/
#define DST_NANBOX

/* Further refines the type of nanboxing to use. */
#define DST_NANBOX_47

/* Alignment for pointers */
#ifdef DST_32
#define DST_WALIGN 4
#else
#define DST_WALIGN 8
#endif

/***** END SECTION CONFIG *****/

/***** START SECTION TYPES *****/

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

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
#define DST_TFLAG_LENGTHABLE (DST_TFLAG_BYTES | DST_TFLAG_INDEXED | DST_TFLAG_DICTIONARY)

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
    Dst *v;
    Dst *ret;
    int32_t n;
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
#define DST_FUNCDEF_FLAG_VARARG 0x10000
#define DST_FUNCDEF_FLAG_NEEDSENV 0x20000
#define DST_FUNCDEF_FLAG_FIXARITY 0x40000
#define DST_FUNCDEF_FLAG_TAG 0xFFFF

/* Source mapping structure for a bytecode instruction */
struct DstSourceMapping {
    int32_t line;
    int32_t column;
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
    size_t line;
    size_t col;
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

/***** END SECTION TYPES *****/

/***** START SECTION OPCODES *****/

/* Bytecode op argument types */
enum DstOpArgType {
    DST_OAT_SLOT,
    DST_OAT_ENVIRONMENT,
    DST_OAT_CONSTANT,
    DST_OAT_INTEGER,
    DST_OAT_TYPE,
    DST_OAT_SIMPLETYPE,
    DST_OAT_LABEL,
    DST_OAT_FUNCDEF
};

/* Various types of instructions */
enum DstInstructionType {
    DIT_0, /* No args */
    DIT_S, /* Slot(3) */
    DIT_L, /* Label(3) */
    DIT_SS, /* Slot(1), Slot(2) */
    DIT_SL, /* Slot(1), Label(2) */
    DIT_ST, /* Slot(1), Slot(2) */
    DIT_SI, /* Slot(1), Immediate(2) */
    DIT_SD, /* Slot(1), Closure(2) */
    DIT_SU, /* Slot(1), Unsigned Immediate(2) */
    DIT_SSS, /* Slot(1), Slot(1), Slot(1) */
    DIT_SSI, /* Slot(1), Slot(1), Immediate(1) */
    DIT_SSU, /* Slot(1), Slot(1), Unsigned Immediate(1) */
    DIT_SES, /* Slot(1), Environment(1), Far Slot(1) */
    DIT_SC /* Slot(1), Constant(2) */
};

enum DstOpCode {
    DOP_NOOP,
    DOP_ERROR,
    DOP_TYPECHECK,
    DOP_RETURN,
    DOP_RETURN_NIL,
    DOP_ADD_INTEGER,
    DOP_ADD_IMMEDIATE,
    DOP_ADD_REAL,
    DOP_ADD,
    DOP_SUBTRACT_INTEGER,
    DOP_SUBTRACT_REAL,
    DOP_SUBTRACT,
    DOP_MULTIPLY_INTEGER,
    DOP_MULTIPLY_IMMEDIATE,
    DOP_MULTIPLY_REAL,
    DOP_MULTIPLY,
    DOP_DIVIDE_INTEGER,
    DOP_DIVIDE_IMMEDIATE,
    DOP_DIVIDE_REAL,
    DOP_DIVIDE,
    DOP_BAND,
    DOP_BOR,
    DOP_BXOR,
    DOP_BNOT,
    DOP_SHIFT_LEFT,
    DOP_SHIFT_LEFT_IMMEDIATE,
    DOP_SHIFT_RIGHT,
    DOP_SHIFT_RIGHT_IMMEDIATE,
    DOP_SHIFT_RIGHT_UNSIGNED,
    DOP_SHIFT_RIGHT_UNSIGNED_IMMEDIATE,
    DOP_MOVE_FAR,
    DOP_MOVE_NEAR,
    DOP_JUMP,
    DOP_JUMP_IF,
    DOP_JUMP_IF_NOT,
    DOP_GREATER_THAN,
    DOP_GREATER_THAN_INTEGER,
    DOP_GREATER_THAN_IMMEDIATE,
    DOP_GREATER_THAN_REAL,
    DOP_GREATER_THAN_EQUAL_REAL,
    DOP_LESS_THAN,
    DOP_LESS_THAN_INTEGER,
    DOP_LESS_THAN_IMMEDIATE,
    DOP_LESS_THAN_REAL,
    DOP_LESS_THAN_EQUAL_REAL,
    DOP_EQUALS,
    DOP_EQUALS_INTEGER,
    DOP_EQUALS_IMMEDIATE,
    DOP_EQUALS_REAL,
    DOP_COMPARE,
    DOP_LOAD_NIL,
    DOP_LOAD_TRUE,
    DOP_LOAD_FALSE,
    DOP_LOAD_INTEGER,
    DOP_LOAD_CONSTANT,
    DOP_LOAD_UPVALUE,
    DOP_LOAD_SELF,
    DOP_SET_UPVALUE,
    DOP_CLOSURE,
    DOP_PUSH,
    DOP_PUSH_2,
    DOP_PUSH_3,
    DOP_PUSH_ARRAY,
    DOP_CALL,
    DOP_TAILCALL,
    DOP_RESUME,
    DOP_SIGNAL,
    DOP_GET,
    DOP_PUT,
    DOP_GET_INDEX,
    DOP_PUT_INDEX,
    DOP_LENGTH,
    DOP_MAKE_ARRAY,
    DOP_MAKE_BUFFER,
    DOP_MAKE_STRING,
    DOP_MAKE_STRUCT,
    DOP_MAKE_TABLE,
    DOP_MAKE_TUPLE,
    DOP_NUMERIC_LESS_THAN,
    DOP_NUMERIC_LESS_THAN_EQUAL,
    DOP_NUMERIC_GREATER_THAN,
    DOP_NUMERIC_GREATER_THAN_EQUAL,
    DOP_NUMERIC_EQUAL,
    DOP_INSTRUCTION_COUNT
};

/* Info about all instructions */
extern enum DstInstructionType dst_instructions[DOP_INSTRUCTION_COUNT];

/***** END SECTION OPCODES *****/

/***** START SECTION MAIN *****/

/* Parsing */
void dst_parser_init(DstParser *parser);
void dst_parser_deinit(DstParser *parser);
int dst_parser_consume(DstParser *parser, uint8_t c);
enum DstParserStatus dst_parser_status(DstParser *parser);
Dst dst_parser_produce(DstParser *parser);
const char *dst_parser_error(DstParser *parser);
void dst_parser_flush(DstParser *parser);
DstParser *dst_check_parser(Dst x);

/* Assembly */
typedef struct DstAssembleResult DstAssembleResult;
typedef struct DstAssembleOptions DstAssembleOptions;
enum DstAssembleStatus {
    DST_ASSEMBLE_OK,
    DST_ASSEMBLE_ERROR
};
struct DstAssembleResult {
    DstFuncDef *funcdef;
    const uint8_t *error;
    enum DstAssembleStatus status;
};
DstAssembleResult dst_asm(Dst source, int flags);
Dst dst_disasm(DstFuncDef *def);
Dst dst_asm_decode_instruction(uint32_t instr);

/* Compilation */
typedef struct DstCompileOptions DstCompileOptions;
typedef struct DstCompileResult DstCompileResult;
enum DstCompileStatus {
    DST_COMPILE_OK,
    DST_COMPILE_ERROR
};
struct DstCompileResult {
    DstFuncDef *funcdef;
    const uint8_t *error;
    DstFiber *macrofiber;
    DstSourceMapping error_mapping;
    enum DstCompileStatus status;
};
DstCompileResult dst_compile(Dst source, DstTable *env, const uint8_t *where);

/* Get the default environment for dst */
DstTable *dst_core_env(void);

int dst_dobytes(DstTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath);
int dst_dostring(DstTable *env, const char *str, const char *sourcePath);

/* Number scanning */
Dst dst_scan_number(const uint8_t *src, int32_t len);
int32_t dst_scan_integer(const uint8_t *str, int32_t len, int *err);
double dst_scan_real(const uint8_t *str, int32_t len, int *err);

/* Array functions */
DstArray *dst_array(int32_t capacity);
DstArray *dst_array_n(const Dst *elements, int32_t n);
DstArray *dst_array_init(DstArray *array, int32_t capacity);
void dst_array_deinit(DstArray *array);
void dst_array_ensure(DstArray *array, int32_t capacity);
void dst_array_setcount(DstArray *array, int32_t count);
void dst_array_push(DstArray *array, Dst x);
Dst dst_array_pop(DstArray *array);
Dst dst_array_peek(DstArray *array);

/* Buffer functions */
DstBuffer *dst_buffer(int32_t capacity);
DstBuffer *dst_buffer_init(DstBuffer *buffer, int32_t capacity);
void dst_buffer_deinit(DstBuffer *buffer);
void dst_buffer_ensure(DstBuffer *buffer, int32_t capacity);
void dst_buffer_setcount(DstBuffer *buffer, int32_t count);
int dst_buffer_extra(DstBuffer *buffer, int32_t n);
int dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, int32_t len);
int dst_buffer_push_string(DstBuffer *buffer, const uint8_t *string);
int dst_buffer_push_cstring(DstBuffer *buffer, const char *cstring);
int dst_buffer_push_u8(DstBuffer *buffer, uint8_t x);
int dst_buffer_push_u16(DstBuffer *buffer, uint16_t x);
int dst_buffer_push_u32(DstBuffer *buffer, uint32_t x);
int dst_buffer_push_u64(DstBuffer *buffer, uint64_t x);

/* Tuple */
#define dst_tuple_raw(t) ((int32_t *)(t) - 4)
#define dst_tuple_length(t) (dst_tuple_raw(t)[0])
#define dst_tuple_hash(t) ((dst_tuple_raw(t)[1]))
#define dst_tuple_sm_line(t) ((dst_tuple_raw(t)[2]))
#define dst_tuple_sm_col(t) ((dst_tuple_raw(t)[3]))
Dst *dst_tuple_begin(int32_t length);
const Dst *dst_tuple_end(Dst *tuple);
const Dst *dst_tuple_n(const Dst *values, int32_t n);
int dst_tuple_equal(const Dst *lhs, const Dst *rhs);
int dst_tuple_compare(const Dst *lhs, const Dst *rhs);

/* String/Symbol functions */
#define dst_string_raw(s) ((int32_t *)(s) - 2)
#define dst_string_length(s) (dst_string_raw(s)[0])
#define dst_string_hash(s) ((dst_string_raw(s)[1]))
uint8_t *dst_string_begin(int32_t length);
const uint8_t *dst_string_end(uint8_t *str);
const uint8_t *dst_string(const uint8_t *buf, int32_t len);
const uint8_t *dst_cstring(const char *cstring);
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equal(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash);
const uint8_t *dst_string_unique(const uint8_t *buf, int32_t len);
const uint8_t *dst_cstring_unique(const char *s);
const uint8_t *dst_description(Dst x);
const uint8_t *dst_to_string(Dst x);
void dst_to_string_b(DstBuffer *buffer, Dst x);
void dst_to_description_b(DstBuffer *buffer, Dst x);
const char *dst_to_zerostring(Dst x);
#define dst_cstringv(cstr) dst_wrap_string(dst_cstring(cstr))
#define dst_stringv(str, len) dst_wrap_string(dst_string((str), (len)))
const uint8_t *dst_formatc(const char *format, ...);
void dst_puts(const uint8_t *str);

/* Symbol functions */
const uint8_t *dst_symbol(const uint8_t *str, int32_t len);
const uint8_t *dst_symbol_from_string(const uint8_t *str);
const uint8_t *dst_csymbol(const char *str);
const uint8_t *dst_symbol_gen(void);
#define dst_symbolv(str, len) dst_wrap_symbol(dst_symbol((str), (len)))
#define dst_csymbolv(cstr) dst_wrap_symbol(dst_csymbol(cstr))

/* Structs */
#define dst_struct_raw(t) ((int32_t *)(t) - 4)
#define dst_struct_length(t) (dst_struct_raw(t)[0])
#define dst_struct_capacity(t) (dst_struct_raw(t)[1])
#define dst_struct_hash(t) (dst_struct_raw(t)[2])
/* Do something with the 4th header slot - flags? */
DstKV *dst_struct_begin(int32_t count);
void dst_struct_put(DstKV *st, Dst key, Dst value);
const DstKV *dst_struct_end(DstKV *st);
Dst dst_struct_get(const DstKV *st, Dst key);
const DstKV *dst_struct_next(const DstKV *st, const DstKV *kv);
DstTable *dst_struct_to_table(const DstKV *st);
int dst_struct_equal(const DstKV *lhs, const DstKV *rhs);
int dst_struct_compare(const DstKV *lhs, const DstKV *rhs);
const DstKV *dst_struct_find(const DstKV *st, Dst key);

/* Table functions */
DstTable *dst_table(int32_t capacity);
DstTable *dst_table_init(DstTable *table, int32_t capacity);
void dst_table_deinit(DstTable *table);
Dst dst_table_get(DstTable *t, Dst key);
Dst dst_table_rawget(DstTable *t, Dst key);
Dst dst_table_remove(DstTable *t, Dst key);
void dst_table_put(DstTable *t, Dst key, Dst value);
const DstKV *dst_table_next(DstTable *t, const DstKV *kv);
const DstKV *dst_table_to_struct(DstTable *t);
void dst_table_merge_table(DstTable *table, DstTable *other);
void dst_table_merge_struct(DstTable *table, const DstKV *other);
DstKV *dst_table_find(DstTable *t, Dst key);

/* Fiber */
DstFiber *dst_fiber(DstFunction *callee, int32_t capacity);
#define dst_fiber_status(f) (((f)->flags & DST_FIBER_STATUS_MASK) >> DST_FIBER_STATUS_OFFSET)

/* Treat similar types through uniform interfaces for iteration */
int dst_indexed_view(Dst seq, const Dst **data, int32_t *len);
int dst_bytes_view(Dst str, const uint8_t **data, int32_t *len);
int dst_dictionary_view(Dst tab, const DstKV **data, int32_t *len, int32_t *cap);

/* Abstract */
#define dst_abstract_header(u) ((DstAbstractHeader *)(u) - 1)
#define dst_abstract_type(u) (dst_abstract_header(u)->type)
#define dst_abstract_size(u) (dst_abstract_header(u)->size)
void *dst_abstract(const DstAbstractType *type, size_t size);

/* Native */
DstCFunction dst_native(const char *name, const uint8_t **error);

/* GC */
void dst_mark(Dst x);
void dst_sweep(void);
void dst_collect(void);
void dst_clear_memory(void);
void dst_gcroot(Dst root);
int dst_gcunroot(Dst root);
int dst_gcunrootall(Dst root);
int dst_gclock(void);
void dst_gcunlock(int handle);

/* Functions */
DstFuncDef *dst_funcdef_alloc(void);
DstFunction *dst_thunk(DstFuncDef *def);
int dst_verify(DstFuncDef *def);

/* Misc */
int dst_equals(Dst x, Dst y);
int32_t dst_hash(Dst x);
int dst_compare(Dst x, Dst y);
int dst_cstrcmp(const uint8_t *str, const char *other);

/* VM functions */
int dst_init(void);
void dst_deinit(void);
DstSignal dst_continue(DstFiber *fiber, Dst in, Dst *out);
#define dst_run(F,O) dst_continue(F, dst_wrap_nil(), O)
DstSignal dst_call(DstFunction *fun, int32_t argn, const Dst *argv, Dst *out, DstFiber **f);

/* Env helpers */
typedef enum {
    DST_BINDING_NONE,
    DST_BINDING_DEF,
    DST_BINDING_VAR,
    DST_BINDING_MACRO
} DstBindingType;
void dst_env_def(DstTable *env, const char *name, Dst val);
void dst_env_var(DstTable *env, const char *name, Dst val);
void dst_env_cfuns(DstTable *env, const DstReg *cfuns);
DstBindingType dst_env_resolve(DstTable *env, const uint8_t *sym, Dst *out);
DstTable *dst_env_arg(DstArgs args);

/* C Function helpers */
int dst_arity_err(DstArgs args, int32_t n, const char *prefix);
int dst_type_err(DstArgs args, int32_t n, DstType expected);
int dst_typemany_err(DstArgs args, int32_t n, int expected);
int dst_typeabstract_err(DstArgs args, int32_t n, const DstAbstractType *at);

/* Initialize builtin libraries */
int dst_lib_io(DstArgs args);
int dst_lib_math(DstArgs args);
int dst_lib_array(DstArgs args);
int dst_lib_tuple(DstArgs args);
int dst_lib_buffer(DstArgs args);
int dst_lib_table(DstArgs args);
int dst_lib_fiber(DstArgs args);
int dst_lib_os(DstArgs args);
int dst_lib_string(DstArgs args);
int dst_lib_marsh(DstArgs args);
int dst_lib_parse(DstArgs args);
int dst_lib_asm(DstArgs args);
int dst_lib_compile(DstArgs args);

/***** END SECTION MAIN *****/

/***** START SECTION MACROS *****/

/* Macros */
#define DST_THROW(a, e) return (*((a).ret) = dst_cstringv(e), DST_SIGNAL_ERROR)
#define DST_THROWV(a, v) return (*((a).ret) = (v), DST_SIGNAL_ERROR)
#define DST_RETURN(a, v) return (*((a).ret) = (v), DST_SIGNAL_OK)

/* Early exit macros */
#define DST_MAXARITY(A, N) do { if ((A).n > (N))\
    return dst_arity_err(A, N, "at most "); } while (0)
#define DST_MINARITY(A, N) do { if ((A).n < (N))\
    return dst_arity_err(A, N, "at least "); } while (0)
#define DST_FIXARITY(A, N) do { if ((A).n != (N))\
    return dst_arity_err(A, N, ""); } while (0)
#define DST_CHECK(A, N, T) do {\
    if ((A).n > (N)) {\
       if (!dst_checktype((A).v[(N)], (T))) return dst_type_err(A, N, T);\
    } else {\
       if ((T) != DST_NIL) return dst_type_err(A, N, T);\
    }\
} while (0)
#define DST_CHECKMANY(A, N, TS) do {\
    if ((A).n > (N)) {\
        DstType t = dst_type((A).v[(N)]);\
        if (!((1 << t) & (TS))) return dst_typemany_err(A, N, TS);\
    } else {\
       if (!((TS) & DST_NIL)) return dst_typemany_err(A, N, TS);\
    }\
} while (0)

#define DST_CHECKABSTRACT(A, N, AT) do {\
    if ((A).n > (N)) {\
        Dst x = (A).v[(N)];\
        if (!dst_checktype(x, DST_ABSTRACT) ||\
                dst_abstract_type(dst_unwrap_abstract(x)) != (AT))\
        return dst_typeabstract_err(A, N, AT);\
    } else {\
        return dst_typeabstract_err(A, N, AT);\
    }\
} while (0)

#define DST_ARG_NUMBER(DEST, A, N) do { \
    if ((A).n <= (N)) \
        return dst_typemany_err(A, N, DST_TFLAG_NUMBER);\
    Dst val = (A).v[(N)];\
    if (dst_checktype(val, DST_REAL)) { \
        DEST = dst_unwrap_real(val); \
    } else if (dst_checktype(val, DST_INTEGER)) {\
        DEST = (double) dst_unwrap_integer(val);\
    }\
    else return dst_typemany_err(A, N, DST_TFLAG_NUMBER); \
} while (0)

#define DST_ARG_BOOLEAN(DEST, A, N) do { \
    DST_CHECKMANY(A, N, DST_TFLAG_TRUE | DST_TFLAG_FALSE);\
    DEST = dst_unwrap_boolean((A).v[(N)]); \
} while (0)

#define DST_ARG_BYTES(DESTBYTES, DESTLEN, A, N) do {\
    if ((A).n <= (N)) return dst_typemany_err(A, N, DST_TFLAG_BYTES);\
    if (!dst_bytes_view((A).v[(N)], &(DESTBYTES), &(DESTLEN))) {\
        return dst_typemany_err(A, N, DST_TFLAG_BYTES);\
    }\
} while (0)

#define DST_ARG_INDEXED(DESTVALS, DESTLEN, A, N) do {\
    if ((A).n <= (N)) return dst_typemany_err(A, N, DST_TFLAG_INDEXED);\
    if (!dst_indexed_view((A).v[(N)], &(DESTVALS), &(DESTLEN))) {\
        return dst_typemany_err(A, N, DST_TFLAG_INDEXED);\
    }\
} while (0)

#define _DST_ARG(TYPE, NAME, DEST, A, N) do { \
    DST_CHECK(A, N, TYPE);\
    DEST = dst_unwrap_##NAME((A).v[(N)]); \
} while (0)

#define DST_ARG_FIBER(DEST, A, N) _DST_ARG(DST_FIBER, fiber, DEST, A, N)
#define DST_ARG_INTEGER(DEST, A, N) _DST_ARG(DST_INTEGER, integer, DEST, A, N)
#define DST_ARG_REAL(DEST, A, N) _DST_ARG(DST_REAL, real, DEST, A, N)
#define DST_ARG_STRING(DEST, A, N) _DST_ARG(DST_STRING, string, DEST, A, N)
#define DST_ARG_SYMBOL(DEST, A, N) _DST_ARG(DST_SYMBOL, symbol, DEST, A, N)
#define DST_ARG_ARRAY(DEST, A, N) _DST_ARG(DST_ARRAY, array, DEST, A, N)
#define DST_ARG_TUPLE(DEST, A, N) _DST_ARG(DST_TUPLE, tuple, DEST, A, N)
#define DST_ARG_TABLE(DEST, A, N) _DST_ARG(DST_TABLE, table, DEST, A, N)
#define DST_ARG_STRUCT(DEST, A, N) _DST_ARG(DST_STRUCT, st, DEST, A, N)
#define DST_ARG_BUFFER(DEST, A, N) _DST_ARG(DST_BUFFER, buffer, DEST, A, N)
#define DST_ARG_FUNCTION(DEST, A, N) _DST_ARG(DST_FUNCTION, function, DEST, A, N)
#define DST_ARG_CFUNCTION(DEST, A, N) _DST_ARG(DST_CFUNCTION, cfunction, DEST, A, N)
#define DST_ARG_ABSTRACT(DEST, A, N) _DST_ARG(DST_ABSTRACT, abstract, DEST, A, N)

#define DST_RETURN_NIL(A) return DST_SIGNAL_OK
#define DST_RETURN_FALSE(A) DST_RETURN(A, dst_wrap_false())
#define DST_RETURN_TRUE(A) DST_RETURN(A, dst_wrap_true())
#define DST_RETURN_BOOLEAN(A, X) DST_RETURN(A, dst_wrap_boolean(X))
#define DST_RETURN_FIBER(A, X) DST_RETURN(A, dst_wrap_fiber(X))
#define DST_RETURN_INTEGER(A, X) DST_RETURN(A, dst_wrap_integer(X))
#define DST_RETURN_REAL(A, X) DST_RETURN(A, dst_wrap_real(X))
#define DST_RETURN_STRING(A, X) DST_RETURN(A, dst_wrap_string(X))
#define DST_RETURN_SYMBOL(A, X) DST_RETURN(A, dst_wrap_symbol(X))
#define DST_RETURN_ARRAY(A, X) DST_RETURN(A, dst_wrap_array(X))
#define DST_RETURN_TUPLE(A, X) DST_RETURN(A, dst_wrap_tuple(X))
#define DST_RETURN_TABLE(A, X) DST_RETURN(A, dst_wrap_table(X))
#define DST_RETURN_STRUCT(A, X) DST_RETURN(A, dst_wrap_struct(X))
#define DST_RETURN_BUFFER(A, X) DST_RETURN(A, dst_wrap_buffer(X))
#define DST_RETURN_FUNCTION(A, X) DST_RETURN(A, dst_wrap_function(X))
#define DST_RETURN_CFUNCTION(A, X) DST_RETURN(A, dst_wrap_cfunction(X))
#define DST_RETURN_ABSTRACT(A, X) DST_RETURN(A, dst_wrap_abstract(X))

#define DST_RETURN_CSTRING(A, X) DST_RETURN(A, dst_cstringv(X))
#define DST_RETURN_CSYMBOL(A, X) DST_RETURN(A, dst_csymbolv(X))

/**** END SECTION MACROS *****/

#ifdef __cplusplus
}
#endif

#endif /* DST_H_defined */

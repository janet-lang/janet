/*
* Copyright (c) 2017 Calvin Rose
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/*
 * Detect OS and endianess.
 * From webkit source. 
 */

/* Unix? */
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
#endif

/* Windows? */
#if defined(WIN32) || defined(_WIN32)
#define DST_WINDOWS 1
#endif

/* 64-bit mode? */
#if ((defined(__x86_64__) || defined(_M_X64)) \
     && (defined(DST_UNIX) || defined(DST_WINDOWS))) \
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

/* Big endian? (Mostly equivallent to how WebKit does it) */
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
#endif

/* What to do when out of memory */
#ifndef DST_OUT_OF_MEMORY
#include <stdio.h>
#define DST_OUT_OF_MEMORY do { printf("out of memory\n"); exit(1); } while (0)
#endif

/* What to do when dst is used in unitialized state */
#ifndef DST_PLEASE_INIT
#include <stdio.h>
#define DST_PLEASE_INIT do { printf("dst is uninitialized\n"); exit(1); } while (0)
#endif

#define DST_INTEGER_MIN INT64_MIN
#define DST_INTEGER_MAX INT64_MAX

/* Prevent some recursive functions from recursing too deeply
 * ands crashing (the parser). Instead, error out. */
#define DST_RECURSION_GUARD 1000

/* Names of all of the types */
extern const char *dst_type_names[15];

/* Various types */
typedef enum DstType {
    DST_NIL = 0,
    DST_REAL,
    DST_INTEGER,
    DST_BOOLEAN,
    DST_STRING,
    DST_SYMBOL,
    DST_ARRAY,
    DST_TUPLE,
    DST_TABLE,
    DST_STRUCT,
    DST_FIBER,
    DST_BUFFER,
    DST_FUNCTION,
    DST_CFUNCTION,
    DST_USERDATA
} DstType;

/* A general dst value type */
typedef struct DstValue DstValue;

/* All of the dst types */
typedef int DstBoolean;
typedef struct DstFunction DstFunction;
typedef struct DstArray DstArray;
typedef struct DstBuffer DstBuffer;
typedef struct DstTable DstTable;
typedef struct DstFiber DstFiber;
typedef int (*DstCFunction)(DstValue *argv, uint32_t argn);

/* Other structs */
typedef struct DstUserdataHeader DstUserdataHeader;
typedef struct DstFuncDef DstFuncDef;
typedef struct DstFuncEnv DstFuncEnv;
typedef struct DstStackFrame DstStackFrame;
typedef union DstValueUnion DstValueUnion;
typedef struct DstUserType DstUserType;

/* Union datatype */
union DstValueUnion {
    DstBoolean boolean;
    double real;
    int64_t integer;
    uint64_t uinteger;
    DstArray *array;
    DstBuffer *buffer;
    DstTable *table;
    DstFiber *fiber;
    const DstValue *tuple;
    DstCFunction cfunction;
    DstFunction *function;
    const DstValue *st;
    const uint8_t *string;
    void *pointer;
};

/* The general dst value type. Contains a large union and
 * the type information of the value */
struct DstValue {
    DstValueUnion as;
    DstType type;
};

/* A lightweight green thread in dst. Does not correspond to
 * operating system threads. */
struct DstFiber {
    DstValue ret; /* Return value */
    DstValue *data;
    DstFiber *parent;
    uint32_t frame; /* Index of the stack frame */
    uint32_t frametop; /* Index of top of stack frame */
    uint32_t stacktop; /* Top of stack. Where values are pushed and popped from. */
    uint32_t capacity;
    enum {
        DST_FIBER_PENDING = 0,
        DST_FIBER_ALIVE,
        DST_FIBER_DEAD,
        DST_FIBER_ERROR
    } status;
};

/* A stack frame on the fiber. Is stored along with the stack values. */
struct DstStackFrame {
    DstFunction *func;
    uint32_t *pc;
    uint32_t prevframe;
};

/* Number of DstValues a frame takes up in the stack */
#define DST_FRAME_SIZE ((sizeof(DstStackFrame) + sizeof(DstValue) - 1)/ sizeof(DstValue))

/* A dynamic array type. */
struct DstArray {
    DstValue *data;
    uint32_t count;
    uint32_t capacity;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct DstBuffer {
    uint8_t *data;
    uint32_t count;
    uint32_t capacity;
};

/* A mutable associative data type. Backed by a hashtable. */
struct DstTable {
    DstValue *data;
    uint32_t count;
    uint32_t capacity;
    uint32_t deleted;
};

/* Some function defintion flags */
#define DST_FUNCDEF_FLAG_VARARG 1
#define DST_FUNCDEF_FLAG_NEEDSENV 4

/* A function definition. Contains information needed to instantiate closures. */
struct DstFuncDef {
    uint32_t *environments; /* Which environments to capture from parent. */
    DstValue *constants; /* Contains strings, FuncDefs, etc. */
    uint32_t *bytecode;

    uint32_t flags;
    uint32_t slotcount; /* The amount of stack space required for the function */
    uint32_t arity; /* Not including varargs */
    uint32_t constants_length;
    uint32_t bytecode_length;
    uint32_t environments_length; 
};

/* A fuction environment */
struct DstFuncEnv {
    union {
        DstFiber *fiber;
        DstValue *values;
    } as;
    uint32_t length; /* Size of environment */
    uint32_t offset; /* Stack offset when values still on stack. If offset is 0, then
        environment is no longer on the stack. */
};

/* A function */
struct DstFunction {
    DstFuncDef *def;
    /* Consider allocating envs with entire function struct */
    DstFuncEnv **envs;
};

/* Defines a type for userdata */
struct DstUserType {
    const char *name;
    int (*serialize)(void *data, uint32_t len);
    int (*deserialize)();
    void (*finalize)(void *data, uint32_t len);
};

/* Contains information about userdata */
struct DstUserdataHeader {
    const DstUserType *type;
    uint32_t size;
};

/* The VM state. Rather than a struct that is passed
 * around, the vm state is global for simplicity and performance. */

/* TODO - somehow wrap these for windows dynamic linking. Either that,
 * or force static linking. see
 * https://stackoverflow.com/questions/19373061/what-happens-to-global-and-static-variables-in-a-shared-library-when-it-is-dynam */

/* Garbage collection */
extern void *dst_vm_blocks;
extern uint32_t dst_vm_memory_interval;
extern uint32_t dst_vm_next_collection;

/* Immutable value cache */
extern const uint8_t **dst_vm_cache;
extern uint32_t dst_vm_cache_capacity;
extern uint32_t dst_vm_cache_count;
extern uint32_t dst_vm_cache_deleted;

/* Syscall table */
extern const DstCFunction dst_vm_syscalls[256];

/* GC roots - TODO consider a top level fiber pool (per thread?) */
extern DstFiber *dst_vm_fiber;

/* Array functions */
DstArray *dst_array(uint32_t capacity);
DstArray *dst_array_init(DstArray *array, uint32_t capacity);
void dst_array_deinit(DstArray *array);
void dst_array_ensure(DstArray *array, uint32_t capacity);
void dst_array_setcount(DstArray *array, uint32_t count);
void dst_array_push(DstArray *array, DstValue x);
DstValue dst_array_pop(DstArray *array);
DstValue dst_array_peek(DstArray *array);

/* Buffer functions */
DstBuffer *dst_buffer(uint32_t capacity);
DstBuffer *dst_buffer_init(DstBuffer *buffer, uint32_t capacity);
void dst_buffer_deinit(DstBuffer *buffer);
void dst_buffer_ensure(DstBuffer *buffer, uint32_t capacity);
void dst_buffer_extra(DstBuffer *buffer, uint32_t n);
void dst_buffer_push_bytes(DstBuffer *buffer, const uint8_t *string, uint32_t len);
void dst_buffer_push_cstring(DstBuffer *buffer, const char *cstring);
void dst_buffer_push_u8(DstBuffer *buffer, uint8_t x);
void dst_buffer_push_u16(DstBuffer *buffer, uint16_t x);
void dst_buffer_push_u32(DstBuffer *buffer, uint32_t x);
void dst_buffer_push_u64(DstBuffer *buffer, uint64_t x);

/* Tuple */
#define dst_tuple_raw(t) ((uint32_t *)(t) - 2)
#define dst_tuple_length(t) (dst_tuple_raw(t)[0])
#define dst_tuple_hash(t) (dst_tuple_raw(t)[1])
DstValue *dst_tuple_begin(uint32_t length);
const DstValue *dst_tuple_end(DstValue *tuple);
const DstValue *dst_tuple_n(DstValue *values, uint32_t n);
int dst_tuple_equal(const DstValue *lhs, const DstValue *rhs);
int dst_tuple_compare(const DstValue *lhs, const DstValue *rhs);

/* String/Symbol functions */
#define dst_string_raw(s) ((uint32_t *)(s) - 2)
#define dst_string_length(s) (dst_string_raw(s)[0])
#define dst_string_hash(s) (dst_string_raw(s)[1])
uint8_t *dst_string_begin(uint32_t length);
const uint8_t *dst_string_end(uint8_t *str);
const uint8_t *dst_string(const uint8_t *buf, uint32_t len);
const uint8_t *dst_cstring(const char *cstring);
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equal(const uint8_t *lhs, const uint8_t *rhs);
int dst_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, uint32_t rlen, uint32_t rhash);
const uint8_t *dst_string_unique(const uint8_t *buf, uint32_t len);
const uint8_t *dst_cstring_unique(const char *s);
const uint8_t *dst_description(DstValue x);
const uint8_t *dst_to_string(DstValue x);
#define dst_cstringv(cstr) dst_wrap_string(dst_cstring(cstr))
const uint8_t *dst_formatc(const char *format, ...);
void dst_puts(const uint8_t *str);

/* Symbol functions */
const uint8_t *dst_symbol(const uint8_t *str, uint32_t len);
const uint8_t *dst_symbol_from_string(const uint8_t *str);
const uint8_t *dst_csymbol(const char *str);
const uint8_t *dst_symbol_gen(const uint8_t *buf, uint32_t len);
#define dst_symbolv(str, len) dst_wrap_symbol(dst_symbol((str), (len)))
#define dst_csymbolv(cstr) dst_wrap_symbol(dst_csymbol(cstr))

/* Structs */
#define dst_struct_raw(t) ((uint32_t *)(t) - 2)
#define dst_struct_length(t) (dst_struct_raw(t)[0])
#define dst_struct_capacity(t) (dst_struct_length(t) * 4)
#define dst_struct_hash(t) (dst_struct_raw(t)[1])
DstValue *dst_struct_begin(uint32_t count);
void dst_struct_put(DstValue *st, DstValue key, DstValue value);
const DstValue *dst_struct_end(DstValue *st);
DstValue dst_struct_get(const DstValue *st, DstValue key);
DstValue dst_struct_next(const DstValue *st, DstValue key);
DstTable *dst_struct_to_table(const DstValue *st);
int dst_struct_equal(const DstValue *lhs, const DstValue *rhs);
int dst_struct_compare(const DstValue *lhs, const DstValue *rhs);

/* Table functions */
DstTable *dst_table(uint32_t capacity);
DstTable *dst_table_init(DstTable *table, uint32_t capacity);
void dst_table_deinit(DstTable *table);
DstValue dst_table_get(DstTable *t, DstValue key);
DstValue dst_table_remove(DstTable *t, DstValue key);
void dst_table_put(DstTable *t, DstValue key, DstValue value);
DstValue dst_table_next(DstTable *t, DstValue key);
const DstValue *dst_table_to_struct(DstTable *t);

/* Fiber */
DstFiber *dst_fiber(uint32_t capacity);
#define dst_stack_frame(s) ((DstStackFrame *)((s) - DST_FRAME_SIZE))
#define dst_fiber_frame(f) dst_stack_frame((f)->data + (f)->frame)
DstFiber *dst_fiber_reset(DstFiber *fiber);
void dst_fiber_setcapacity(DstFiber *fiber, uint32_t n);
void dst_fiber_push(DstFiber *fiber, DstValue x);
void dst_fiber_push2(DstFiber *fiber, DstValue x, DstValue y);
void dst_fiber_push3(DstFiber *fiber, DstValue x, DstValue y, DstValue z);
void dst_fiber_pushn(DstFiber *fiber, const DstValue *arr, uint32_t n);
DstValue dst_fiber_popvalue(DstFiber *fiber);
void dst_fiber_funcframe(DstFiber *fiber, DstFunction *func);
void dst_fiber_funcframe_tail(DstFiber *fiber, DstFunction *func);
void dst_fiber_cframe(DstFiber *fiber);
void dst_fiber_cframe_tail(DstFiber *fiber);
void dst_fiber_popframe(DstFiber *fiber);

/* Functions */
void dst_function_detach(DstFunction *func);

/* Assembly */
typedef enum {
    DST_ASSEMBLE_OK,
    DST_ASSEMBLE_ERROR
} DstAssembleStatus;
typedef struct DstAssembleResult DstAssembleResult;
typedef struct DstAssembleOptions DstAssembleOptions;
struct DstAssembleResult {
    union {
        DstFuncDef *def;
        const uint8_t *error;
    } result;
    DstAssembleStatus status;
};
struct DstAssembleOptions {
    DstValue parsemap;
    DstValue source;
    uint32_t flags;
};
DstAssembleResult dst_asm(DstAssembleOptions opts);
DstFunction *dst_asm_func(DstAssembleResult result);

/* Treat similar types through uniform interfaces for iteration */
int dst_seq_view(DstValue seq, const DstValue **data, uint32_t *len);
int dst_chararray_view(DstValue str, const uint8_t **data, uint32_t *len);
int dst_hashtable_view(DstValue tab, const DstValue **data, uint32_t *len, uint32_t *cap);

/* Userdata */
#define dst_userdata_header(u) ((DstUserdataHeader *)(u) - 1)
#define dst_userdata_type(u) (dst_userdata_header(u)->type)
#define dst_userdata_size(u) (dst_userdata_header(u)->size)

/* Value functions */
int dst_truthy(DstValue v);
int dst_equals(DstValue x, DstValue y);
uint32_t dst_hash(DstValue x);
int dst_compare(DstValue x, DstValue y);
uint32_t dst_calchash_array(const DstValue *array, uint32_t len);
DstValue dst_get(DstValue ds, DstValue key);
void dst_put(DstValue ds, DstValue key, DstValue value);
DstValue dst_next(DstValue ds, DstValue key);
uint32_t dst_length(DstValue x);
uint32_t dst_capacity(DstValue x);
DstValue dst_getindex(DstValue ds, uint32_t index);
void dst_setindex(DstValue ds, DstValue value, uint32_t index);

/* Utils */
extern const char dst_base64[65];
int64_t dst_real_to_integer(double real);
double dst_integer_to_real(int64_t integer);
uint32_t dst_array_calchash(const DstValue *array, uint32_t len);
uint32_t dst_string_calchash(const uint8_t *str, uint32_t len);

/* Parsing */
typedef enum {
    DST_PARSE_OK,
    DST_PARSE_ERROR,
    DST_PARSE_UNEXPECTED_EOS
} DstParseStatus;
typedef struct DstParseResult DstParseResult;
struct DstParseResult {
    union {
        DstValue value;
        const uint8_t *error;
    } result;
    DstValue map;
    uint32_t bytes_read;
    DstParseStatus status;
};
DstParseResult dst_parse(const uint8_t *src, uint32_t len);
DstParseResult dst_parsec(const char *src);

/* VM functions */
int dst_init();
void dst_deinit();
int dst_continue();
int dst_run(DstValue callee);
DstValue dst_transfer(DstFiber *fiber, DstValue x);

/* Wrap data in DstValue */
DstValue dst_wrap_nil();
DstValue dst_wrap_real(double x);
DstValue dst_wrap_integer(int64_t x);
DstValue dst_wrap_boolean(int x);
DstValue dst_wrap_string(const uint8_t *x);
DstValue dst_wrap_symbol(const uint8_t *x);
DstValue dst_wrap_array(DstArray *x);
DstValue dst_wrap_tuple(const DstValue *x);
DstValue dst_wrap_struct(const DstValue *x);
DstValue dst_wrap_fiber(DstFiber *x);
DstValue dst_wrap_buffer(DstBuffer *x);
DstValue dst_wrap_function(DstFunction *x);
DstValue dst_wrap_cfunction(DstCFunction x);
DstValue dst_wrap_table(DstTable *x);
DstValue dst_wrap_userdata(void *x);

/* GC */

/* The metadata header associated with an allocated block of memory */
#define dst_gc_header(mem) ((DstGCMemoryHeader *)(mem) - 1)

#define DST_MEM_TYPEBITS 0xFF
#define DST_MEM_REACHABLE 0x100
#define DST_MEM_DISABLED 0x200

#define dst_gc_settype(m, t) ((dst_gc_header(m)->flags |= (0xFF & (t))))
#define dst_gc_type(m) (dst_gc_header(m)->flags & 0xFF)

#define dst_gc_mark(m) (dst_gc_header(m)->flags |= DST_MEM_REACHABLE)
#define dst_gc_unmark(m) (dst_gc_header(m)->flags &= ~DST_MEM_COLOR)
#define dst_gc_reachable(m) (dst_gc_header(m)->flags & DST_MEM_REACHABLE)


/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct DstGCMemoryHeader DstGCMemoryHeader;
struct DstGCMemoryHeader {
    DstGCMemoryHeader *next;
    uint32_t flags;
};

/* Memory types for the GC. Different from DstType to include funcenv and funcdef. */
typedef enum DstMemoryType DstMemoryType;
enum DstMemoryType {
    DST_MEMORY_NONE,
    DST_MEMORY_STRING,
    DST_MEMORY_SYMBOL,
    DST_MEMORY_ARRAY,
    DST_MEMORY_TUPLE,
    DST_MEMORY_TABLE,
    DST_MEMORY_STRUCT,
    DST_MEMORY_FIBER,
    DST_MEMORY_BUFFER,
    DST_MEMORY_FUNCTION,
    DST_MEMORY_USERDATA,
    DST_MEMORY_FUNCENV,
    DST_MEMORY_FUNCDEF
};

/* Preventn GC from freeing some memory. */
#define dst_disablegc(m) dst_gc_header(m)->flags |= DST_MEM_DISABLED

/* To allocate collectable memory, one must calk dst_alloc, initialize the memory,
 * and then call when dst_enablegc when it is initailize and reachable by the gc (on the DST stack) */
void *dst_alloc(DstMemoryType type, size_t size);
#define dst_enablegc(m) dst_gc_header(m)->flags &= ~DST_MEM_DISABLED

/* When doing C interop, it is often needed to disable GC on a value.
 * This is needed when a garbage collection could occur in the middle
 * of a c function. This could happen, for example, if one calls back
 * into dst inside of a c function. The pin and unpin functions toggle
 * garbage collection on a value when needed. Note that no dst functions
 * will call gc when you don't want it to. GC only happens automatically
 * in the interpreter loop. */
void dst_pin(DstValue x);
void dst_unpin(DstValue x);

/* Specific types can also be pinned and unpinned as well. */
#define dst_pin_table dst_disablegc
#define dst_pin_array dst_disablegc
#define dst_pin_buffer dst_disablegc
#define dst_pin_function dst_disablegc
#define dst_pin_fiber dst_disablegc
#define dst_pin_string(s) dst_disablegc(dst_string_raw(s))
#define dst_pin_symbol(s) dst_disablegc(dst_string_raw(s))
#define dst_pin_tuple(s) dst_disablegc(dst_tuple_raw(s))
#define dst_pin_struct(s) dst_disablegc(dst_struct_raw(s))
#define dst_pin_userdata(s) dst_disablegc(dst_userdata_header(s))

#define dst_unpin_table dst_enablegc
#define dst_unpin_array dst_enablegc
#define dst_unpin_buffer dst_enablegc
#define dst_unpin_function dst_enablegc
#define dst_unpin_fiber dst_enablegc
#define dst_unpin_string(s) dst_enablegc(dst_string_raw(s))
#define dst_unpin_symbol(s) dst_enablegc(dst_string_raw(s))
#define dst_unpin_tuple(s) dst_enablegc(dst_tuple_raw(s))
#define dst_unpin_struct(s) dst_enablegc(dst_struct_raw(s))
#define dst_unpin_userdata(s) dst_enablegc(dst_userdata_header(s))

void dst_mark(DstValue x);
void dst_sweep();

/* Collect some memory */
void dst_collect();

/* Clear all memory. */
void dst_clear_memory();

/* Run garbage collection if needed */
#define dst_maybe_collect() do {\
    if (dst_vm_next_collection >= dst_vm_memory_interval) dst_collect(); } while (0)

#endif /* DST_H_defined */

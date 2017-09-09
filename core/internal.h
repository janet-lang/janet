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

#ifndef DST_INTERNAL_H_defined
#define DST_INTERNAL_H_defined

#include <dst/dst.h>
#include <setjmp.h>

/* String utils */
#define dst_string_raw(s) ((uint32_t *)(s) - 2)
#define dst_string_length(s) (dst_string_raw(s)[0])
#define dst_string_hash(s) (dst_string_raw(s)[1])

/* Tuple utils */
#define dst_tuple_raw(t) ((uint32_t *)(t) - 2)
#define dst_tuple_length(t) (dst_tuple_raw(t)[0])
#define dst_tuple_hash(t) (dst_tuple_raw(t)[1])

/* Struct utils */
#define dst_struct_raw(t) ((uint32_t *)(t) - 2)
#define dst_struct_length(t) (dst_struct_raw(t)[0])
#define dst_struct_capacity(t) (dst_struct_length(t) * 4)
#define dst_struct_hash(t) (dst_struct_raw(t)[1])

/* Userdata utils */
#define dst_udata_header(u) ((DstUserdataHeader *)(u) - 1)
#define dst_udata_type(u) (dst_udata_header(u)->type)
#define dst_udata_size(u) (dst_udata_header(u)->size)

/* Memcpy for moving memory */
#ifndef dst_memcpy
#include <string.h>
#define dst_memcpy memcpy
#endif

/* Allocation */
#ifndef dst_raw_alloc
#include <stdlib.h>
#define dst_raw_alloc malloc
#endif

/* Zero allocation */
#ifndef dst_raw_calloc
#include <stdlib.h>
#define dst_raw_calloc calloc
#endif

/* Realloc */
#ifndef dst_raw_realloc
#include <stdlib.h>
#define dst_raw_realloc realloc
#endif

/* Free */
#ifndef dst_raw_free
#include <stdlib.h>
#define dst_raw_free free
#endif

/* Null */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Stack frame manipulation */

/* Size of stack frame in number of values */
#define DST_FRAME_SIZE 5

/* Prevent some recursive functions from recursing too deeply
 * ands crashing. */
#define DST_RECURSION_GUARD 2056

/* Macros for referencing a stack frame given a stack */
#define dst_frame_callee(s)     (*(s - 1))
#define dst_frame_size(s)       ((s - 2)->data.dwords[0])
#define dst_frame_prevsize(s)   ((s - 2)->data.dwords[1])
#define dst_frame_args(s)       ((s - 3)->data.dwords[0])
#define dst_frame_ret(s)        ((s - 3)->data.dwords[1])
#define dst_frame_pc(s)         ((s - 4)->data.u16p)
#define dst_frame_env(s)        ((s - 5)->data.env)

/* C function helpers */

/* Return in a c function */
#define dst_c_return(vm, x) do { (vm)->ret = (x); return DST_RETURN_OK; } while (0)

/* Throw error from a c function */
#define dst_c_throw(vm, e) do { (vm)->ret = (e); return DST_RETURN_ERROR; } while (0)

/* Throw c string error from a c function */
#define dst_c_throwc(vm, e) dst_c_throw((vm), dst_string_cv((vm), (e)))

/* Assert from a c function */
#define dst_c_assert(vm, cond, e) do { if (cond) dst_c_throw((vm), (e)); } while (0)

/* What to do when out of memory */
#ifndef DST_OUT_OF_MEMORY
#include <stdlib.h>
#include <stdio.h>
#define DST_OUT_OF_MEMORY do { printf("out of memory\n"); exit(1); } while (0)
#endif

/* What to do when memory is low */
#ifndef DST_LOW_MEMORY
#include <stdlib.h>
#include <stdio.h>
#define DST_LOW_MEMORY do { printf("low memory\n"); } while (0)
#endif

/* A general dst value type */
typedef struct DstValue DstValue;

/* All of the dst types */
typedef double DstReal;
typedef int64_t DstInteger;
typedef int DstBoolean;
typedef struct DstFunction DstFunction;
typedef struct DstArray DstArray;
typedef struct DstBuffer DstBuffer;
typedef struct DstTable DstTable;
typedef struct DstThread DstThread;

/* Other structs */
typedef struct DstUserdataHeader DstUserdataHeader;
typedef struct DstFuncDef DstFuncDef;
typedef struct DstFuncEnv DstFuncEnv;
typedef union DstValueUnion DstValueUnion;
typedef struct DstModuleItem DstModuleItem;
typedef struct DstUserType DstUserType;
typedef struct DstParser DstParser;
typedef struct DstParseState DstParseState;

/* C Api data types */
struct DstModuleItem {
    const char *name;
    DstCFunction data;
};

/* Union datatype */
union DstValueUnion {
    DstBoolean boolean;
    DstReal real;
    DstInteger integer;
    DstArray *array;
    DstBuffer *buffer;
    DstTable *table;
    DstThread *thread;
    const DstValue *tuple;
    DstCFunction cfunction;
    DstFunction *function;
    DstFuncEnv *env;
    DstFuncDef *def;
    const DstValue *st;
    const uint8_t *string;
    /* Indirectly used union members */
    uint16_t *u16p;
    uint32_t dwords[2];
    uint16_t words[4];
    uint8_t bytes[8];
    void *pointer;
    const char *cstring;
};

/* The general dst value type. Contains a large union and
 * the type information of the value */
struct DstValue {
    DstType type;
    DstValueUnion data;
};

/* A lightweight green thread in dst. Does not correspond to
 * operating system threads. */
struct DstThread {
    uint32_t count;
    uint32_t capacity;
    DstValue *data;
    DstThread *parent;
    enum {
        DST_THREAD_PENDING = 0,
        DST_THREAD_ALIVE,
        DST_THREAD_DEAD,
        DST_THREAD_ERROR
    } status;
};

/* A dynamic array type. */
struct DstArray {
    uint32_t count;
    uint32_t capacity;
    DstValue *data;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct DstBuffer {
    uint32_t count;
    uint32_t capacity;
    uint8_t *data;
};

/* A mutable associative data type. Backed by a hashtable. */
struct DstTable {
    uint32_t count;
    uint32_t capacity;
    uint32_t deleted;
    DstValue *data;
};

/* Some function defintion flags */
#define DST_FUNCDEF_FLAG_VARARG 1
#define DST_FUNCDEF_FLAG_NEEDSPARENT 2
#define DST_FUNCDEF_FLAG_NEEDSENV 4

/* A function definition. Contains information need to instantiate closures. */
struct DstFuncDef {
    uint32_t locals;
    uint32_t arity; /* Not including varargs */
    uint32_t literalsLen;
    uint32_t byteCodeLen;
    uint32_t flags;
    DstValue *literals; /* Contains strings, FuncDefs, etc. */
    uint16_t *byteCode;
};

/* A fuction environment */
struct DstFuncEnv {
    DstThread *thread; /* When nil, index the local values */
    uint32_t stackOffset; /* Used as environment size when off stack */
    DstValue *values;
};

/* A function */
struct DstFunction {
    DstFuncDef *def;
    DstFuncEnv *env;
    DstFunction *parent;
};

/* Defines a type for userdata */
struct DstUserType {
    const char *name;
    int (*serialize)(Dst *vm, void *data, uint32_t len);
    int (*deserialize)(Dst *vm);
    void (*finalize)(Dst *vm, void *data, uint32_t len);
};

/* Contains information about userdata */
struct DstUserdataHeader {
    uint32_t size;
    const DstUserType *type;
};

/* The VM state */
struct Dst {
    /* Garbage collection */
    void *blocks;
    uint32_t memoryInterval;
    uint32_t nextCollection;
    uint32_t black : 1;
    /* Immutable value cache */
    DstValue *cache;
    uint32_t cache_capacity;
    uint32_t cache_count;
    uint32_t cache_deleted;
    /* GC roots */
    DstThread *thread;
    DstTable *modules;
    DstTable *registry;
    DstTable *env;
    /* Return state */
    DstValue ret;
    uint32_t flags;
};

/* Bytecode */
enum DstOpCode {
    DST_OP_FLS,     /* Load false */
    DST_OP_TRU,     /* Load true */
    DST_OP_NIL,     /* Load nil */
    DST_OP_UPV,     /* Load upvalue */
    DST_OP_JIF,     /* Jump if */
    DST_OP_JMP,     /* Jump */
    DST_OP_SUV,     /* Set upvalue */
    DST_OP_CST,     /* Load constant */
    DST_OP_I16,     /* Load 16 bit signed integer */
    DST_OP_I32,     /* Load 32 bit signed integer */
    DST_OP_I64,     /* Load 64 bit signed integer */
    DST_OP_F64,     /* Load 64 bit IEEE double */
    DST_OP_MOV,     /* Move value */
    DST_OP_CLN,     /* Create a closure */
    DST_OP_ARR,     /* Create array */
    DST_OP_DIC,     /* Create object */
    DST_OP_TUP,     /* Create tuple */
    DST_OP_RET,     /* Return from function */
    DST_OP_RTN,     /* Return nil */
    DST_OP_PSK,     /* Push stack */
    DST_OP_PAR,     /* Push array or tuple */
    DST_OP_CAL,     /* Call function */
    DST_OP_TCL,     /* Tail call */
    DST_OP_TRN      /* Transfer to new thread */
};

/****/
/* Internal buffer functions */
/****/
void dst_value_buffer_ensure(Dst *vm, DstBuffer *buffer, uint32_t capacity);
void dst_buffer_append_bytes(Dst *vm, DstBuffer *buffer, const uint8_t *string, uint32_t length);
void dst_buffer_append_cstring(Dst *vm, DstBuffer *buffer, const char *cstring);

/* Define a push function for pushing a certain type to the buffer */
#define BUFFER_DEFINE(name, type) \
static void dst_buffer_push_##name(Dst *vm, DstBuffer *buffer, type x) { \
    union { type t; uint8_t bytes[sizeof(type)]; } u; \
    u.t = x; dst_buffer_append(vm, buffer, u.bytes, sizeof(type)); \
}

/****/
/* Table functions */
/****/
DstArray *dst_make_array(Dst *vm, uint32_t capacity);

/****/
/* Tuple functions */
/****/

DstValue *dst_tuple_begin(Dst *vm, uint32_t length);
const DstValue *dst_tuple_end(Dst *vm, DstValue *tuple);

/****/
/* String/Symbol functions */
/****/

uint8_t *dst_string_begin(Dst *vm, uint32_t len);
void dst_string_end(Dst *vm, uint32_t dest, uint8_t *str);
const uint8_t *dst_string_b(Dst *vm, const uint8_t *buf, uint32_t len);
const uint8_t *dst_string_c(Dst *vm, const char *cstring);
DstValue dst_string_cv(Dst *vm, const char *string);
DstValue dst_string_cvs(Dst *vm, const char *string);
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs);
const uint8_t *dst_string_bu(Dst *vm, const uint8_t *buf, uint32_t len);
const uint8_t *dst_string_cu(Dst *vm, const char *s);

/****/
/* Struct functions */
/****/

DstValue *dst_struct_begin(Dst *vm, uint32_t count);
void dst_struct_put(DstValue *st, DstValue key, DstValue value);
const DstValue *dst_struct_end(Dst *vm, DstValue *st);
DstValue dst_struct_get(const DstValue *st, DstValue key);
DstValue dst_struct_next(const DstValue *st, DstValue key);

/****/
/* Table functions */
/****/

DstTable *dst_make_table(Dst *vm, uint32_t capacity);
DstValue dst_table_get(DstTable *t, DstValue key);
DstValue dst_table_remove(DstTable *t, DstValue key);
void dst_table_put(Dst *vm, DstTable *t, DstValue key, DstValue value);
DstValue dst_table_next(DstTable *o, DstValue key);

/****/
/* Threads */
/****/

#define dst_thread_stack(t) ((t)->data + (t)->count)
DstThread *dst_thread(Dst *vm, DstValue callee, uint32_t capacity);
DstThread *dst_thread_reset(Dst *vm, DstThread *thread, DstValue callee);
void dst_thread_ensure_extra(Dst *vm, DstThread *thread, uint32_t extra);
void dst_thread_push(Dst *vm, DstThread *thread, DstValue x);
void dst_thread_pushnil(Dst *vm, DstThread *thread, uint32_t n);
void dst_thread_tuplepack(Dst *vm, DstThread *thread, uint32_t n);
DstValue *dst_thread_beginframe(Dst *vm, DstThread *thread, DstValue callee, uint32_t arity);
void dst_thread_endframe(Dst *vm, DstThread *thread);
DstValue *dst_thread_popframe(Dst *vm, DstThread *thread);
uint32_t dst_thread_countframes(DstThread *thread);

/****/
/* Serialization */
/****/

const char *dst_deserialize_internal(
        Dst *vm,
        const uint8_t *data,
        uint32_t len,
        DstValue *out,
        const uint8_t **nextData);

const char *dst_serialize_internal(Dst *vm, DstBuffer *buffer, DstValue x);

/****/
/* GC */
/****/

#define DST_MEMTAG_STRING 4
#define DST_MEMTAG_TUPLE 8
#define DST_MEMTAG_STRUCT 16
#define DST_MEMTAG_USER 32

void dst_mark_value(Dst *vm, DstValue x);
void dst_mark(Dst *vm, DstValueUnion x, DstType type);
void dst_sweep(Dst *vm);
void *dst_alloc(Dst *vm, uint32_t size);
void *dst_zalloc(Dst *vm, uint32_t size);
void dst_mem_tag(void *mem, uint32_t tags);
void dst_collect(Dst *vm);
void dst_maybe_collect(Dst *vm);
void dst_clear_memory(Dst *vm);
void dst_mark_mem(Dst *vm, void *mem);

/****/
/* VM */
/****/

DstValue dst_arg(Dst *vm, uint32_t index);
void dst_set_arg(Dst *vm, uint32_t index, DstValue x);
uint32_t dst_args(Dst *vm);

/***/
/* Stl */
/***/

void dst_stl_load(Dst *vm);

/****/
/* C Api */
/****/

void dst_module(Dst *vm, const char *name, const DstModuleItem *mod);
void dst_module_mutable(Dst *vm, const char *name, const DstModuleItem *mod);
void dst_module_put(Dst *vm, const char *packagename, const char *name, DstValue x);
DstValue dst_module_get(Dst *vm, const char *packagename);
void dst_register_put(Dst *vm, const char *packagename, DstValue mod);
DstValue dst_register_get(Dst *vm, const char *name);
int dst_callc(Dst *vm, DstCFunction fn, int numargs, ...);

/* Treat similar types through uniform interfaces for iteration */
int dst_seq_view(DstValue seq, const DstValue **data, uint32_t *len);
int dst_chararray_view(DstValue str, const uint8_t **data, uint32_t *len);
int dst_hashtable_view(DstValue tab, const DstValue **data, uint32_t *cap);

/****/
/* Caching for immutable data */
/****/

void dst_cache_remove_string(Dst *vm, char *strmem);
void dst_cache_remove_tuple(Dst *vm, char *tuplemem);
void dst_cache_remove_struct(Dst *vm, char *structmem);

/****/
/* Misc */
/****/

uint32_t dst_index(Dst *vm, int i);
int dst_read_real(const uint8_t *string, const uint8_t *end, double *ret, int forceInt);
int dst_read_integer(const uint8_t *string, const uint8_t *end, int64_t *ret);
DstReal dst_integer_to_real(DstInteger x);
DstInteger dst_real_to_integer(DstReal x);
uint32_t dst_startrange(DstInteger raw, uint32_t len);
uint32_t dst_endrange(DstInteger raw, uint32_t len);
void dst_env_merge(Dst *vm, DstTable *destEnv, DstTable *srcEnv);
DstTable *dst_env_nils(Dst *vm, DstTable *env);
DstTable *dst_env_meta(Dst *vm, DstTable *env);
void dst_env_put(Dst *vm, DstTable *env, DstValue key, DstValue value);
void dst_env_putc(Dst *vm, DstTable *env, const char *key, DstValue value);
void dst_env_putvar(Dst *vm, DstTable *env, DstValue key, DstValue value);
void dst_env_putvarc(Dst *vm, DstTable *env, const char *key, DstValue value);

/****/
/* Value functions */
/****/

int dst_value_truthy(DstValue v);
int dst_value_equals(DstValue x, DstValue y);
uint32_t dst_value_hash(DstValue x);
int dst_value_compare(DstValue x, DstValue y);

/* Wrap data in GstValue */
DstValue dst_wrap_nil();
DstValue dst_wrap_real(DstReal x);
DstValue dst_wrap_integer(DstInteger x);
DstValue dst_wrap_boolean(int x);
DstValue dst_wrap_string(const uint8_t *x);
DstValue dst_wrap_symbol(const uint8_t *x);
DstValue dst_wrap_array(DstArray *x);
DstValue dst_wrap_tuple(const DstValue *x);
DstValue dst_wrap_struct(const DstValue *x);
DstValue dst_wrap_thread(DstThread *x);
DstValue dst_wrap_buffer(DstBuffer *x);
DstValue dst_wrap_function(DstFunction *x);
DstValue dst_wrap_cfunction(DstCFunction x);
DstValue dst_wrap_table(DstTable *x);
DstValue dst_wrap_userdata(void *x);
DstValue dst_wrap_funcenv(DstFuncEnv *x);
DstValue dst_wrap_funcdef(DstFuncDef *x);

/* Check data from arguments */
int dst_check_nil(Dst *vm, uint32_t i);
int dst_check_real(Dst *vm, uint32_t i);
int dst_check_integer(Dst *vm, uint32_t i);
int dst_check_boolean(Dst *vm, uint32_t i);
int dst_check_string(Dst *vm, uint32_t i);
int dst_check_symbol(Dst *vm, uint32_t i);
int dst_check_array(Dst *vm, uint32_t i);
int dst_check_tuple(Dst *vm, uint32_t i);
int dst_check_struct(Dst *vm, uint32_t i);
int dst_check_thread(Dst *vm, uint32_t i);
int dst_check_buffer(Dst *vm, uint32_t i);
int dst_check_function(Dst *vm, uint32_t i);
int dst_check_cfunction(Dst *vm, uint32_t i);
int dst_check_table(Dst *vm, uint32_t i);
int dst_check_funcenv(Dst *vm, uint32_t i);
int dst_check_funcdef(Dst *vm, uint32_t i);
void dst_check_userdata(Dst *vm, uint32_t i;

#endif /* DST_INTERNAL_H_defined */

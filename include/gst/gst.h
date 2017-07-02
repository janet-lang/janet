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

#ifndef GST_H_defined
#define GST_H_defined

#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>

/* String utils */
#define gst_string_raw(s) ((uint32_t *)(s) - 2)
#define gst_string_length(s) (gst_string_raw(s)[0])
#define gst_string_hash(s) (gst_string_raw(s)[1])

/* Tuple utils */
#define gst_tuple_raw(t) ((uint32_t *)(t) - 2)
#define gst_tuple_length(t) (gst_tuple_raw(t)[0])
#define gst_tuple_hash(t) (gst_tuple_raw(t)[1])

/* Struct utils */
#define gst_struct_raw(t) ((uint32_t *)(t) - 2)
#define gst_struct_length(t) (gst_struct_raw(t)[0])
#define gst_struct_capacity(t) (gst_struct_length(t) * 4)
#define gst_struct_hash(t) (gst_struct_raw(t)[1])

/* Userdata utils */
#define gst_udata_header(u) ((GstUserdataHeader *)(u) - 1)
#define gst_udata_type(u) (gst_udata_header(u)->type)
#define gst_udata_size(u) (gst_udata_header(u)->size)

/* Memcpy for moving memory */
#ifndef gst_memcpy
#include <string.h>
#define gst_memcpy memcpy
#endif

/* Allocation */
#ifndef gst_raw_alloc
#include <stdlib.h>
#define gst_raw_alloc malloc
#endif

/* Zero allocation */
#ifndef gst_raw_calloc
#include <stdlib.h>
#define gst_raw_calloc calloc
#endif

/* Realloc */
#ifndef gst_raw_realloc
#include <stdlib.h>
#define gst_raw_realloc realloc
#endif

/* Free */
#ifndef gst_raw_free
#include <stdlib.h>
#define gst_raw_free free
#endif

/* Null */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Stack frame manipulation */

/* Size of stack frame in number of values */
#define GST_FRAME_SIZE 5

/* Prevent some recursive functions from recursing too deeply
 * ands crashing. */
#define GST_RECURSION_GUARD 2056

/* Macros for referencing a stack frame given a stack */
#define gst_frame_callee(s)     (*(s - 1))
#define gst_frame_size(s)       ((s - 2)->data.dwords[0])
#define gst_frame_prevsize(s)   ((s - 2)->data.dwords[1])
#define gst_frame_args(s)       ((s - 3)->data.dwords[0])
#define gst_frame_ret(s)        ((s - 3)->data.dwords[1])
#define gst_frame_pc(s)         ((s - 4)->data.u16p)
#define gst_frame_env(s)        ((s - 5)->data.env)

/* C function helpers */

/* Return in a c function */
#define gst_c_return(vm, x) do { (vm)->ret = (x); return GST_RETURN_OK; } while (0)

/* Throw error from a c function */
#define gst_c_throw(vm, e) do { (vm)->ret = (e); return GST_RETURN_ERROR; } while (0)

/* Throw c string error from a c function */
#define gst_c_throwc(vm, e) gst_c_throw((vm), gst_string_cv((vm), (e)))

/* Assert from a c function */
#define gst_c_assert(vm, cond, e) do {if (cond) gst_c_throw((vm), (e)); } while (0)

/* What to do when out of memory */
#ifndef GST_OUT_OF_MEMORY
#include <stdlib.h>
#include <stdio.h>
#define GST_OUT_OF_MEMORY do { printf("out of memory\n"); exit(1); } while (0)
#endif

/* Various types */
typedef enum GstType {
    GST_NIL = 0,
    GST_REAL,
    GST_INTEGER,
    GST_BOOLEAN,
    GST_STRING,
    GST_SYMBOL,
    GST_ARRAY,
    GST_TUPLE,
    GST_TABLE,
    GST_STRUCT,
    GST_THREAD,
    GST_BYTEBUFFER,
    GST_FUNCTION,
    GST_CFUNCTION,
    GST_USERDATA,
    GST_FUNCENV,
    GST_FUNCDEF
} GstType;

/* The state of the virtual machine */
typedef struct Gst Gst;

/* A general gst value type */
typedef struct GstValue GstValue;

/* All of the gst types */
typedef double GstReal;
typedef int64_t GstInteger;
typedef int GstBoolean;
typedef struct GstFunction GstFunction;
typedef struct GstArray GstArray;
typedef struct GstBuffer GstBuffer;
typedef struct GstTable GstTable;
typedef struct GstThread GstThread;
typedef int (*GstCFunction)(Gst * vm);

/* Other structs */
typedef struct GstUserdataHeader GstUserdataHeader;
typedef struct GstFuncDef GstFuncDef;
typedef struct GstFuncEnv GstFuncEnv;
typedef union GstValueUnion GstValueUnion;
typedef struct GstModuleItem GstModuleItem;
typedef struct GstUserType GstUserType;
typedef struct GstParser GstParser;
typedef struct GstParseState GstParseState;
typedef struct GstCompiler GstCompiler;
typedef struct GstScope GstScope;

/* C Api data types */
struct GstModuleItem {
    const char *name;
    GstCFunction data;
};

/* Union datatype */
union GstValueUnion {
    GstBoolean boolean;
    GstReal real;
    GstInteger integer;
    GstArray *array;
    GstBuffer *buffer;
    GstTable *table;
    GstThread *thread;
    const GstValue *tuple;
    GstCFunction cfunction;
    GstFunction *function;
    GstFuncEnv *env;
    GstFuncDef *def;
    const GstValue *st;
    const uint8_t *string;
    /* Indirectly used union members */
    uint16_t *u16p;
    uint32_t dwords[2];
    uint16_t words[4];
    uint8_t bytes[8];
    void *pointer;
    const char *cstring;
};

/* The general gst value type. Contains a large union and
 * the type information of the value */
struct GstValue {
    GstType type;
    GstValueUnion data;
};

/* A lightweight thread in gst. Does not correspond to
 * operating system threads. Used in coroutines and continuations. */
struct GstThread {
    uint32_t count;
    uint32_t capacity;
    GstValue *data;
    GstThread *parent;
    GstThread *errorParent;
    enum {
        GST_THREAD_PENDING = 0,
        GST_THREAD_ALIVE,
        GST_THREAD_DEAD,
        GST_THREAD_ERROR
    } status;
};

/* A dynamic array type. */
struct GstArray {
    uint32_t count;
    uint32_t capacity;
    GstValue *data;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct GstBuffer {
    uint32_t count;
    uint32_t capacity;
    uint8_t *data;
};

/* A mutable associative data type. Backed by a hashtable. */
struct GstTable {
    uint32_t count;
    uint32_t capacity;
    uint32_t deleted;
    GstValue *data;
};

/* Some function defintion flags */
#define GST_FUNCDEF_FLAG_VARARG 1
#define GST_FUNCDEF_FLAG_NEEDSPARENT 2
#define GST_FUNCDEF_FLAG_NEEDSENV 4

/* A function definition. Contains information need to instantiate closures. */
struct GstFuncDef {
    uint32_t locals;
    uint32_t arity; /* Not including varargs */
    uint32_t literalsLen;
    uint32_t byteCodeLen;
    uint32_t flags;
    GstValue *literals; /* Contains strings, FuncDefs, etc. */
    uint16_t *byteCode;
};

/* A fuction environment */
struct GstFuncEnv {
    GstThread *thread; /* When nil, index the local values */
    uint32_t stackOffset; /* Used as environment size when off stack */
    GstValue *values;
};

/* A function */
struct GstFunction {
    GstFuncDef *def;
    GstFuncEnv *env;
    GstFunction *parent;
};

/* Defines a type for userdata */
struct GstUserType {
    const char *name;
    GstValue (*serialize)(Gst *vm, void *data, uint32_t len);
    GstValue (*deserialize)(Gst *vm, GstValue in);
    void (*finalize)(Gst *vm, void *data, uint32_t len);
    void (*gcmark)(Gst *vm, void *data, uint32_t len);
};

/* Contains information about userdata */
struct GstUserdataHeader {
    uint32_t size;
    const GstUserType *type;
};

/* VM return status from c function */
#define GST_RETURN_OK 0
#define GST_RETURN_ERROR 1
#define GST_RETURN_CRASH 2

/* The VM state */
struct Gst {
    /* Garbage collection */
    void *blocks;
    uint32_t memoryInterval;
    uint32_t nextCollection;
    uint32_t black : 1;
    /* Immutable value cache */
    GstValue *cache;
    uint32_t cache_capacity;
    uint32_t cache_count;
    uint32_t cache_deleted;
    /* Scratch memory (should be marked in gc) */
    char *scratch;
    uint32_t scratch_len;
    /* GC roots */
    GstThread *thread;
    GstTable *modules;
    GstTable *registry;
    GstTable *env;
    /* Return state */
    const char *crash;
    GstValue ret; /* Returned value from gst_start. */
};

/* The type of a ParseState */
typedef enum ParseType {
    PTYPE_FORM,
    PTYPE_STRING,
    PTYPE_TOKEN
} ParseType;

/* Contain a parse state that goes on the parse stack */
struct GstParseState {
    ParseType type;
    union {
        struct {
            uint8_t endDelimiter;
            GstArray *array;
        } form;
        struct {
            GstBuffer *buffer;
            uint32_t count;
            uint32_t accum;
            enum {
                STRING_STATE_BASE,
                STRING_STATE_ESCAPE,
                STRING_STATE_ESCAPE_UNICODE,
                STRING_STATE_ESCAPE_HEX
            } state;
        } string;
    } buf;
};

/* Holds the parsing state */
struct GstParser {
    Gst *vm;
    const char *error;
    GstParseState *data;
    GstValue value;
    uint32_t count;
    uint32_t cap;
    uint32_t index;
    uint32_t line;
    uint32_t quoteCount;
    enum {
        GST_PARSER_PENDING = 0,
        GST_PARSER_FULL,
        GST_PARSER_ERROR,
        GST_PARSER_ROOT
    } status;
    enum {
        GST_PCOMMENT_NOT,
        GST_PCOMMENT_EXPECTING,
        GST_PCOMMENT_INSIDE
    } comment;
};

/* Compilation state */
struct GstCompiler {
    Gst *vm;
    GstValue error;
    jmp_buf onError;
    GstScope *tail;
    GstBuffer *buffer;
    GstTable *env;
    int recursionGuard;
};

/* Bytecode */
enum GstOpCode {
    GST_OP_FLS,     /* Load false */
    GST_OP_TRU,     /* Load true */
    GST_OP_NIL,     /* Load nil */
    GST_OP_UPV,     /* Load upvalue */
    GST_OP_JIF,     /* Jump if */
    GST_OP_JMP,     /* Jump */
    GST_OP_SUV,     /* Set upvalue */
    GST_OP_CST,     /* Load constant */
    GST_OP_I16,     /* Load 16 bit signed integer */
    GST_OP_I32,     /* Load 32 bit signed integer */
    GST_OP_I64,     /* Load 64 bit signed integer */
    GST_OP_F64,     /* Load 64 bit IEEE double */
    GST_OP_MOV,     /* Move value */
    GST_OP_CLN,     /* Create a closure */
    GST_OP_ARR,     /* Create array */
    GST_OP_DIC,     /* Create object */
    GST_OP_TUP,     /* Create tuple */
    GST_OP_RET,     /* Return from function */
    GST_OP_RTN,     /* Return nil */
    GST_OP_PSK,     /* Push stack */
    GST_OP_PAR,     /* Push array or tuple */
    GST_OP_CAL,     /* Call function */
    GST_OP_TCL,     /* Tail call */
    GST_OP_TRN      /* Transfer to new thread */
};

/****/
/* Buffer functions */
/****/

GstBuffer *gst_buffer(Gst *vm, uint32_t capacity);
void gst_buffer_ensure(Gst *vm, GstBuffer *buffer, uint32_t capacity);
int gst_buffer_get(GstBuffer *buffer, uint32_t index);
void gst_buffer_push(Gst *vm, GstBuffer *buffer, uint8_t c);
void gst_buffer_append(Gst *vm, GstBuffer *buffer, const uint8_t *string, uint32_t length);
void gst_buffer_append_cstring(Gst *vm, GstBuffer *buffer, const char *cstring);
const uint8_t *gst_buffer_to_string(Gst *vm, GstBuffer *buffer);

/* Define a push function for pushing a certain type to the buffer */
#define BUFFER_DEFINE(name, type) \
static void gst_buffer_push_##name(Gst *vm, GstBuffer *buffer, type x) { \
    union { type t; uint8_t bytes[sizeof(type)]; } u; \
    u.t = x; gst_buffer_append(vm, buffer, u.bytes, sizeof(type)); \
}

/****/
/* Array functions */
/****/

GstArray *gst_array(Gst *vm, uint32_t capacity);
GstValue gst_array_get(GstArray *array, uint32_t index);
int gst_array_set(GstArray *array, uint32_t index, GstValue x);
void gst_array_ensure(Gst *vm, GstArray *array, uint32_t capacity);
void gst_array_push(Gst *vm, GstArray *array, GstValue x);
GstValue gst_array_pop(GstArray *array);
GstValue gst_array_peek(GstArray *array);

/****/
/* Userdata functions */
/****/

void *gst_userdata(Gst *vm, uint32_t size, const GstUserType *utype);

/****/
/* Tuple functions */
/****/

GstValue *gst_tuple_begin(Gst *vm, uint32_t length);
const GstValue *gst_tuple_end(Gst *vm, GstValue *tuple);

/****/
/* String/Symbol functions */
/****/

uint8_t *gst_string_begin(Gst *vm, uint32_t len);
const uint8_t *gst_string_end(Gst *vm, uint8_t *str);
const uint8_t *gst_string_b(Gst *vm, const uint8_t *buf, uint32_t len);
const uint8_t *gst_string_c(Gst *vm, const char *cstring);
GstValue gst_string_cv(Gst *vm, const char *string);
GstValue gst_string_cvs(Gst *vm, const char *string);
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs);

/****/
/* Struct functions */
/****/

GstValue *gst_struct_begin(Gst *vm, uint32_t count);
void gst_struct_put(GstValue *st, GstValue key, GstValue value);
const GstValue *gst_struct_end(Gst *vm, GstValue *st);
GstValue gst_struct_get(const GstValue *st, GstValue key);
GstValue gst_struct_next(const GstValue *st, GstValue key);

/****/
/* Table functions */
/****/

GstTable *gst_table(Gst *vm, uint32_t capacity);
GstValue gst_table_get(GstTable *t, GstValue key);
GstValue gst_table_remove(GstTable *t, GstValue key);
void gst_table_put(Gst *vm, GstTable *t, GstValue key, GstValue value);
GstValue gst_table_next(GstTable *o, GstValue key);

/****/
/* Threads */
/****/

#define gst_thread_stack(t) ((t)->data + (t)->count)
GstThread *gst_thread(Gst *vm, GstValue callee, uint32_t capacity);
GstThread *gst_thread_reset(Gst *vm, GstThread *thread, GstValue callee);
void gst_thread_ensure_extra(Gst *vm, GstThread *thread, uint32_t extra);
void gst_thread_push(Gst *vm, GstThread *thread, GstValue x);
void gst_thread_pushnil(Gst *vm, GstThread *thread, uint32_t n);
void gst_thread_tuplepack(Gst *vm, GstThread *thread, uint32_t n);
GstValue *gst_thread_beginframe(Gst *vm, GstThread *thread, GstValue callee, uint32_t arity);
void gst_thread_endframe(Gst *vm, GstThread *thread);
GstValue *gst_thread_popframe(Gst *vm, GstThread *thread);
uint32_t gst_thread_countframes(GstThread *thread);

/****/
/* Value manipulation */
/****/

int gst_truthy(GstValue x);
int gst_compare(GstValue x, GstValue y);
int gst_equals(GstValue x, GstValue y);
const char *gst_get(GstValue ds, GstValue key, GstValue *out);
const char *gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value);
const uint8_t *gst_to_string(Gst *vm, GstValue x);
const uint8_t *gst_description(Gst *vm, GstValue x);
const uint8_t *gst_short_description(Gst *vm, GstValue x);
uint32_t gst_hash(GstValue x);
GstInteger gst_length(Gst *vm, GstValue x);

/****/
/* Serialization */
/****/

const char *gst_deserialize(
        Gst *vm,
        const uint8_t *data,
        uint32_t len,
        GstValue *out,
        const uint8_t **nextData);

const char *gst_serialize(Gst *vm, GstBuffer *buffer, GstValue x);

/***/
/* Parsing */
/***/

void gst_parser(GstParser *p, Gst *vm);
int gst_parse_cstring(GstParser *p, const char *string);
int gst_parse_string(GstParser *p, const uint8_t *string);
void gst_parse_byte(GstParser *p, uint8_t byte);
int gst_parse_hasvalue(GstParser *p);
GstValue gst_parse_consume(GstParser *p);

/***/
/* Compilation */
/***/

void gst_compiler(GstCompiler *c, Gst *vm);
void gst_compiler_mergeenv(GstCompiler *c, GstValue env);
void gst_compiler_global(GstCompiler *c, const char *name, GstValue x);
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form);

/****/
/* GC */
/****/

#define GST_MEMTAG_STRING 4
#define GST_MEMTAG_TUPLE 8
#define GST_MEMTAG_STRUCT 16
#define GST_MEMTAG_USER 32

void gst_mark_value(Gst *vm, GstValue x);
void gst_mark(Gst *vm, GstValueUnion x, GstType type);
void gst_sweep(Gst *vm);
void *gst_alloc(Gst *vm, uint32_t size);
void *gst_zalloc(Gst *vm, uint32_t size);
void gst_mem_tag(void *mem, uint32_t tags);
void gst_collect(Gst *vm);
void gst_maybe_collect(Gst *vm);
void gst_clear_memory(Gst *vm);
void gst_mark_mem(Gst *vm, void *mem);

/****/
/* VM */
/****/

void gst_init(Gst *vm);
void gst_deinit(Gst *vm);
int gst_run(Gst *vm, GstValue func);
int gst_continue(Gst *vm);
GstValue gst_arg(Gst *vm, uint32_t index);
void gst_set_arg(Gst *vm, uint32_t index, GstValue x);
uint32_t gst_count_args(Gst *vm);

/***/
/* Stl */
/***/

void gst_stl_load(Gst *vm);

/****/
/* C Api */
/****/

void gst_module(Gst *vm, const char *name, const GstModuleItem *mod);
void gst_module_mutable(Gst *vm, const char *name, const GstModuleItem *mod);
void gst_module_put(Gst *vm, const char *packagename, const char *name, GstValue x);
GstValue gst_module_get(Gst *vm, const char *packagename);
void gst_register_put(Gst *vm, const char *packagename, GstValue mod);
GstValue gst_register_get(Gst *vm, const char *name);
int gst_callc(Gst *vm, GstCFunction fn, int numargs, ...);

/* Wrap data in GstValue */
GstValue gst_wrap_nil();
GstValue gst_wrap_real(GstReal x);
GstValue gst_wrap_integer(GstInteger x);
GstValue gst_wrap_boolean(int x);
GstValue gst_wrap_string(const uint8_t *x);
GstValue gst_wrap_symbol(const uint8_t *x);
GstValue gst_wrap_array(GstArray *x);
GstValue gst_wrap_tuple(const GstValue *x);
GstValue gst_wrap_struct(const GstValue *x);
GstValue gst_wrap_thread(GstThread *x);
GstValue gst_wrap_buffer(GstBuffer *x);
GstValue gst_wrap_function(GstFunction *x);
GstValue gst_wrap_cfunction(GstCFunction x);
GstValue gst_wrap_table(GstTable *x);
GstValue gst_wrap_userdata(void *x);
GstValue gst_wrap_funcenv(GstFuncEnv *x);
GstValue gst_wrap_funcdef(GstFuncDef *x);

/* Check data from arguments */
int gst_check_nil(Gst *vm, uint32_t i);
int gst_check_real(Gst *vm, uint32_t i, GstReal (*x));
int gst_check_integer(Gst *vm, uint32_t i, GstInteger (*x));
int gst_check_boolean(Gst *vm, uint32_t i, int (*x));
int gst_check_string(Gst *vm, uint32_t i, const uint8_t *(*x));
int gst_check_symbol(Gst *vm, uint32_t i, const uint8_t *(*x));
int gst_check_array(Gst *vm, uint32_t i, GstArray *(*x));
int gst_check_tuple(Gst *vm, uint32_t i, const GstValue *(*x));
int gst_check_struct(Gst *vm, uint32_t i, const GstValue *(*x));
int gst_check_thread(Gst *vm, uint32_t i, GstThread *(*x));
int gst_check_buffer(Gst *vm, uint32_t i, GstBuffer *(*x));
int gst_check_function(Gst *vm, uint32_t i, GstFunction *(*x));
int gst_check_cfunction(Gst *vm, uint32_t i, GstCFunction (*x));
int gst_check_table(Gst *vm, uint32_t i, GstTable *(*x));
int gst_check_funcenv(Gst *vm, uint32_t i, GstFuncEnv *(*x));
int gst_check_funcdef(Gst *vm, uint32_t i, GstFuncDef *(*x));
void *gst_check_userdata(Gst *vm, uint32_t i, const GstUserType *type);

/* Treat similar types through uniform interfaces */
int gst_seq_view(GstValue seq, const GstValue **data, uint32_t *len);
int gst_chararray_view(GstValue str, const uint8_t **data, uint32_t *len);
int gst_hashtable_view(GstValue tab, const GstValue **data, uint32_t *cap);

/****/
/* Misc */
/****/

#define GST_ENV_NILS 0
#define GST_ENV_METADATA 1
#define GST_ENV_VARS 2

GstReal gst_integer_to_real(GstInteger x);
GstInteger gst_real_to_integer(GstReal x);
GstInteger gst_startrange(GstInteger raw, uint32_t len);
GstInteger gst_endrange(GstInteger raw, uint32_t len);
void gst_env_merge(Gst *vm, GstTable *destEnv, GstTable *srcEnv);
GstTable *gst_env_nils(Gst *vm, GstTable *env);
GstTable *gst_env_meta(Gst *vm, GstTable *env);
void gst_env_put(Gst *vm, GstTable *env, GstValue key, GstValue value);
void gst_env_putc(Gst *vm, GstTable *env, const char *key, GstValue value);
void gst_env_putvar(Gst *vm, GstTable *env, GstValue key, GstValue value);
void gst_env_putvarc(Gst *vm, GstTable *env, const char *key, GstValue value);
const uint8_t *gst_description(Gst *vm, GstValue x);

#endif // GST_H_defined

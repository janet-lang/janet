#ifndef GST_H_defined
#define GST_H_defined

#include <stdint.h>

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
#define gst_struct_capacity(t) (gst_struct_length(t) * 3)
#define gst_struct_hash(t) (gst_struct_raw(t)[1])

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

/* Macros for referencing a stack frame given a stack */
#define gst_frame_callee(s)     (*(s - 1))
#define gst_frame_size(s)       ((s - 2)->data.hws[0])
#define gst_frame_prevsize(s)   ((s - 2)->data.hws[1])
#define gst_frame_errloc(s)     ((s - 2)->data.hws[2])
#define gst_frame_ret(s)        ((s - 2)->data.hws[3])
#define gst_frame_pc(s)         ((s - 3)->data.u16p)
#define gst_frame_errjmp(s)     ((s - 4)->data.u16p)
#define gst_frame_env(s)        ((s - 5)->data.env)

/* C function helpers */

/* Return in a c function */
#define gst_c_return(vm, x) do { (vm)->ret = (x); return GST_RETURN_OK; } while (0)

/* Throw error from a c function */
#define gst_c_throw(vm, e) do { (vm)->ret = (e); return GST_RETURN_ERROR; } while (0)

/* Throw c string error from a c function */
#define gst_c_throwc(vm, e) gst_c_throw((vm), gst_load_cstring((vm), (e)))

/* Assert from a c function */
#define gst_c_assert(vm, cond, e) do {if (cond) gst_c_throw((vm), (e)); } while (0)

/* What to do when out of memory */
#ifndef GST_OUT_OF_MEMORY
#include <stdlib.h>
#include <stdio.h>
#define GST_OUT_OF_MEMORY do { printf("out of memory.\n"); exit(1); } while (0)
#endif

/* Max search depth for classes. */
#define GST_MAX_SEARCH_DEPTH 128

/* Various types */
typedef enum GstType {
    GST_NIL = 0,
    GST_NUMBER,
    GST_BOOLEAN,
    GST_STRING,
    GST_ARRAY,
    GST_TUPLE,
    GST_THREAD,
    GST_BYTEBUFFER,
    GST_FUNCTION,
    GST_CFUNCTION,
    GST_OBJECT,
    GST_USERDATA,
    GST_FUNCENV,
    GST_FUNCDEF
} GstType;

/* The state of the virtual machine */
typedef struct Gst Gst;

/* A general gst value type */
typedef struct GstValue GstValue;

/* All of the gst types */
typedef double GstNumber;
typedef uint8_t GstBoolean;
typedef struct GstFunction GstFunction;
typedef struct GstArray GstArray;
typedef struct GstBuffer GstBuffer;
typedef struct GstObject GstObject;
typedef struct GstThread GstThread;
typedef int (*GstCFunction)(Gst * vm);

/* Implementation details */
typedef struct GstUserdataHeader GstUserdataHeader;
typedef struct GstFuncDef GstFuncDef;
typedef struct GstFuncEnv GstFuncEnv;
typedef union GstValueUnion GstValueUnion;

/* Definitely implementation details */
typedef struct GstBucket GstBucket;

/* API Types */
typedef struct GstModuleItem GstModuleItem;

/* C Api data types */
struct GstModuleItem {
    const char *name;
    GstCFunction data;
};

/* Union datatype */
union GstValueUnion {
    GstBoolean boolean;
    GstNumber number;
    GstArray *array;
    GstBuffer *buffer;
    GstObject *object;
    GstThread *thread;
    GstValue *tuple;
    GstCFunction cfunction;
    GstFunction *function;
    GstFuncEnv *env;
    GstFuncDef *def;
    GstValue *st;
    const uint8_t *string;
    const char *cstring; /* Alias for ease of use from c */
    /* Indirectly used union members */
    uint16_t *u16p;
    uint16_t hws[4];
    uint8_t bytes[8];
    void *pointer;
};

/* The general gst value type. Contains a large union and
 * the type information of the value */
struct GstValue {
    GstType type;
    GstValueUnion data;
};

/* A lightweight thread in gst. Does not correspond to
 * operating system threads. Used in coroutines. */
struct GstThread {
    uint32_t count;
    uint32_t capacity;
    GstValue *data;
    GstThread *parent;
    enum {
        GST_THREAD_PENDING = 0,
        GST_THREAD_ALIVE,
        GST_THREAD_DEAD
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

/* The main Gst type, an obect. Objects are just hashtables with a parent */
struct GstObject {
    uint32_t count;
    uint32_t capacity;
    GstBucket **buckets;
    GstObject *parent;
};

/* Some function defintion flags */
#define GST_FUNCDEF_FLAG_VARARG 1

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

/* A hash table bucket in an object */
struct GstBucket {
    GstValue key;
    GstValue value;
    GstBucket *next;
};

/* Contains information about userdata */
struct GstUserdataHeader {
    uint32_t size;
    GstObject *meta;
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
    /* String cache */
    const uint8_t **strings;
    uint32_t stringsCapacity;
    uint32_t stringsCount;
    uint32_t stringsDeleted;
    /* Thread */
    GstThread *thread;
    /* A GC root */
    GstObject *rootenv;
    /* Return state */
    const char *crash;
    GstValue ret; /* Returned value from gst_start. Also holds errors. */
};

/* Bytecode */
enum GstOpCode {
    GST_OP_ADD = 0, /* Addition */
    GST_OP_SUB,     /* Subtraction */
    GST_OP_MUL,     /* Multiplication */
    GST_OP_DIV,     /* Division */
    GST_OP_NOT,     /* Boolean invert */
    GST_OP_NEG,     /* Unary negation */ 
    GST_OP_INV,     /* Unary multiplicative inverse */
    GST_OP_FLS,     /* Load false */
    GST_OP_TRU,     /* Load true */
    GST_OP_NIL,     /* Load nil */
    GST_OP_I16,     /* Load 16 bit signed integer */
    GST_OP_UPV,     /* Load upvalue */
    GST_OP_JIF,     /* Jump if */
    GST_OP_JMP,     /* Jump */
    GST_OP_SUV,     /* Set upvalue */
    GST_OP_CST,     /* Load constant */
    GST_OP_I32,     /* Load 32 bit signed integer */
    GST_OP_F64,     /* Load 64 bit IEEE double */
    GST_OP_MOV,     /* Move value */
    GST_OP_CLN,     /* Create a closure */
    GST_OP_EQL,     /* Check equality */
    GST_OP_LTN,     /* Check less than */
    GST_OP_LTE,     /* Check less than or equal to */
    GST_OP_ARR,     /* Create array */
    GST_OP_DIC,     /* Create object */
    GST_OP_TUP,     /* Create tuple */
    GST_OP_ERR,     /* Throw error */
    GST_OP_TRY,     /* Begin try block */
    GST_OP_UTY,     /* End try block */
    GST_OP_RET,     /* Return from function */
    GST_OP_RTN,     /* Return nil */
    GST_OP_CAL,     /* Call function */
    GST_OP_TCL,     /* Tail call */
    GST_OP_YLD      /* Yield from function */
};

/****/
/* Buffer functions */
/****/

GstBuffer *gst_buffer(Gst *vm, uint32_t capacity);
void gst_buffer_ensure(Gst *vm, GstBuffer *buffer, uint32_t capacity);
int gst_buffer_get(GstBuffer *buffer, uint32_t index);
void gst_buffer_push(Gst *vm, GstBuffer *buffer, uint8_t c);
void gst_buffer_append(Gst *vm, GstBuffer *buffer, const uint8_t *string, uint32_t length);
const uint8_t *gst_buffer_to_string(Gst *vm, GstBuffer *buffer);

/* Define a push function for pushing a certain type to the buffer */
#define BUFFER_DEFINE(name, type) \
static void gst_buffer_push_##name(Gst * vm, GstBuffer * buffer, type x) { \
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

/****/
/* Tuple functions */
/****/

GstValue *gst_tuple(Gst *vm, uint32_t length);

/****/
/* String functions */
/****/

uint8_t *gst_string_begin(Gst *vm, uint32_t len);
const uint8_t *gst_string_end(Gst *vm, uint8_t *str);
const uint8_t *gst_string_loadbuffer(Gst *vm, const uint8_t *buf, uint32_t len);
const uint8_t *gst_cstring_to_string(Gst *vm, const char *cstring);
GstValue gst_load_cstring(Gst *vm, const char *string);
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs);

/****/
/* Userdata functions */
/****/

void *gst_userdata(Gst *vm, uint32_t size, GstObject *meta);

/****/
/* Object functions */
/****/

GstObject *gst_object(Gst *vm, uint32_t capacity);
GstValue gst_object_get(GstObject *obj, GstValue key);
GstValue gst_object_remove(Gst *vm, GstObject *obj, GstValue key);
void gst_object_put(Gst *vm, GstObject *obj, GstValue key, GstValue value);

/****/
/* Threads */
/****/

#define gst_thread_stack(t) ((t)->data + (t)->count)
GstThread *gst_thread(Gst *vm, GstValue callee, uint32_t capacity); 
void gst_thread_ensure_extra(Gst *vm, GstThread *thread, uint32_t extra); 
void gst_thread_push(Gst *vm, GstThread *thread, GstValue x); 
void gst_thread_pushnil(Gst *vm, GstThread *thread, uint32_t n); 
void gst_thread_tuplepack(Gst *vm, GstThread *thread, uint32_t n); 
GstValue *gst_thread_beginframe(Gst *vm, GstThread *thread, GstValue callee, uint32_t arity); 
void gst_thread_endframe(Gst *vm, GstThread *thread);
GstValue *gst_thread_popframe(Gst *vm, GstThread *thread); 
GstValue *gst_thread_tail(Gst *vm, GstThread *thread);


/****/
/* Value manipulation */
/****/

int gst_truthy(GstValue x);
int gst_compare(GstValue x, GstValue y);
int gst_equals(GstValue x, GstValue y);
const char *gst_get(GstValue ds, GstValue key, GstValue *out);
const char *gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value);
const uint8_t *gst_to_string(Gst *vm, GstValue x);
uint32_t gst_hash(GstValue x);
int gst_length(Gst *vm, GstValue x, GstValue *len);

/****/
/* Serialization */
/****/

/**
 * Data format
 * State is encoded as a string of unsigned bytes.
 *
 * Types:
 *
 * Byte 0 to 200: small integer byte - 100
 * Byte 201: Nil
 * Byte 202: True
 * Byte 203: False
 * Byte 204: Number  - double format
 * Byte 205: String  - [u32 length]*[u8... characters]
 * Byte 206: Symbol  - [u32 length]*[u8... characters]
 * Byte 207: Buffer  - [u32 length]*[u8... characters]
 * Byte 208: Array   - [u32 length]*[value... elements]
 * Byte 209: Tuple   - [u32 length]*[value... elements]
 * Byte 210: Thread  - [u8 state][u32 frames]*[[value callee][value env]
 *  [u32 pcoffset][u32 erroffset][u16 ret][u16 errloc][u16 size]*[value ...stack]
 * Byte 211: Object  - [value meta][u32 length]*2*[value... kvs]
 * Byte 212: FuncDef - [u32 locals][u32 arity][u32 flags][u32 literallen]*[value...
 *  literals][u32 bytecodelen]*[u16... bytecode]
 * Byte 213: FunEnv  - [value thread][u32 length]*[value ...upvalues]
 *  (upvalues is not read if thread is a thread object)
 * Byte 214: Func    - [value parent][value def][value env]
 *  (nil values indicate empty)
 * Byte 215: LUdata  - [value meta][u32 length]*[u8... bytes]
 * Byte 216: CFunc   - [u32 length]*[u8... idstring]
 * Byte 217: Ref     - [u32 id]
 */

const char *gst_deserialize(
        Gst *vm,
        const uint8_t *data,
        uint32_t len,
        GstValue *out,
        const uint8_t *nextData);
const char *gst_serialize(Gst *vm, GstBuffer *buffer, GstValue x);

/****/
/* GC */
/****/

#define GST_MEMTAG_STRING 4

void gst_mark_value(Gst *vm, GstValue x);
void gst_mark(Gst *vm, GstValueUnion x, GstType type);
void gst_sweep(Gst *vm);
void *gst_alloc(Gst *vm, uint32_t size);
void *gst_zalloc(Gst *vm, uint32_t size);
void gst_mem_tag(void *mem, uint32_t tags);
void gst_collect(Gst *vm);
void gst_maybe_collect(Gst *vm);
void gst_clear_memory(Gst *vm);

/****/
/* VM */
/****/

void gst_init(Gst *vm);
void gst_deinit(Gst *vm);
int gst_run(Gst *vm, GstValue func);
int gst_continue(Gst *vm);
GstValue gst_arg(Gst *vm, uint16_t index);
void gst_set_arg(Gst *vm, uint16_t index, GstValue x);
uint16_t gst_count_args(Gst *vm);

/***/
/* C Api */
/***/

GstObject *gst_c_module(Gst *vm, const GstModuleItem *mod);
void gst_c_register(Gst *vm, const char *packagename, GstObject *mod);

#endif // GST_H_defined

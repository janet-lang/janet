#ifndef DATATYPES_H_PJJ035NT
#define DATATYPES_H_PJJ035NT

#include <stdint.h>

/* Max search depth for classes. */
#define GST_MAX_SEARCH_DEPTH 128

/* Verious types */
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
    GST_USERDATA
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

/* Definitely implementation details */
typedef struct GstBucket GstBucket;

/* The general gst value type. Contains a large union and
 * the type information of the value */
struct GstValue {
    GstType type;
    union {
        GstBoolean boolean;
        GstNumber number;
        GstArray *array;
        GstBuffer *buffer;
        GstObject *object;
        GstThread *thread;
        GstValue *tuple;
        GstCFunction cfunction;
        GstFunction *function;
        uint8_t *string;
        char *cstring; /* Allias for ease of use from c */
        /* Indirectly used union members */
        uint16_t *u16p;
        GstFuncEnv *env;
        uint16_t hws[4];
        uint8_t bytes[8];
        void *pointer;
    } data;
};

/* A lightweight thread in gst. Does not correspond to
 * operating system threads. Used in coroutines. */
struct GstThread {
    uint32_t count;
    uint32_t capacity;
    GstValue *data;
    enum {
        GST_THREAD_PENDING = 0,
        GST_THREAD_ALIVE,
        GST_TRHEAD_DEAD
    } status;
};

/* A dynamic array type. Useful for implementing a stack. */
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

/* The main Gst type, an obect. Objects are just hashtables with some meta
 * information attached in the meta value */
struct GstObject {
    uint32_t count;
    uint32_t capacity;
    GstBucket **buckets;
    GstObject *meta;
};

/* Some function defintion flags */
#define GST_FUNCDEF_FLAG_VARARG 1

/* A function definition. Contains information need to instatiate closures. */
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
    /* Thread */
    GstThread *thread;
    /* A GC root */
    GstValue rootenv;
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
    GST_OP_MOD,     /* Modulo division */
    GST_OP_IDV,     /* Integer division */
    GST_OP_EXP,     /* Exponentiation */
    GST_OP_CCT,     /* Concatenation */
    GST_OP_NOT,     /* Boolean invert */
    GST_OP_NEG,     /* Unary negation */ 
    GST_OP_INV,     /* Unary multiplicative inverse */
    GST_OP_LEN,     /* Length */
    GST_OP_TYP,     /* Type */
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
    GST_OP_SET,     /* Assocaitive set */
    GST_OP_GET,     /* Associative get */
    GST_OP_ERR,     /* Throw error */
    GST_OP_TRY,     /* Begin try block */
    GST_OP_UTY,     /* End try block */
    GST_OP_RET,     /* Return from function */
    GST_OP_RTN,     /* Return nil */
    GST_OP_CAL,     /* Call function */
    GST_OP_TCL      /* Tail call */
};

#endif

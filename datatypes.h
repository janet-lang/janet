#ifndef DATATYPES_H_PJJ035NT
#define DATATYPES_H_PJJ035NT

#include <stdint.h>
#include <setjmp.h>

typedef enum GstType {
    GST_NIL = 0,
    GST_NUMBER,
    GST_BOOLEAN,
    GST_STRING,
    GST_ARRAY,
    GST_THREAD,
    GST_BYTEBUFFER,
    GST_FUNCTION,
    GST_CFUNCTION,
    GST_OBJECT
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
typedef GstValue (*GstCFunction)(Gst * vm);

/* Implementation details */
typedef struct GstParser GstParser;
typedef struct GstCompiler GstCompiler;
typedef struct GstFuncDef GstFuncDef;
typedef struct GstFuncEnv GstFuncEnv;

/* Definitely implementation details */
typedef struct GstStackFrame GstStackFrame;
typedef struct GstParseState GstParseState;
typedef struct GstBucket GstBucket;
typedef struct GstScope GstScope;

/* The general gst value type. Contains a large union and
 * the type information of the value */
struct GstValue {
    GstType type;
    union {
        /* The various types */
        GstBoolean boolean;
        GstNumber number;
        GstArray *array;
        GstBuffer *buffer;
        GstObject *object;
        GstThread *thread;
        GstCFunction cfunction;
        GstFunction *function;
        uint8_t *string;
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

/* A dynamic array type */
struct GstArray {
    uint32_t count;
    uint32_t capacity;
    GstValue *data;
    uint32_t flags;
};

/* A bytebuffer type. Used as a mutable string or string builder. */
struct GstBuffer {
    uint32_t count;
    uint32_t capacity;
    uint8_t *data;
    uint32_t flags;
};

/* The main Gst type, an obect. Objects are just hashtables with some meta
 * information attached in the meta value */
struct GstObject {
    uint32_t count;
    uint32_t capacity;
    GstBucket **buckets;
    uint32_t flags;
    GstValue meta;
};

/* A function defintion. Contains information need to instatiate closures. */
struct GstFuncDef {
    uint32_t locals;
    uint32_t arity;
    uint32_t literalsLen;
    uint32_t byteCodeLen;
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

/* A stack frame in the VM */
struct GstStackFrame {
    GstValue callee;
    uint16_t size;
    uint16_t prevSize;
    uint16_t ret;
    uint16_t errorSlot;
    uint16_t *errorJump;
    GstFuncEnv *env;
    uint16_t *pc;
};

/* The VM state */
struct Gst {
    /* Garbage collection */
    void *blocks;
    uint32_t memoryInterval;
    uint32_t nextCollection;
    uint32_t black : 1;
    uint32_t lock : 31;
    /* Thread */
    uint16_t *pc;
    GstThread *thread;
    GstValue *base;
    GstStackFrame *frame;
    /* Return state */
    const char *crash;
    jmp_buf jump;
    GstValue error;
    GstValue ret; /* Returned value from VMStart. Also holds errors. */
};

struct GstParser {
    Gst *vm;
    const char *error;
    GstParseState *data;
    GstValue value;
    uint32_t count;
    uint32_t cap;
    uint32_t index;
    enum {
		GST_PARSER_PENDING = 0,
		GST_PARSER_FULL,
		GST_PARSER_ERROR
    } status;
};

/* Compilation state */
struct GstCompiler {
    Gst *vm;
    const char *error;
    jmp_buf onError;
    GstScope *tail;
    GstArray *env;
    GstBuffer *buffer;
};

/* String utils */
#define gst_string_raw(s) ((uint32_t *)(s) - 2)
#define gst_string_length(v) (gst_string_raw(v)[0])
#define gst_string_hash(v) (gst_string_raw(v)[1])

/* Bytecode */
enum GstOpCode {
    GST_OP_ADD = 0,    /* 0x0000 */
    GST_OP_SUB,        /* 0x0001 */
    GST_OP_MUL,        /* 0x0002 */
    GST_OP_DIV,        /* 0x0003 */
    GST_OP_NOT,        /* 0x0004 */
    GST_OP_LD0,        /* 0x0005 */
    GST_OP_LD1,        /* 0x0006 */
    GST_OP_FLS,        /* 0x0007 */
    GST_OP_TRU,        /* 0x0008 */
    GST_OP_NIL,        /* 0x0009 */
    GST_OP_I16,        /* 0x000a */
    GST_OP_UPV,        /* 0x000b */
    GST_OP_JIF,        /* 0x000c */
    GST_OP_JMP,        /* 0x000d */
    GST_OP_CAL,        /* 0x000e */
    GST_OP_RET,        /* 0x000f */
    GST_OP_SUV,        /* 0x0010 */
    GST_OP_CST,        /* 0x0011 */
    GST_OP_I32,        /* 0x0012 */
    GST_OP_F64,        /* 0x0013 */
    GST_OP_MOV,        /* 0x0014 */
    GST_OP_CLN,        /* 0x0015 */
    GST_OP_EQL,        /* 0x0016 */
    GST_OP_LTN,        /* 0x0017 */
    GST_OP_LTE,        /* 0x0018 */
    GST_OP_ARR,        /* 0x0019 */
    GST_OP_DIC,        /* 0x001a */
    GST_OP_TCL,        /* 0x001b */
    GST_OP_ADM,        /* 0x001c */
    GST_OP_SBM,        /* 0x001d */
    GST_OP_MUM,        /* 0x001e */
    GST_OP_DVM,        /* 0x001f */
    GST_OP_RTN,        /* 0x0020 */
    GST_OP_SET,        /* 0x0021 */
    GST_OP_GET,        /* 0x0022 */
	GST_OP_ERR,        /* 0x0023 */
	GST_OP_TRY,        /* 0x0024 */
	GST_OP_UTY         /* 0x0025 */
};

#endif

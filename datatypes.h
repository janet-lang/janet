#ifndef DATATYPES_H_PJJ035NT
#define DATATYPES_H_PJJ035NT

#include <stdint.h>
#include <setjmp.h>

#define THREAD_STATUS_ALIVE 0
#define THREAD_STATUS_DEAD 1
#define THREAD_STATUS_PENDING 2

typedef enum Type {
    TYPE_NIL = 0,
    TYPE_NUMBER,
    TYPE_BOOLEAN,
    TYPE_STRING,
    TYPE_SYMBOL,
    TYPE_ARRAY,
    TYPE_THREAD,
    TYPE_FORM,
    TYPE_BYTEBUFFER,
    TYPE_FUNCTION,
    TYPE_CFUNCTION,
    TYPE_DICTIONARY,
    TYPE_FUNCDEF,
    TYPE_FUNCENV
} Type;

typedef double Number;
typedef uint8_t Boolean;
typedef struct VM VM;
typedef struct Value Value;
typedef Value (*CFunction)(VM * vm);
typedef struct Func Func;
typedef struct FuncDef FuncDef;
typedef struct FuncEnv FuncEnv;
typedef union ValueData ValueData;
typedef struct DictBucket DictBucket;
typedef struct Array Array;
typedef struct Buffer Buffer;
typedef struct Dictionary Dictionary;
typedef struct DictionaryIterator DictionaryIterator;
typedef struct GC GC;
typedef struct Parser Parser;
typedef struct ParseState ParseState;
typedef struct Scope Scope;
typedef struct Compiler Compiler;

union ValueData {
    Boolean boolean;
    Number number;
    uint8_t * string;
    Array * array;
    Buffer * buffer;
    Dictionary * dict;
    Func * func;
    void * pointer;
    FuncDef * funcdef;
    FuncEnv * funcenv;
    CFunction cfunction;
    uint16_t u16[4];
    uint8_t u8[8];
} data;

/* Use an Array to represent the stack. A Stack frame is 
 * represented by a grouping of FRAME_SIZE values. */
#define FRAME_SIZE 4

#define ThreadStack(t)   ((t)->data + (t)->count)

#define FrameMeta(t)     (ThreadStack(t)[-1])
#define FrameReturn(t)   ((ThreadStack(t) - 1)->data.u16[0])
#define FrameSize(t)     ((ThreadStack(t) - 1)->data.u16[1])
#define FramePrevSize(t) ((ThreadStack(t) - 1)->data.u16[2])

#define FrameCallee(t)   (ThreadStack(t)[-2])
#define FrameEnvValue(t) (ThreadStack(t)[-3])
#define FrameEnv(t)      ((ThreadStack(t) - 3)->data.funcenv)
#define FramePCValue(t)  (ThreadStack(t)[-4])
#define FramePC(t)       ((ThreadStack(t)[-1]).data.pointer)

struct Array {
    uint32_t count;
    uint32_t capacity;
    Value * data;
};

struct Buffer {
    uint32_t count;
    uint32_t capacity;
    uint8_t * data;
};

struct Dictionary {
    uint32_t count;
    uint32_t capacity;
    DictBucket ** buckets;
};

struct DictionaryIterator {
    Dictionary * dict;
    uint32_t index;
    DictBucket * bucket;
};

struct FuncDef {
    uint32_t locals;
    uint32_t arity;
    uint32_t literalsLen;
    uint32_t byteCodeLen;
    Value * literals; /* Contains strings, FuncDefs, etc. */
    uint16_t * byteCode;
};

struct FuncEnv {
    Array * thread; /* When nil, index the local values */
    uint32_t stackOffset; /* Used as environment size when off stack */
    Value * values;
};

struct Func {
    FuncDef * def;
    FuncEnv * env;
    Func * parent;
};

struct Value {
    Type type;
    ValueData data;
};

struct DictBucket {
    Value key;
    Value value;
    DictBucket * next;
};

struct GC {
    void * blocks;
    void * user;
    void (*handleOutOfMemory)(GC * gc);
    uint32_t memoryInterval;
    uint32_t nextCollection;
    uint32_t black : 1;
};

struct VM {
    GC gc;
    const char * error;
    uint16_t * pc;
    Array * thread;
    Value * base;
    jmp_buf jump;
    Value tempRoot; /* Temporary GC root */
};

/* Parsing */

#define PARSER_PENDING 0
#define PARSER_FULL 1
#define PARSER_ERROR -1

struct Parser {
    VM * vm;
    const char * error;
    ParseState * data;
    Value value;
    uint32_t count;
    uint32_t cap;
    uint32_t index;
    uint32_t status;
};

/* Compiling */

struct Compiler {
    VM * vm;
    const char * error;
    jmp_buf onError;
    Scope * root;
    Scope * tail;
    Array * env;
    Buffer * buffer;
};

#endif /* end of include guard: DATATYPES_H_PJJ035NT */

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
typedef struct Parser Parser;
typedef struct ParseState ParseState;
typedef struct Scope Scope;
typedef struct Compiler Compiler;
typedef struct StackFrame StackFrame;

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

struct StackFrame {
    Value callee;
    uint16_t size;
    uint16_t prevSize;
    uint16_t ret;
    FuncEnv * env;
    uint16_t * pc;
};

struct VM {
    /* Garbage collection */
    void * blocks;
    uint32_t memoryInterval;
    uint32_t nextCollection;
    uint32_t black : 1;
    uint32_t lock : 31;
    /* Thread */
    uint16_t * pc;
    Array * thread;
    Value * base;
    StackFrame * frame;
    /* Return state */
    const char * error;
    jmp_buf jump;
    Value ret; /* Returned value from VMStart */
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
    Scope * tail;
    Array * env;
    Buffer * buffer;
};

/* String utils */

#define VStringRaw(s) ((uint32_t *)(s) - 2)
#define VStringSize(v) (VStringRaw(v)[0])
#define VStringHash(v) (VStringRaw(v)[1])

/* Bytecode */

enum OpCode {
    VM_OP_ADD = 0,    /* 0x0000 */
    VM_OP_SUB,        /* 0x0001 */
    VM_OP_MUL,        /* 0x0002 */
    VM_OP_DIV,        /* 0x0003 */
    VM_OP_NOT,        /* 0x0004 */
    VM_OP_LD0,        /* 0x0005 */
    VM_OP_LD1,        /* 0x0006 */
    VM_OP_FLS,        /* 0x0007 */
    VM_OP_TRU,        /* 0x0008 */
    VM_OP_NIL,        /* 0x0009 */
    VM_OP_I16,        /* 0x000a */
    VM_OP_UPV,        /* 0x000b */
    VM_OP_JIF,        /* 0x000c */
    VM_OP_JMP,        /* 0x000d */
    VM_OP_CAL,        /* 0x000e */
    VM_OP_RET,        /* 0x000f */
    VM_OP_SUV,        /* 0x0010 */
    VM_OP_CST,        /* 0x0011 */
    VM_OP_I32,        /* 0x0012 */
    VM_OP_F64,        /* 0x0013 */
    VM_OP_MOV,        /* 0x0014 */
    VM_OP_CLN,        /* 0x0015 */
    VM_OP_EQL,        /* 0x0016 */
    VM_OP_LTN,        /* 0x0017 */
    VM_OP_LTE,        /* 0x0018 */
    VM_OP_ARR,        /* 0x0019 */
    VM_OP_DIC,        /* 0x001a */
    VM_OP_TCL,        /* 0x001b */
    VM_OP_ADM,        /* 0x001c */
    VM_OP_SBM,        /* 0x001d */
    VM_OP_MUM,        /* 0x001e */
    VM_OP_DVM,        /* 0x001f */
    VM_OP_RTN,        /* 0x0020 */
    VM_OP_SET,        /* 0x0021 */
    VM_OP_GET         /* 0x0022 */
};

#endif

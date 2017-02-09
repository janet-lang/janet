#ifndef OPCODES_H_EFPEYNZ0
#define OPCODES_H_EFPEYNZ0

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
    VM_OP_RTN         /* 0x0020 */
};

#endif /* end of include guard: OPCODES_H_EFPEYNZ0 */

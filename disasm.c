#include "disasm.h"

/* Width of padded opcode names */
#define OP_WIDTH 20

/* Print various register and arguments to instructions */
static void dasmPrintSlot(FILE * out, uint16_t index) { fprintf(out, "%d ", index); }
static void dasmPrintI16(FILE * out, int16_t number) { fprintf(out, "#%d ", number); }
static void dasmPrintI32(FILE * out, int32_t number) { fprintf(out, "#%d ", number); }
static void dasmPrintF64(FILE * out, double number) { fprintf(out, "#%f ", number); }
static void dasmPrintLiteral(FILE * out, uint16_t index) { fprintf(out, "(%d) ", index); }
static void dasmPrintUpValue(FILE * out, uint16_t level, uint16_t index) {
	fprintf(out, "<%d, %d> ", level, index);
}

/* Print the name of the argument but pad it */
static void dasmPrintArg(FILE * out, const char * name) { 
    uint32_t i = 0;
    char c;
    while ((c = *name++)) {
		fputc(c, out);
		++i;
    }
	for (; i < OP_WIDTH; ++i)
		fputc(' ', out);
}

/* Print instructions that take a fixed number of arguments */
static uint32_t dasmPrintFixedOp(FILE * out, const uint16_t * current,
                                 const char * name, uint32_t size) {
    uint32_t i;
	dasmPrintArg(out, name);
	for (i = 1; i <= size; ++i) {
		dasmPrintSlot(out, current[i]);
	}
	return size + 1;
}

/* Print instructions that take a variable number of arguments */
static uint32_t dasmPrintVarArgOp(FILE * out, const uint16_t * current,
                                  const char * name, uint32_t extra) {
	uint32_t i, argCount;
	dasmPrintArg(out, name);
	for (i = 0; i < extra; ++i) {
		dasmPrintSlot(out, current[i + 1]);
	}
	argCount = current[extra + 1];
	fprintf(out, ": "); /* Argument separator */
	for (i = 0; i < argCount; ++i) {
		dasmPrintSlot(out, current[i + extra + 2]);
	}
	return argCount + extra + 2;
}

/* Print the disassembly for a function definition */
void dasmFuncDef(FILE * out, FuncDef * def) {
	dasm(out, def->byteCode, def->byteCodeLen);
}

/* Print the disassembly for a function */
void dasmFunc(FILE * out, Func * f) {
	dasm(out, f->def->byteCode, f->def->byteCodeLen);
}

/* Disassemble some bytecode and display it as opcode + arguments assembly */
void dasm(FILE * out, uint16_t *byteCode, uint32_t len) {
	uint16_t *current = byteCode;
	uint16_t *end = byteCode + len;

	fprintf(out, "----- ASM BYTECODE AT %p -----\n", byteCode);

	while (current < end) {
		switch (*current) {
		case VM_OP_ADD:
    		current += dasmPrintFixedOp(out, current, "add", 3);
    		break;
		case VM_OP_SUB:
    		current += dasmPrintFixedOp(out, current, "sub", 3);
    		break;		
    	case VM_OP_MUL:
    		current += dasmPrintFixedOp(out, current, "mul", 3);
    		break;		
    	case VM_OP_DIV:
    		current += dasmPrintFixedOp(out, current, "div", 3);
    		break;
    	case VM_OP_NOT:
        	current += dasmPrintFixedOp(out, current, "not", 2);
        	break;
       	case VM_OP_LD0:
           	current += dasmPrintFixedOp(out, current, "load0", 1);
           	break;
       	case VM_OP_LD1:
           	current += dasmPrintFixedOp(out, current, "load1", 1);
           	break;
       	case VM_OP_FLS:
           	current += dasmPrintFixedOp(out, current, "loadFalse", 1);
           	break;
       	case VM_OP_TRU:
           	current += dasmPrintFixedOp(out, current, "loadTrue", 1);
           	break;
       	case VM_OP_NIL:
           	current += dasmPrintFixedOp(out, current, "loadNil", 1);
           	break;
       	case VM_OP_I16:
           	dasmPrintArg(out, "loadInt16");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintI16(out, ((int16_t *)current)[2]);
           	current += 3;
           	break;
       	case VM_OP_UPV:
           	dasmPrintArg(out, "loadUpValue");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintUpValue(out, current[2], current[3]);
           	current += 4;
           	break;
       	case VM_OP_JIF:
           	dasmPrintArg(out, "jumpIf");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintI32(out, ((int32_t *)(current + 2))[0]);
           	current += 4;
           	break;
       	case VM_OP_JMP:
           	dasmPrintArg(out, "jump");
           	dasmPrintI32(out, ((int32_t *)(current + 1))[0]);
           	current += 3;
           	break;
       	case VM_OP_CAL:
           	current += dasmPrintVarArgOp(out, current, "call", 2);
           	break;
       	case VM_OP_RET:
           	current += dasmPrintFixedOp(out, current, "return", 1);
           	break;
       	case VM_OP_SUV:
           	dasmPrintArg(out, "setUpValue");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintUpValue(out, current[2], current[3]);
           	current += 4;
           	break;
       	case VM_OP_CST:
           	dasmPrintArg(out, "loadLiteral");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintLiteral(out, current[2]);
           	current += 3;
           	break;
       	case VM_OP_I32:
           	dasmPrintArg(out, "loadInt32");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintI32(out, ((int32_t *)(current + 2))[0]);
           	current += 4;
           	break;
       	case VM_OP_F64:
           	dasmPrintArg(out, "loadFloat64");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintF64(out, ((double *)(current + 2))[0]);
           	current += 6;
           	break;
       	case VM_OP_MOV:
           	current += dasmPrintFixedOp(out, current, "move", 2);
           	break;
       	case VM_OP_CLN:
           	dasmPrintArg(out, "makeClosure");
           	dasmPrintSlot(out, current[1]);
           	dasmPrintLiteral(out, current[2]);
           	current += 3;
           	break;
       	case VM_OP_EQL:
           	current += dasmPrintFixedOp(out, current, "equals", 3);
           	break;
       	case VM_OP_LTN:
           	current += dasmPrintFixedOp(out, current, "lessThan", 3);
           	break;
       	case VM_OP_LTE:
           	current += dasmPrintFixedOp(out, current, "lessThanEquals", 3);
           	break;
       	case VM_OP_ARR:
           	current += dasmPrintVarArgOp(out, current, "array", 1);
           	break;
       	case VM_OP_DIC:
           	current += dasmPrintVarArgOp(out, current, "dictionary", 1);
           	break;
       	case VM_OP_TCL:
           	current += dasmPrintVarArgOp(out, current, "tailCall", 1);
           	break;
       	case VM_OP_ADM:
           	current += dasmPrintVarArgOp(out, current, "addMultiple", 1);
           	break;
       	case VM_OP_SBM:
           	current += dasmPrintVarArgOp(out, current, "subMultiple", 1);
           	break;
       	case VM_OP_MUM:
           	current += dasmPrintVarArgOp(out, current, "mulMultiple", 1);
           	break;
       	case VM_OP_DVM:
           	current += dasmPrintVarArgOp(out, current, "divMultiple", 1);
           	break;
        case VM_OP_RTN:
			current += dasmPrintFixedOp(out, current, "returnNil", 0);
            break;
		}
		fprintf(out, "\n");
	}
	fprintf(out, "----- END ASM BYTECODE -----\n");
}

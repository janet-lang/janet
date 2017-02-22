#include "disasm.h"

/* Width of padded opcode names */
#define OP_WIDTH 20

/* Print various register and arguments to instructions */
static void dasm_print_slot(FILE * out, uint16_t index) { fprintf(out, "%d ", index); }
static void dasm_print_i16(FILE * out, int16_t number) { fprintf(out, "#%d ", number); }
static void dasm_print_i32(FILE * out, int32_t number) { fprintf(out, "#%d ", number); }
static void dasm_print_f64(FILE * out, double number) { fprintf(out, "#%f ", number); }
static void dasm_print_literal(FILE * out, uint16_t index) { fprintf(out, "(%d) ", index); }
static void dasm_print_upvalue(FILE * out, uint16_t level, uint16_t index) {
	fprintf(out, "<%d, %d> ", level, index);
}

/* Print the name of the argument but pad it */
static void dasm_print_arg(FILE * out, const char * name) { 
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
static uint32_t dasm_fixed_op(FILE * out, const uint16_t * current,
                                 const char * name, uint32_t size) {
    uint32_t i;
	dasm_print_arg(out, name);
	for (i = 1; i <= size; ++i) {
		dasm_print_slot(out, current[i]);
	}
	return size + 1;
}

/* Print instructions that take a variable number of arguments */
static uint32_t dasm_varg_op(FILE * out, const uint16_t * current,
                                  const char * name, uint32_t extra) {
	uint32_t i, argCount;
	dasm_print_arg(out, name);
	for (i = 0; i < extra; ++i) {
		dasm_print_slot(out, current[i + 1]);
	}
	argCount = current[extra + 1];
	fprintf(out, ": "); /* Argument separator */
	for (i = 0; i < argCount; ++i) {
		dasm_print_slot(out, current[i + extra + 2]);
	}
	return argCount + extra + 2;
}

/* Print the disassembly for a function definition */
void gst_dasm_funcdef(FILE * out, GstFuncDef * def) {
	gst_dasm(out, def->byteCode, def->byteCodeLen);
}

/* Print the disassembly for a function */
void gst_dasm_function(FILE * out, GstFunction * f) {
	gst_dasm(out, f->def->byteCode, f->def->byteCodeLen);
}

/* Disassemble some bytecode and display it as opcode + arguments assembly */
void gst_dasm(FILE * out, uint16_t *byteCode, uint32_t len) {
	uint16_t *current = byteCode;
	uint16_t *end = byteCode + len;

	fprintf(out, "----- ASM BYTECODE START -----\n");

	while (current < end) {
		switch (*current) {
		case GST_OP_ADD:
    		current += dasm_fixed_op(out, current, "add", 3);
    		break;
		case GST_OP_SUB:
    		current += dasm_fixed_op(out, current, "sub", 3);
    		break;		
    	case GST_OP_MUL:
    		current += dasm_fixed_op(out, current, "mul", 3);
    		break;		
    	case GST_OP_DIV:
    		current += dasm_fixed_op(out, current, "div", 3);
    		break;
    	case GST_OP_NOT:
        	current += dasm_fixed_op(out, current, "not", 2);
        	break;
       	case GST_OP_LD0:
           	current += dasm_fixed_op(out, current, "load0", 1);
           	break;
       	case GST_OP_LD1:
           	current += dasm_fixed_op(out, current, "load1", 1);
           	break;
       	case GST_OP_FLS:
           	current += dasm_fixed_op(out, current, "loadFalse", 1);
           	break;
       	case GST_OP_TRU:
           	current += dasm_fixed_op(out, current, "loadTrue", 1);
           	break;
       	case GST_OP_NIL:
           	current += dasm_fixed_op(out, current, "loadNil", 1);
           	break;
       	case GST_OP_I16:
           	dasm_print_arg(out, "loadInt16");
           	dasm_print_slot(out, current[1]);
           	dasm_print_i16(out, ((int16_t *)current)[2]);
           	current += 3;
           	break;
       	case GST_OP_UPV:
           	dasm_print_arg(out, "loadUpValue");
           	dasm_print_slot(out, current[1]);
           	dasm_print_upvalue(out, current[2], current[3]);
           	current += 4;
           	break;
       	case GST_OP_JIF:
           	dasm_print_arg(out, "jumpIf");
           	dasm_print_slot(out, current[1]);
           	dasm_print_i32(out, ((int32_t *)(current + 2))[0]);
           	current += 4;
           	break;
       	case GST_OP_JMP:
           	dasm_print_arg(out, "jump");
           	dasm_print_i32(out, ((int32_t *)(current + 1))[0]);
           	current += 3;
           	break;
       	case GST_OP_CAL:
           	current += dasm_varg_op(out, current, "call", 2);
           	break;
       	case GST_OP_RET:
           	current += dasm_fixed_op(out, current, "return", 1);
           	break;
       	case GST_OP_SUV:
           	dasm_print_arg(out, "setUpValue");
           	dasm_print_slot(out, current[1]);
           	dasm_print_upvalue(out, current[2], current[3]);
           	current += 4;
           	break;
       	case GST_OP_CST:
           	dasm_print_arg(out, "loadLiteral");
           	dasm_print_slot(out, current[1]);
           	dasm_print_literal(out, current[2]);
           	current += 3;
           	break;
       	case GST_OP_I32:
           	dasm_print_arg(out, "loadInt32");
           	dasm_print_slot(out, current[1]);
           	dasm_print_i32(out, ((int32_t *)(current + 2))[0]);
           	current += 4;
           	break;
       	case GST_OP_F64:
           	dasm_print_arg(out, "loadFloat64");
           	dasm_print_slot(out, current[1]);
           	dasm_print_f64(out, ((double *)(current + 2))[0]);
           	current += 6;
           	break;
       	case GST_OP_MOV:
           	current += dasm_fixed_op(out, current, "move", 2);
           	break;
       	case GST_OP_CLN:
           	dasm_print_arg(out, "makeClosure");
           	dasm_print_slot(out, current[1]);
           	dasm_print_literal(out, current[2]);
           	current += 3;
           	break;
       	case GST_OP_EQL:
           	current += dasm_fixed_op(out, current, "equals", 3);
           	break;
       	case GST_OP_LTN:
           	current += dasm_fixed_op(out, current, "lessThan", 3);
           	break;
       	case GST_OP_LTE:
           	current += dasm_fixed_op(out, current, "lessThanEquals", 3);
           	break;
       	case GST_OP_ARR:
           	current += dasm_varg_op(out, current, "array", 1);
           	break;
       	case GST_OP_DIC:
           	current += dasm_varg_op(out, current, "object", 1);
           	break;
       	case GST_OP_TCL:
           	current += dasm_varg_op(out, current, "tailCall", 1);
           	break;
       	case GST_OP_ADM:
           	current += dasm_varg_op(out, current, "addMultiple", 1);
           	break;
       	case GST_OP_SBM:
           	current += dasm_varg_op(out, current, "subMultiple", 1);
           	break;
       	case GST_OP_MUM:
           	current += dasm_varg_op(out, current, "mulMultiple", 1);
           	break;
       	case GST_OP_DVM:
           	current += dasm_varg_op(out, current, "divMultiple", 1);
           	break;
        case GST_OP_RTN:
            current += dasm_fixed_op(out, current, "returnNil", 0);
            break;
        case GST_OP_GET:
            current += dasm_fixed_op(out, current, "get", 3);
            break;
        case GST_OP_SET:
            current += dasm_fixed_op(out, current, "set", 3);
            break;
        case GST_OP_ERR:
            current += dasm_fixed_op(out, current, "error", 1);
            break;
        case GST_OP_TRY:
            dasm_print_arg(out, "try");
            dasm_print_slot(out, current[1]);
            dasm_print_i32(out, *(int32_t *)(current + 2));
            current += 4;
            break;
        case GST_OP_UTY:
            current += dasm_fixed_op(out, current, "untry", 0);
            break;
		}
		fprintf(out, "\n");
	}
	fprintf(out, "----- END ASM BYTECODE -----\n");
}

#ifndef disasm_h_INCLUDED
#define disasm_h_INCLUDED

#include "datatypes.h"
#include <stdio.h>

/* Print disassembly for a given funciton */
void dasm(FILE * out, uint16_t * byteCode, uint32_t len);

/* Print the disassembly for a function definition */
void dasmFuncDef(FILE * out, FuncDef * def);

/* Print the disassembly for a function */
void dasmFunc(FILE * out, Func * f);

#endif // disasm_h_INCLUDED


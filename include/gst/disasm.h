#ifndef disasm_h_INCLUDED
#define disasm_h_INCLUDED

#include <gst/gst.h>
#include <stdio.h>

/* Print disassembly for a given funciton */
void gst_dasm(FILE * out, uint16_t * byteCode, uint32_t len);

/* Print the disassembly for a function definition */
void gst_dasm_funcdef(FILE * out, GstFuncDef * def);

/* Print the disassembly for a function */
void gst_dasm_function(FILE * out, GstFunction * f);

#endif // disasm_h_INCLUDED


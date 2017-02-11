#ifndef COMPILE_H_9VXF71HY
#define COMPILE_H_9VXF71HY

#include "datatypes.h"

/* Initialize the Compiler */
void CompilerInit(Compiler * c, VM * vm);

/* Register a global for the compilation environment. */
void CompilerAddGlobal(Compiler * c, const char * name, Value x);

/* Register a global c function for the compilation environment. */
void CompilerAddGlobalCFunc(Compiler * c, const char * name, CFunction f);

/* Compile a function that evaluates the given form. */
Func * CompilerCompile(Compiler * c, Value form);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

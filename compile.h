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

/* Macro expansion. Macro expansion happens prior to the compilation process
 * and is completely separate. This allows the compilation to not have to worry
 * about garbage collection and other issues that would complicate both the
 * runtime and the compilation. */
int CompileMacroExpand(VM * vm, Value x, Dictionary * macros, Value * out);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

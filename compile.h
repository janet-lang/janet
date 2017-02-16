#ifndef COMPILE_H_9VXF71HY
#define COMPILE_H_9VXF71HY

#include "datatypes.h"

/* Initialize the Compiler */
void gst_compiler(GstCompiler *c, Gst *vm);

/* Register a global for the compilation environment. */
void gst_compiler_add_global(GstCompiler *c, const char *name, GstValue x);

/* Register a global c function for the compilation environment. */
void gst_compiler_add_global_cfunction(GstCompiler *c, const char *name, GstCFunction f);

/* Compile a function that evaluates the given form. */
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form);

/* Macro expansion. Macro expansion happens prior to the compilation process
 * and is completely separate. This allows the compilation to not have to worry
 * about garbage collection and other issues that would complicate both the
 * runtime and the compilation. */
int gst_macro_expand(Gst *vm, GstValue x, GstObject *macros, GstValue *out);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

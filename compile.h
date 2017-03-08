#ifndef COMPILE_H_9VXF71HY
#define COMPILE_H_9VXF71HY

#include "datatypes.h"
#include <setjmp.h>

typedef struct GstCompiler GstCompiler;
typedef struct GstScope GstScope;

/* Compilation state */
struct GstCompiler {
    Gst *vm;
    const char *error;
    jmp_buf onError;
    GstScope *tail;
    GstArray *env;
    GstBuffer *buffer;
};

/* Initialize the Compiler */
void gst_compiler(GstCompiler *c, Gst *vm);

/* Register a global for the compilation environment. */
void gst_compiler_add_global(GstCompiler *c, const char *name, GstValue x);

/* Register a global c function for the compilation environment. */
void gst_compiler_add_global_cfunction(GstCompiler *c, const char *name, GstCFunction f);

/* Compile a function that evaluates the given form. */
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

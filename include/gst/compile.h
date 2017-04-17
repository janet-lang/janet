#ifndef COMPILE_H_9VXF71HY
#define COMPILE_H_9VXF71HY

#include <gst/gst.h>
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

/* Add many globals */
void gst_compiler_globals(GstCompiler *c, GstValue env);

/* Register a global for the compilation environment. */
void gst_compiler_global(GstCompiler *c, const char *name, GstValue x);

/* Use a module */
void gst_compiler_usemodule(GstCompiler *c, const char *modulename);

/* Compile a function that evaluates the given form. */
GstFunction *gst_compiler_compile(GstCompiler *c, GstValue form);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

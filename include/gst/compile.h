/*
* Copyright (c) 2017 Calvin Rose
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef COMPILE_H_9VXF71HY
#define COMPILE_H_9VXF71HY

#include <gst/gst.h>
#include <setjmp.h>

typedef struct GstCompiler GstCompiler;
typedef struct GstScope GstScope;

/* Compilation state */
struct GstCompiler {
    Gst *vm;
    GstValue error;
    jmp_buf onError;
    GstScope *tail;
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

/* Load the library */
void gst_compile_load(Gst *vm);

#endif /* end of include guard: COMPILE_H_9VXF71HY */

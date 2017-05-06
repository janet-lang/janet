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

#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>

/* Use readline support for now */
#include <readline/readline.h>
#include <readline/history.h>

/* Compile and run an ast */
int debug_compile_and_run(Gst *vm, GstValue ast, GstValue last) {
    GstCompiler c;
    GstValue func;
    /* Try to compile generated AST */
    gst_compiler(&c, vm);
    gst_compiler_usemodule(&c, "std");
    gst_compiler_usemodule(&c, "std.io");
    gst_compiler_usemodule(&c, "std.parse");
    gst_compiler_usemodule(&c, "std.compile");
    gst_compiler_global(&c, "_", last);
    gst_compiler_globals(&c, gst_wrap_table(vm->registry));
    func = gst_wrap_function(gst_compiler_compile(&c, ast));
    /* Check for compilation errors */
    if (c.error) {
        printf("Compiler error: %s\n", c.error);
        return 1;
    }
    /* Execute function */
    if (gst_run(vm, func)) {
        if (vm->crash) {
            printf("VM crash: %s\n", vm->crash);
        } else {
            printf("VM error: %s\n", (char *)gst_to_string(vm, vm->ret));
        }
        return 1;
    }
    return 0;
}

/* Parse a file and execute it */
int debug_run(Gst *vm, FILE *in) {
    char buffer[1024] = {0};
    const char *reader = buffer;
    GstValue ast;
    GstParser p;
    /* Init parser */
    gst_parser(&p, vm);
    /* Get and parse input until we have a full form */
    while (p.status != GST_PARSER_ERROR) {
        if (*reader == '\0') {
            if (!fgets(buffer, sizeof(buffer), in)) {
                /* Check that parser is complete */
                if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
                    printf("Unexpected end of source\n");
                    return 1;
                }
                return 0;
            }
            reader = buffer;
        }
        if (p.status != GST_PARSER_FULL)
            reader += gst_parse_cstring(&p, reader);
        if (gst_parse_hasvalue(&p)) {
            ast = gst_parse_consume(&p);
            debug_compile_and_run(vm, ast, gst_wrap_nil());
        }
    }
    /* Check if file read in correctly */
    if (p.error) {
        printf("Parse error: %s\n", p.error);
    }
    return 1;
}

/* A simple repl */
int debug_repl(Gst *vm) {
    const char *buffer, *reader;
    GstParser p;
    buffer = reader = NULL;
    for (;;) {
        /* Init parser */
        gst_parser(&p, vm);
        while (p.status != GST_PARSER_ERROR && p.status != GST_PARSER_FULL) {
            gst_parse_cstring(&p, "\n");
            if (p.status == GST_PARSER_ERROR || p.status == GST_PARSER_FULL)
                break;
            if (!reader || *reader == '\0') {
                buffer = readline(">> ");
                add_history(buffer);
                reader = buffer;
            }
            reader += gst_parse_cstring(&p, reader);
        }
        /* Check if file read in correctly */
        if (p.error) {
            printf("Parse error: %s\n", p.error);
            continue;
        }
        /* Check that parser is complete */
        if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
            printf("Unexpected end of source\n");
            continue;
        }
        if (!debug_compile_and_run(vm, gst_parse_consume(&p), vm->ret)) {
            printf("%s\n", gst_to_string(vm, vm->ret));
        }
    }
}

int main(int argc, const char **argv) {
    Gst vm;
    int status = 0;

    gst_init(&vm);
    gst_stl_load(&vm);
    gst_parse_load(&vm);
    gst_compile_load(&vm);
    if (argc > 1) {
        const char *filename;
        FILE *f;
        filename = argv[1];
        f = fopen(filename, "rb"); 
        status = debug_run(&vm, f);
    } else {
        status = debug_repl(&vm);
    }

    gst_deinit(&vm);

    return status;
}

#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>
#include <gst/disasm.h>

/* Compile and run an ast */
int debug_compile_and_run(Gst *vm, GstValue ast) {
    GstCompiler c;
    GstValue func;
    /* Try to compile generated AST */
    gst_compiler(&c, vm);
    gst_compiler_usemodule(&c, "std");
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
    GstValue *tup;
    GstArray *arr;
    GstParser p;
    /* Init parser */
    gst_parser(&p, vm);
    /* Create do struct */
    arr = gst_array(vm, 10);
    gst_array_push(vm, arr, gst_string_cv(vm, "do"));
    /* Get and parse input until we have a full form */
    while (p.status != GST_PARSER_ERROR) {
        if (*reader == '\0') {
            if (!fgets(buffer, sizeof(buffer), in)) {
                break;
            }
            reader = buffer;
        }
        reader += gst_parse_cstring(&p, reader);
        if (gst_parse_hasvalue(&p))
            gst_array_push(vm, arr, gst_parse_consume(&p));
    }
    /* Turn array into tuple */
    tup = gst_tuple_begin(vm, arr->count);
    gst_memcpy(tup, arr->data, arr->count * sizeof(GstValue));
    ast = gst_wrap_tuple(gst_tuple_end(vm, tup));
    /* Check if file read in correctly */
    if (p.error) {
        printf("Parse error: %s\n", p.error);
        return 1;
    }
    /* Check that parser is complete */
    if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
        printf("Unexpected end of source\n");
        return 1;
    }
    return debug_compile_and_run(vm, ast);
}

/* A simple repl */
int debug_repl(Gst *vm) {
    char buffer[1024] = {0};
    const char *reader = buffer;
    GstParser p;
    int reset;
    for (;;) {
        /* Init parser */
        gst_parser(&p, vm);
        reset = 1;
        while (p.status != GST_PARSER_ERROR && p.status != GST_PARSER_FULL) {
            if (*reader == '\0') {
                if (reset)
                    printf(">> ");
                reset = 0;
                if (!fgets(buffer, sizeof(buffer), stdin)) {
                    break;
                }
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
        if (0 == debug_compile_and_run(vm, gst_parse_consume(&p))) {
            printf("%s\n", gst_to_string(vm, vm->ret));
        }
    }
}

int main(int argc, const char **argv) {
    Gst vm;
    int status = 0;

    gst_init(&vm);
    gst_stl_load(&vm);
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

#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>
#include <gst/disasm.h>

/* Parse a file and execute it */
int debug_run(Gst *vm, FILE *in) {
    char buffer[1024] = {0};
    const char *reader = buffer;
    GstValue func;
    GstParser p;
    GstCompiler c;

    gst_parser(&p, vm);

    /* Create do struct */
    GstArray *arr = gst_array(vm, 10);
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
    GstValue *tup = gst_tuple_begin(vm, arr->count);
    gst_memcpy(tup, arr->data, arr->count * sizeof(GstValue));
    vm->ret.type = GST_TUPLE;
    vm->ret.data.tuple = gst_tuple_end(vm, tup);

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

    /* Try to compile generated AST */
    gst_compiler(&c, vm);
    func.type = GST_NIL;
    gst_compiler_usemodule(&c, "std");
    func.type = GST_FUNCTION;
    func.data.function = gst_compiler_compile(&c, vm->ret);

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

int main(int argc, const char **argv) {

    Gst vm;
    int status = 0;
    gst_init(&vm);
    gst_stl_load(&vm);

    const char *filename;

   // if (argc > 1) {
    //    filename = argv[1];
    //} else {
        filename = "libs/stl.gst";
    //}

    FILE *f = fopen(filename, "rb"); 
    status = debug_run(&vm, f);

    gst_deinit(&vm);

    return status;
}

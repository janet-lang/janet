#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/parse.h>
#include <gst/compile.h>
#include <gst/stl.h>
#include <gst/disasm.h>

/* A simple repl for debugging */
void debug_repl(FILE *in, FILE *out) {
    char buffer[1024] = {0};
    const char * reader = buffer;
    GstValue func;
    Gst vm;
    GstParser p;
    GstCompiler c;

    gst_init(&vm);
    gst_stl_load(&vm);

    for (;;) {

        /* Reset state */
        gst_parser(&p, &vm);

        /* Get and parse input until we have a full form */
        while (p.status == GST_PARSER_PENDING) {
            /* Get some input if we are done */
            if (*reader == '\0') {
                if (out)
                    fprintf(out, ">> ");
                if (!fgets(buffer, sizeof(buffer), in)) {
                    return;
                }
                p.index = 0;
                reader = buffer;
            }
            reader += gst_parse_cstring(&p, reader);
        }

        /* Check for parsing errors */
        if (p.error) {
            unsigned i;
            if (out) {
                fprintf(out, "\n");
                fprintf(out, "%s\n", buffer);
                for (i = 1; i < p.index; ++i) {
                    fprintf(out, " ");
                }
                fprintf(out, "^\n");
                fprintf(out, "\nParse error: %s\n", p.error);
            }
            reader = buffer; /* Flush the input buffer */
            buffer[0] = '\0';
            continue;
        }

        /* Try to compile generated AST */
        gst_compiler(&c, &vm);
        func.type = GST_NIL;
        gst_compiler_usemodule(&c, "std");
        gst_compiler_global(&c, "ans", gst_object_get(vm.rootenv, gst_string_cv(&vm, "ans")));
        func.type = GST_FUNCTION;
        func.data.function = gst_compiler_compile(&c, p.value);

        /* Check for compilation errors */
        if (c.error) {
            if (out) {
                fprintf(out, "Compiler error: %s\n", c.error);
            }
            reader = buffer;
            buffer[0] = 0;
            continue;
        }

        /* Execute function */
        if (gst_run(&vm, func)) {
            if (out) {
                if (vm.crash) {
                    fprintf(out, "VM crash: %s\n", vm.crash);
                } else {
                    fprintf(out, "VM error: ");
                    fprintf(out, "%s\n", (char *)gst_to_string(&vm, vm.ret));
                }
            }
            reader = buffer;
            buffer[0] = 0;
            continue;
        } else if (out) {
            fprintf(out, "%s\n", (char *)gst_to_string(&vm, vm.ret));
            gst_object_put(&vm, vm.rootenv, gst_string_cv(&vm, "ans"), vm.ret);
        }
    }

}

int main() {
    printf("GST v0.0 repl\nCopyright 2017 Calvin Rose\n");
    debug_repl(stdin, stdout);
    return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
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

    vm.rootenv.type = GST_OBJECT;
    vm.rootenv.data.object = gst_object(&vm, 10);
    gst_object_put(&vm, vm.rootenv.data.object, gst_load_cstring(&vm, "_ENV"), vm.rootenv);

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
        gst_compiler_add_global(&c, "ans", func);
        gst_stl_load(&c);
        gst_compiler_env(&c, vm.rootenv);
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

        /* Print asm */
        //if (out) {
            //fprintf(out, "\n");
            //gst_dasm_function(out, func.data.function);
        //}

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
            gst_object_put(&vm, vm.rootenv.data.object, gst_load_cstring(&vm, "ans"), vm.ret);
        }
    }

}

int main() {
    printf("Super cool interpreter v0.0\n");
    debug_repl(stdin, stdout);
    return 0;
}

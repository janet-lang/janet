#include <stdlib.h>
#include <stdio.h>
#include "datatypes.h"
#include "vm.h"
#include "parse.h"
#include "compile.h"
#include "value.h"
#include "disasm.h"

void string_put(uint8_t * string) {
    uint32_t i;
    uint32_t len = gst_string_length(string);
    for (i = 0; i < len; ++i)
        fputc(string[i], stdout);
}

/* Test c function */
GstValue print(Gst *vm) {
    uint32_t j, count;
    GstValue nil;
    count = gst_count_args(vm);
    for (j = 0; j < count; ++j) {
        string_put(gst_to_string(vm, gst_arg(vm, j)));
        fputc('\n', stdout);
    }
    nil.type = GST_NIL;
    return nil;
}

/* A simple repl for debugging */
void debugRepl() {
    char buffer[1024] = {0};
    const char * reader = buffer;
    GstValue func;
    Gst vm;
    GstParser p;
    GstCompiler c;

    gst_init(&vm);

    for (;;) {

        /* Reset state */
        gst_parser(&p, &vm);

        /* Get and parse input until we have a full form */
        while (p.status == GST_PARSER_PENDING) {
            /* Get some input if we are done */
            if (*reader == '\0') {
                printf(">> ");
                if (!fgets(buffer, sizeof(buffer), stdin)) {
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
            printf("\n");
            printf("%s\n", buffer);
            for (i = 0; i < p.index; ++i) {
                printf(" ");
            }
            printf("^\n");
            printf("\nParse error: %s\n", p.error);
            reader = buffer; /* Flush the input buffer */
            buffer[0] = '\0';
            continue;
        }

        /* Try to compile generated AST */
        gst_compiler(&c, &vm);
        gst_compiler_add_global_cfunction(&c, "print", print);
        func.type = GST_FUNCTION;
        func.data.function = gst_compiler_compile(&c, p.value);

        /* Check for compilation errors */
        if (c.error) {
            printf("Compiler error: %s\n", c.error);
            reader = buffer;
            buffer[0] = 0;
            continue;
        }

        /* Print asm */
        printf("\n");
        gst_dasm_function(stdout, func.data.function);
        printf("\n");

        /* Execute function */
        gst_load(&vm, func);
        if (gst_start(&vm)) {
            printf("VM error: %s\n", vm.error);
            reader = buffer;
            buffer[0] = 0;
            continue;
        } else {
            string_put(gst_to_string(&vm, vm.ret));
            printf("\n");
        }
    }

}

int main() {
    printf("Super cool interpreter v0.0\n");
    debugRepl();
    return 0;
}

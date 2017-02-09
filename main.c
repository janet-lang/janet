#include <stdlib.h>
#include <stdio.h>
#include "datatypes.h"
#include "gc.h"
#include "vm.h"
#include "parse.h"
#include "compile.h"
#include "value.h"

/* A simple repl for debugging */
void debugRepl() {
    char buffer[128] = {0};
    const char * reader = buffer;
    Func * func;
    VM vm;
    Parser p;
    Compiler c;

    VMInit(&vm);

    for (;;) {

        /* Run garbage collection */
        VMMaybeCollect(&vm);

        /* Reset state */
        ParserInit(&p, &vm);

        /* Get and parse input until we have a full form */
        while (p.status == PARSER_PENDING) {
            /* Get some input if we are done */
            if (*reader == '\0') {
                printf(">> ");
                if (!fgets(buffer, sizeof(buffer), stdin)) {
                    return;
                }
                p.index = 0;
                reader = buffer;
            }
            reader += ParserParseCString(&p, reader);
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
        CompilerInit(&c, &vm);
        func = CompilerCompile(&c, p.value);

        /* Check for compilation errors */
        if (c.error) {
            printf("Compiler error: %s\n", c.error);
            reader = buffer;
            buffer[0] = 0;
            continue;
        }

        /* Execute function */
        VMLoad(&vm, func);
        if (VMStart(&vm)) {
            printf("VM error: %s\n", vm.error);
            reader = buffer;
            buffer[0] = 0;
            continue;
        } else {
            ValuePrint(&vm.tempRoot, 0);
            printf("\n");
        }
    }

}

int main() {
    printf("Super cool interpreter v0.0\n");
    debugRepl();
}

#include <stdint.h>
#include <string.h>
#include <janet.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    /* init Janet */
    janet_init();

    /* fuzz the parser */
    JanetParser parser;
    janet_parser_init(&parser);
    for (int i = 0, done = 0; i < size; i++) {
        switch (janet_parser_status(&parser)) {
            case JANET_PARSE_DEAD:
            case JANET_PARSE_ERROR:
                done = 1;
                break;
            case JANET_PARSE_PENDING:
                if (i == size) {
                    janet_parser_eof(&parser);
                } else {
                    janet_parser_consume(&parser, data[i]);
                }
                break;
            case JANET_PARSE_ROOT:
                if (i >= size) {
                    janet_parser_eof(&parser);
                } else {
                    janet_parser_consume(&parser, data[i]);
                }
                break;
        }

        if (done == 1)
            break;
    }
    janet_parser_deinit(&parser);

    /* cleanup Janet */
    janet_deinit();

    return 0;
}


#include <stddef.h>
#include <stdint.h>
#include <janet.h>

#define MAX_ASM_INPUT 65536

#ifdef JANET_ASSEMBLER
static void assemble_forms(JanetParser *parser) {
    while (janet_parser_has_more(parser)) {
        Janet form = janet_parser_produce(parser);
        janet_gcroot(form);

        for (int mode = 0; mode < 2; mode++) {
            JanetTryState try_state;
            if (janet_try(&try_state) == JANET_SIGNAL_OK) {
                int flags = mode ? JANET_ASSEMBLE_FLAG_OPTIMIZE : 0;
                JanetAssembleResult result = janet_asm(form, flags);
                (void)result;
            }
            janet_restore(&try_state);
        }

        janet_gcunroot(form);
    }
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
#ifdef JANET_ASSEMBLER
    if (size == 0 || size > MAX_ASM_INPUT) return 0;

    janet_init();
    JanetParser parser;
    janet_parser_init(&parser);

    JanetTryState try_state;
    if (janet_try(&try_state) == JANET_SIGNAL_OK) {
        for (size_t index = 0; index < size; index++) {
            enum JanetParserStatus status = janet_parser_status(&parser);
            if (status == JANET_PARSE_ERROR || status == JANET_PARSE_DEAD)
                break;
            janet_parser_consume(&parser, data[index]);
            assemble_forms(&parser);
        }

        enum JanetParserStatus status = janet_parser_status(&parser);
        if (status != JANET_PARSE_ERROR && status != JANET_PARSE_DEAD) {
            janet_parser_eof(&parser);
            assemble_forms(&parser);
        }
    }
    janet_restore(&try_state);

    janet_parser_deinit(&parser);
    janet_deinit();
#else
    (void)data;
    (void)size;
#endif
    return 0;
}
#include <stdint.h>
#include <stddef.h>
#include <janet.h>

#define JANET_PEG_FUZZ_MAX_INPUT (1U << 20)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > JANET_PEG_FUZZ_MAX_INPUT)
        return 0;

    /* Byte 0 selects the split point for a Janet PEG pattern and subject. */
    size_t patlen = (size_t)data[0] % (size - 1);
    const uint8_t *patsrc = data + 1;
    const uint8_t *text = data + 1 + patlen;
    size_t textlen = size - 1 - patlen;

    janet_init();

    JanetParser parser;
    janet_parser_init(&parser);
    Janet matchfn = janet_resolve_core("peg/match");
    if (janet_checktype(matchfn, JANET_CFUNCTION)) {
        JanetCFunction cfun = janet_unwrap_cfunction(matchfn);

        /* Stop after parser errors: consuming again would panic. */
        for (size_t i = 0; i < patlen; i++) {
            janet_parser_consume(&parser, patsrc[i]);
            if (janet_parser_status(&parser) == JANET_PARSE_ERROR)
                break;
        }
        if (janet_parser_status(&parser) != JANET_PARSE_ERROR)
            janet_parser_eof(&parser);

        if (janet_parser_status(&parser) != JANET_PARSE_ERROR &&
                janet_parser_has_more(&parser)) {
            Janet pattern = janet_parser_produce(&parser);
            JanetTryState tstate;
            if (janet_try(&tstate) == JANET_SIGNAL_OK) {
                Janet argv[2];
                argv[0] = pattern;
                argv[1] = janet_stringv(text, (int32_t)textlen);
                cfun(2, argv);
            }
            janet_restore(&tstate);
        }
    }

    janet_parser_deinit(&parser);
    janet_deinit();
    return 0;
}

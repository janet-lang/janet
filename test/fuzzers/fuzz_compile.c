#include <stdint.h>
#include <janet.h>

/* Disable leak sanitizer */
int __lsan_is_turned_off(void) {
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    /* init Janet */
    janet_init();

    /* fuzz the compiler in src/core/compile.c: parse the untrusted source and
       compile each produced form to bytecode */
    JanetTable *env = janet_core_env(NULL);
    JanetParser parser;
    janet_parser_init(&parser);

    JanetTryState tstate;
    if (janet_try(&tstate) == JANET_SIGNAL_OK) {
        for (size_t i = 0; i < size; i++) {
            if (janet_parser_status(&parser) == JANET_PARSE_ERROR)
                break;
            janet_parser_consume(&parser, data[i]);
            while (janet_parser_has_more(&parser)) {
                Janet form = janet_parser_produce(&parser);
                JanetCompileResult res = janet_compile(form, env, janet_cstring("fuzz"));
                (void) res;
            }
        }
    }
    janet_restore(&tstate);

    janet_parser_deinit(&parser);

    /* cleanup Janet */
    janet_deinit();

    return 0;
}

#include <stdint.h>
#include <janet.h>

/* Disable leak sanitizer */
int __lsan_is_turned_off(void) {
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    /* init Janet */
    janet_init();

    /* fuzz the binary unmarshal (deserialization) path in src/core/marsh.c */
    JanetTable *reg = janet_env_lookup(janet_core_env(NULL));
    JanetTryState tstate;
    if (janet_try(&tstate) == JANET_SIGNAL_OK) {
        const uint8_t *next = NULL;
        janet_unmarshal(data, size, 0, reg, &next);
    }
    janet_restore(&tstate);

    /* cleanup Janet */
    janet_deinit();

    return 0;
}

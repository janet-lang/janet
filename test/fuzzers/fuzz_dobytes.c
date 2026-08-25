#include <stdint.h>
#include <janet.h>

/* Disable leak sanitizer */
int __lsan_is_turned_off(void) {
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    /* init Janet */
    janet_init();

    /* fuzz the compile + bytecode VM path (parse, compile, run) in src/core/compile.c and src/core/vm.c */
    JanetTable *env = janet_core_env(NULL);
    JanetTryState tstate;
    if (janet_try(&tstate) == JANET_SIGNAL_OK) {
        Janet out;
        janet_dobytes(env, data, (int32_t) size, "<fuzz>", &out);
    }
    janet_restore(&tstate);

    /* cleanup Janet */
    janet_deinit();

    return 0;
}

#include <stdint.h>
#include <stdio.h>
#include <janet.h>

#define MAX_FUZZ_INPUT 4096
#define MAX_FIBER_STEPS 64

static Janet cfun_fuzz_fiber(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);

    JanetFunction *function = janet_getfunction(argv, 0);
    int32_t step_budget = janet_getinteger(argv, 1);
    int32_t control = janet_getinteger(argv, 2);
    int32_t step_mode = control & 1;
    if (step_budget < 1) step_budget = 1;
    if (step_budget > MAX_FIBER_STEPS) step_budget = MAX_FIBER_STEPS;

    JanetFiber *fiber = NULL;
    Janet output = janet_wrap_nil();
    JanetSignal signal = JANET_SIGNAL_OK;
    int rooted = 0;
    int32_t runs = 1 + ((control >> 1) & 3);

    for (int32_t run = 0; run < runs; run++) {
        signal = janet_pcall(function, 0, NULL, &output, &fiber);
        if (fiber == NULL) break;

        if (!rooted) {
            janet_gcroot(janet_wrap_fiber(fiber));
            rooted = 1;
        }

        for (int32_t step = 0; step < step_budget; step++) {
            JanetFiberStatus status = janet_fiber_status(fiber);
            if (status == JANET_STATUS_DEAD ||
                    status == JANET_STATUS_ERROR ||
                    status == JANET_STATUS_ALIVE ||
                    (status != JANET_STATUS_PENDING &&
                            status != JANET_STATUS_DEBUG)) {
                break;
            }

            Janet input = janet_wrap_integer(step);
            if (step_mode & 1) {
                signal = janet_step(fiber, input, &output);
            } else {
                signal = janet_continue(fiber, input, &output);
            }
        }
    }

    if (rooted) janet_gcunroot(janet_wrap_fiber(fiber));
    return janet_wrap_integer(signal);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > MAX_FUZZ_INPUT) return 0;

    char source[1024];
    size_t source_length = 0;
    int written = snprintf(source,
                           sizeof(source),
                           "(fuzz-fiber (fn [] (var value 0)");
    if (written < 0 || (size_t)written >= sizeof(source)) return 0;
    source_length = (size_t)written;

    size_t yield_count = 1 + (data[0] & 3);
    for (size_t i = 0; i < yield_count; i++) {
        unsigned int yielded = data[1 + (i % (size - 1))];
        written = snprintf(source + source_length,
                           sizeof(source) - source_length,
                           "(set value (+ value (yield %u)))",
                           yielded);
        if (written < 0 || (size_t)written >= sizeof(source) - source_length)
            return 0;
        source_length += (size_t)written;
    }

    int32_t step_budget = 8 + ((data[0] >> 2) & 31);
        int32_t control = ((data[0] >> 7) & 1) |
            (((data[0] >> 4) & 3) << 1);
    written = snprintf(source + source_length,
                       sizeof(source) - source_length,
                       "value) %d %d)",
                       step_budget,
                       control);
    if (written < 0 || (size_t)written >= sizeof(source) - source_length)
        return 0;
    source_length += (size_t)written;

    janet_init();
    JanetTable *env = janet_core_env(NULL);
    janet_def(env,
              "fuzz-fiber",
              janet_wrap_cfunction(cfun_fuzz_fiber),
              "Exercise bounded native fiber control operations.");

    JanetTryState try_state;
    if (janet_try(&try_state) == JANET_SIGNAL_OK) {
        Janet output;
        janet_dobytes(env,
                      (const uint8_t *)source,
                      (int32_t)source_length,
                      "<fuzz-fiber>",
                      &output);
    }
    janet_restore(&try_state);
    janet_deinit();

    return 0;
}
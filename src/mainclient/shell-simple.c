#include <janet.h>

int main(int argc, char **argv) {
    int status = 0;
    JanetArray *args;
    JanetTable *env;

    janet_init();
    env = janet_core_env(NULL);

    // One of several ways to begin the Janet vm.
    janet_dostring(env, "(print `hello, world!`)", "main", NULL);

    janet_deinit();
    return status;
}


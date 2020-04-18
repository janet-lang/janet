#include <stdint.h>
#include <string.h>
#include <janet.h>


char *cmd_start = "(def v (unmarshal (slurp ((dyn :";
char *cmd_end = ") 1)) load-image-dict));

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *new_str = (char *)malloc(size + strlen(cmd_start) + strlen(cmd_end) + 1);
    if (new_str == NULL) {
        return 0;
    }
    // Copy start of command into the string we will execute
    memcpy(new_str, cmd_start, strlen(cmd_start));

    // Copy fuzz-data into the exec string
    char *datap = new_str + strlen(cmd_start);
    memcpy(datap, data, size);

    // Copy the end of the command into the exec string
    end_data = datap + size;
    memcpy(end_data, cmd_end, strlen(cmd_end));

    char *null_terminator = end_data + strlen(cmd_end) + 1;
    *null_terminator = '\0';

    // Remove any parentheses from the fuzz data
    for (int i = 0; i < size; i++)
    {
        if (datap[i] == '(')
            datap[i] = (char)(datap[i] + 1);
        if (datap[i] == ')')
            datap[i] = (char)(datap[i] + 1);
    }

    // janet logic
    janet_init();
    JanetTable *env = janet_core_env(NULL);
    janet_dostring(env, new_str, "main", NULL);
    janet_deinit();

    free(new_str);
    return 0;
}


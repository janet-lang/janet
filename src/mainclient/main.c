/*
* Copyright (c) 2019 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include <janet.h>
#include "line.h"

extern const unsigned char *janet_gen_init;
extern int32_t janet_gen_init_size;

int main(int argc, char **argv) {
    int i, status;
    JanetArray *args;
    JanetTable *env;

    /* Set up VM */
    janet_init();

    /* Replace original getline with new line getter */
    JanetTable *replacements = janet_table(0);
    janet_table_put(replacements, janet_csymbolv("getline"), janet_wrap_cfunction(janet_line_getter));
    janet_line_init();

    /* Get core env */
    env = janet_core_env(replacements);

    /* Create args tuple */
    args = janet_array(argc);
    for (i = 0; i < argc; i++)
        janet_array_push(args, janet_cstringv(argv[i]));
    janet_def(env, "process/args", janet_wrap_array(args), "Command line arguments.");

    /* Run startup script */
    status = janet_dobytes(env, janet_gen_init, janet_gen_init_size, "init.janet", NULL);

    /* Deinitialize vm */
    janet_deinit();
    janet_line_deinit();

    return status;
}

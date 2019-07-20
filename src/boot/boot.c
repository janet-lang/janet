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
#include "tests.h"

extern const unsigned char *janet_gen_boot;
extern int32_t janet_gen_boot_size;

int main(int argc, const char **argv) {

    /* Init janet */
    janet_init();

    /* Run tests */
    array_test();
    buffer_test();
    number_test();
    system_test();
    table_test();

    /* C tests passed */

    /* Set up VM */
    int status;
    JanetTable *env;

    env = janet_core_env(NULL);

    /* Create args tuple */
    JanetArray *args = janet_array(argc);
    for (int i = 0; i < argc; i++)
        janet_array_push(args, janet_cstringv(argv[i]));
    janet_def(env, "boot/args", janet_wrap_array(args), "Command line arguments.");

    /* Add in options from janetconf.h so boot.janet can configure the image as needed. */
    JanetTable *opts = janet_table(0);
#ifdef JANET_NO_DOCSTRINGS
    janet_table_put(opts, janet_ckeywordv("no-docstrings"), janet_wrap_true());
#endif
#ifdef JANET_NO_SOURCEMAPS
    janet_table_put(opts, janet_ckeywordv("no-sourcemaps"), janet_wrap_true());
#endif
    janet_def(env, "boot/config", janet_wrap_table(opts), "Boot options");

    /* Run bootstrap script to generate core image */
    const char *boot_file;
#ifdef JANET_NO_SOURCEMAPS
    boot_file = NULL;
#else
    boot_file = "boot.janet";
#endif
    status = janet_dobytes(env, janet_gen_boot, janet_gen_boot_size, boot_file, NULL);

    /* Deinitialize vm */
    janet_deinit();

    return status;
}

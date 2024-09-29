/*
* Copyright (c) 2024 Calvin Rose
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

#ifdef JANET_WINDOWS
#include <direct.h>
#define chdir(x) _chdir(x)
#else
#include <unistd.h>
#endif

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
    const char *boot_filename;
#ifdef JANET_NO_SOURCEMAPS
    boot_filename = NULL;
#else
    boot_filename = "boot.janet";
#endif

    int chdir_status = chdir(argv[1]);
    if (chdir_status) {
        fprintf(stderr, "Could not change to directory %s\n", argv[1]);
        exit(1);
    }

    FILE *boot_file = fopen("src/boot/boot.janet", "rb");
    if (NULL == boot_file) {
        fprintf(stderr, "Could not open src/boot/boot.janet\n");
        exit(1);
    }

    /* Slurp file into buffer */
    fseek(boot_file, 0, SEEK_END);
    size_t boot_size = ftell(boot_file);
    fseek(boot_file, 0, SEEK_SET);
    unsigned char *boot_buffer = janet_malloc(boot_size);
    if (NULL == boot_buffer) {
        fprintf(stderr, "Failed to allocate boot buffer\n");
        exit(1);
    }
    if (!fread(boot_buffer, 1, boot_size, boot_file)) {
        fprintf(stderr, "Failed to read into boot buffer\n");
        exit(1);
    }
    fclose(boot_file);

    status = janet_dobytes(env, boot_buffer, (int32_t) boot_size, boot_filename, NULL);
    janet_free(boot_buffer);

    /* Deinitialize vm */
    janet_deinit();

    return status;
}

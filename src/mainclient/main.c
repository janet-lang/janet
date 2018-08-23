/*
* Copyright (c) 2018 Calvin Rose
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

#include <dst/dst.h>

#include <generated/init.h>
#include "line.h"

int main(int argc, char **argv) {
    int i, status;
    DstArray *args;
    DstTable *env;

    /* Set up VM */
    dst_init();
    env = dst_core_env();

    /* Create args tuple */
    args = dst_array(argc);
    for (i = 0; i < argc; i++)
        dst_array_push(args, dst_cstringv(argv[i]));
    dst_env_def(env, "process.args", dst_wrap_array(args));

    /* Expose line getter */
    dst_env_def(env, "getline", dst_wrap_cfunction(dst_line_getter));
    dst_register("getline", dst_wrap_cfunction(dst_line_getter));
    dst_line_init();

    /* Run startup script */
    status = dst_dobytes(env, dst_gen_init, sizeof(dst_gen_init), "init.dst");

    /* Deinitialize vm */
    dst_deinit();
    dst_line_deinit();

    return status;
}

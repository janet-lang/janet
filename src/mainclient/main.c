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

#ifndef JANET_AMALG
#include <janet.h>
#include "line.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

int main(int argc, char **argv) {
    int i, status;
    JanetArray *args;
    JanetTable *env;

#ifdef _WIN32
    /* Enable color console on windows 10 console and utf8 output. */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    SetConsoleOutputCP(65001);
#endif

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
    for (i = 1; i < argc; i++)
        janet_array_push(args, janet_cstringv(argv[i]));

    /* Save current executable path to (dyn :executable) */
    janet_table_put(env, janet_ckeywordv("executable"), janet_cstringv(argv[0]));

    /* Run startup script */
    Janet mainfun, out;
    janet_resolve(env, janet_csymbol("cli-main"), &mainfun);
    Janet mainargs[1] = { janet_wrap_array(args) };
    JanetFiber *fiber = janet_fiber(janet_unwrap_function(mainfun), 64, 1, mainargs);
    fiber->env = env;
    status = janet_continue(fiber, janet_wrap_nil(), &out);
    if (status != JANET_SIGNAL_OK) {
        janet_stacktrace(fiber, out);
    }

    /* Deinitialize vm */
    janet_deinit();
    janet_line_deinit();

    return status;
}

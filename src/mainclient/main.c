/*
* Copyright (c) 2017 Calvin Rose
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

#include <stdlib.h>
#include <stdio.h>
#include <dst/dst.h>
#include <dst/dstcompile.h>

#define DST_CLIENT_HELP 1
#define DST_CLIENT_VERBOSE 2
#define DST_CLIENT_VERSION 4
#define DST_CLIENT_REPL 8
#define DST_CLIENT_UNKNOWN 16

static const char *replsplash =
"Dst " DST_VERSION "  Copyright (C) 2017-2018 Calvin Rose";

static int client_strequal(const char *a, const char *b) {
    while (*a) if (*a++ != *b++) return 0;
    return *a == *b;
}

static int client_strequal_witharg(const char *a, const char *b) {
    while (*b) if (*a++ != *b++) return 0;
    return *a == '=';
}

int main(int argc, char **argv) {
    int status = 0;
    int i;
    int fileRead = 0;
    uint32_t gcinterval = 0x10000;
    uint64_t flags = 0;
    DstTable *env;

    /* Read the arguments. Ignore files. */
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (*arg == '-') {
            /* Flag or option */
            if (arg[1] == '-') {
                /* Option */
                if (client_strequal(arg + 2, "help")) {
                    flags |= DST_CLIENT_HELP;
                } else if (client_strequal(arg + 2, "version")) {
                    flags |= DST_CLIENT_VERSION;
                } else if (client_strequal(arg + 2, "verbose")) {
                    flags |= DST_CLIENT_VERBOSE;
                } else if (client_strequal(arg + 2, "repl")) {
                    flags |= DST_CLIENT_REPL;
                } else if (client_strequal_witharg(arg + 2, "gcinterval")) {
                    int status = 0;
                    int32_t m;
                    const uint8_t *start = (const uint8_t *)(arg + 13);
                    const uint8_t *end = start;
                    while (*end) ++end;
                    m = dst_scan_integer(start, end - start, &status);
                    if (!status)
                        gcinterval = m;
                } else {
                    flags |= DST_CLIENT_UNKNOWN;
                }
            } else {
                /* Flag */
                const char *c = arg;
                while (*(++c)) {
                   switch (*c) {
                        case 'h':
                            flags |= DST_CLIENT_HELP;
                            break;
                        case 'v':
                            flags |= DST_CLIENT_VERSION;
                            break;
                        case 'V':
                            flags |= DST_CLIENT_VERBOSE;
                            break;
                        case 'r':
                            flags |= DST_CLIENT_REPL;
                            break;
                        default:
                            flags |= DST_CLIENT_UNKNOWN;
                            break;
                   }
                }
            }
        }
    }

    /* Handle flags and options */
    if ((flags & DST_CLIENT_HELP) || (flags & DST_CLIENT_UNKNOWN)) {
        printf( "Usage:\n"
                "%s -opts --fullopt1 --fullopt2 file1 file2...\n"
                "\n"
                "  -h      --help              Shows this information.\n"
                "  -V      --verbose           Show more output.\n"
                "  -r      --repl              Launch a repl after all files are processed.\n"
                "  -v      --version           Print the version number and exit.\n"
                "          --gcinterval=[int]  Set the amount of memory to allocate before\n"
                "                              forcing a collection in bytes. Max is 2^31-1,\n"
                "                              min is 0.\n\n",
                argv[0]);
        return 0;
    }
    if (flags & DST_CLIENT_VERSION) {
        printf("%s\n", DST_VERSION);
        return 0;
    }

    /* Set up VM */
    dst_init();
    dst_vm_gc_interval = gcinterval;
    env = dst_stl_env();
    dst_gcroot(dst_wrap_table(env));

    /* Read the arguments. Only process files. */
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (*arg != '-') {
            DstContext ctxt;
            fileRead = 1;
            if (dst_context_file(&ctxt, env, arg)) {
                printf("file %s not found\n", arg);
                continue;
            }
            status = dst_context_run(&ctxt, DST_PARSEFLAG_SOURCEMAP);
        }
    }

    /* Run a repl if nothing else happened, or the flag is set */
    if (!fileRead || (flags & DST_CLIENT_REPL)) {
        DstContext ctxt;
        dst_context_repl(&ctxt, env);
        puts(replsplash);
        status = dst_context_run(&ctxt, DST_PARSEFLAG_SOURCEMAP);
    }

    dst_deinit();

    return status;
}

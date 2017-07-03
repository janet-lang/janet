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
#include <gst/gst.h>

static int client_strequal(const char *a, const char *b) {
    while (*a)
        if (*a++ != *b++) return 0;
    return *a == *b;
}

static int client_strequal_witharg(const char *a, const char *b) {
    while (*b)
        if (*a++ != *b++) return 0;
    return *a == '=';
}

#define GST_CLIENT_HELP 1
#define GST_CLIENT_VERBOSE 2
#define GST_CLIENT_VERSION 4
#define GST_CLIENT_REPL 8
#define GST_CLIENT_NOCOLOR 16
#define GST_CLIENT_UNKNOWN 32

static void printf_flags(int64_t flags, const char *col, const char *fmt, const char *arg) {
    if (!(flags & GST_CLIENT_NOCOLOR))
        printf("\x1B[%sm", col);
    printf(fmt, arg);
    if (!(flags & GST_CLIENT_NOCOLOR))
        printf("\x1B[0m");
}

/* Simple read line functionality */
static char *gst_getline() {
    char *line = malloc(100);
    char *linep = line;
    size_t lenmax = 100;
    size_t len = lenmax;
    int c;
    if (line == NULL)
        return NULL;
    for (;;) {
        c = fgetc(stdin);
        if (c == EOF)
            break;
        if (--len == 0) {
            len = lenmax;
            char *linen = realloc(linep, lenmax *= 2);
            if (linen == NULL) {
                free(linep);
                return NULL;
            }
            line = linen + (line - linep);
            linep = linen;
        }
        if ((*line++ = c) == '\n')
            break;
    }
    *line = '\0';
    return linep;
}

/* Compile and run an ast */
static int debug_compile_and_run(Gst *vm, GstValue ast, GstValue last, int64_t flags) {
    GstCompiler c;
    GstValue func;
    /* Try to compile generated AST */
    gst_compiler(&c, vm);
    gst_env_putc(vm, vm->env, "_", last);
    func = gst_wrap_function(gst_compiler_compile(&c, ast));
    /* Check for compilation errors */
    if (c.error.type != GST_NIL) {
        printf_flags(flags, "31", "compiler error: %s\n", (const char *)gst_to_string(vm, c.error));
        return 1;
    }
    /* Execute function */
    if (gst_run(vm, func)) {
        if (vm->crash) {
            printf_flags(flags, "31", "vm crash: %s\n", vm->crash);
        } else {
            printf_flags(flags, "31", "vm error: %s\n", (const char *)gst_to_string(vm, vm->ret));
        }
        return 1;
    }
    return 0;
}

/* Parse a file and execute it */
static int debug_run(Gst *vm, FILE *in, int64_t flags) {
    char buffer[2048] = {0};
    const char *reader = buffer;
    GstParser p;
    for (;;) {
        /* Init parser */
        gst_parser(&p, vm);
        while (p.status != GST_PARSER_ERROR && p.status != GST_PARSER_FULL) {
            if (*reader == '\0') {
                if (!fgets(buffer, sizeof(buffer), in)) {
                    /* Check that parser is complete */
                    if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
                        printf_flags(flags, "31", "parse error: unexpected end of source%s\n", "");
                        return 1;
                    }
                    /* Otherwise we finished the file with no problems*/
                    return 0;
                }
                reader = buffer;
            }
            reader += gst_parse_cstring(&p, reader);
        }
        /* Check if file read in correctly */
        if (p.error) {
            printf_flags(flags, "31", "parse error: %s\n", p.error);
            break;
        }
        /* Check that parser is complete */
        if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
            printf_flags(flags, "31", "parse error: unexpected end of source%s\n", "");
            break;
        }
        if (debug_compile_and_run(vm, gst_parse_consume(&p), vm->ret, flags)) {
            break;
        }
    }
    return 1;
}

/* A simple repl */
static int debug_repl(Gst *vm, uint64_t flags) {
    char *buffer, *reader;
    GstParser p;
    buffer = reader = NULL;
    for (;;) {
        /* Init parser */
        gst_parser(&p, vm);
        while (p.status != GST_PARSER_ERROR && p.status != GST_PARSER_FULL) {
            if (p.status == GST_PARSER_ERROR || p.status == GST_PARSER_FULL)
                break;
            if (!reader || *reader == '\0') {
                printf_flags(flags, "33", "> %s", "");
                if (buffer)
                    free(buffer);
                buffer = gst_getline();
                if (!buffer || *buffer == '\0')
                    return 0;
                reader = buffer;
            }
            reader += gst_parse_cstring(&p, reader);
        }
        /* Check if file read in correctly */
        if (p.error) {
            printf_flags(flags, "31", "parse error: %s\n", p.error);
            buffer = reader = NULL;
            continue;
        }
        /* Check that parser is complete */
        if (p.status != GST_PARSER_FULL && p.status != GST_PARSER_ROOT) {
            printf_flags(flags, "31", "parse error: unexpected end of source%s\n", "");
            continue;
        }
        if (!debug_compile_and_run(vm, gst_parse_consume(&p), vm->ret, flags)) {
            printf_flags(flags, "36", "%s\n", (const char *) gst_description(vm, vm->ret));
        }
    }
}

int main(int argc, const char **argv) {
    Gst vm;
    int status = -1;
    int i;
    int fileRead = 0;
    uint32_t memoryInterval = 4096;
    uint64_t flags = 0;

    /* Read the arguments. Ignore files. */
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (*arg == '-') {
            /* Flag or option */
            if (arg[1] == '-') {
                /* Option */
                if (client_strequal(arg + 2, "help")) {
                    flags |= GST_CLIENT_HELP;
                } else if (client_strequal(arg + 2, "version")) {
                    flags |= GST_CLIENT_VERSION;
                } else if (client_strequal(arg + 2, "verbose")) {
                    flags |= GST_CLIENT_VERBOSE;
                } else if (client_strequal(arg + 2, "repl")) {
                    flags |= GST_CLIENT_REPL;
                } else if (client_strequal(arg + 2, "nocolor")) {
                    flags |= GST_CLIENT_NOCOLOR;
                } else if (client_strequal_witharg(arg + 2, "memchunk")) {
                    int64_t val = memoryInterval;
                    const uint8_t *end = (const uint8_t *)(arg + 2);
                    while (*end) ++end;
                    int status = gst_read_integer((const uint8_t *)arg + 11, end, &val);
                    if (status) {
                        if (val > 0xFFFFFFFF) {
                            memoryInterval = 0xFFFFFFFF;
                        } else if (val < 0) {
                            memoryInterval = 0;
                        } else {
                            memoryInterval = val;
                        }
                    }
                } else {
                    flags |= GST_CLIENT_UNKNOWN;
                }
            } else {
                /* Flag */
                const char *c = arg;
                while (*(++c)) {
                   switch (*c) {
                        case 'h':
                            flags |= GST_CLIENT_HELP;
                            break;
                        case 'v':
                            flags |= GST_CLIENT_VERSION;
                            break;
                        case 'V':
                            flags |= GST_CLIENT_VERBOSE;
                            break;
                        case 'r':
                            flags |= GST_CLIENT_REPL;
                            break;
                        case 'c':
                            flags |= GST_CLIENT_NOCOLOR;
                            break;
                        default:
                            flags |= GST_CLIENT_UNKNOWN;
                            break;
                   }
                }
            }
        }
    }

    /* Handle flags and options */
    if ((flags & GST_CLIENT_HELP) || (flags & GST_CLIENT_UNKNOWN)) {
        printf( "Usage:\n"
                "%s -opts --fullopt1 --fullopt2 file1 file2...\n"
                "\n"
                "  -h      --help           : Shows this information.\n"
                "  -V      --verbose        : Show more output.\n"
                "  -r      --repl           : Launch a repl after all files are processed.\n"
                "  -c      --nocolor        : Don't use VT100 color codes in the repl.\n"
                "  -v      --version        : Print the version number and exit.\n"
                "          --memchunk=[int] : Set the amount of memory to allocate before\n"
                "                             forcing a collection in bytes. Max is 2^32-1,\n"
                "                             min is 0.\n\n",
                argv[0]);
        return 0;
    }
    if (flags & GST_CLIENT_VERSION) {
        printf("%s\n", GST_VERSION);
        return 0;
    }

    /* Set up VM */
    gst_init(&vm);
    vm.memoryInterval = memoryInterval;
    gst_stl_load(&vm);

    /* Read the arguments. Only process files. */
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (*arg != '-') {
            FILE *f;
            f = fopen(arg, "rb");
            fileRead = 1;
            status = debug_run(&vm, f, flags);
        }
    }

    if (!fileRead || (flags & GST_CLIENT_REPL)) {
        status = debug_repl(&vm, flags);
    }

    gst_deinit(&vm);

    return status;
}

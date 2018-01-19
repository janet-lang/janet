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

#include <dst/dst.h>
#include <dst/dstcompile.h>
#include <dst/dstparse.h>

#define CHUNKSIZE 1024

/* Read input for a repl */
static int replread(DstContext *c) {
    if (c->buffer.count == 0)
        printf("> ");
    else
        printf(">> ");
    for (;;) {
        int x = fgetc(stdin);
        if (x == EOF) {
            dst_buffer_push_u8(&c->buffer, '\n');
            printf("\n");
            break;
        }
        dst_buffer_push_u8(&c->buffer, x);
        if (x == '\n') break;
    }
    return 0;
}

/* Output for a repl */
static void replonvalue(DstContext *c, Dst value) {
    (void) c;
    dst_puts(dst_formatc("%v\n", value)); 
    dst_env_def(c->env, "_", value);
}

/* Handle errors on repl */
static void simpleerror(DstContext *c, DstContextErrorType type, Dst err, size_t start, size_t end) {
    const char *errtype;
    (void) c;
    (void) start;
    (void) end;
    switch (type) {
        case DST_CONTEXT_ERROR_PARSE:
            errtype = "parse";
            break;
        case DST_CONTEXT_ERROR_RUNTIME:
            errtype = "runtime";
            break;
        case DST_CONTEXT_ERROR_COMPILE:
            errtype = "compile";
            break;
    }
    dst_puts(dst_formatc("%s error: %s\n", errtype, dst_to_string(err)));
}

static void filedeinit(DstContext *c) {
    fclose((FILE *) (c->user));
}

static int fileread(DstContext *c) {
    size_t nread;
    FILE *f = (FILE *) c->user;
    dst_buffer_ensure(&c->buffer, CHUNKSIZE);
    nread = fread(c->buffer.data, 1, CHUNKSIZE, f);
    if (nread != CHUNKSIZE && ferror(f)) {
        return -1;
    }
    c->buffer.count = (int32_t) nread;
    return 0;
}

void dst_context_init(DstContext *c, DstTable *env) {
    dst_buffer_init(&c->buffer, CHUNKSIZE);
    c->env = env;
    dst_gcroot(dst_wrap_table(env));
    c->index = 0;
    c->read_chunk = NULL;
    c->on_error = NULL;
    c->on_value = NULL;
    c->deinit = NULL;
}

void dst_context_deinit(DstContext *c) {
    dst_buffer_deinit(&c->buffer);
    if (c->deinit) c->deinit(c);
    dst_gcunroot(dst_wrap_table(c->env));
}

int dst_context_repl(DstContext *c, DstTable *env) {
    dst_context_init(c, env);
    c->user = NULL;
    dst_env_def(c->env, "_", dst_wrap_nil());
    c->read_chunk = replread;
    c->on_error = simpleerror;
    c->on_value = replonvalue;
    return 0;
}

int dst_context_file(DstContext *c, DstTable *env, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    dst_context_init(c, env);
    c->user = f;
    c->read_chunk = fileread;
    c->on_error = simpleerror;
    c->deinit = filedeinit;
    return 0;
}

/* Do something on an error. Return flags to or with current flags. */
static int doerror(
        DstContext *c,
        DstContextErrorType type,
        Dst err,
        int32_t bstart,
        int32_t bend) {
    if (c->on_error) {
        c->on_error(c, type, 
                err,
                bstart,
                bend);
    }
    return 1 << type;
}

/* Start a context */
int dst_context_run(DstContext *c, int flags) {
    int done = 0;
    int errflags = 0;
    DstParser parser;
    dst_parser_init(&parser, flags);
    while (!done) {
        int bufferdone = 0;
        while (!bufferdone) {
            DstParserStatus status = dst_parser_status(&parser);
            switch (status) {
                case DST_PARSE_FULL:
                    {
                        DstCompileResult cres = dst_compile(dst_parser_produce(&parser), c->env, flags);
                        if (cres.status == DST_COMPILE_OK) {
                            DstFunction *f = dst_function(cres.funcdef, NULL);
                            Dst ret;
                            if (dst_run(dst_wrap_function(f), &ret)) {
                                /* Get location from stacktrace? */
                                errflags |= doerror(c, DST_CONTEXT_ERROR_RUNTIME, ret, -1, -1);
                            } else {
                                if (c->on_value) {
                                    c->on_value(c, ret);
                                }
                            }
                        } else {
                            errflags |= doerror(c, DST_CONTEXT_ERROR_COMPILE,
                                    dst_wrap_string(cres.error),
                                    cres.error_start,
                                    cres.error_end);
                        }
                    }
                    break;
                case DST_PARSE_ERROR:
                    doerror(c, DST_CONTEXT_ERROR_PARSE, 
                            dst_cstringv(dst_parser_error(&parser)),
                            parser.index,
                            parser.index);
                    break;
                case DST_PARSE_PENDING:
                case DST_PARSE_ROOT:
                    if (c->index >= c->buffer.count) {
                        bufferdone = 1;
                        break;
                    }
                    dst_parser_consume(&parser, c->buffer.data[c->index++]);
                    break;
            }
        }
        /* Refill the buffer */
        c->buffer.count = 0;
        c->index = 0;
        if (c->read_chunk(c) || c->buffer.count == 0) {
            done = 1;
        }
    }

    dst_parser_deinit(&parser);
    return errflags;
}

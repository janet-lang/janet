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

/*static void deinit_file(DstContext *c) {*/
    /*FILE *f = (FILE *) (c->user);*/
    /*fclose(f);*/
/*}*/

/* Read input for a repl */
static void replread(DstContext *c) {
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
}

/* Output for a repl */
static void replonvalue(DstContext *c, Dst value) {
    (void) c;
    dst_puts(dst_formatc("%v\n", value)); 
    if (dst_checktype(c->env, DST_TABLE))
        dst_module_def(dst_unwrap_table(c->env), "_", dst_wrap_nil());
}

/* Handle errors on repl */
static void replerror(DstContext *c, DstContextErrorType type, Dst err, size_t start, size_t end) {
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
    dst_puts(dst_formatc("%s error: %v\n", errtype, err));
}

void dst_context_init(DstContext *c, Dst env) {
    dst_buffer_init(&c->buffer, 1024);
    c->env = env;
    dst_gcroot(env);
    c->flushed_bytes = 0;
}

void dst_context_deinit(DstContext *c) {
    dst_buffer_deinit(&c->buffer);
    if (c->deinit) c->deinit(c);
    dst_gcunroot(c->env);
}

void dst_context_repl(DstContext *c, Dst env) {
    dst_buffer_init(&c->buffer, 1024);
    c->env = env;
    dst_gcroot(env);
    c->flushed_bytes = 0;
    c->user = NULL;
    if (dst_checktype(c->env, DST_TABLE))
        dst_module_def(dst_unwrap_table(c->env), "_", dst_wrap_nil());
    c->read_chunk = replread;
    c->on_error = replerror;
    c->on_value = replonvalue;
}

/* Remove everything in the current buffer */
static void flushcontext(DstContext *c) {
    c->flushed_bytes += c->buffer.count;
    c->buffer.count = 0;
}

/* Shift bytes in buffer down */
/* TODO Make parser online so there is no need to
 * do too much book keeping with the buffer. */
static void bshift(DstContext *c, int32_t delta) {
    c->buffer.count -= delta;
    if (delta) {
        memmove(c->buffer.data, c->buffer.data + delta, c->buffer.count);
    }
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
                c->flushed_bytes + bstart,
                c->flushed_bytes + bend);
    }
    return 1 << type;
}

/* Start a context */
int dst_context_run(DstContext *c) {
    int done = 0;
    int flags = 0;
    while (!done) {
        DstCompileResult cres;
        DstCompileOptions opts;
        DstParseResult res = dst_parse(c->buffer.data, c->buffer.count);
        switch (res.status) {
            case DST_PARSE_NODATA:
                flushcontext(c);
            case DST_PARSE_UNEXPECTED_EOS:
                {
                    int32_t countbefore = c->buffer.count;
                    c->read_chunk(c);
                    /* If the last chunk was empty, finish */
                    if (c->buffer.count == countbefore) {
                        done = 1;
                        flags |= doerror(c, DST_CONTEXT_ERROR_PARSE, 
                                dst_cstringv("unexpected end of source"),
                                res.bytes_read,
                                res.bytes_read);
                    }
                    break;
                }
            case DST_PARSE_ERROR:
                flags |= doerror(c, DST_CONTEXT_ERROR_PARSE, 
                        dst_wrap_string(res.error),
                        res.bytes_read,
                        res.bytes_read);
                bshift(c, res.bytes_read);
                break;
            case DST_PARSE_OK:
                {
                    opts.source = res.value;
                    opts.flags = 0;
                    opts.env = c->env;
                    cres = dst_compile(opts);
                    if (cres.status == DST_COMPILE_OK) {
                        DstFunction *f = dst_compile_func(cres);
                        Dst ret;
                        if (dst_run(dst_wrap_function(f), &ret)) {
                            /* Get location from stacktrace? */
                            flags |= doerror(c, DST_CONTEXT_ERROR_RUNTIME, ret, -1, -1);
                        } else {
                            if (c->on_value) {
                                c->on_value(c, ret);
                            }
                        }
                    } else {
                        flags |= doerror(c, DST_CONTEXT_ERROR_COMPILE,
                                dst_wrap_string(cres.error),
                                cres.error_start,
                                cres.error_end);
                    }
                    bshift(c, res.bytes_read);
                }
                break;
        }
    }

    return flags;
}

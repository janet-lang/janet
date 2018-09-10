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

#include <janet/janet.h>
#include "state.h"

/* Error reporting */
void janet_stacktrace(JanetFiber *fiber, const char *errtype, Janet err) {
    const char *errstr = (const char *)janet_to_string(err);
    printf("%s error: %s\n", errtype, errstr);
    if (!fiber) return;
    int32_t i = fiber->frame;
    while (i > 0) {
        JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
        JanetFuncDef *def = NULL;
        i = frame->prevframe;
        
        printf("  in");

        if (frame->func) {
            def = frame->func->def;
            printf(" %s", def->name ? (const char *)def->name : "<anonymous>");
            if (def->source) {
                printf(" [%s]", (const char *)def->source);
            }
        } else {
            JanetCFunction cfun = (JanetCFunction)(frame->pc);
            if (cfun) {
                Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
                if (!janet_checktype(name, JANET_NIL))
                    printf(" %s", (const char *)janet_to_string(name));
            }
        }
        if (frame->flags & JANET_STACKFRAME_TAILCALL)
            printf(" (tailcall)");
        if (frame->func && frame->pc) {
            int32_t off = (int32_t) (frame->pc - def->bytecode);
            if (def->sourcemap) {
                JanetSourceMapping mapping = def->sourcemap[off];
                printf(" on line %d, column %d", mapping.line, mapping.column);
            } else {
                printf(" pc=%d", off);
            }
        }
        printf("\n");
    }
}

/* Run a string */
int janet_dobytes(JanetTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath) {
    JanetParser parser;
    int errflags = 0;
    int32_t index = 0;
    int dudeol = 0;
    int done = 0;
    const uint8_t *where = sourcePath ? janet_cstring(sourcePath) : NULL;
    if (where) janet_gcroot(janet_wrap_string(where));
    janet_parser_init(&parser);

    while (!errflags && !done) {
        switch (janet_parser_status(&parser)) {
            case JANET_PARSE_FULL:
                {
                    Janet form = janet_parser_produce(&parser);
                    JanetCompileResult cres = janet_compile(form, env, where);
                    if (cres.status == JANET_COMPILE_OK) {
                        JanetFunction *f = janet_thunk(cres.funcdef);
                        JanetFiber *fiber = janet_fiber(f, 64);
                        Janet ret = janet_wrap_nil();
                        JanetSignal status = janet_run(fiber, &ret);
                        if (status != JANET_SIGNAL_OK) {
                            janet_stacktrace(fiber, "runtime", ret);
                            errflags |= 0x01;
                        }
                    } else {
                        janet_stacktrace(cres.macrofiber, "compile",
                                janet_wrap_string(cres.error));
                        errflags |= 0x02;
                    }
                }
                break;
            case JANET_PARSE_ERROR:
                errflags |= 0x04;
                printf("parse error: %s\n", janet_parser_error(&parser));
                break;
            case JANET_PARSE_PENDING:
                if (index >= len) {
                    if (dudeol) {
                        errflags |= 0x04;
                        printf("internal parse error: unexpected end of source\n");
                    } else {
                        dudeol = 1;
                        janet_parser_consume(&parser, '\n');
                    }
                } else {
                    janet_parser_consume(&parser, bytes[index++]);
                }
                break;
            case JANET_PARSE_ROOT:
                if (index >= len) {
                    done = 1;
                } else {
                    janet_parser_consume(&parser, bytes[index++]);
                }
                break;
        }
    }
    janet_parser_deinit(&parser);
    if (where) janet_gcunroot(janet_wrap_string(where));
    return errflags;
}

int janet_dostring(JanetTable *env, const char *str, const char *sourcePath) {
    int32_t len = 0;
    while (str[len]) ++len;
    return janet_dobytes(env, (const uint8_t *)str, len, sourcePath);
}


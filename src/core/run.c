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

#include <janet/janet.h>
#include "state.h"
#include "vector.h"

/* Error reporting */
void janet_stacktrace(JanetFiber *fiber, const char *errtype, Janet err) {
    int32_t fi;
    const char *errstr = (const char *)janet_to_string(err);
    JanetFiber **fibers = NULL;
    fprintf(stderr, "%s error: %s\n", errtype, errstr);

    while (fiber) {
        janet_v_push(fibers, fiber);
        fiber = fiber->child;
    }

    for (fi = janet_v_count(fibers) - 1; fi >= 0; fi--) {
        fiber = fibers[fi];
        int32_t i = fiber->frame;
        while (i > 0) {
            JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
            JanetFuncDef *def = NULL;
            i = frame->prevframe;
            fprintf(stderr, "  in");
            if (frame->func) {
                def = frame->func->def;
                fprintf(stderr, " %s", def->name ? (const char *)def->name : "<anonymous>");
                if (def->source) {
                    fprintf(stderr, " [%s]", (const char *)def->source);
                }
            } else {
                JanetCFunction cfun = (JanetCFunction)(frame->pc);
                if (cfun) {
                    Janet name = janet_table_get(janet_vm_registry, janet_wrap_cfunction(cfun));
                    if (!janet_checktype(name, JANET_NIL))
                        fprintf(stderr, " %s", (const char *)janet_to_string(name));
                    else
                        fprintf(stderr, " <cfunction>");
                }
            }
            if (frame->flags & JANET_STACKFRAME_TAILCALL)
                fprintf(stderr, " (tailcall)");
            if (frame->func && frame->pc) {
                int32_t off = (int32_t) (frame->pc - def->bytecode);
                if (def->sourcemap) {
                    JanetSourceMapping mapping = def->sourcemap[off];
                    fprintf(stderr, " at (%d:%d)", mapping.start, mapping.end);
                } else {
                    fprintf(stderr, " pc=%d", off);
                }
            }
            fprintf(stderr, "\n");
        }
    }

    janet_v_free(fibers);
}

/* Run a string */
int janet_dobytes(JanetTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath, Janet *out) {
    JanetParser parser;
    int errflags = 0;
    int32_t index = 0;
    int dudeol = 0;
    int done = 0;
    Janet ret = janet_wrap_nil();
    const uint8_t *where = sourcePath ? janet_cstring(sourcePath) : NULL;
    if (where) janet_gcroot(janet_wrap_string(where));
    janet_parser_init(&parser);

    while (!errflags && !done) {

        /* Evaluate parsed values */
        while (janet_parser_has_more(&parser)) {
            Janet form = janet_parser_produce(&parser);
            JanetCompileResult cres = janet_compile(form, env, where);
            if (cres.status == JANET_COMPILE_OK) {
                JanetFunction *f = janet_thunk(cres.funcdef);
                JanetFiber *fiber = janet_fiber(f, 64, 0, NULL);
                JanetSignal status = janet_continue(fiber, janet_wrap_nil(), &ret);
                if (status != JANET_SIGNAL_OK) {
                    janet_stacktrace(fiber, "runtime", ret);
                    errflags |= 0x01;
                }
            } else {
                fprintf(stderr, "source path: %s\n", sourcePath);
                janet_stacktrace(cres.macrofiber, "compile",
                        janet_wrap_string(cres.error));
                errflags |= 0x02;
            }
        }

        /* Dispatch based on parse state */
        switch (janet_parser_status(&parser)) {
            case JANET_PARSE_ERROR:
                errflags |= 0x04;
                fprintf(stderr, "parse error: %s\n", janet_parser_error(&parser));
                break;
            case JANET_PARSE_PENDING:
                if (index >= len) {
                    if (dudeol) {
                        errflags |= 0x04;
                        fprintf(stderr, "internal parse error: unexpected end of source\n");
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
    if (out) *out = ret;
    return errflags;
}

int janet_dostring(JanetTable *env, const char *str, const char *sourcePath, Janet *out) {
    int32_t len = 0;
    while (str[len]) ++len;
    return janet_dobytes(env, (const uint8_t *)str, len, sourcePath, out);
}


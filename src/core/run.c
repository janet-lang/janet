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
#include "state.h"

/* Error reporting */
static void print_error_report(DstFiber *fiber, const char *errtype, Dst err) {
    const char *errstr = (const char *)dst_to_string(err);
    printf("%s error: %s\n", errtype, errstr);
    if (!fiber) return;
    int32_t i = fiber->frame;
    while (i > 0) {
        DstStackFrame *frame = (DstStackFrame *)(fiber->data + i - DST_FRAME_SIZE);
        DstFuncDef *def = NULL;
        i = frame->prevframe;
        
        printf("  at");

        if (frame->func) {
            def = frame->func->def;
            printf(" %s", def->name ? (const char *)def->name : "<anonymous>");
            if (def->source) {
                printf(" %s", (const char *)def->source);
            }
        } else {
            DstCFunction cfun = (DstCFunction)(frame->pc);
            if (cfun) {
                Dst name = dst_table_get(dst_vm_registry, dst_wrap_cfunction(cfun));
                if (!dst_checktype(name, DST_NIL))
                    printf(" [%s]", (const char *)dst_to_string(name));
            }
        }
        if (frame->flags & DST_STACKFRAME_TAILCALL)
            printf(" (tailcall)");
        if (frame->func && frame->pc) {
            int32_t off = (int32_t) (frame->pc - def->bytecode);
            if (def->sourcemap) {
                DstSourceMapping mapping = def->sourcemap[off];
                printf(" on line %d, column %d", mapping.line, mapping.column);
            } else {
                printf(" pc=%d", off);
            }
        }
        printf("\n");
    }
}

/* Run a string */
int dst_dobytes(DstTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath) {
    DstParser parser;
    int errflags = 0;
    int32_t index = 0;
    int dudeol = 0;
    int done = 0;
    const uint8_t *where = sourcePath ? dst_cstring(sourcePath) : NULL;
    if (where) dst_gcroot(dst_wrap_string(where));
    dst_parser_init(&parser);

    while (!errflags && !done) {
        switch (dst_parser_status(&parser)) {
            case DST_PARSE_FULL:
                {
                    Dst form = dst_parser_produce(&parser);
                    DstCompileResult cres = dst_compile(form, env, where);
                    if (cres.status == DST_COMPILE_OK) {
                        DstFunction *f = dst_thunk(cres.funcdef);
                        DstFiber *fiber = dst_fiber(f, 64);
                        Dst ret = dst_wrap_nil();
                        DstSignal status = dst_run(fiber, &ret);
                        if (status != DST_SIGNAL_OK) {
                            print_error_report(fiber, "runtime", ret);
                            errflags |= 0x01;
                        }
                    } else {
                        print_error_report(cres.macrofiber, "compile",
                                dst_wrap_string(cres.error));
                        errflags |= 0x02;
                    }
                }
                break;
            case DST_PARSE_ERROR:
                errflags |= 0x04;
                printf("parse error: %s\n", dst_parser_error(&parser));
                break;
            case DST_PARSE_PENDING:
                if (index >= len) {
                    if (dudeol) {
                        errflags |= 0x04;
                        printf("internal parse error: unexpected end of source\n");
                    } else {
                        dudeol = 1;
                        dst_parser_consume(&parser, '\n');
                    }
                } else {
                    dst_parser_consume(&parser, bytes[index++]);
                }
                break;
            case DST_PARSE_ROOT:
                if (index >= len) {
                    done = 1;
                } else {
                    dst_parser_consume(&parser, bytes[index++]);
                }
                break;
        }
    }
    dst_parser_deinit(&parser);
    if (where) dst_gcunroot(dst_wrap_string(where));
    return errflags;
}

int dst_dostring(DstTable *env, const char *str, const char *sourcePath) {
    int32_t len = 0;
    while (str[len]) ++len;
    return dst_dobytes(env, (const uint8_t *)str, len, sourcePath);
}


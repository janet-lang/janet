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
#include <dst/dstcorelib.h>
#include <dst/dstcompile.h>

/* Run a string */
int dst_dobytes(DstTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath) {
    DstParser parser;
    int errflags = 0;
    int32_t index = 0;
    int dudeol = 0;
    int done = 0;
    const uint8_t *source = sourcePath ? dst_cstring(sourcePath) : NULL;

    dst_parser_init(&parser, sourcePath ? DST_PARSEFLAG_SOURCEMAP : 0);
    parser.source = source;
    while (!errflags && !done) {
        switch (dst_parser_status(&parser)) {
            case DST_PARSE_FULL:
                {
                    Dst form = dst_parser_produce(&parser);
                    DstCompileResult cres = dst_compile(form, env, 0, &parser);
                    if (cres.status == DST_COMPILE_OK) {
                        DstFunction *f = dst_thunk(cres.funcdef);
                        DstFiber *fiber = dst_fiber(f, 64);
                        Dst ret = dst_wrap_nil();
                        DstSignal status = dst_run(fiber, &ret);
                        if (status != DST_SIGNAL_OK) {
                            printf("internal runtime error: %s\n", (const char *) dst_to_string(ret));
                            errflags |= 0x01;
                        }
                    } else {
                        printf("internal compile error: %s\n", (const char *) cres.error);
                        errflags |= 0x02;
                    }
                }
                break;
            case DST_PARSE_ERROR:
                errflags |= 0x04;
                printf("internal parse error: %s\n", dst_parser_error(&parser));
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
    return errflags;
}

int dst_dostring(DstTable *env, const char *str, const char *sourcePath) {
    int32_t len = 0;
    while (str[len]) ++len;
    return dst_dobytes(env, (const uint8_t *)str, len, sourcePath);
}


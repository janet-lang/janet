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

#ifndef DST_PARSE_H_defined
#define DST_PARSE_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

/* AST */
Dst dst_ast_wrap(Dst x, int32_t start, int32_t end);
DstAst *dst_ast_node(Dst x);
Dst dst_ast_unwrap1(Dst x);
Dst dst_ast_unwrap(Dst x);

typedef struct DstParseState DstParseState;
typedef struct DstParser DstParser;

enum DstParserStatus {
    DST_PARSE_ROOT,
    DST_PARSE_ERROR,
    DST_PARSE_FULL,
    DST_PARSE_PENDING
};

struct DstParser {
    Dst* argstack;
    DstParseState *states;
    uint8_t *buf;
    const char *error;
    size_t index;
    int lookback;
    int flags;
};

/* Parsing */
#define DST_PARSEFLAG_SOURCEMAP 1
void dst_parser_init(DstParser *parser, int flags);
void dst_parser_deinit(DstParser *parser);
int dst_parser_consume(DstParser *parser, uint8_t c);
enum DstParserStatus dst_parser_status(DstParser *parser);
Dst dst_parser_produce(DstParser *parser);
const char *dst_parser_error(DstParser *parser);

int dst_parse_cfun(DstArgs args);

int dst_lib_parse(DstArgs args);

#ifdef __cplusplus
}
#endif

#endif /* DST_PARSE_H_defined */

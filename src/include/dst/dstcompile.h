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

#ifndef DST_COMPILE_H_defined
#define DST_COMPILE_H_defined

#include "dsttypes.h"
#include "dstparse.h"

typedef enum DstCompileStatus DstCompileStatus;
typedef struct DstCompileOptions DstCompileOptions;
typedef struct DstCompileResult DstCompileResult;
enum DstCompileStatus {
    DST_COMPILE_OK,
    DST_COMPILE_ERROR
};
struct DstCompileResult {
    DstCompileStatus status;
    DstFuncDef *funcdef;
    const uint8_t *error;
    int32_t error_start;
    int32_t error_end;
};
DstCompileResult dst_compile(Dst source, DstTable *env, int flags);
int dst_compile_cfun(DstArgs args);

/* Context - for executing dst in the interpretted form. */
typedef struct DstContext DstContext;

void dst_context_init(DstContext *c, DstTable *env);
void dst_context_deinit(DstContext *c);
int dst_context_repl(DstContext *c, DstTable *env);
int dst_context_file(DstContext *c, DstTable *env, const char *path);
int dst_context_run(DstContext *c, int flags);

/* Parse structs */
typedef enum {
    DST_CONTEXT_ERROR_PARSE,
    DST_CONTEXT_ERROR_COMPILE,
    DST_CONTEXT_ERROR_RUNTIME
} DstContextErrorType;

/* Evaluation context. Encapsulates parsing and compiling for easier integration
 * with client programs. */
struct DstContext {
    DstTable *env;
    DstBuffer buffer;
    void *user;
    int32_t index;

    int (*read_chunk)(DstContext *self, DstParserStatus status);
    void (*on_error)(DstContext *self, DstContextErrorType type, Dst err, size_t start, size_t end);
    void (*on_value)(DstContext *self, Dst value);
    void (*deinit)(DstContext *self);
};


#endif /* DST_COMPILE_H_defined */

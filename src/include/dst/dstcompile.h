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

#ifndef DST_COMPILE_H_defined
#define DST_COMPILE_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

typedef struct DstCompileOptions DstCompileOptions;
typedef struct DstCompileResult DstCompileResult;
enum DstCompileStatus {
    DST_COMPILE_OK,
    DST_COMPILE_ERROR
};
struct DstCompileResult {
    enum DstCompileStatus status;
    DstFuncDef *funcdef;
    const uint8_t *error;
    int32_t error_start;
    int32_t error_end;
};
DstCompileResult dst_compile(Dst source, DstTable *env, int flags, DstParser *parser);
int dst_compile_cfun(DstArgs args);
int dst_lib_compile(DstArgs args);

/* Get the default environment for dst */
DstTable *dst_stl_env();

int dst_dobytes(DstTable *env, const uint8_t *bytes, int32_t len, const char *sourcePath);
int dst_dostring(DstTable *env, const char *str, const char *sourcePath);

#ifdef __cplusplus
}
#endif

#endif 

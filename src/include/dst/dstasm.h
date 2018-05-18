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

#ifndef DST_ASM_H_defined
#define DST_ASM_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

/* Assembly */
typedef struct DstAssembleResult DstAssembleResult;
typedef struct DstAssembleOptions DstAssembleOptions;
enum DstAssembleStatus {
    DST_ASSEMBLE_OK,
    DST_ASSEMBLE_ERROR
};
struct DstAssembleResult {
    DstFuncDef *funcdef;
    const uint8_t *error;
    enum DstAssembleStatus status;
};
DstAssembleResult dst_asm(Dst source, int flags);
Dst dst_disasm(DstFuncDef *def);
Dst dst_asm_decode_instruction(uint32_t instr);
int dst_asm_cfun(DstArgs args);
int dst_disasm_cfun(DstArgs args);

int dst_lib_asm(DstArgs args);

#ifdef __cplusplus
}
#endif

#endif /* DST_ASM_H_defined */

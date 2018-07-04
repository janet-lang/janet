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

#ifndef DST_EMIT_H
#define DST_EMIT_H

#include "compile.h"

void dstc_emit(DstCompiler *c, uint32_t instr);

int32_t dstc_allocfar(DstCompiler *c);
int32_t dstc_allocnear(DstCompiler *c, DstcRegisterTemp);

int32_t dstc_emit_s(DstCompiler *c, uint8_t op, DstSlot s, int wr);
int32_t dstc_emit_sl(DstCompiler *c, uint8_t op, DstSlot s, int32_t label);
int32_t dstc_emit_st(DstCompiler *c, uint8_t op, DstSlot s, int32_t tflags);
int32_t dstc_emit_si(DstCompiler *c, uint8_t op, DstSlot s, int16_t immediate, int wr);
int32_t dstc_emit_su(DstCompiler *c, uint8_t op, DstSlot s, uint16_t immediate, int wr);
int32_t dstc_emit_ss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int wr);
int32_t dstc_emit_ssi(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, int8_t immediate, int wr);
int32_t dstc_emit_ssu(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, uint8_t immediate, int wr);
int32_t dstc_emit_sss(DstCompiler *c, uint8_t op, DstSlot s1, DstSlot s2, DstSlot s3, int wr);

/* Move value from one slot to another. Cannot copy to constant slots. */
void dstc_copy(DstCompiler *c, DstSlot dest, DstSlot src);

#endif

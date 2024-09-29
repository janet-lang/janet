/*
* Copyright (c) 2024 Calvin Rose
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

#ifndef JANET_EMIT_H
#define JANET_EMIT_H

#ifndef JANET_AMALG
#include "compile.h"
#endif

void janetc_emit(JanetCompiler *c, uint32_t instr);

int32_t janetc_allocfar(JanetCompiler *c);
int32_t janetc_allocnear(JanetCompiler *c, JanetcRegisterTemp);

int32_t janetc_emit_s(JanetCompiler *c, uint8_t op, JanetSlot s, int wr);
int32_t janetc_emit_sl(JanetCompiler *c, uint8_t op, JanetSlot s, int32_t label);
int32_t janetc_emit_st(JanetCompiler *c, uint8_t op, JanetSlot s, int32_t tflags);
int32_t janetc_emit_si(JanetCompiler *c, uint8_t op, JanetSlot s, int16_t immediate, int wr);
int32_t janetc_emit_su(JanetCompiler *c, uint8_t op, JanetSlot s, uint16_t immediate, int wr);
int32_t janetc_emit_ss(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, int wr);
int32_t janetc_emit_ssi(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, int8_t immediate, int wr);
int32_t janetc_emit_ssu(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, uint8_t immediate, int wr);
int32_t janetc_emit_sss(JanetCompiler *c, uint8_t op, JanetSlot s1, JanetSlot s2, JanetSlot s3, int wr);

/* Check if two slots are equivalent */
int janetc_sequal(JanetSlot x, JanetSlot y);

/* Move value from one slot to another. Cannot copy to constant slots. */
void janetc_copy(JanetCompiler *c, JanetSlot dest, JanetSlot src);

#endif

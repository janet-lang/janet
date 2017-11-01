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

#ifndef DST_WRAP_H_defined
#define DST_WRAP_H_defined

#include <dst/dst.h>

/* Wrap data in DstValue */
DstValue dst_wrap_nil();
DstValue dst_wrap_real(double x);
DstValue dst_wrap_integer(int64_t x);
DstValue dst_wrap_boolean(int x);
DstValue dst_wrap_string(const uint8_t *x);
DstValue dst_wrap_symbol(const uint8_t *x);
DstValue dst_wrap_array(DstArray *x);
DstValue dst_wrap_tuple(const DstValue *x);
DstValue dst_wrap_struct(const DstValue *x);
DstValue dst_wrap_thread(DstThread *x);
DstValue dst_wrap_buffer(DstBuffer *x);
DstValue dst_wrap_function(DstFunction *x);
DstValue dst_wrap_cfunction(DstCFunction x);
DstValue dst_wrap_table(DstTable *x);
DstValue dst_wrap_userdata(void *x);

/* Unwrap values */
double dst_unwrap_real(Dst *vm, int64_t i);
int64_t dst_unwrap_integer(Dst *vm, int64_t i);
int dst_unwrap_boolean(Dst *vm, int64_t i);
const uint8_t *dst_unwrap_string(Dst *vm, int64_t i);
const uint8_t *dst_unwrap_symbol(Dst *vm, int64_t i);
DstArray *dst_unwrap_array(Dst *vm, int64_t i);
const DstValue *dst_unwrap_tuple(Dst *vm, int64_t i);
const DstValue *dst_unwrap_struct(Dst *vm, int64_t i);
DstThread *dst_unwrap_thread(Dst *vm, int64_t i);
DstBuffer *dst_unwrap_buffer(Dst *vm, int64_t i);
DstFunction *dst_unwrap_function(Dst *vm, int64_t i);
DstCFunction dst_unwrap_cfunction(Dst *vm, int64_t i);
DstTable *dst_unwrap_table(Dst *vm, int64_t i);
void *dst_unwrap_userdata(Dst *vm, int64_t i);

#endif

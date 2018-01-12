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

#ifndef DST_MATH_H_defined
#define DST_MATH_H_defined

#include "dsttypes.h"

/* File type definition */
extern DstAbstractType dst_stl_filetype;

int dst_int(int32_t argn, Dst *argv, Dst *ret);
int dst_real(int32_t argn, Dst *argv, Dst *ret);

int dst_add(int32_t argn, Dst *argv, Dst *ret);
int dst_subtract(int32_t argn, Dst *argv, Dst *ret);
int dst_multiply(int32_t argn, Dst *argv, Dst *ret);
int dst_divide(int32_t argn, Dst *argv, Dst *ret);
int dst_modulo(int32_t argn, Dst *argv, Dst *ret);

int dst_acos(int32_t argn, Dst *argv, Dst *ret);
int dst_asin(int32_t argn, Dst *argv, Dst *ret);
int dst_atan(int32_t argn, Dst *argv, Dst *ret);
int dst_cos(int32_t argn, Dst *argv, Dst *ret); 
int dst_cosh(int32_t argn, Dst *argv, Dst *ret); 
int dst_sin(int32_t argn, Dst *argv, Dst *ret); 
int dst_sinh(int32_t argn, Dst *argv, Dst *ret); 
int dst_tan(int32_t argn, Dst *argv, Dst *ret); 
int dst_tanh(int32_t argn, Dst *argv, Dst *ret); 
int dst_exp(int32_t argn, Dst *argv, Dst *ret); 
int dst_log(int32_t argn, Dst *argv, Dst *ret); 
int dst_log10(int32_t argn, Dst *argv, Dst *ret); 
int dst_sqrt(int32_t argn, Dst *argv, Dst *ret); 
int dst_ceil(int32_t argn, Dst *argv, Dst *ret); 
int dst_fabs(int32_t argn, Dst *argv, Dst *ret); 
int dst_floor(int32_t argn, Dst *argv, Dst *ret); 
int dst_pow(int32_t argn, Dst *argv, Dst *ret); 

int dst_stl_table(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_array(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_struct(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_tuple(int32_t argn, Dst *argv, Dst *ret);

int dst_band(int32_t argn, Dst *argv, Dst *ret);
int dst_bor(int32_t argn, Dst *argv, Dst *ret);
int dst_bxor(int32_t argn, Dst *argv, Dst *ret);

int dst_lshift(int argn, Dst *argv, Dst *ret);
int dst_rshift(int argn, Dst *argv, Dst *ret);
int dst_lshiftu(int argn, Dst *argv, Dst *ret);

int dst_stl_fileopen(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_slurp(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_fileread(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_filewrite(int32_t argn, Dst *argv, Dst *ret);
int dst_stl_fileclose(int32_t argn, Dst *argv, Dst *ret);

#endif /* DST_MATH_H_defined */

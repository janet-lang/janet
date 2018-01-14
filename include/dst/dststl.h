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

int dst_int(DstArgs args);
int dst_real(DstArgs args);

int dst_add(DstArgs args);
int dst_subtract(DstArgs args);
int dst_multiply(DstArgs args);
int dst_divide(DstArgs args);
int dst_modulo(DstArgs args);

int dst_acos(DstArgs args);
int dst_asin(DstArgs args);
int dst_atan(DstArgs args);
int dst_cos(DstArgs args); 
int dst_cosh(DstArgs args); 
int dst_sin(DstArgs args); 
int dst_sinh(DstArgs args); 
int dst_tan(DstArgs args); 
int dst_tanh(DstArgs args); 
int dst_exp(DstArgs args); 
int dst_log(DstArgs args); 
int dst_log10(DstArgs args); 
int dst_sqrt(DstArgs args); 
int dst_ceil(DstArgs args); 
int dst_fabs(DstArgs args); 
int dst_floor(DstArgs args); 
int dst_pow(DstArgs args); 

int dst_stl_table(DstArgs args);
int dst_stl_array(DstArgs args);
int dst_stl_struct(DstArgs args);
int dst_stl_tuple(DstArgs args);

int dst_band(DstArgs args);
int dst_bor(DstArgs args);
int dst_bxor(DstArgs args);

int dst_lshift(DstArgs arsg);
int dst_rshift(DstArgs args);
int dst_lshiftu(DstArgs args);

int dst_stl_fileopen(DstArgs args);
int dst_stl_slurp(DstArgs args);
int dst_stl_fileread(DstArgs args);
int dst_stl_filewrite(DstArgs args);
int dst_stl_fileclose(DstArgs args);

#endif /* DST_MATH_H_defined */

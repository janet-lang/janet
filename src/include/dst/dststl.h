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

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

/* Basic C Functions. These are good
 * candidates for optimizations like bytecode
 * inlining and costant folding. They are exposed publicly
 * so that compiles can inject them into funcdefs. Later, a
 * different serialization mechanism might be used for cfunctions. */

int dst_load_native(DstArgs args);
int dst_int(DstArgs args);
int dst_real(DstArgs args);
int dst_add(DstArgs args);
int dst_subtract(DstArgs args);
int dst_multiply(DstArgs args);
int dst_divide(DstArgs args);
int dst_modulo(DstArgs args);
int dst_band(DstArgs args);
int dst_bor(DstArgs args);
int dst_bxor(DstArgs args);
int dst_lshift(DstArgs arsg);
int dst_rshift(DstArgs args);
int dst_lshiftu(DstArgs args);

Dst dst_op_add(Dst lhs, Dst rhs);
Dst dst_op_subtract(Dst lhs, Dst rhs);

/* Native type constructors */
int dst_cfun_table(DstArgs args);
int dst_cfun_array(DstArgs args);
int dst_cfun_struct(DstArgs args);
int dst_cfun_tuple(DstArgs args);

/* Initialize builtin libraries */
int dst_lib_io(DstArgs args);
int dst_lib_math(DstArgs args);
int dst_lib_array(DstArgs args);
int dst_lib_ast(DstArgs args);
int dst_lib_tuple(DstArgs args);
int dst_lib_buffer(DstArgs args);
int dst_lib_parse(DstArgs args);
int dst_lib_compile(DstArgs args);

#ifdef __cplusplus
}
#endif

#endif /* DST_MATH_H_defined */

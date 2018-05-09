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

#ifndef DST_CORELIB_H_defined
#define DST_CORELIB_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#include "dsttypes.h"

/* Native */
int dst_core_native(DstArgs args);

/* Arithmetic */
int dst_int(DstArgs args);
int dst_real(DstArgs args);
int dst_add(DstArgs args);
int dst_subtract(DstArgs args);
int dst_multiply(DstArgs args);
int dst_divide(DstArgs args);
int dst_modulo(DstArgs args);
int dst_rand(DstArgs args);
int dst_srand(DstArgs args);
int dst_strict_equal(DstArgs args);
int dst_strict_notequal(DstArgs args);
int dst_ascending(DstArgs args);
int dst_descending(DstArgs args);
int dst_notdescending(DstArgs args);
int dst_notascending(DstArgs args);
int dst_numeric_eq(DstArgs args);
int dst_numeric_neq(DstArgs args);
int dst_numeric_gt(DstArgs args);
int dst_numeric_lt(DstArgs args);
int dst_numeric_gte(DstArgs args);
int dst_numeric_lte(DstArgs args);
int dst_bor(DstArgs args);
int dst_band(DstArgs args);
int dst_bxor(DstArgs args);
int dst_bnot(DstArgs args);
int dst_lshift(DstArgs args);
int dst_rshift(DstArgs args);
int dst_lshiftu(DstArgs args);
int dst_not(DstArgs args);

/* Math */
int dst_cos(DstArgs args);
int dst_sin(DstArgs args);
int dst_tan(DstArgs args);
int dst_acos(DstArgs args);
int dst_asin(DstArgs args);
int dst_atan(DstArgs args);
int dst_exp(DstArgs args);
int dst_log(DstArgs args);
int dst_log10(DstArgs args);
int dst_sqrt(DstArgs args);
int dst_floor(DstArgs args);
int dst_ceil(DstArgs args);
int dst_pow(DstArgs args);

/* Misc core functions */
int dst_core_print(DstArgs args);
int dst_core_describe(DstArgs args);
int dst_core_string(DstArgs args);
int dst_core_symbol(DstArgs args);
int dst_core_buffer(DstArgs args);
int dst_core_format(DstArgs args);
int dst_core_scannumber(DstArgs args);
int dst_core_scaninteger(DstArgs args);
int dst_core_scanreal(DstArgs args);
int dst_core_tuple(DstArgs args);
int dst_core_array(DstArgs args);
int dst_core_table(DstArgs args);
int dst_core_struct(DstArgs args);
int dst_core_buffer(DstArgs args);
int dst_core_gensym(DstArgs args);
int dst_core_length(DstArgs args);
int dst_core_get(DstArgs args);
int dst_core_put(DstArgs args);
int dst_core_type(DstArgs args);
int dst_core_next(DstArgs args);
int dst_core_hash(DstArgs args);
int dst_core_string_slice(DstArgs args);

/* GC */
int dst_core_gccollect(DstArgs args);
int dst_core_gcsetinterval(DstArgs args);
int dst_core_gcinterval(DstArgs args);

/* Initialize builtin libraries */
int dst_lib_io(DstArgs args);
int dst_lib_math(DstArgs args);
int dst_lib_array(DstArgs args);
int dst_lib_tuple(DstArgs args);
int dst_lib_buffer(DstArgs args);
int dst_lib_table(DstArgs args);
int dst_lib_fiber(DstArgs args);
int dst_lib_os(DstArgs args);

/* Useful for compiler */
Dst dst_op_add(Dst lhs, Dst rhs);
Dst dst_op_subtract(Dst lhs, Dst rhs);

#ifdef __cplusplus
}
#endif

#endif /* DST_CORELIB_H_defined */

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

#ifndef JANET_VECTOR_H_defined
#define JANET_VECTOR_H_defined

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#endif

/*
 * vector code modified from
 * https://github.com/nothings/stb/blob/master/stretchy_buffer.h
*/

/* This is mainly used code such as the assembler or compiler, which
 * need vector like data structures that are only garbage collected in case
 * of an error, and normally rely on malloc/free. */

#define janet_v_free(v)         (((v) != NULL) ? (janet_sfree(janet_v__raw(v)), 0) : 0)
#define janet_v_push(v, x)      (janet_v__maybegrow(v, 1), (v)[janet_v__cnt(v)++] = (x))
#define janet_v_pop(v)          (janet_v_count(v) ? janet_v__cnt(v)-- : 0)
#define janet_v_count(v)        (((v) != NULL) ? janet_v__cnt(v) : 0)
#define janet_v_last(v)         ((v)[janet_v__cnt(v) - 1])
#define janet_v_empty(v)        (((v) != NULL) ? (janet_v__cnt(v) = 0) : 0)
#define janet_v_flatten(v)      (janet_v_flattenmem((v), sizeof(*(v))))

#define janet_v__raw(v) ((int32_t *)(v) - 2)
#define janet_v__cap(v) janet_v__raw(v)[0]
#define janet_v__cnt(v) janet_v__raw(v)[1]

#define janet_v__needgrow(v, n)  ((v) == NULL || janet_v__cnt(v) + (n) >= janet_v__cap(v))
#define janet_v__maybegrow(v, n) (janet_v__needgrow((v), (n)) ? janet_v__grow((v), (n)) : 0)
#define janet_v__grow(v, n)      ((v) = janet_v_grow((v), (n), sizeof(*(v))))

/* Actual functions defined in vector.c */
void *janet_v_grow(void *v, int32_t increment, int32_t itemsize);
void *janet_v_flattenmem(void *v, int32_t itemsize);

#endif

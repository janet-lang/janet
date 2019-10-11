/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include "vector.h"
#include "util.h"
#endif

/* Grow the buffer dynamically. Used for push operations. */
void *janet_v_grow(void *v, int32_t increment, int32_t itemsize) {
    int32_t dbl_cur = (NULL != v) ? 2 * janet_v__cap(v) : 0;
    int32_t min_needed = janet_v_count(v) + increment;
    int32_t m = dbl_cur > min_needed ? dbl_cur : min_needed;
    size_t newsize = ((size_t) itemsize) * m + sizeof(int32_t) * 2;
    int32_t *p = (int32_t *) janet_srealloc(v ? janet_v__raw(v) : 0, newsize);
    if (!v) p[1] = 0;
    p[0] = m;
    return p + 2;
}

/* Convert a buffer to normal allocated memory (forget capacity) */
void *janet_v_flattenmem(void *v, int32_t itemsize) {
    int32_t *p;
    int32_t sizen;
    if (NULL == v) return NULL;
    sizen = itemsize * janet_v__cnt(v);
    p = malloc(sizen);
    if (NULL != p) {
        memcpy(p, v, sizen);
        return p;
    } else {
        {
            JANET_OUT_OF_MEMORY;
        }
        return NULL;
    }
}


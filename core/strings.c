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

#include <gst/gst.h>

/* Compares two strings */
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = gst_string_length(lhs);
    uint32_t ylen = gst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

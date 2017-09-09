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

#ifndef DST_BOOTSTRAP_H_defined
#define DST_BOOTSTRAP_H_defined

#define PARSE_OK 0
#define PARSE_ERROR 1
#define PARSE_UNEXPECTED_EOS 2

int dst_parseb(Dst *vm, uint32_t dest, const uint8_t *src, const uint8_t **newsrc, uint32_t len);

/* Parse a c string */
int dst_parsec(Dst *vm, uint32_t dest, const char *src);

/* Parse a DST char seq (Buffer, String, Symbol) */
int dst_parse(Dst *vm, uint32_t dest, uint32_t src);

#endif /* DST_BOOTSTRAP_H_defined */
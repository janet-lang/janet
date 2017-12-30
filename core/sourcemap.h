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

#ifndef DST_SOURCEMAP_H_defined
#define DST_SOURCEMAP_H_defined

#include <dst/dst.h>

/* Get the sub source map by indexing a value. Used to traverse
 * into arrays and tuples */
const DstValue *dst_sourcemap_index(const DstValue *map, int32_t index);

/* Traverse into a key of a table or struct */
const DstValue *dst_sourcemap_key(const DstValue *map, DstValue key);

/* Traverse into a value of a table or struct */
const DstValue *dst_sourcemap_value(const DstValue *map, DstValue key);

/* Try to rebuild a source map from given another map */
const DstValue *dst_sourcemap_remap(
        const DstValue *oldmap,
        DstValue oldsource,
        DstValue newsource);

#endif

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

#include <dst/dst.h>
#include "sourcemap.h"

/* Get the sub source map by indexing a value. Used to traverse
 * into arrays and tuples */
const DstValue *dst_sourcemap_index(const DstValue *map, int32_t index) {
    if (NULL != map && dst_tuple_length(map) >= 3) {
        const DstValue *seq;
        int32_t len;
        if (dst_seq_view(map[2], &seq, &len)) {
            if (index >= 0 && index < len) {
                if (dst_checktype(seq[index], DST_TUPLE)) {
                    const DstValue *ret = dst_unwrap_tuple(seq[index]);
                    if (dst_tuple_length(ret) >= 2 &&
                        dst_checktype(ret[0], DST_INTEGER) &&
                        dst_checktype(ret[1], DST_INTEGER)) {
                        return ret;
                    }
                }
            }
        }
    }
    return NULL;
}

/* Traverse into tables and structs */
static const DstValue *dst_sourcemap_kv(const DstValue *map, DstValue key, int kv) {
    if (NULL != map && dst_tuple_length(map) >= 3) {
        DstValue kvpair = dst_get(map[2], key);
        if (dst_checktype(kvpair, DST_TUPLE)) {
            const DstValue *kvtup = dst_unwrap_tuple(kvpair);
            if (dst_tuple_length(kvtup) >= 2) {
                if (dst_checktype(kvtup[kv], DST_TUPLE)) {
                    const DstValue *ret = dst_unwrap_tuple(kvtup[kv]);
                    if (dst_tuple_length(ret) >= 2 &&
                        dst_checktype(ret[0], DST_INTEGER) &&
                        dst_checktype(ret[1], DST_INTEGER)) {
                        return ret;
                    }
                }
            }
        }
    }
    return NULL;
}

/* Traverse into a key of a table or struct */
const DstValue *dst_sourcemap_key(const DstValue *map, DstValue key) {
    return dst_sourcemap_kv(map, key, 0);
}

/* Traverse into a value of a table or struct */
const DstValue *dst_sourcemap_value(const DstValue *map, DstValue key) {
    return dst_sourcemap_kv(map, key, 1);
}

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

/* Base 64 lookup table for digits */
const char dst_base64[65] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* The DST value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
const char *dst_type_names[15] = {
    "nil",
    "real",
    "integer",
    "boolean",
    "string",
    "symbol",
    "array",
    "tuple",
    "table",
    "struct",
    "fiber",
    "buffer",
    "function",
    "cfunction",
    "userdata"
};

/* Computes hash of an array of values */
int32_t dst_array_calchash(const DstValue *array, int32_t len) {
    const DstValue *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + dst_hash(*array++);
    return (int32_t) hash;
}

/* Calculate hash for string */
int32_t dst_string_calchash(const uint8_t *str, int32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return (int32_t) hash;
}

/* Read both tuples and arrays as c pointers + int32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int dst_seq_view(DstValue seq, const DstValue **data, int32_t *len) {
    if (dst_checktype(seq, DST_ARRAY)) {
        *data = dst_unwrap_array(seq)->data;
        *len = dst_unwrap_array(seq)->count;
        return 1;
    } else if (dst_checktype(seq, DST_TUPLE)) {
        *data = dst_unwrap_struct(seq);
        *len = dst_tuple_length(dst_unwrap_struct(seq));
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + int32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int dst_chararray_view(DstValue str, const uint8_t **data, int32_t *len) {
    if (dst_checktype(str, DST_STRING) || dst_checktype(str, DST_SYMBOL)) {
        *data = dst_unwrap_string(str);
        *len = dst_string_length(dst_unwrap_string(str));
        return 1;
    } else if (dst_checktype(str, DST_BUFFER)) {
        *data = dst_unwrap_buffer(str)->data;
        *len = dst_unwrap_buffer(str)->count;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int dst_hashtable_view(DstValue tab, const DstValue **data, int32_t *len, int32_t *cap) {
    if (dst_checktype(tab, DST_TABLE)) {
        *data = dst_unwrap_table(tab)->data;
        *cap = dst_unwrap_table(tab)->capacity;
        *len = dst_unwrap_table(tab)->count;
        return 1;
    } else if (dst_checktype(tab, DST_STRUCT)) {
        *data = dst_unwrap_struct(tab);
        *cap = dst_struct_capacity(dst_unwrap_struct(tab));
        *len = dst_struct_length(dst_unwrap_struct(tab));
        return 1;
    }
    return 0;
}

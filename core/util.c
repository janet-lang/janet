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

#include "internal.h"

int dst_checkerr(Dst *vm) { return !!vm->flags; }

void dst_return(Dst *vm, DstValue x) {
    vm->ret = x;
}

void dst_throw(Dst *vm) {
    vm->flags = 1;
    vm->ret = dst_popv(vm);
}

void dst_cerr(Dst *vm, const char *message) {
    vm->flags = 1;
    vm->ret = dst_string_cv(vm, message);
}

/* Read both tuples and arrays as c pointers + uint32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int dst_seq_view(DstValue seq, const DstValue **data, uint32_t *len) {
    if (seq.type == DST_ARRAY) {
        *data = seq.as.array->data;
        *len = seq.as.array->count;
        return 1;
    } else if (seq.type == DST_TUPLE) {
        *data = seq.as.st;
        *len = dst_tuple_length(seq.as.st);
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + uint32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int dst_chararray_view(DstValue str, const uint8_t **data, uint32_t *len) {
    if (str.type == DST_STRING || str.type == DST_SYMBOL) {
        *data = str.as.string;
        *len = dst_string_length(str.as.string);
        return 1;
    } else if (str.type == DST_BUFFER) {
        *data = str.as.buffer->data;
        *len = str.as.buffer->count;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int dst_hashtable_view(DstValue tab, const DstValue **data, uint32_t *cap) {
    if (tab.type == DST_TABLE) {
        *data = tab.as.table->data;
        *cap = tab.as.table->capacity;
        return 1;
    } else if (tab.type == DST_STRUCT) {
        *data = tab.as.st;
        *cap = dst_struct_capacity(tab.as.st);
        return 1;
    }
    return 0;
}

/* Convert an index used by the capi to an absolute index */
uint32_t dst_startrange(int64_t index, uint32_t modulo) {
    if (index < 0) index += modulo;
    return ((index >= 0 && index < modulo)) ? ((uint32_t) index) : 0;
}

/* Convert an index used by the capi to an absolute index */
uint32_t dst_endrange(int64_t index, uint32_t modulo) {
    return dst_startrange(index, modulo + 1);
}

/* Convert a possibly negative index to a positive index on the current stack. */
int64_t dst_normalize_index(Dst *vm, int64_t index) {
    if (index < 0) {
        index += dst_frame_size(dst_thread_stack(vm->thread));
    }
    return index;
}

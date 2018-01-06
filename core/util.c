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
#include "util.h"

/* Base 64 lookup table for digits */
const char dst_base64[65] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* The DST value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
const char *dst_type_names[16] = {
    "nil",
    "false",
    "true",
    "fiber",
    "integer",
    "real",
    "string",
    "symbol",
    "array",
    "tuple",
    "table",
    "struct",
    "buffer",
    "function",
    "cfunction",
    "abstract"
};

/* Computes hash of an array of values */
int32_t dst_array_calchash(const Dst *array, int32_t len) {
    const Dst *end = array + len;
    uint32_t hash = 5381;
    while (array < end)
        hash = (hash << 5) + hash + dst_hash(*array++);
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t dst_kv_calchash(const DstKV *kvs, int32_t len) {
    const DstKV *end = kvs + len;
    uint32_t hash = 5381;
    while (kvs < end) {
        hash = (hash << 5) + hash + dst_hash(kvs->key);
        hash = (hash << 5) + hash + dst_hash(kvs->value);
        kvs++;
    }
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

/* Calculate next power of 2. May overflow. If n is 0,
 * will return 0. */
int32_t dst_tablen(int32_t n) {
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* Compare a dst string with a cstring. more efficient than loading
 * c string as a dst string. */
int dst_cstrcmp(const uint8_t *str, const char *other) {
    int32_t len = dst_string_length(str);
    int32_t index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)other)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (other[index] == '\0') ? 0 : -1;
}

/* Do a binary search on a static array of structs. Each struct must
 * have a string as its first element, and the struct must be sorted
 * lexogrpahically by that element. */
const void *dst_strbinsearch(
        const void *tab,
        size_t tabcount,
        size_t itemsize,
        const uint8_t *key) {
    size_t low = 0;
    size_t hi = tabcount;
    while (low < hi) {
        size_t mid = low + ((hi - low) / 2);
        const char **item = (const char **)(tab + mid * itemsize);
        const char *name = *item;
        int comp = dst_cstrcmp(key, name);
        if (comp < 0) {
            hi = mid;
        } else if (comp > 0) {
            low = mid + 1;
        } else {
            return (const void *)item;
        }
    }
    return NULL;
}

/* Read both tuples and arrays as c pointers + int32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int dst_seq_view(Dst seq, const Dst **data, int32_t *len) {
    if (dst_checktype(seq, DST_ARRAY)) {
        *data = dst_unwrap_array(seq)->data;
        *len = dst_unwrap_array(seq)->count;
        return 1;
    } else if (dst_checktype(seq, DST_TUPLE)) {
        *data = dst_unwrap_tuple(seq);
        *len = dst_tuple_length(dst_unwrap_struct(seq));
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + int32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int dst_chararray_view(Dst str, const uint8_t **data, int32_t *len) {
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
int dst_hashtable_view(Dst tab, const DstKV **data, int32_t *len, int32_t *cap) {
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

/* Load c functions into an environment */
Dst dst_loadreg(DstReg *regs, size_t count) {
    size_t i;
    DstTable *t = dst_table(count);
    for (i = 0; i < count; i++) {
        Dst sym = dst_csymbolv(regs[i].name);
        Dst func = dst_wrap_cfunction(regs[i].function);
        DstTable *subt = dst_table(1);
        dst_table_put(subt, dst_csymbolv("value"), func);
        dst_table_put(t, sym, dst_wrap_table(subt));
    }
    return dst_wrap_table(t);
}

/* Vector code */

/* Grow the buffer dynamically. Used for push operations. */
void *dst_v_grow(void *v, int32_t increment, int32_t itemsize) {
    int32_t dbl_cur = (NULL != v) ? 2 * dst_v__cap(v) : 0;
    int32_t min_needed = dst_v_count(v) + increment;
    int32_t m = dbl_cur > min_needed ? dbl_cur : min_needed;
    int32_t *p = (int32_t *) realloc(v ? dst_v__raw(v) : 0, itemsize * m + sizeof(int32_t)*2);
    if (NULL != p) {
        if (!v) p[1] = 0;
        p[0] = m;
        return p + 2;
   } else {
       {
           DST_OUT_OF_MEMORY;
       }
       return (void *) (2 * sizeof(int32_t)); // try to force a NULL pointer exception later
   }
}

/* Clone a buffer. */
void *dst_v_copymem(void *v, int32_t itemsize) {
    int32_t *p;
    if (NULL == v) return NULL;
    p = malloc(2 * sizeof(int32_t) + itemsize * dst_v__cap(v));
    if (NULL != p) {
        memcpy(p, dst_v__raw(v), 2 * sizeof(int32_t) + itemsize * dst_v__cnt(v));
        return p + 2;
    } else {
       {
           DST_OUT_OF_MEMORY;
       }
       return (void *) (2 * sizeof(int32_t)); // try to force a NULL pointer exception later
    }
}

/* Convert a buffer to normal allocated memory (forget capacity) */
void *dst_v_flattenmem(void *v, int32_t itemsize) {
    int32_t *p;
    int32_t sizen;
    if (NULL == v) return NULL;
    sizen = itemsize * dst_v__cnt(v);
    p = malloc(sizen);
    if (NULL != p) {
        memcpy(p, v, sizen);
        return p;
    } else {
       {
           DST_OUT_OF_MEMORY;
       }
       return NULL;
    }
}

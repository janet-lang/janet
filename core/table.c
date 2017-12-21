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
#include "gc.h"

#define dst_table_maphash(cap, hash) (((uint32_t)(hash) % (cap)) & 0xFFFFFFFE)

/* Initialize a table */
DstTable *dst_table_init(DstTable *table, int32_t capacity) {
    DstValue *data;
    if (capacity < 2) capacity = 2;
    data = (DstValue *) dst_memalloc_empty(capacity);
    if (NULL == data) {
        DST_OUT_OF_MEMORY;
    }
    table->data = data;
    table->capacity = capacity;
    table->count = 0;
    table->deleted = 0;
    return table;
}

/* Deinitialize a table */
void dst_table_deinit(DstTable *table) {
    free(table->data);
}

/* Create a new table */
DstTable *dst_table(int32_t capacity) {
    DstTable *table = dst_gcalloc(DST_MEMORY_TABLE, sizeof(DstTable));
    return dst_table_init(table, capacity);
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in the table. */
static DstValue *dst_table_find(DstTable *t, DstValue key) {
    int32_t index = dst_table_maphash(t->capacity, dst_hash(key));
    int32_t i, j;
    int32_t start[2], end[2];
    DstValue *first_bucket = NULL;
    start[0] = index; end[0] = t->capacity;
    start[1] = 0; end[1] = index;
    for (j = 0; j < 2; ++j) {
        for (i = start[j]; i < end[j]; i += 2) {
            if (dst_checktype(t->data[i], DST_NIL)) {
                if (dst_checktype(t->data[i + 1], DST_NIL)) {
                    /* Empty */
                    return t->data + i;
                } else if (NULL == first_bucket) {
                    /* Marked deleted and not seen free bucket yet. */
                    first_bucket = t->data + i;
                }
            } else if (dst_equals(t->data[i], key)) {
                return t->data + i;
            }
        }
    }
    return first_bucket;
}

/* Resize the dictionary table. */
static void dst_table_rehash(DstTable *t, int32_t size) {
    DstValue *olddata = t->data;
    DstValue *newdata = (DstValue *) dst_memalloc_empty(size);
    if (NULL == newdata) {
        DST_OUT_OF_MEMORY;
    }
    int32_t i, oldcapacity;
    oldcapacity = t->capacity;
    t->data = newdata;
    t->capacity = size;
    t->deleted = 0;
    for (i = 0; i < oldcapacity; i += 2) {
        if (!dst_checktype(olddata[i], DST_NIL)) {
            DstValue *bucket = dst_table_find(t, olddata[i]);
            bucket[0] = olddata[i];
            bucket[1] = olddata[i + 1];
        }
    }
    free(olddata);
}

/* Get a value out of the object */
DstValue dst_table_get(DstTable *t, DstValue key) {
    DstValue *bucket = dst_table_find(t, key);
    if (NULL != bucket && !dst_checktype(bucket[0], DST_NIL))
        return bucket[1];
    else
        return dst_wrap_nil();
}

/* Remove an entry from the dictionary. Return the value that
 * was removed. */
DstValue dst_table_remove(DstTable *t, DstValue key) {
    DstValue *bucket = dst_table_find(t, key);
    if (NULL != bucket && !dst_checktype(bucket[0], DST_NIL)) {
        DstValue ret = bucket[1];
        t->count--;
        t->deleted++;
        bucket[0] = dst_wrap_nil();
        bucket[1] = dst_wrap_false();
        return ret;
    } else {
        return dst_wrap_nil();
    }
}

/* Put a value into the object */
void dst_table_put(DstTable *t, DstValue key, DstValue value) {
    if (dst_checktype(key, DST_NIL)) return;
    if (dst_checktype(value, DST_NIL)) {
        dst_table_remove(t, key);
    } else {
        DstValue *bucket = dst_table_find(t, key);
        if (NULL != bucket && !dst_checktype(bucket[0], DST_NIL)) {
            bucket[1] = value;
        } else {
            if (NULL == bucket || 4 * (t->count + t->deleted) >= t->capacity) {
                dst_table_rehash(t, 4 * t->count + 6);
            }
            bucket = dst_table_find(t, key);
            if (dst_checktype(bucket[1], DST_FALSE))
                --t->deleted;
            bucket[0] = key;
            bucket[1] = value;
            ++t->count;
        }
    }
}

/* Clear a table */
void dst_table_clear(DstTable *t) {
    int32_t capacity = t->capacity;
    DstValue *data = t->data;
    dst_memempty(data, capacity);
    t->count = 0;
    t->deleted = 0;
}

/* Find next key in an object. Returns nil if no next key. */
DstValue dst_table_next(DstTable *t, DstValue key) {
    const DstValue *bucket, *end;
    end = t->data + t->capacity;
    if (dst_checktype(key, DST_NIL)) {
        bucket = t->data;
    } else {
        bucket = dst_table_find(t, key);
        if (NULL == bucket || dst_checktype(bucket[0], DST_NIL))
            return dst_wrap_nil();
        bucket += 2;
    }
    for (; bucket < end; bucket += 2) {
        if (!dst_checktype(bucket[0], DST_NIL))
            return bucket[0];
    }
    return dst_wrap_nil();
}

/* Convert table to struct */
const DstValue *dst_table_to_struct(DstTable *t) {
    int32_t i;
    DstValue *st = dst_struct_begin(t->count);
    for (i = 0; i < t->capacity; i += 2) {
        if (!dst_checktype(t->data[i], DST_NIL)) {
            dst_struct_put(st, t->data[i], t->data[i + 1]);
        }
    }
    return dst_struct_end(st);
}

/* Merge a struct or another table into a table. */
void dst_table_merge(DstTable *t,  DstValue other) {
    int32_t count, cap, i;
    const DstValue *hmap;
    if (dst_hashtable_view(other, &hmap, &count, &cap)) {
        for (i = 0; i < cap; i += 2) {
            if (!dst_checktype(hmap[i], DST_NIL)) {
                dst_table_put(t, hmap[i], hmap[i + 1]);
            }
        }
    }
}

#undef dst_table_maphash

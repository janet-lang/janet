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
#include "wrap.h"
#include "gc.h"

/* Initialize a table */
DstTable *dst_table(Dst *vm, uint32_t capacity) {
    DstTable *table = dst_alloc(vm, DST_MEMORY_TABLE, sizeof(DstTable));
    DstValue *data = calloc(sizeof(DstValue), capacity);
    if (capacity < 2) capacity = 2;
    if (NULL == data) {
        DST_OUT_OF_MEMORY;
    }
    table->data = data;
    table->capacity = capacity;
    table->count = 0;
    table->deleted = 0;
    return table;
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in the table. */
static DstValue *dst_table_find(DstTable *t, DstValue key) {
    uint32_t index = (dst_hash(key) % (t->capacity / 2)) * 2;
    uint32_t i, j;
    uint32_t start[2], end[2];
    start[0] = index; end[0] = t->capacity;
    start[1] = 0; end[1] = index;
    for (j = 0; j < 2; ++j) {
        for (i = start[j]; i < end[j]; i += 2) {
            if (t->data[i].type == DST_NIL) {
                if (t->data[i + 1].type == DST_NIL) {
                    /* Empty */
                    return t->data + i;
                }
            } else if (dst_equals(t->data[i], key)) {
                return t->data + i;
            }
        }
    }
    return NULL;
}

/* Resize the dictionary table. */
static void dst_table_rehash(Dst *vm, DstTable *t, uint32_t size) {
    DstValue *olddata = t->data;
    DstValue *newdata = calloc(sizeof(DstValue), size);
    if (NULL == newdata) {
        DST_OUT_OF_MEMORY;
    }
    uint32_t i, oldcapacity;
    oldcapacity = t->capacity;
    t->data = newdata;
    t->capacity = size;
    t->deleted = 0;
    for (i = 0; i < oldcapacity; i += 2) {
        if (olddata[i].type != DST_NIL) {
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
    if (bucket && bucket[0].type != DST_NIL)
        return bucket[1];
    else
        return dst_wrap_nil();
}

/* Remove an entry from the dictionary. Return the value that
 * was removed. */
DstValue dst_table_remove(DstTable *t, DstValue key) {
    DstValue *bucket = dst_table_find(t, key);
    if (bucket && bucket[0].type != DST_NIL) {
        DstValue ret = bucket[1];
        t->count--;
        t->deleted++;
        bucket[0].type = DST_NIL;
        bucket[1].type = DST_BOOLEAN;
        return ret;
    } else {
        return dst_wrap_nil();
    }
}

/* Put a value into the object */
void dst_table_put(Dst *vm, DstTable *t, DstValue key, DstValue value) {
    if (key.type == DST_NIL) return;
    if (value.type == DST_NIL) {
        dst_table_remove(t, key);
    } else {
        DstValue *bucket = dst_table_find(t, key);
        if (bucket && bucket[0].type != DST_NIL) {
            bucket[1] = value;
        } else {
            if (!bucket || 4 * (t->count + t->deleted) >= t->capacity) {
                dst_table_rehash(vm, t, 4 * t->count + 6);
            }
            bucket = dst_table_find(t, key);
            if (bucket[1].type == DST_BOOLEAN)
                --t->deleted;
            bucket[0] = key;
            bucket[1] = value;
            ++t->count;
        }
    }
}

/* Clear a table */
void dst_table_clear(DstTable *t) {
    uint32_t capacity = t->capacity;
    uint32_t i;
    DstValue *data = t->data;
    for (i = 0; i < capacity; i += 2)
        data[i].type = DST_NIL;
    t->count = 0;
    t->deleted = 0;
}

/* Find next key in an object. Returns nil if no next key. */
DstValue dst_table_next(DstTable *t, DstValue key) {
    const DstValue *bucket, *end;
    end = t->data + t->capacity;
    if (key.type == DST_NIL) {
        bucket = t->data;
    } else {
        bucket = dst_table_find(t, key);
        if (!bucket || bucket[0].type == DST_NIL)
            return dst_wrap_nil();
        bucket += 2;
    }
    for (; bucket < end; bucket += 2) {
        if (bucket[0].type != DST_NIL)
            return bucket[0];
    }
    return dst_wrap_nil();
}

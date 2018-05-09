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
#include "util.h"

#define dst_table_maphash(cap, hash) ((uint32_t)(hash) & (cap - 1))

/* Initialize a table */
DstTable *dst_table_init(DstTable *table, int32_t capacity) {
    DstKV *data;
    capacity = dst_tablen(capacity);
    if (capacity) {
        data = (DstKV *) dst_memalloc_empty(capacity);
        if (NULL == data) {
            DST_OUT_OF_MEMORY;
        }
        table->data = data;
        table->capacity = capacity;
    } else {
        table->data = NULL;
        table->capacity = 0;
    }
    table->count = 0;
    table->deleted = 0;
    table->proto = NULL;
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
DstKV *dst_table_find(DstTable *t, Dst key) {
    int32_t index = dst_table_maphash(t->capacity, dst_hash(key));
    int32_t i;
    DstKV *first_bucket = NULL;
    /* Higher half */
    for (i = index; i < t->capacity; i++) {
        DstKV *kv = t->data + i;
        if (dst_checktype(kv->key, DST_NIL)) {
            if (dst_checktype(kv->value, DST_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (dst_equals(kv->key, key)) {
            return t->data + i;
        }
    }
    /* Lower half */
    for (i = 0; i < index; i++) {
        DstKV *kv = t->data + i;
        if (dst_checktype(kv->key, DST_NIL)) {
            if (dst_checktype(kv->value, DST_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (dst_equals(kv->key, key)) {
            return t->data + i;
        }
    }
    return first_bucket;
}

/* Resize the dictionary table. */
static void dst_table_rehash(DstTable *t, int32_t size) {
    DstKV *olddata = t->data;
    DstKV *newdata = (DstKV *) dst_memalloc_empty(size);
    if (NULL == newdata) {
        DST_OUT_OF_MEMORY;
    }
    int32_t i, oldcapacity;
    oldcapacity = t->capacity;
    t->data = newdata;
    t->capacity = size;
    t->deleted = 0;
    for (i = 0; i < oldcapacity; i++) {
        DstKV *kv = olddata + i;
        if (!dst_checktype(kv->key, DST_NIL)) {
            DstKV *newkv = dst_table_find(t, kv->key);
            *newkv = *kv;
        }
    }
    free(olddata);
}

/* Get a value out of the table */
Dst dst_table_get(DstTable *t, Dst key) {
    DstKV *bucket = dst_table_find(t, key);
    if (NULL != bucket && !dst_checktype(bucket->key, DST_NIL))
        return bucket->value;
    /* Check prototypes */
    {
        int i;
        for (i = DST_MAX_PROTO_DEPTH, t = t->proto; t && i; t = t->proto, --i) {
            bucket = dst_table_find(t, key);
            if (NULL != bucket && !dst_checktype(bucket->key, DST_NIL))
                return bucket->value;
        }
    }
    return dst_wrap_nil();
}

/* Get a value out of the table. Don't check prototype tables. */
Dst dst_table_rawget(DstTable *t, Dst key) {
    DstKV *bucket = dst_table_find(t, key);
    if (NULL != bucket && !dst_checktype(bucket->key, DST_NIL))
        return bucket->value;
    else
        return dst_wrap_nil();
}

/* Remove an entry from the dictionary. Return the value that
 * was removed. */
Dst dst_table_remove(DstTable *t, Dst key) {
    DstKV *bucket = dst_table_find(t, key);
    if (NULL != bucket && !dst_checktype(bucket->key, DST_NIL)) {
        Dst ret = bucket->key;
        t->count--;
        t->deleted++;
        bucket->key = dst_wrap_nil();
        bucket->value = dst_wrap_false();
        return ret;
    } else {
        return dst_wrap_nil();
    }
}

/* Put a value into the object */
void dst_table_put(DstTable *t, Dst key, Dst value) {
    if (dst_checktype(key, DST_NIL)) return;
    if (dst_checktype(value, DST_NIL)) {
        dst_table_remove(t, key);
    } else {
        DstKV *bucket = dst_table_find(t, key);
        if (NULL != bucket && !dst_checktype(bucket->key, DST_NIL)) {
            bucket->value = value;
        } else {
            if (NULL == bucket || 2 * (t->count + t->deleted + 1) > t->capacity) {
                dst_table_rehash(t, dst_tablen(2 * t->count + 2));
            }
            bucket = dst_table_find(t, key);
            if (dst_checktype(bucket->value, DST_FALSE))
                --t->deleted;
            bucket->key = key;
            bucket->value = value;
            ++t->count;
        }
    }
}

/* Clear a table */
void dst_table_clear(DstTable *t) {
    int32_t capacity = t->capacity;
    DstKV *data = t->data;
    dst_memempty(data, capacity);
    t->count = 0;
    t->deleted = 0;
}

/* Find next key in an object. Returns NULL if no next key. */
const DstKV *dst_table_next(DstTable *t, const DstKV *kv) {
    DstKV *end = t->data + t->capacity;
    kv = (kv == NULL) ? t->data : kv + 1;
    while (kv < end) {
        if (!dst_checktype(kv->key, DST_NIL))
            return kv;
        kv++;
    }
    return NULL;
}

/* Convert table to struct */
const DstKV *dst_table_to_struct(DstTable *t) {
    DstKV *st = dst_struct_begin(t->count);
    DstKV *kv = t->data;
    DstKV *end = t->data + t->capacity;
    while (kv < end) {
        if (!dst_checktype(kv->key, DST_NIL))
            dst_struct_put(st, kv->key, kv->value);
        kv++;
    }
    return dst_struct_end(st);
}

/* Merge a table or struct into a table */
static void dst_table_mergekv(DstTable *table, const DstKV *kvs, int32_t cap) {
    int32_t i;
    for (i = 0; i < cap; i++) {
        const DstKV *kv = kvs + i;
        if (!dst_checktype(kv->key, DST_NIL)) {
            dst_table_put(table, kv->key, kv->value);
        }
    }
}

/* Merge a table other into another table */
void dst_table_merge_table(DstTable *table, DstTable *other) {
    dst_table_mergekv(table, other->data, other->capacity);
}

/* Merge a struct into a table */
void dst_table_merge_struct(DstTable *table, const DstKV *other) {
    dst_table_mergekv(table, other, dst_struct_capacity(other));
}

/* C Functions */

static int cfun_new(DstArgs args) {
    DstTable *t;
    int32_t cap;
    dst_fixarity(args, 1);
    dst_arg_integer(cap, args, 0);
    t = dst_table(cap);
    return dst_return(args, dst_wrap_table(t));
}

static int cfun_getproto(DstArgs args) {
    DstTable *t;
    dst_fixarity(args, 1);
    dst_check(args, 0, DST_TABLE);
    t = dst_unwrap_table(args.v[0]);
    return dst_return(args, t->proto
            ? dst_wrap_table(t->proto)
            : dst_wrap_nil());
}

static int cfun_setproto(DstArgs args) {
    dst_fixarity(args, 2);
    dst_check(args, 0, DST_TABLE);
    dst_checkmany(args, 1, DST_TFLAG_TABLE | DST_TFLAG_NIL);
    dst_unwrap_table(args.v[0])->proto = dst_checktype(args.v[1], DST_TABLE)
        ? dst_unwrap_table(args.v[1])
        : NULL;
    return dst_return(args, args.v[0]);
}

static int cfun_tostruct(DstArgs args) {
    DstTable *t;
    dst_fixarity(args, 1);
    dst_arg_table(t, args, 0);
    return dst_return(args, dst_wrap_struct(dst_table_to_struct(t)));
}

static int cfun_rawget(DstArgs args) {
    dst_fixarity(args, 2);
    dst_check(args, 0, DST_TABLE);
    return dst_return(args, dst_table_rawget(dst_unwrap_table(args.v[0]), args.v[1]));
}

static const DstReg cfuns[] = {
    {"table.new", cfun_new},
    {"table.to-struct", cfun_tostruct},
    {"table.getproto", cfun_getproto},
    {"table.setproto", cfun_setproto},
    {"table.rawget", cfun_rawget},
    {NULL, NULL}
};

/* Load the table module */
int dst_lib_table(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

#undef dst_table_maphash

/*
* Copyright (c) 2024 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "gc.h"
#include "util.h"
#include <math.h>
#endif

#define JANET_TABLE_FLAG_STACK 0x10000

static void *janet_memalloc_empty_local(int32_t count) {
    int32_t i;
    void *mem = janet_smalloc((size_t) count * sizeof(JanetKV));
    JanetKV *mmem = (JanetKV *)mem;
    for (i = 0; i < count; i++) {
        JanetKV *kv = mmem + i;
        kv->key = janet_wrap_nil();
        kv->value = janet_wrap_nil();
    }
    return mem;
}

static JanetTable *janet_table_init_impl(JanetTable *table, int32_t capacity, int stackalloc) {
    JanetKV *data;
    capacity = janet_tablen(capacity);
    if (stackalloc) table->gc.flags = JANET_TABLE_FLAG_STACK;
    if (capacity) {
        if (stackalloc) {
            data = janet_memalloc_empty_local(capacity);
        } else {
            data = (JanetKV *) janet_memalloc_empty(capacity);
            if (NULL == data) {
                JANET_OUT_OF_MEMORY;
            }
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

/* Initialize a table (for use with scratch memory) */
JanetTable *janet_table_init(JanetTable *table, int32_t capacity) {
    return janet_table_init_impl(table, capacity, 1);
}

/* Initialize a table without using scratch memory */
JanetTable *janet_table_init_raw(JanetTable *table, int32_t capacity) {
    return janet_table_init_impl(table, capacity, 0);
}

/* Deinitialize a table */
void janet_table_deinit(JanetTable *table) {
    if (table->gc.flags & JANET_TABLE_FLAG_STACK) {
        janet_sfree(table->data);
    } else {
        janet_free(table->data);
    }
}

/* Create a new table */

JanetTable *janet_table(int32_t capacity) {
    JanetTable *table = janet_gcalloc(JANET_MEMORY_TABLE, sizeof(JanetTable));
    return janet_table_init_impl(table, capacity, 0);
}

JanetTable *janet_table_weakk(int32_t capacity) {
    JanetTable *table = janet_gcalloc(JANET_MEMORY_TABLE_WEAKK, sizeof(JanetTable));
    return janet_table_init_impl(table, capacity, 0);
}

JanetTable *janet_table_weakv(int32_t capacity) {
    JanetTable *table = janet_gcalloc(JANET_MEMORY_TABLE_WEAKV, sizeof(JanetTable));
    return janet_table_init_impl(table, capacity, 0);
}

JanetTable *janet_table_weakkv(int32_t capacity) {
    JanetTable *table = janet_gcalloc(JANET_MEMORY_TABLE_WEAKKV, sizeof(JanetTable));
    return janet_table_init_impl(table, capacity, 0);
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in the table. */
JanetKV *janet_table_find(JanetTable *t, Janet key) {
    return (JanetKV *) janet_dict_find(t->data, t->capacity, key);
}

/* Resize the dictionary table. */
static void janet_table_rehash(JanetTable *t, int32_t size) {
    JanetKV *olddata = t->data;
    JanetKV *newdata;
    int islocal = t->gc.flags & JANET_TABLE_FLAG_STACK;
    if (islocal) {
        newdata = (JanetKV *) janet_memalloc_empty_local(size);
    } else {
        newdata = (JanetKV *) janet_memalloc_empty(size);
        if (NULL == newdata) {
            JANET_OUT_OF_MEMORY;
        }
    }
    int32_t oldcapacity = t->capacity;
    t->data = newdata;
    t->capacity = size;
    t->deleted = 0;
    for (int32_t i = 0; i < oldcapacity; i++) {
        JanetKV *kv = olddata + i;
        if (!janet_checktype(kv->key, JANET_NIL)) {
            JanetKV *newkv = janet_table_find(t, kv->key);
            *newkv = *kv;
        }
    }
    if (islocal) {
        janet_sfree(olddata);
    } else {
        janet_free(olddata);
    }
}

/* Get a value out of the table */
Janet janet_table_get(JanetTable *t, Janet key) {
    for (int i = JANET_MAX_PROTO_DEPTH; t && i; t = t->proto, --i) {
        JanetKV *bucket = janet_table_find(t, key);
        if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL))
            return bucket->value;
    }
    return janet_wrap_nil();
}

/* Get a value out of the table, and record which prototype it was from. */
Janet janet_table_get_ex(JanetTable *t, Janet key, JanetTable **which) {
    for (int i = JANET_MAX_PROTO_DEPTH; t && i; t = t->proto, --i) {
        JanetKV *bucket = janet_table_find(t, key);
        if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL)) {
            *which = t;
            return bucket->value;
        }
    }
    return janet_wrap_nil();
}

/* Get a value out of the table. Don't check prototype tables. */
Janet janet_table_rawget(JanetTable *t, Janet key) {
    JanetKV *bucket = janet_table_find(t, key);
    if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL))
        return bucket->value;
    else
        return janet_wrap_nil();
}

/* Remove an entry from the dictionary. Return the value that
 * was removed. */
Janet janet_table_remove(JanetTable *t, Janet key) {
    JanetKV *bucket = janet_table_find(t, key);
    if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL)) {
        Janet ret = bucket->value;
        t->count--;
        t->deleted++;
        bucket->key = janet_wrap_nil();
        bucket->value = janet_wrap_false();
        return ret;
    } else {
        return janet_wrap_nil();
    }
}

/* Put a value into the object */
void janet_table_put(JanetTable *t, Janet key, Janet value) {
    if (janet_checktype(key, JANET_NIL)) return;
    if (janet_checktype(key, JANET_NUMBER) && isnan(janet_unwrap_number(key))) return;
    if (janet_checktype(value, JANET_NIL)) {
        janet_table_remove(t, key);
    } else {
        JanetKV *bucket = janet_table_find(t, key);
        if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL)) {
            bucket->value = value;
        } else {
            if (NULL == bucket || 2 * (t->count + t->deleted + 1) > t->capacity) {
                janet_table_rehash(t, janet_tablen(2 * t->count + 2));
            }
            bucket = janet_table_find(t, key);
            if (janet_checktype(bucket->value, JANET_BOOLEAN))
                --t->deleted;
            bucket->key = key;
            bucket->value = value;
            ++t->count;
        }
    }
}

/* Used internally so don't check arguments
 * Put into a table, but if the key already exists do nothing. */
static void janet_table_put_no_overwrite(JanetTable *t, Janet key, Janet value) {
    JanetKV *bucket = janet_table_find(t, key);
    if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL))
        return;
    if (NULL == bucket || 2 * (t->count + t->deleted + 1) > t->capacity) {
        janet_table_rehash(t, janet_tablen(2 * t->count + 2));
    }
    bucket = janet_table_find(t, key);
    if (janet_checktype(bucket->value, JANET_BOOLEAN))
        --t->deleted;
    bucket->key = key;
    bucket->value = value;
    ++t->count;
}

/* Clear a table */
void janet_table_clear(JanetTable *t) {
    int32_t capacity = t->capacity;
    JanetKV *data = t->data;
    janet_memempty(data, capacity);
    t->count = 0;
    t->deleted = 0;
}

/* Clone a table. */
JanetTable *janet_table_clone(JanetTable *table) {
    JanetTable *newTable = janet_gcalloc(JANET_MEMORY_TABLE, sizeof(JanetTable));
    newTable->count = table->count;
    newTable->capacity = table->capacity;
    newTable->deleted = table->deleted;
    newTable->proto = table->proto;
    newTable->data = janet_malloc(newTable->capacity * sizeof(JanetKV));
    if (NULL == newTable->data) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(newTable->data, table->data, (size_t) table->capacity * sizeof(JanetKV));
    return newTable;
}

/* Merge a table or struct into a table */
static void janet_table_mergekv(JanetTable *table, const JanetKV *kvs, int32_t cap) {
    int32_t i;
    for (i = 0; i < cap; i++) {
        const JanetKV *kv = kvs + i;
        if (!janet_checktype(kv->key, JANET_NIL)) {
            janet_table_put(table, kv->key, kv->value);
        }
    }
}

/* Merge a table into another table */
void janet_table_merge_table(JanetTable *table, JanetTable *other) {
    janet_table_mergekv(table, other->data, other->capacity);
}

/* Merge a struct into a table */
void janet_table_merge_struct(JanetTable *table, const JanetKV *other) {
    janet_table_mergekv(table, other, janet_struct_capacity(other));
}

/* Convert table to struct */
const JanetKV *janet_table_to_struct(JanetTable *t) {
    JanetKV *st = janet_struct_begin(t->count);
    JanetKV *kv = t->data;
    JanetKV *end = t->data + t->capacity;
    while (kv < end) {
        if (!janet_checktype(kv->key, JANET_NIL))
            janet_struct_put(st, kv->key, kv->value);
        kv++;
    }
    return janet_struct_end(st);
}

JanetTable *janet_table_proto_flatten(JanetTable *t) {
    JanetTable *newTable = janet_table(0);
    while (t) {
        JanetKV *kv = t->data;
        JanetKV *end = t->data + t->capacity;
        while (kv < end) {
            if (!janet_checktype(kv->key, JANET_NIL))
                janet_table_put_no_overwrite(newTable, kv->key, kv->value);
            kv++;
        }
        t = t->proto;
    }
    return newTable;
}

/* C Functions */

JANET_CORE_FN(cfun_table_new,
              "(table/new capacity)",
              "Creates a new empty table with pre-allocated memory "
              "for `capacity` entries. This means that if one knows the number of "
              "entries going into a table on creation, extra memory allocation "
              "can be avoided. "
              "Returns the new table.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getnat(argv, 0);
    return janet_wrap_table(janet_table(cap));
}

JANET_CORE_FN(cfun_table_weak,
              "(table/weak capacity)",
              "Creates a new empty table with weak references to keys and values. Similar to `table/new`. "
              "Returns the new table.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getnat(argv, 0);
    return janet_wrap_table(janet_table_weakkv(cap));
}

JANET_CORE_FN(cfun_table_weak_keys,
              "(table/weak-keys capacity)",
              "Creates a new empty table with weak references to keys and normal references to values. Similar to `table/new`. "
              "Returns the new table.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getnat(argv, 0);
    return janet_wrap_table(janet_table_weakk(cap));
}

JANET_CORE_FN(cfun_table_weak_values,
              "(table/weak-values capacity)",
              "Creates a new empty table with normal references to keys and weak references to values. Similar to `table/new`. "
              "Returns the new table.") {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getnat(argv, 0);
    return janet_wrap_table(janet_table_weakv(cap));
}

JANET_CORE_FN(cfun_table_getproto,
              "(table/getproto tab)",
              "Get the prototype table of a table. Returns nil if the table "
              "has no prototype, otherwise returns the prototype.") {
    janet_fixarity(argc, 1);
    JanetTable *t = janet_gettable(argv, 0);
    return t->proto
           ? janet_wrap_table(t->proto)
           : janet_wrap_nil();
}

JANET_CORE_FN(cfun_table_setproto,
              "(table/setproto tab proto)",
              "Set the prototype of a table. Returns the original table `tab`.") {
    janet_fixarity(argc, 2);
    JanetTable *table = janet_gettable(argv, 0);
    JanetTable *proto = NULL;
    if (!janet_checktype(argv[1], JANET_NIL)) {
        proto = janet_gettable(argv, 1);
    }
    table->proto = proto;
    return argv[0];
}

JANET_CORE_FN(cfun_table_tostruct,
              "(table/to-struct tab &opt proto)",
              "Convert a table to a struct. Returns a new struct.") {
    janet_arity(argc, 1, 2);
    JanetTable *t = janet_gettable(argv, 0);
    JanetStruct proto = janet_optstruct(argv, argc, 1, NULL);
    JanetStruct st = janet_table_to_struct(t);
    janet_struct_proto(st) = proto;
    return janet_wrap_struct(st);
}

JANET_CORE_FN(cfun_table_rawget,
              "(table/rawget tab key)",
              "Gets a value from a table `tab` without looking at the prototype table. "
              "If `tab` does not contain the key directly, the function will return "
              "nil without checking the prototype. Returns the value in the table.") {
    janet_fixarity(argc, 2);
    JanetTable *table = janet_gettable(argv, 0);
    return janet_table_rawget(table, argv[1]);
}

JANET_CORE_FN(cfun_table_clone,
              "(table/clone tab)",
              "Create a copy of a table. Updates to the new table will not change the old table, "
              "and vice versa.") {
    janet_fixarity(argc, 1);
    JanetTable *table = janet_gettable(argv, 0);
    return janet_wrap_table(janet_table_clone(table));
}

JANET_CORE_FN(cfun_table_clear,
              "(table/clear tab)",
              "Remove all key-value pairs in a table and return the modified table `tab`.") {
    janet_fixarity(argc, 1);
    JanetTable *table = janet_gettable(argv, 0);
    janet_table_clear(table);
    return janet_wrap_table(table);
}

JANET_CORE_FN(cfun_table_proto_flatten,
              "(table/proto-flatten tab)",
              "Create a new table that is the result of merging all prototypes into a new table.") {
    janet_fixarity(argc, 1);
    JanetTable *table = janet_gettable(argv, 0);
    return janet_wrap_table(janet_table_proto_flatten(table));
}

/* Load the table module */
void janet_lib_table(JanetTable *env) {
    JanetRegExt table_cfuns[] = {
        JANET_CORE_REG("table/new", cfun_table_new),
        JANET_CORE_REG("table/weak", cfun_table_weak),
        JANET_CORE_REG("table/weak-keys", cfun_table_weak_keys),
        JANET_CORE_REG("table/weak-values", cfun_table_weak_values),
        JANET_CORE_REG("table/to-struct", cfun_table_tostruct),
        JANET_CORE_REG("table/getproto", cfun_table_getproto),
        JANET_CORE_REG("table/setproto", cfun_table_setproto),
        JANET_CORE_REG("table/rawget", cfun_table_rawget),
        JANET_CORE_REG("table/clone", cfun_table_clone),
        JANET_CORE_REG("table/clear", cfun_table_clear),
        JANET_CORE_REG("table/proto-flatten", cfun_table_proto_flatten),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, table_cfuns);
}

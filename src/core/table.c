/*
* Copyright (c) 2019 Calvin Rose
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

#include <janet/janet.h>
#include "gc.h"
#include "util.h"

/* Initialize a table */
JanetTable *janet_table_init(JanetTable *table, int32_t capacity) {
    JanetKV *data;
    capacity = janet_tablen(capacity);
    if (capacity) {
        data = (JanetKV *) janet_memalloc_empty(capacity);
        if (NULL == data) {
            JANET_OUT_OF_MEMORY;
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
void janet_table_deinit(JanetTable *table) {
    free(table->data);
}

/* Create a new table */
JanetTable *janet_table(int32_t capacity) {
    JanetTable *table = janet_gcalloc(JANET_MEMORY_TABLE, sizeof(JanetTable));
    return janet_table_init(table, capacity);
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in the table. */
JanetKV *janet_table_find(JanetTable *t, Janet key) {
    return (JanetKV *) janet_dict_find(t->data, t->capacity, key);
}

/* Resize the dictionary table. */
static void janet_table_rehash(JanetTable *t, int32_t size) {
    JanetKV *olddata = t->data;
    JanetKV *newdata = (JanetKV *) janet_memalloc_empty(size);
    if (NULL == newdata) {
        JANET_OUT_OF_MEMORY;
    }
    int32_t i, oldcapacity;
    oldcapacity = t->capacity;
    t->data = newdata;
    t->capacity = size;
    t->deleted = 0;
    for (i = 0; i < oldcapacity; i++) {
        JanetKV *kv = olddata + i;
        if (!janet_checktype(kv->key, JANET_NIL)) {
            JanetKV *newkv = janet_table_find(t, kv->key);
            *newkv = *kv;
        }
    }
    free(olddata);
}

/* Get a value out of the table */
Janet janet_table_get(JanetTable *t, Janet key) {
    JanetKV *bucket = janet_table_find(t, key);
    if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL))
        return bucket->value;
    /* Check prototypes */
    {
        int i;
        for (i = JANET_MAX_PROTO_DEPTH, t = t->proto; t && i; t = t->proto, --i) {
            bucket = janet_table_find(t, key);
            if (NULL != bucket && !janet_checktype(bucket->key, JANET_NIL))
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
        Janet ret = bucket->key;
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
            if (janet_checktype(bucket->value, JANET_FALSE))
                --t->deleted;
            bucket->key = key;
            bucket->value = value;
            ++t->count;
        }
    }
}

/* Clear a table */
void janet_table_clear(JanetTable *t) {
    int32_t capacity = t->capacity;
    JanetKV *data = t->data;
    janet_memempty(data, capacity);
    t->count = 0;
    t->deleted = 0;
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

/* Merge a table other into another table */
void janet_table_merge_table(JanetTable *table, JanetTable *other) {
    janet_table_mergekv(table, other->data, other->capacity);
}

/* Merge a struct into a table */
void janet_table_merge_struct(JanetTable *table, const JanetKV *other) {
    janet_table_mergekv(table, other, janet_struct_capacity(other));
}

/* C Functions */

static Janet cfun_new(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int32_t cap = janet_getinteger(argv, 0);
    return janet_wrap_table(janet_table(cap));
}

static Janet cfun_getproto(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTable *t = janet_gettable(argv, 0);
    return t->proto
        ? janet_wrap_table(t->proto)
        : janet_wrap_nil();
}

static Janet cfun_setproto(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetTable *table = janet_gettable(argv, 0);
    JanetTable *proto = NULL;
    if (!janet_checktype(argv[1], JANET_NIL)) {
        proto = janet_gettable(argv, 1);
    }
    table->proto = proto;
    return argv[0];
}

static Janet cfun_tostruct(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTable *t = janet_gettable(argv, 0);
    return janet_wrap_struct(janet_table_to_struct(t));
}

static Janet cfun_rawget(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetTable *table = janet_gettable(argv, 0);
    return janet_table_rawget(table, argv[1]);
}

static const JanetReg cfuns[] = {
    {
        "table/new", cfun_new,
        JDOC("(table/new capacity)\n\n"
                "Creates a new empty table with pre-allocated memory "
                "for capacity entries. This means that if one knows the number of "
                "entries going to go in a table on creation, extra memory allocation "
                "can be avoided. Returns the new table.")
    },
    {
        "table/to-struct", cfun_tostruct,
        JDOC("(table/to-struct tab)\n\n"
                "Convert a table to a struct. Returns a new struct. This function "
                "does not take into account prototype tables.")
    },
    {
        "table/getproto", cfun_getproto,
        JDOC("(table/getproto tab)\n\n"
                "Get the prototype table of a table. Returns nil if a table "
                "has no prototype, otherwise returns the prototype.")
    },
    {
        "table/setproto", cfun_setproto,
        JDOC("(table/setproto tab proto)\n\n"
                "Set the prototype of a table. Returns the original table tab.")
    },
    {
        "table/rawget", cfun_rawget,
        JDOC("(table/rawget tab key)\n\n"
                "Gets a value from a table without looking at the prototype table. "
                "If a table tab does not contain t directly, the function will return "
                "nil without checking the prototype. Returns the value in the table.")
    },
    {NULL, NULL, NULL}
};

/* Load the table module */
void janet_lib_table(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
}

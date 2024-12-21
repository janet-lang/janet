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

/* Begin creation of a struct */
JanetKV *janet_struct_begin(int32_t count) {
    /* Calculate capacity as power of 2 after 2 * count. */
    int32_t capacity = janet_tablen(2 * count);
    if (capacity < 0) capacity = janet_tablen(count + 1);

    size_t size = sizeof(JanetStructHead) + (size_t) capacity * sizeof(JanetKV);
    JanetStructHead *head = janet_gcalloc(JANET_MEMORY_STRUCT, size);
    head->length = count;
    head->capacity = capacity;
    head->hash = 0;
    head->proto = NULL;

    JanetKV *st = (JanetKV *)(head->data);
    janet_memempty(st, capacity);
    return st;
}

/* Find an item in a struct without looking for prototypes. Should be similar to janet_dict_find, but
 * specialized to structs (slightly more compact). */
const JanetKV *janet_struct_find(const JanetKV *st, Janet key) {
    int32_t cap = janet_struct_capacity(st);
    int32_t index = janet_maphash(cap, janet_hash(key));
    int32_t i;
    for (i = index; i < cap; i++)
        if (janet_checktype(st[i].key, JANET_NIL) || janet_equals(st[i].key, key))
            return st + i;
    for (i = 0; i < index; i++)
        if (janet_checktype(st[i].key, JANET_NIL) || janet_equals(st[i].key, key))
            return st + i;
    return NULL;
}

/* Put a kv pair into a struct that has not yet been fully constructed.
 * Nil keys and values are ignored, extra keys are ignore, and duplicate keys are
 * ignored.
 *
 * Runs will be in sorted order, as the collisions resolver essentially
 * preforms an in-place insertion sort. This ensures the internal structure of the
 * hash map is independent of insertion order.
 */
void janet_struct_put_ext(JanetKV *st, Janet key, Janet value, int replace) {
    int32_t cap = janet_struct_capacity(st);
    int32_t hash = janet_hash(key);
    int32_t index = janet_maphash(cap, hash);
    int32_t i, j, dist;
    int32_t bounds[4] = {index, cap, 0, index};
    if (janet_checktype(key, JANET_NIL) || janet_checktype(value, JANET_NIL)) return;
    if (janet_checktype(key, JANET_NUMBER) && isnan(janet_unwrap_number(key))) return;
    /* Avoid extra items */
    if (janet_struct_hash(st) == janet_struct_length(st)) return;
    for (dist = 0, j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j + 1]; i++, dist++) {
            int status;
            int32_t otherhash;
            int32_t otherindex, otherdist;
            JanetKV *kv = st + i;
            /* We found an empty slot, so just add key and value */
            if (janet_checktype(kv->key, JANET_NIL)) {
                kv->key = key;
                kv->value = value;
                /* Update the temporary count */
                janet_struct_hash(st)++;
                return;
            }
            /* Robinhood hashing - check if colliding kv pair
             * is closer to their source than current. We use robinhood
             * hashing to ensure that equivalent structs that are constructed
             * with different order have the same internal layout, and therefor
             * will compare properly - i.e., {1 2 3 4} should equal {3 4 1 2}.
             * Collisions are resolved via an insertion sort insertion. */
            otherhash = janet_hash(kv->key);
            otherindex = janet_maphash(cap, otherhash);
            otherdist = (i + cap - otherindex) & (cap - 1);
            if (dist < otherdist)
                status = -1;
            else if (otherdist < dist)
                status = 1;
            else if (hash < otherhash)
                status = -1;
            else if (otherhash < hash)
                status = 1;
            else
                status = janet_compare(key, kv->key);
            /* If other is closer to their ideal slot */
            if (status == 1) {
                /* Swap current kv pair with pair in slot */
                JanetKV temp = *kv;
                kv->key = key;
                kv->value = value;
                key = temp.key;
                value = temp.value;
                /* Save dist and hash of new kv pair */
                dist = otherdist;
                hash = otherhash;
            } else if (status == 0) {
                if (replace) {
                    /* A key was added to the struct more than once - replace old value */
                    kv->value = value;
                }
                return;
            }
        }
}

void janet_struct_put(JanetKV *st, Janet key, Janet value) {
    janet_struct_put_ext(st, key, value, 1);
}

/* Finish building a struct */
const JanetKV *janet_struct_end(JanetKV *st) {
    if (janet_struct_hash(st) != janet_struct_length(st)) {
        /* Error building struct, probably duplicate values. We need to rebuild
         * the struct using only the values that went in. The second creation should always
         * succeed. */
        JanetKV *newst = janet_struct_begin(janet_struct_hash(st));
        for (int32_t i = 0; i < janet_struct_capacity(st); i++) {
            JanetKV *kv = st + i;
            if (!janet_checktype(kv->key, JANET_NIL)) {
                janet_struct_put(newst, kv->key, kv->value);
            }
        }
        janet_struct_proto(newst) = janet_struct_proto(st);
        st = newst;
    }
    janet_struct_hash(st) = janet_kv_calchash(st, janet_struct_capacity(st));
    if (janet_struct_proto(st)) {
        janet_struct_hash(st) += 2654435761u * janet_struct_hash(janet_struct_proto(st));
    }
    return (const JanetKV *)st;
}

/* Get an item from a struct without looking into prototypes. */
Janet janet_struct_rawget(const JanetKV *st, Janet key) {
    const JanetKV *kv = janet_struct_find(st, key);
    return kv ? kv->value : janet_wrap_nil();
}

/* Get an item from a struct */
Janet janet_struct_get(const JanetKV *st, Janet key) {
    for (int i = JANET_MAX_PROTO_DEPTH; st && i; --i, st = janet_struct_proto(st)) {
        const JanetKV *kv = janet_struct_find(st, key);
        if (NULL != kv && !janet_checktype(kv->key, JANET_NIL)) {
            return kv->value;
        }
    }
    return janet_wrap_nil();
}

/* Get an item from a struct, and record which prototype the item came from. */
Janet janet_struct_get_ex(const JanetKV *st, Janet key, JanetStruct *which) {
    for (int i = JANET_MAX_PROTO_DEPTH; st && i; --i, st = janet_struct_proto(st)) {
        const JanetKV *kv = janet_struct_find(st, key);
        if (NULL != kv && !janet_checktype(kv->key, JANET_NIL)) {
            *which = st;
            return kv->value;
        }
    }
    return janet_wrap_nil();
}

/* Convert struct to table */
JanetTable *janet_struct_to_table(const JanetKV *st) {
    JanetTable *table = janet_table(janet_struct_capacity(st));
    int32_t i;
    for (i = 0; i < janet_struct_capacity(st); i++) {
        const JanetKV *kv = st + i;
        if (!janet_checktype(kv->key, JANET_NIL)) {
            janet_table_put(table, kv->key, kv->value);
        }
    }
    return table;
}

/* C Functions */

JANET_CORE_FN(cfun_struct_with_proto,
              "(struct/with-proto proto & kvs)",
              "Create a structure, as with the usual struct constructor but set the "
              "struct prototype as well.") {
    janet_arity(argc, 1, -1);
    JanetStruct proto = janet_optstruct(argv, argc, 0, NULL);
    if (!(argc & 1))
        janet_panic("expected odd number of arguments");
    JanetKV *st = janet_struct_begin(argc / 2);
    for (int32_t i = 1; i < argc; i += 2) {
        janet_struct_put(st, argv[i], argv[i + 1]);
    }
    janet_struct_proto(st) = proto;
    return janet_wrap_struct(janet_struct_end(st));
}

JANET_CORE_FN(cfun_struct_getproto,
              "(struct/getproto st)",
              "Return the prototype of a struct, or nil if it doesn't have one.") {
    janet_fixarity(argc, 1);
    JanetStruct st = janet_getstruct(argv, 0);
    return janet_struct_proto(st)
           ? janet_wrap_struct(janet_struct_proto(st))
           : janet_wrap_nil();
}

JANET_CORE_FN(cfun_struct_flatten,
              "(struct/proto-flatten st)",
              "Convert a struct with prototypes to a struct with no prototypes by merging "
              "all key value pairs from recursive prototypes into one new struct.") {
    janet_fixarity(argc, 1);
    JanetStruct st = janet_getstruct(argv, 0);

    /* get an upper bounds on the number of items in the final struct */
    int64_t pair_count = 0;
    JanetStruct cursor = st;
    while (cursor) {
        pair_count += janet_struct_length(cursor);
        cursor = janet_struct_proto(cursor);
    }

    if (pair_count > INT32_MAX) {
        janet_panic("struct too large");
    }

    JanetKV *accum = janet_struct_begin((int32_t) pair_count);
    cursor = st;
    while (cursor) {
        for (int32_t i = 0; i < janet_struct_capacity(cursor); i++) {
            const JanetKV *kv = cursor + i;
            if (!janet_checktype(kv->key, JANET_NIL)) {
                janet_struct_put_ext(accum, kv->key, kv->value, 0);
            }
        }
        cursor = janet_struct_proto(cursor);
    }
    return janet_wrap_struct(janet_struct_end(accum));
}

JANET_CORE_FN(cfun_struct_to_table,
              "(struct/to-table st &opt recursive)",
              "Convert a struct to a table. If recursive is true, also convert the "
              "table's prototypes into the new struct's prototypes as well.") {
    janet_arity(argc, 1, 2);
    JanetStruct st = janet_getstruct(argv, 0);
    int recursive = argc > 1 && janet_truthy(argv[1]);
    JanetTable *tab = NULL;
    JanetStruct cursor = st;
    JanetTable *tab_cursor = tab;
    do {
        if (tab) {
            tab_cursor->proto = janet_table(janet_struct_length(cursor));
            tab_cursor = tab_cursor->proto;
        } else {
            tab = janet_table(janet_struct_length(cursor));
            tab_cursor = tab;
        }
        /* TODO - implement as memcpy since struct memory should be compatible
         * with table memory */
        for (int32_t i = 0; i < janet_struct_capacity(cursor); i++) {
            const JanetKV *kv = cursor + i;
            if (!janet_checktype(kv->key, JANET_NIL)) {
                janet_table_put(tab_cursor, kv->key, kv->value);
            }
        }
        cursor = janet_struct_proto(cursor);
    } while (recursive && cursor);
    return janet_wrap_table(tab);
}

JANET_CORE_FN(cfun_struct_rawget,
              "(struct/rawget st key)",
              "Gets a value from a struct `st` without looking at the prototype struct. "
              "If `st` does not contain the key directly, the function will return "
              "nil without checking the prototype. Returns the value in the struct.") {
    janet_fixarity(argc, 2);
    JanetStruct st = janet_getstruct(argv, 0);
    return janet_struct_rawget(st, argv[1]);
}

/* Load the struct module */
void janet_lib_struct(JanetTable *env) {
    JanetRegExt struct_cfuns[] = {
        JANET_CORE_REG("struct/with-proto", cfun_struct_with_proto),
        JANET_CORE_REG("struct/getproto", cfun_struct_getproto),
        JANET_CORE_REG("struct/proto-flatten", cfun_struct_flatten),
        JANET_CORE_REG("struct/to-table", cfun_struct_to_table),
        JANET_CORE_REG("struct/rawget", cfun_struct_rawget),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, struct_cfuns);
}

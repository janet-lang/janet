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

#ifndef JANET_AMALG
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

    size_t size = sizeof(JanetStructHead) + capacity * sizeof(JanetKV);
    JanetStructHead *head = janet_gcalloc(JANET_MEMORY_STRUCT, size);
    head->length = count;
    head->capacity = capacity;
    head->hash = 0;

    JanetKV *st = (JanetKV *)(head->data);
    janet_memempty(st, capacity);
    return st;
}

/* Find an item in a struct. Should be similar to janet_dict_find, but
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
void janet_struct_put(JanetKV *st, Janet key, Janet value) {
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
                /* A key was added to the struct more than once */
                return;
            }
        }
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
        st = newst;
    }
    janet_struct_hash(st) = janet_kv_calchash(st, janet_struct_capacity(st));
    return (const JanetKV *)st;
}

/* Get an item from a struct */
Janet janet_struct_get(const JanetKV *st, Janet key) {
    const JanetKV *kv = janet_struct_find(st, key);
    return kv ? kv->value : janet_wrap_nil();
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

/* Check if two structs are equal */
int janet_struct_equal(const JanetKV *lhs, const JanetKV *rhs) {
    int32_t index;
    int32_t llen = janet_struct_capacity(lhs);
    int32_t rlen = janet_struct_capacity(rhs);
    int32_t lhash = janet_struct_hash(lhs);
    int32_t rhash = janet_struct_hash(rhs);
    if (llen != rlen)
        return 0;
    if (lhash != rhash)
        return 0;
    for (index = 0; index < llen; index++) {
        const JanetKV *l = lhs + index;
        const JanetKV *r = rhs + index;
        if (!janet_equals(l->key, r->key))
            return 0;
        if (!janet_equals(l->value, r->value))
            return 0;
    }
    return 1;
}

/* Compare structs */
int janet_struct_compare(const JanetKV *lhs, const JanetKV *rhs) {
    int32_t i;
    int32_t lhash = janet_struct_hash(lhs);
    int32_t rhash = janet_struct_hash(rhs);
    int32_t llen = janet_struct_capacity(lhs);
    int32_t rlen = janet_struct_capacity(rhs);
    if (llen < rlen)
        return -1;
    if (llen > rlen)
        return 1;
    if (lhash < rhash)
        return -1;
    if (lhash > rhash)
        return 1;
    for (i = 0; i < llen; ++i) {
        const JanetKV *l = lhs + i;
        const JanetKV *r = rhs + i;
        int comp = janet_compare(l->key, r->key);
        if (comp != 0) return comp;
        comp = janet_compare(l->value, r->value);
        if (comp != 0) return comp;
    }
    return 0;
}

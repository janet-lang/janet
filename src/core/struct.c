/*
* Copyright (c) 2018 Calvin Rose
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

#define dst_struct_maphash(cap, hash) ((uint32_t)(hash & (cap - 1)));

/* Begin creation of a struct */
DstKV *dst_struct_begin(int32_t count) {

    /* Calculate capacity as power of 2 after 2 * count. */
    int32_t capacity = dst_tablen(2 * count);
    if (capacity < 0) capacity = dst_tablen(count + 1);

    size_t s = sizeof(int32_t) * 4 + (capacity * sizeof(DstKV));
    char *data = dst_gcalloc(DST_MEMORY_STRUCT, s);
    DstKV *st = (DstKV *) (data + 4 * sizeof(int32_t));
    dst_memempty(st, capacity);
    dst_struct_length(st) = count;
    dst_struct_capacity(st) = capacity;
    dst_struct_hash(st) = 0;
    return st;
}

/* Find an item in a struct */
const DstKV *dst_struct_find(const DstKV *st, Dst key) {
    int32_t cap = dst_struct_capacity(st);
    int32_t index = dst_struct_maphash(cap, dst_hash(key));
    int32_t i;
    for (i = index; i < cap; i++)
        if (dst_checktype(st[i].key, DST_NIL) || dst_equals(st[i].key, key))
            return st + i;
    for (i = 0; i < index; i++)
        if (dst_checktype(st[i].key, DST_NIL) || dst_equals(st[i].key, key))
            return st + i;
    return NULL;
}

/* Put a kv pair into a struct that has not yet been fully constructed.
 * Nil keys and values are ignored, extra keys are ignore, and duplicate keys are
 * ignored.
 *
 * Runs will be in sorted order, as the collisions resolver essentially
 * preforms an in-place insertion sort. This ensures the internal structure of the
 * hash map is independant of insertion order.
 */
void dst_struct_put(DstKV *st, Dst key, Dst value) {
    int32_t cap = dst_struct_capacity(st);
    int32_t hash = dst_hash(key);
    int32_t index = dst_struct_maphash(cap, hash);
    int32_t i, j, dist;
    int32_t bounds[4] = {index, cap, 0, index};
    if (dst_checktype(key, DST_NIL) || dst_checktype(value, DST_NIL)) return;
    /* Avoid extra items */
    if (dst_struct_hash(st) == dst_struct_length(st)) return;
    for (dist = 0, j = 0; j < 4; j += 2)
    for (i = bounds[j]; i < bounds[j + 1]; i++, dist++) {
        int status;
        int32_t otherhash;
        int32_t otherindex, otherdist;
        DstKV *kv = st + i;
        /* We found an empty slot, so just add key and value */
        if (dst_checktype(kv->key, DST_NIL)) {
            kv->key = key;
            kv->value = value;
            /* Update the temporary count */
            dst_struct_hash(st)++;
            return;
        }
        /* Robinhood hashing - check if colliding kv pair
         * is closer to their source than current. We use robinhood
         * hashing to ensure that equivalent structs that are contsructed
         * with different order have the same internal layout, and therefor
         * will compare properly - i.e., {1 2 3 4} should equal {3 4 1 2}. 
         * Collisions are resolved via an insertion sort insertion. */
        otherhash = dst_hash(kv->key);
        otherindex = dst_struct_maphash(cap, otherhash);
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
            status = dst_compare(key, kv->key);
        /* If other is closer to their ideal slot */
        if (status == 1) {
            /* Swap current kv pair with pair in slot */
            DstKV temp = *kv;
            kv->key = key;
            kv->value = value;
            key = temp.key;
            value = temp.value;
            /* Save dist and hash of new kv pair */
            dist = otherdist;
            hash = otherhash;
        } else if (status == 0) {
            /* This should not happen - it means
             * than a key was added to the struct more than once */
            dst_exit("struct double put fail");
            return;
        }
    }
}

/* Finish building a struct */
const DstKV *dst_struct_end(DstKV *st) {
    if (dst_struct_hash(st) != dst_struct_length(st)) {
        /* Error building struct, probably duplicate values. We need to rebuild
         * the struct using only the values that went in. The second creation should always
         * succeed. */
        int32_t i, realCount;
        DstKV *newst;
        realCount = 0;
        for (i = 0; i < dst_struct_capacity(st); i++) {
            DstKV *kv = st + i;
            realCount += dst_checktype(kv->key, DST_NIL) ? 1 : 0;
        }
        newst = dst_struct_begin(realCount);
        for (i = 0; i < dst_struct_capacity(st); i++) {
            DstKV *kv = st + i;
            if (!dst_checktype(kv->key, DST_NIL)) {
                dst_struct_put(newst, kv->key, kv->value);
            }
        }
        st = newst;
    }
    dst_struct_hash(st) = dst_kv_calchash(st, dst_struct_capacity(st));
    return (const DstKV *)st;
}

/* Get an item from a struct */
Dst dst_struct_get(const DstKV *st, Dst key) {
    const DstKV *kv = dst_struct_find(st, key);
    return kv ? kv->value : dst_wrap_nil();
}

/* Get the next key in a struct */
const DstKV *dst_struct_next(const DstKV *st, const DstKV *kv) {
    const DstKV *end = st + dst_struct_capacity(st);
    kv = (kv == NULL) ? st : kv + 1;
    while (kv < end) {
        if (!dst_checktype(kv->key, DST_NIL)) return kv;
        kv++;
    }
    return NULL;
}

/* Convert struct to table */
DstTable *dst_struct_to_table(const DstKV *st) {
    DstTable *table = dst_table(dst_struct_capacity(st));
    int32_t i;
    for (i = 0; i < dst_struct_capacity(st); i++) {
        const DstKV *kv = st + i;
        if (!dst_checktype(kv->key, DST_NIL)) {
            dst_table_put(table, kv->key, kv->value);
        }
    }
    return table;
}

/* Check if two structs are equal */
int dst_struct_equal(const DstKV *lhs, const DstKV *rhs) {
    int32_t index;
    int32_t llen = dst_struct_capacity(lhs);
    int32_t rlen = dst_struct_capacity(rhs);
    int32_t lhash = dst_struct_hash(lhs);
    int32_t rhash = dst_struct_hash(rhs);
    if (llen != rlen)
        return 0;
    if (lhash != rhash)
        return 0;
    for (index = 0; index < llen; index++) {
        const DstKV *l = lhs + index;
        const DstKV *r = rhs + index;
        if (!dst_equals(l->key, r->key))
            return 0;
        if (!dst_equals(l->value, r->value))
            return 0;
    }
    return 1;
}

/* Compare structs */
int dst_struct_compare(const DstKV *lhs, const DstKV *rhs) {
    int32_t i;
    int32_t lhash = dst_struct_hash(lhs);
    int32_t rhash = dst_struct_hash(rhs);
    int32_t llen = dst_struct_capacity(lhs);
    int32_t rlen = dst_struct_capacity(rhs);
    if (llen < rlen)
        return -1;
    if (llen > rlen)
        return 1;
    if (lhash < rhash)
        return -1;
    if (lhash > rhash)
        return 1;
    for (i = 0; i < llen; ++i) {
        const DstKV *l = lhs + i;
        const DstKV *r = rhs + i;
        int comp = dst_compare(l->key, r->key);
        if (comp != 0) return comp;
        comp = dst_compare(l->value, r->value);
        if (comp != 0) return comp;
    }
    return 0;
}

#undef dst_struct_maphash

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

/* Begin creation of a struct */
DstValue *dst_struct_begin(int32_t count) {
    /* This expression determines size of structs. It must be a pure
     * function of count, and hold at least enough space for count 
     * key value pairs. The minimum it could be is 
     * sizeof(int32_t) * 2 + 2 * count * sizeof(DstValue). Adding more space
     * ensures that structs are less likely to have hash collisions. If more space
     * is added or s is changed, change the macro dst_struct_capacity in internal.h */
    size_t s = sizeof(int32_t) * 2 + 4 * count * sizeof(DstValue);
    char *data = dst_alloc(DST_MEMORY_STRUCT, s);
    memset(data, 0, s);
    DstValue *st = (DstValue *) (data + 2 * sizeof(int32_t));
    dst_struct_length(st) = count;
    /* Use the hash storage space as a counter to see how many items
     * were successfully added. If this number is not equal to the
     * original number, we will need to remake the struct using
     * only the kv pairs in the struct */
    dst_struct_hash(st) = 0;
    return st;
}

/* Find an item in a struct */
static const DstValue *dst_struct_find(const DstValue *st, DstValue key) {
    int32_t cap = dst_struct_capacity(st);
    int32_t index = (dst_hash(key) % cap) & (~1);
    int32_t i;
    for (i = index; i < cap; i += 2)
        if (st[i].type == DST_NIL || dst_equals(st[i], key))
            return st + i;
    for (i = 0; i < index; i += 2)
        if (st[i].type == DST_NIL || dst_equals(st[i], key))
            return st + i;
    return NULL;
}

/* Put a kv pair into a struct that has not yet been fully constructed.
 * Nil keys and values are ignored, extra keys are ignore, and duplicate keys are
 * ignored. */
void dst_struct_put(DstValue *st, DstValue key, DstValue value) {
    int32_t cap = dst_struct_capacity(st);
    int32_t hash = dst_hash(key);
    int32_t index = (hash % cap) & (~1);
    int32_t i, j, dist;
    int32_t bounds[4] = {index, cap, 0, index};
    if (key.type == DST_NIL || value.type == DST_NIL) return;
    /* Avoid extra items */
    if (dst_struct_hash(st) == dst_struct_length(st)) return;
    for (dist = 0, j = 0; j < 4; j += 2)
    for (i = bounds[j]; i < bounds[j + 1]; i += 2, dist += 2) {
        int status;
        int32_t otherhash;
        int32_t otherindex, otherdist;
        /* We found an empty slot, so just add key and value */
        if (st[i].type == DST_NIL) {
            st[i] = key;
            st[i + 1] = value;
            /* Update the temporary count */
            dst_struct_hash(st)++;
            return;
        }
        /* Robinhood hashing - check if colliding kv pair
         * is closer to their source than current. We use robinhood
         * hashing to ensure that equivalent structs that are contsructed
         * with different order have the same internal layout, and therefor
         * will compare properly - i.e., {1 2 3 4} should equal {3 4 1 2}. */
        otherhash = dst_hash(st[i]);
        otherindex = (otherhash % cap) & (~1);
        otherdist = (i + cap - otherindex) % cap;
        if (dist < otherdist)
            status = -1;
        else if (otherdist < dist)
            status = 1;
        else if (hash < otherhash)
            status = -1;
        else if (otherhash < hash)
            status = 1;
        else
            status = dst_compare(key, st[i]);
        /* If other is closer to their ideal slot */
        if (status == 1) {
            /* Swap current kv pair with pair in slot */
            DstValue t1, t2;
            t1 = st[i];
            t2 = st[i + 1];
            st[i] = key;
            st[i + 1] = value;
            key = t1;
            value = t2;
            /* Save dist and hash of new kv pair */
            dist = otherdist;
            hash = otherhash;
        } else if (status == 0) {
            /* This should not happen - it means
             * than a key was added to the struct more than once */
            return;
        }
    }
}

/* Finish building a struct */
const DstValue *dst_struct_end(DstValue *st) {
    if (dst_struct_hash(st) != dst_struct_length(st)) {
        /* Error building struct, probably duplicate values. We need to rebuild
         * the struct using only the values that went in. The second creation should always
         * succeed. */
        int32_t i, realCount;
        DstValue *newst;
        realCount = 0;
        for (i = 0; i < dst_struct_capacity(st); i += 2) {
            realCount += st[i].type != DST_NIL;
        }
        newst = dst_struct_begin(realCount);
        for (i = 0; i < dst_struct_capacity(st); i += 2) {
            if (st[i].type != DST_NIL) {
                dst_struct_put(newst, st[i], st[i + 1]);
            }
        }
        st = newst;
    }
    dst_struct_hash(st) = 0;
    return (const DstValue *)st;
}

/* Get an item from a struct */
DstValue dst_struct_get(const DstValue *st, DstValue key) {
    const DstValue *bucket = dst_struct_find(st, key);
    if (!bucket || bucket[0].type == DST_NIL) {
        DstValue ret;
        ret.type = DST_NIL;
        return  ret;
    } else {
        return bucket[1];
    }
}

/* Get the next key in a struct */
DstValue dst_struct_next(const DstValue *st, DstValue key) {
    const DstValue *bucket, *end;
    end = st + dst_struct_capacity(st);
    if (key.type == DST_NIL) {
        bucket = st;
    } else {
        bucket = dst_struct_find(st, key);
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

/* Convert struct to table */
DstTable *dst_struct_to_table(const DstValue *st) {
    DstTable *table = dst_table(dst_struct_capacity(st));
    int32_t i;
    for (i = 0; i < dst_struct_capacity(st); i += 2) {
        if (st[i].type != DST_NIL) {
            dst_table_put(table, st[i], st[i + 1]);
        }
    }
    return table;
}

/* Check if two structs are equal */
int dst_struct_equal(const DstValue *lhs, const DstValue *rhs) {
    int32_t index;
    int32_t llen = dst_struct_capacity(lhs);
    int32_t rlen = dst_struct_capacity(rhs);
    int32_t lhash = dst_struct_hash(lhs);
    int32_t rhash = dst_struct_hash(rhs);
    if (llen != rlen)
        return 0;
    if (lhash == 0)
        lhash = dst_struct_hash(lhs) = dst_array_calchash(lhs, llen);
    if (rhash == 0)
        rhash = dst_struct_hash(rhs) = dst_array_calchash(rhs, rlen);
    if (lhash != rhash)
        return 0;
    for (index = 0; index < llen; index++) {
        if (!dst_equals(lhs[index], rhs[index]))
            return 0;
    }
    return 1;
}

/* Compare structs */
int dst_struct_compare(const DstValue *lhs, const DstValue *rhs) {
    int32_t i;
    int32_t lhash = dst_struct_hash(lhs);
    int32_t rhash = dst_struct_hash(rhs);
    int32_t llen = dst_struct_capacity(lhs);
    int32_t rlen = dst_struct_capacity(rhs);
    if (llen < rlen)
        return -1;
    if (llen > rlen)
        return 1;
    if (0 == lhash)
        lhash = dst_struct_hash(lhs) = dst_array_calchash(lhs, llen);
    if (0 == rhash)
        rhash = dst_struct_hash(rhs) = dst_array_calchash(rhs, rlen);
    if (lhash < rhash)
        return -1;
    if (lhash > rhash)
        return 1;
    for (i = 0; i < llen; ++i) {
        int comp = dst_compare(lhs[i], rhs[i]);
        if (comp != 0) return comp;
    }
    return 0;
}

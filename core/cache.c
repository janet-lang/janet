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
#include "cache.h"

/* All immutable values are cached in a global hash table. When an immutable
 * value is created, this hashtable is checked to see if the value exists. If it
 * does, return the cached copy instead. This trades creation time and memory for
 * fast equality, which is especially useful for symbols and strings. This may not
 * be useful for structs and tuples, in which case it may be removed. However, in cases
 * where ther are many copies of the same tuple in the program, this approach may
 * save memory. Values are removed from the cache when they are garbage collected.
 */

/* Check if two not necesarrily finalized immutable values
 * are equal. Does caching logic */
static int dst_cache_equal(DstValue x, DstValue y) {
    uint32_t i, len;
    if (x.type != y.type) return 0;
    switch (x.type) {
    /* Don't bother implementing equality checks for all types. We only care
     * about immutable data structures */
    default:
        return 0;
    case DST_STRING:
        if (dst_string_hash(x.as.string) != dst_string_hash(y.as.string)) return 0;
        if (dst_string_length(x.as.string) != dst_string_length(y.as.string)) return 0;
        len = dst_string_length(x.as.string);
        for (i = 0; i < len; ++i)
            if (x.as.string[i] != y.as.string[i])
                return 0;
        return 1;
    case DST_STRUCT:
        if (dst_struct_hash(x.as.st) != dst_struct_hash(y.as.st)) return 0;
        if (dst_struct_length(x.as.st) != dst_struct_length(y.as.st)) return 0;
        len = dst_struct_capacity(x.as.st);
        for (i = 0; i < len; ++i)
            if (!dst_value_equals(x.as.st[i], y.as.st[i]))
                return 0;
        return 1;
    case DST_TUPLE:
        if (dst_tuple_hash(x.as.tuple) != dst_tuple_hash(y.as.tuple)) return 0;
        if (dst_tuple_length(x.as.tuple) != dst_tuple_length(y.as.tuple)) return 0;
        len = dst_tuple_length(x.as.tuple);
        for (i = 0; i < len; ++i)
            if (!dst_value_equals(x.as.tuple[i], y.as.tuple[i]))
                return 0;
        return 1;
    }
}

/* Check if a value x is equal to a string. Special version of
 * dst_cache_equal */
static int dst_cache_strequal(DstValue x, const uint8_t *str, uint32_t len, uint32_t hash) {
    uint32_t i;
    if (x.type != DST_STRING) return 0;
    if (dst_string_hash(x.as.string) != hash) return 0;
    if (dst_string_length(x.as.string) != len) return 0;
    for (i = 0; i < len; ++i)
        if (x.as.string[i] != str[i])
            return 0;
    return 1;
}

/* Find an item in the cache and return its location.
 * If the item is not found, return the location
 * where one would put it. */
static DstValue *dst_cache_find(Dst *vm, DstValue key, int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index;
    uint32_t hash = dst_value_hash(key);
    DstValue *firstEmpty = NULL;
    index = hash % vm->cache_capacity;
    bounds[0] = index;
    bounds[1] = vm->cache_capacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j+1]; ++i) {
            DstValue test = vm->cache[i];
            /* Check empty spots */
            if (test.type == DST_NIL) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                goto notfound;
            }
            /* Check for marked deleted - use booleans as deleted */
            if (test.type == DST_BOOLEAN) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                continue;
            }
            if (dst_cache_equal(test, key)) {
                /* Replace first deleted */
                *success = 1;
                if (firstEmpty != NULL) {
                    *firstEmpty = test;
                    vm->cache[i].type = DST_BOOLEAN;
                    return firstEmpty;
                }
                return vm->cache + i;
            }
        }
    notfound:
    *success = 0;
    return firstEmpty;
}

/* Find an item in the cache and return its location.
 * If the item is not found, return the location
 * where one would put it. Special case of dst_cache_find */
DstValue *dst_cache_strfind(Dst *vm,
        const uint8_t *str,
        uint32_t len,
        uint32_t hash,
        int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index;
    DstValue *firstEmpty = NULL;
    index = hash % vm->cache_capacity;
    bounds[0] = index;
    bounds[1] = vm->cache_capacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j+1]; ++i) {
            DstValue test = vm->cache[i];
            /* Check empty spots */
            if (test.type == DST_NIL) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                goto notfound;
            }
            /* Check for marked deleted - use booleans as deleted */
            if (test.type == DST_BOOLEAN) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                continue;
            }
            if (dst_cache_strequal(test, str, len, hash)) {
                /* Replace first deleted */
                *success = 1;
                if (firstEmpty != NULL) {
                    *firstEmpty = test;
                    vm->cache[i].type = DST_BOOLEAN;
                    return firstEmpty;
                }
                return vm->cache + i;
            }
        }
    notfound:
    *success = 0;
    return firstEmpty;
}

/* Resize the cache. */
static void dst_cache_resize(Dst *vm, uint32_t newCapacity) {
    uint32_t i, oldCapacity;
    DstValue *oldCache = vm->cache;
    DstValue *newCache = calloc(1, newCapacity * sizeof(DstValue));
    if (newCache == NULL) {
        DST_OUT_OF_MEMORY;
    }
    oldCapacity = vm->cache_capacity;
    vm->cache = newCache;
    vm->cache_capacity = newCapacity;
    vm->cache_deleted = 0;
    /* Add all of the old cache entries back */
    for (i = 0; i < oldCapacity; ++i) {
        int status;
        DstValue *bucket;
        DstValue x = oldCache[i];
        if (x.type != DST_NIL && x.type != DST_BOOLEAN) {
            bucket = dst_cache_find(vm, x, &status);
            if (status || bucket == NULL) {
                /* there was a problem with the algorithm. */
                break;
            }
            *bucket = x;
        }
    }
    /* Free the old cache */
    free(oldCache);
}

/* Add a value to the cache given we know it is not
 * already in the cache and we have a bucket. */
DstValue dst_cache_add_bucket(Dst *vm, DstValue x, DstValue *bucket) {
    if ((vm->cache_count + vm->cache_deleted) * 2 > vm->cache_capacity) {
        int status;
        dst_cache_resize(vm, vm->cache_count * 4);
        bucket = dst_cache_find(vm, x, &status);
    }
    /* Add x to the cache */
    vm->cache_count++;
    *bucket = x;
    return x;
}

/* Add a value to the cache */
DstValue dst_cache_add(Dst *vm, DstValue x) {
    int status = 0;
    DstValue *bucket = dst_cache_find(vm, x, &status);
    if (!status) {
        return dst_cache_add_bucket(vm, x, bucket);
    } else {
        return *bucket;
    }
}

/* Remove a value from the cache */
void dst_cache_remove(Dst *vm, DstValue x) {
    int status = 0;
    DstValue *bucket = dst_cache_find(vm, x, &status);
    if (status) {
        vm->cache_count--;
        vm->cache_deleted++;
        bucket->type = DST_BOOLEAN;
    }
}

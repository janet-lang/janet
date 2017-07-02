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

#include <gst/gst.h>
#include "cache.h"

/****/
/* Cache */
/****/

/* Calculate hash for string */
static uint32_t gst_string_calchash(const uint8_t *str, uint32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Calculate hash for tuple (and struct) */
static uint32_t gst_tuple_calchash(const GstValue *tuple, uint32_t len) {
    const GstValue *end = tuple + len;
    uint32_t hash = 5381;
    while (tuple < end)
        hash = (hash << 5) + hash + gst_hash(*tuple++);
    return hash;
}

/* Check if two not necesarrily finalized immutable values
 * are equal. Does caching logic */
static int gst_cache_equal(GstValue x, GstValue y) {
    uint32_t i, len;
    if (x.type != y.type) return 0;
    switch (x.type) {
    /* Don't bother implementing equality checks for all types. We only care
     * about immutable data structures */
    default:
        return 0;
    case GST_STRING:
        if (gst_string_hash(x.data.string) != gst_string_hash(y.data.string)) return 0;
        if (gst_string_length(x.data.string) != gst_string_length(y.data.string)) return 0;
        len = gst_string_length(x.data.string);
        for (i = 0; i < len; ++i)
            if (x.data.string[i] != y.data.string[i])
                return 0;
        return 1;
    case GST_STRUCT:
        if (gst_struct_hash(x.data.st) != gst_struct_hash(y.data.st)) return 0;
        if (gst_struct_length(x.data.st) != gst_struct_length(y.data.st)) return 0;
        len = gst_struct_capacity(x.data.st);
        for (i = 0; i < len; ++i)
            if (!gst_equals(x.data.st[i], y.data.st[i]))
                return 0;
        return 1;
    case GST_TUPLE:
        if (gst_tuple_hash(x.data.tuple) != gst_tuple_hash(y.data.tuple)) return 0;
        if (gst_tuple_length(x.data.tuple) != gst_tuple_length(y.data.tuple)) return 0;
        len = gst_tuple_length(x.data.tuple);
        for (i = 0; i < len; ++i)
            if (!gst_equals(x.data.tuple[i], y.data.tuple[i]))
                return 0;
        return 1;
    }
}

/* Find an item in the cache and return its location.
 * If the item is not found, return the location
 * where one would put it. */
static GstValue *gst_cache_find(Gst *vm, GstValue key, int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index;
    uint32_t hash = gst_hash(key);
    GstValue *firstEmpty = NULL;
    index = hash % vm->cache_capacity;
    bounds[0] = index;
    bounds[1] = vm->cache_capacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j+1]; ++i) {
            GstValue test = vm->cache[i];
            /* Check empty spots */
            if (test.type == GST_NIL) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                goto notfound;
            }
            /* Check for marked deleted - use booleans as deleted */
            if (test.type == GST_BOOLEAN) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->cache + i;
                continue;
            }
            if (gst_cache_equal(test, key)) {
                /* Replace first deleted */
                *success = 1;
                if (firstEmpty != NULL) {
                    *firstEmpty = test;
                    vm->cache[i].type = GST_BOOLEAN;
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
static void gst_cache_resize(Gst *vm, uint32_t newCapacity) {
    uint32_t i, oldCapacity;
    GstValue *oldCache = vm->cache;
    GstValue *newCache = gst_raw_calloc(1, newCapacity * sizeof(GstValue));
    if (newCache == NULL)
        GST_OUT_OF_MEMORY;
    oldCapacity = vm->cache_capacity;
    vm->cache = newCache;
    vm->cache_capacity = newCapacity;
    vm->cache_deleted = 0;
    /* Add all of the old strings back */
    for (i = 0; i < oldCapacity; ++i) {
        int status;
        GstValue *bucket;
        GstValue x = oldCache[i];
        if (x.type != GST_NIL && x.type != GST_BOOLEAN) {
            bucket = gst_cache_find(vm, x, &status);
            if (status || bucket == NULL) {
                /* there was a problem with the algorithm. */
                break;
            }
            *bucket = x;
        }
    }
    /* Free the old cache */
    gst_raw_free(oldCache);
}

/* Add a value to the cache */
static GstValue gst_cache_add(Gst *vm, GstValue x) {
    int status = 0;
    GstValue *bucket = gst_cache_find(vm, x, &status);
    if (!status) {
        if ((vm->cache_count + vm->cache_deleted) * 2 > vm->cache_capacity) {
            gst_cache_resize(vm, vm->cache_count * 4);
            bucket = gst_cache_find(vm, x, &status);
        }
        /* Mark the memory for the gc */
        switch (x.type) {
        default:
            break;
        case GST_STRING:
            gst_mem_tag(gst_string_raw(x.data.string), GST_MEMTAG_STRING);
            break;
        case GST_STRUCT:
            gst_mem_tag(gst_struct_raw(x.data.st), GST_MEMTAG_STRUCT);
            break;
        case GST_TUPLE:
            gst_mem_tag(gst_tuple_raw(x.data.tuple), GST_MEMTAG_TUPLE);
            break;
        }
        /* Add x to the cache */
        vm->cache_count++;
        *bucket = x;
        return x;
    } else {
        return *bucket;
    }
}

/* Remove a value from the cache */
static void gst_cache_remove(Gst *vm, GstValue x) {
    int status = 0;
    GstValue *bucket = gst_cache_find(vm, x, &status);
    if (status) {
        vm->cache_count--;
        vm->cache_deleted++;
        bucket->type = GST_BOOLEAN;
    }
}

/* Remove a string from cache (called from gc) */
void gst_cache_remove_string(Gst *vm, char *strmem) {
    GstValue x;
    x.type = GST_STRING;
    x.data.string = (const uint8_t *)(strmem + 2 * sizeof(uint32_t));
    gst_cache_remove(vm, x);
}

/* Remove a tuple from cache (called from gc) */
void gst_cache_remove_tuple(Gst *vm, char *tuplemem) {
    GstValue x;
    x.type = GST_TUPLE;
    x.data.tuple = (const GstValue *)(tuplemem + 2 * sizeof(uint32_t));
    gst_cache_remove(vm, x);
}

/* Remove a struct from cache (called from gc) */
void gst_cache_remove_struct(Gst *vm, char *structmem) {
    GstValue x;
    x.type = GST_STRUCT;
    x.data.st = (const GstValue *)(structmem + 2 * sizeof(uint32_t));
    gst_cache_remove(vm, x);
}

/****/
/* Struct Functions */
/****/

/* Begin creation of a struct */
GstValue *gst_struct_begin(Gst *vm, uint32_t count) {
    char *data = gst_zalloc(vm, sizeof(uint32_t) * 2 + 4 * count * sizeof(GstValue));
    GstValue *st = (GstValue *) (data + 2 * sizeof(uint32_t));
    gst_struct_length(st) = count;
    return st;
}

/* Find an item in a struct */
static const GstValue *gst_struct_find(const GstValue *st, GstValue key) {
    uint32_t cap = gst_struct_capacity(st);
    uint32_t index = (gst_hash(key) % (cap / 2)) * 2;
    uint32_t i;
    for (i = index; i < cap; i += 2)
        if (st[i].type == GST_NIL || gst_equals(st[i], key))
            return st + i;
    for (i = 0; i < index; i += 2)
        if (st[i].type == GST_NIL || gst_equals(st[i], key))
            return st + i;
    return NULL;
}

/* Put a kv pair into a struct that has not yet been fully constructed.
 * Behavior is undefined if too many keys are added, or if a key is added
 * twice. Nil keys and values are ignored. */
void gst_struct_put(GstValue *st, GstValue key, GstValue value) {
    uint32_t cap = gst_struct_capacity(st);
    uint32_t hash = gst_hash(key);
    uint32_t index = (hash % (cap / 2)) * 2;
    uint32_t i, j, dist;
    uint32_t bounds[4] = {index, cap, 0, index};
    if (key.type == GST_NIL || value.type == GST_NIL) return;
    for (dist = 0, j = 0; j < 4; j += 2)
    for (i = bounds[j]; i < bounds[j + 1]; i += 2, dist += 2) {
        int status;
        uint32_t otherhash, otherindex, otherdist;
        /* We found an empty slot, so just add key and value */
        if (st[i].type == GST_NIL) {
            st[i] = key;
            st[i + 1] = value;
            return;
        }
        /* Robinhood hashing - check if colliding kv pair
         * is closer to their source than current. */
        otherhash = gst_hash(st[i]);
        otherindex = (otherhash % (cap / 2)) * 2;
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
            status = gst_compare(key, st[i]);
        /* If other is closer to their ideal slot */
        if (status == 1) {
            /* Swap current kv pair with pair in slot */
            GstValue t1, t2;
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
const GstValue *gst_struct_end(Gst *vm, GstValue *st) {
    GstValue cached;
    GstValue check;
    gst_struct_hash(st) = gst_tuple_calchash(st, gst_struct_capacity(st));
    check.type = GST_STRUCT;
    check.data.st = (const GstValue *) st;
    cached = gst_cache_add(vm, check);
    return cached.data.st;
}

/* Get an item from a struct */
GstValue gst_struct_get(const GstValue *st, GstValue key) {
    const GstValue *bucket = gst_struct_find(st, key);
    if (!bucket || bucket[0].type == GST_NIL) {
        GstValue ret;
        ret.type = GST_NIL;
        return  ret;
    } else {
        return bucket[1];
    }
}

/* Get the next key in a struct */
GstValue gst_struct_next(const GstValue *st, GstValue key) {
    const GstValue *bucket, *end;
    end = st + gst_struct_capacity(st);
    if (key.type == GST_NIL) {
        bucket = st;
    } else {
        bucket = gst_struct_find(st, key);
        if (!bucket || bucket[0].type == GST_NIL)
            return gst_wrap_nil();
        bucket += 2;
    }
    for (; bucket < end; bucket += 2) {
        if (bucket[0].type != GST_NIL)
            return bucket[0];
    }
    return gst_wrap_nil();
}

/****/
/* Tuple functions */
/****/

/* Create a new empty tuple of the given size. Expected to be
 * mutated immediately */
GstValue *gst_tuple_begin(Gst *vm, uint32_t length) {
    char *data = gst_alloc(vm, 2 * sizeof(uint32_t) + length * sizeof(GstValue));
    GstValue *tuple = (GstValue *)(data + (2 * sizeof(uint32_t)));
    gst_tuple_length(tuple) = length;
    return tuple;
}

/* Finish building a tuple */
const GstValue *gst_tuple_end(Gst *vm, GstValue *tuple) {
    GstValue cached;
    GstValue check;
    gst_tuple_hash(tuple) = gst_tuple_calchash(tuple, gst_tuple_length(tuple));
    check.type = GST_TUPLE;
    check.data.tuple = (const GstValue *) tuple;
    cached = gst_cache_add(vm, check);
    return cached.data.tuple;
}

/****/
/* String Functions */
/****/

/* Begin building a string */
uint8_t *gst_string_begin(Gst *vm, uint32_t length) {
    char *data = gst_alloc(vm, 2 * sizeof(uint32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(uint32_t));
    gst_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *gst_string_end(Gst *vm, uint8_t *str) {
    GstValue cached;
    GstValue check;
    gst_string_hash(str) = gst_string_calchash(str, gst_string_length(str));
    check.type = GST_STRING;
    check.data.string = (const uint8_t *) str;
    cached = gst_cache_add(vm, check);
    return cached.data.string;

}

/* Load a buffer as a string */
const uint8_t *gst_string_b(Gst *vm, const uint8_t *buf, uint32_t len) {
    GstValue cached;
    GstValue check;
    uint32_t newbufsize = len + 2 * sizeof(uint32_t) + 1;
    uint8_t *str;
    /* Ensure enough scratch memory */
    if (vm->scratch_len < newbufsize) {
        vm->scratch = gst_alloc(vm, newbufsize);
        vm->scratch_len = newbufsize;
    }
    str = (uint8_t *)(vm->scratch + 2 * sizeof(uint32_t));
    gst_memcpy(str, buf, len);
    gst_string_length(str) = len;
    gst_string_hash(str) = gst_string_calchash(str, gst_string_length(str));
    str[len] = 0;
    check.type = GST_STRING;
    check.data.string = (const uint8_t *) str;
    cached = gst_cache_add(vm, check);
    if (cached.data.string == (const uint8_t *) str) {
        vm->scratch_len = 0;
        vm->scratch = NULL;
    }
    return cached.data.string;
}

/* Load a c string */
const uint8_t *gst_string_c(Gst *vm, const char *str) {
    uint32_t len = 0;
    while (str[len]) ++len;
    return gst_string_b(vm, (const uint8_t *)str, len);
}

/* Load a c string and return it as a GstValue */
GstValue gst_string_cv(Gst *vm, const char *str) {
    GstValue ret;
    const uint8_t *data = gst_string_c(vm, str);
    ret.type = GST_STRING;
    ret.data.string = data;
    return ret;
}

/* Load a c string and return it as a GstValue. Return the symbol. */
GstValue gst_string_cvs(Gst *vm, const char *str) {
    GstValue ret;
    /* Only put strings in cache */
    const uint8_t *data = gst_string_c(vm, str);
    ret.type = GST_SYMBOL;
    ret.data.string = data;
    return ret;
}

/* Compares two strings */
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = gst_string_length(lhs);
    uint32_t ylen = gst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

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
#include <dst/dst.h>

/****/
/* Buffer functions */
/****/

/* Create a new Buffer */
void dst_buffer(Dst *vm, uint32_t dest, uint32_t capacity) {
    DstValue buf;
    DstBuffer *buffer = dst_alloc(vm, sizeof(DstBuffer));
    /* Prevent immediate collection */
    dst_mark_mem(vm, buffer);
    uint8_t *data = dst_alloc(vm, sizeof(uint8_t) * capacity);
    buffer->data = data;
    buffer->count = 0;
    buffer->capacity = capacity;
    buf.type = DST_BYTEBUFFER;
    buf.data.buffer = buffer;
    dst_set_arg(vm, dest, buf);
}

/* Ensure that the buffer has enough internal capacity */
void dst_value_buffer_ensure(Dst *vm, DstBuffer *buffer, uint32_t capacity) {
    uint8_t *newData;
    if (capacity <= buffer->capacity) return;
    newData = dst_alloc(vm, capacity * sizeof(uint8_t));
    dst_memcpy(newData, buffer->data, buffer->capacity * sizeof(uint8_t));
    buffer->data = newData;
    buffer->capacity = capacity;
}

/* Push multiple bytes into the buffer */
void dst_buffer_append_bytes(Dst *vm, DstBuffer *buffer, const uint8_t *string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        dst_value_buffer_ensure(vm, buffer, 2 * newSize);
    }
    dst_memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

/* Push a cstring to buffer */
void dst_buffer_append_cstring(Dst *vm, DstBuffer *buffer, const char *cstring) {
    uint32_t len = 0;
    while (cstring[len]) ++len;
    dst_buffer_append_bytes(vm, buffer, (const uint8_t *) cstring, len);
}

/****/
/* Array functions */
/****/

/* Creates a new array */
DstArray *dst_make_array(Dst *vm, uint32_t capacity) {
    DstArray *array = dst_alloc(vm, sizeof(DstArray));
    /* Prevent immediate collection */
    dst_mark_mem(vm, array);
    DstValue *data = dst_alloc(vm, capacity * sizeof(DstValue));
    array->data = data;
    array->count = 0;
    array->capacity = capacity;
    return array;
}

void dst_array(Dst *vm, uint32_t dest, uint32_t capacity) {
    DstValue arr;
    arr.type = DST_ARRAY;
    arr.data.array = dst_make_array(vm, capacity);
    dst_set_arg(vm, dest, arr);
}

/* Ensure the array has enough capacity for elements */
static void dst_value_array_ensure(Dst *vm, DstArray *array, uint32_t capacity) {
    DstValue *newData;
    if (capacity <= array->capacity) return;
    newData = dst_alloc(vm, capacity * sizeof(DstValue));
    dst_memcpy(newData, array->data, array->capacity * sizeof(DstValue));
    array->data = newData;
    array->capacity = capacity;
}

void dst_array_set(Dst *vm, uint32_t arr, uint32_t index, uint32_t val) {
    DstValue a = dst_arg(vm, arr);
    DstArray *array;
    if (a.type != DST_ARRAY) {
        dst_cerr(vm, "expected array");
        return;
    }
    array = a.data.array;
    dst_value_array_ensure(vm, array, index + 1);
    array->data[index] = dst_arg(vm, val);
}

void dst_array_get(Dst *vm, uint32_t dest, uint32_t arr, uint32_t index) {
    DstValue a = dst_arg(vm, arr);
    DstArray *array;
    if (a.type != DST_ARRAY) {
        dst_cerr(vm, "expected array");
        return;
    }
    array = a.data.array;
    if (index >= array->count) {
        dst_set_arg(vm, dest, dst_wrap_nil());
    } else {
        dst_set_arg(vm, dest, array->data[index]);
    }
}

/****/
/* Userdata functions */
/****/

/* Create new userdata */
void *dst_userdata(Dst *vm, uint32_t dest, uint32_t size, const DstUserType *utype) {
    DstValue ud;
    char *data = dst_alloc(vm, sizeof(DstUserdataHeader) + size);
    DstUserdataHeader *header = (DstUserdataHeader *)data;
    void *user = data + sizeof(DstUserdataHeader);
    header->size = size;
    header->type = utype;
    dst_mem_tag(header, DST_MEMTAG_USER);
    ud.type = DST_USERDATA;
    ud.data.pointer = user;
    dst_set_arg(vm, dest, ud);
    return user;
}

/****/
/* Table functions */
/****/

DstTable *dst_make_table(Dst *vm, uint32_t capacity) {
    DstTable *t = dst_alloc(vm, sizeof(DstTable));
    /* Prevent immediate collection */
    dst_mark_mem(vm, t);
    DstValue *data;
    if (capacity < 2) capacity = 2;
    data = dst_zalloc(vm, capacity * sizeof(DstValue));
    t->data = data;
    t->capacity = capacity;
    t->count = 0;
    t->deleted = 0;
    return t;
}

/* Create a new table */
void dst_table(Dst *vm, uint32_t dest, uint32_t capacity) {
    DstValue tab;
    tab.type = DST_TABLE;
    tab.data.table = dst_make_table(vm, capacity);
    dst_set_arg(vm, dest, tab);
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in the table. */
static DstValue *dst_table_find(DstTable *t, DstValue key) {
    uint32_t index = (dst_value_hash(key) % (t->capacity / 2)) * 2;
    uint32_t i, j;
    uint32_t start[2], end[2];
    start[0] = index; end[0] = t->capacity;
    start[1] = 0; end[1] = index;
    for (j = 0; j < 2; ++j)
        for (i = start[j]; i < end[j]; i += 2) {
            if (t->data[i].type == DST_NIL) {
                if (t->data[i + 1].type == DST_NIL) {
                    /* Empty */
                    return t->data + i;
                }
            } else if (dst_value_equals(t->data[i], key)) {
                return t->data + i;
            }
        }
    return NULL;
}

/* Resize the dictionary table. */
static void dst_table_rehash(Dst *vm, DstTable *t, uint32_t size) {
    DstValue *olddata = t->data;
    DstValue *newdata = dst_zalloc(vm, size * sizeof(DstValue));
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
}

/* Get a value out of the object */
DstValue dst_table_get(DstTable *t, DstValue key) {
    DstValue *bucket = dst_table_find(t, key);
    if (bucket && bucket[0].type != DST_NIL)
        return bucket[1];
    else
        return dst_wrap_nil();
}

/* Remove an entry from the dictionary */
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
            bucket[0] = key;
            bucket[1] = value;
            ++t->count;
        }
    }
}

/* Clear a table */
void dst_table_clear(Dst *vm, uint32_t table) {
    DstTable *t;
    if (!dst_checktype(vm, table, DST_TABLE)) {
        dst_cerr(vm, "expected table");
        return;
    }
    t = dst_arg(vm, table).data.table;
    uint32_t capacity = t->capacity;
    uint32_t i;
    DstValue *data = t->data;
    for (i = 0; i < capacity; i += 2)
        data[i].type = DST_NIL;
    t->count = 0;
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

/****/
/* Immutable Data Structures */
/****/

/* Calculate hash for string */
static uint32_t dst_string_calchash(const uint8_t *str, uint32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Calculate hash for tuple (and struct) */
static uint32_t dst_tuple_calchash(const DstValue *tuple, uint32_t len) {
    const DstValue *end = tuple + len;
    uint32_t hash = 5381;
    while (tuple < end)
        hash = (hash << 5) + hash + dst_value_hash(*tuple++);
    return hash;
}

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
        if (dst_string_hash(x.data.string) != dst_string_hash(y.data.string)) return 0;
        if (dst_string_length(x.data.string) != dst_string_length(y.data.string)) return 0;
        len = dst_string_length(x.data.string);
        for (i = 0; i < len; ++i)
            if (x.data.string[i] != y.data.string[i])
                return 0;
        return 1;
    case DST_STRUCT:
        if (dst_struct_hash(x.data.st) != dst_struct_hash(y.data.st)) return 0;
        if (dst_struct_length(x.data.st) != dst_struct_length(y.data.st)) return 0;
        len = dst_struct_capacity(x.data.st);
        for (i = 0; i < len; ++i)
            if (!dst_value_equals(x.data.st[i], y.data.st[i]))
                return 0;
        return 1;
    case DST_TUPLE:
        if (dst_tuple_hash(x.data.tuple) != dst_tuple_hash(y.data.tuple)) return 0;
        if (dst_tuple_length(x.data.tuple) != dst_tuple_length(y.data.tuple)) return 0;
        len = dst_tuple_length(x.data.tuple);
        for (i = 0; i < len; ++i)
            if (!dst_value_equals(x.data.tuple[i], y.data.tuple[i]))
                return 0;
        return 1;
    }
}

/* Check if a value x is equal to a string. Special version of
 * dst_cache_equal */
static int dst_cache_strequal(DstValue x, const uint8_t *str, uint32_t len, uint32_t hash) {
    uint32_t i;
    if (x.type != DST_STRING) return 0;
    if (dst_string_hash(x.data.string) != hash) return 0;
    if (dst_string_length(x.data.string) != len) return 0;
    for (i = 0; i < len; ++i)
        if (x.data.string[i] != str[i])
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
static DstValue *dst_cache_strfind(Dst *vm,
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
    DstValue *newCache = dst_raw_calloc(1, newCapacity * sizeof(DstValue));
    if (newCache == NULL)
        DST_OUT_OF_MEMORY;
    oldCapacity = vm->cache_capacity;
    vm->cache = newCache;
    vm->cache_capacity = newCapacity;
    vm->cache_deleted = 0;
    /* Add all of the old strings back */
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
    dst_raw_free(oldCache);
}

/* Add a value to the cache given we know it is not
 * already in the cache and we have a bucket. */
static DstValue dst_cache_add_bucket(Dst *vm, DstValue x, DstValue *bucket) {
    if ((vm->cache_count + vm->cache_deleted) * 2 > vm->cache_capacity) {
        int status;
        dst_cache_resize(vm, vm->cache_count * 4);
        bucket = dst_cache_find(vm, x, &status);
    }
    /* Mark the memory for the gc */
    switch (x.type) {
    default:
        break;
    case DST_STRING:
        dst_mem_tag(dst_string_raw(x.data.string), DST_MEMTAG_STRING);
        break;
    case DST_STRUCT:
        dst_mem_tag(dst_struct_raw(x.data.st), DST_MEMTAG_STRUCT);
        break;
    case DST_TUPLE:
        dst_mem_tag(dst_tuple_raw(x.data.tuple), DST_MEMTAG_TUPLE);
        break;
    }
    /* Add x to the cache */
    vm->cache_count++;
    *bucket = x;
    return x;
}

/* Add a value to the cache */
static DstValue dst_cache_add(Dst *vm, DstValue x) {
    int status = 0;
    DstValue *bucket = dst_cache_find(vm, x, &status);
    if (!status) {
        return dst_cache_add_bucket(vm, x, bucket);
    } else {
        return *bucket;
    }
}


/* Remove a value from the cache */
static void dst_cache_remove(Dst *vm, DstValue x) {
    int status = 0;
    DstValue *bucket = dst_cache_find(vm, x, &status);
    if (status) {
        vm->cache_count--;
        vm->cache_deleted++;
        bucket->type = DST_BOOLEAN;
    }
}

/* Remove a string from cache (called from gc) */
void dst_cache_remove_string(Dst *vm, char *strmem) {
    DstValue x;
    x.type = DST_STRING;
    x.data.string = (const uint8_t *)(strmem + 2 * sizeof(uint32_t));
    dst_cache_remove(vm, x);
}

/* Remove a tuple from cache (called from gc) */
void dst_cache_remove_tuple(Dst *vm, char *tuplemem) {
    DstValue x;
    x.type = DST_TUPLE;
    x.data.tuple = (const DstValue *)(tuplemem + 2 * sizeof(uint32_t));
    dst_cache_remove(vm, x);
}

/* Remove a struct from cache (called from gc) */
void dst_cache_remove_struct(Dst *vm, char *structmem) {
    DstValue x;
    x.type = DST_STRUCT;
    x.data.st = (const DstValue *)(structmem + 2 * sizeof(uint32_t));
    dst_cache_remove(vm, x);
}

/****/
/* Struct Functions */
/****/

/* Begin creation of a struct */
DstValue *dst_struct_begin(Dst *vm, uint32_t count) {
    char *data = dst_zalloc(vm, sizeof(uint32_t) * 2 + 4 * count * sizeof(DstValue));
    DstValue *st = (DstValue *) (data + 2 * sizeof(uint32_t));
    dst_struct_length(st) = count;
    return st;
}

/* Find an item in a struct */
static const DstValue *dst_struct_find(const DstValue *st, DstValue key) {
    uint32_t cap = dst_struct_capacity(st);
    uint32_t index = (dst_value_hash(key) % (cap / 2)) * 2;
    uint32_t i;
    for (i = index; i < cap; i += 2)
        if (st[i].type == DST_NIL || dst_value_equals(st[i], key))
            return st + i;
    for (i = 0; i < index; i += 2)
        if (st[i].type == DST_NIL || dst_value_equals(st[i], key))
            return st + i;
    return NULL;
}

/* Put a kv pair into a struct that has not yet been fully constructed.
 * Behavior is undefined if too many keys are added, or if a key is added
 * twice. Nil keys and values are ignored. */
void dst_struct_put(DstValue *st, DstValue key, DstValue value) {
    uint32_t cap = dst_struct_capacity(st);
    uint32_t hash = dst_value_hash(key);
    uint32_t index = (hash % (cap / 2)) * 2;
    uint32_t i, j, dist;
    uint32_t bounds[4] = {index, cap, 0, index};
    if (key.type == DST_NIL || value.type == DST_NIL) return;
    for (dist = 0, j = 0; j < 4; j += 2)
    for (i = bounds[j]; i < bounds[j + 1]; i += 2, dist += 2) {
        int status;
        uint32_t otherhash, otherindex, otherdist;
        /* We found an empty slot, so just add key and value */
        if (st[i].type == DST_NIL) {
            st[i] = key;
            st[i + 1] = value;
            return;
        }
        /* Robinhood hashing - check if colliding kv pair
         * is closer to their source than current. */
        otherhash = dst_value_hash(st[i]);
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
            status = dst_value_compare(key, st[i]);
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
const DstValue *dst_struct_end(Dst *vm, DstValue *st) {
    DstValue cached;
    DstValue check;
    dst_struct_hash(st) = dst_tuple_calchash(st, dst_struct_capacity(st));
    check.type = DST_STRUCT;
    check.data.st = (const DstValue *) st;
    cached = dst_cache_add(vm, check);
    return cached.data.st;
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

/****/
/* Tuple functions */
/****/

/* Create a new empty tuple of the given size. Expected to be
 * mutated immediately */
DstValue *dst_tuple_begin(Dst *vm, uint32_t length) {
    char *data = dst_alloc(vm, 2 * sizeof(uint32_t) + length * sizeof(DstValue));
    DstValue *tuple = (DstValue *)(data + (2 * sizeof(uint32_t)));
    dst_tuple_length(tuple) = length;
    return tuple;
}

/* Finish building a tuple */
const DstValue *dst_tuple_end(Dst *vm, DstValue *tuple) {
    DstValue cached;
    DstValue check;
    dst_tuple_hash(tuple) = dst_tuple_calchash(tuple, dst_tuple_length(tuple));
    check.type = DST_TUPLE;
    check.data.tuple = (const DstValue *) tuple;
    cached = dst_cache_add(vm, check);
    return cached.data.tuple;
}

/****/
/* String Functions */
/****/

/* Begin building a string */
uint8_t *dst_string_begin(Dst *vm, uint32_t length) {
    char *data = dst_alloc(vm, 2 * sizeof(uint32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(uint32_t));
    dst_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
void dst_string_end(Dst *vm, uint32_t dest, uint8_t *str) {
    DstValue cached;
    DstValue check;
    dst_string_hash(str) = dst_string_calchash(str, dst_string_length(str));
    check.type = DST_STRING;
    check.data.string = (const uint8_t *) str;
    cached = dst_cache_add(vm, check);
    dst_set_arg(vm, dest, cached);
}

void dst_symbol_end(Dst *vm, uint32_t dest, uint8_t *str) {
    DstValue cached;
    DstValue check;
    dst_string_hash(str) = dst_string_calchash(str, dst_string_length(str));
    check.type = DST_STRING;
    check.data.string = (const uint8_t *) str;
    cached = dst_cache_add(vm, check);
    cached.type = DST_SYMBOL;
    dst_set_arg(vm, dest, cached);
}

static DstValue wrap_string(const uint8_t *str) {
    DstValue x;
    x.type = DST_STRING;
    x.data.string = str;
    return x;
}

/* Load a buffer as a string */
const uint8_t *dst_string_b(Dst *vm, const uint8_t *buf, uint32_t len) {
    uint32_t hash = dst_string_calchash(buf, len);
    int status = 0;
    DstValue *bucket = dst_cache_strfind(vm, buf, len, hash, &status);
    if (status) {
        return bucket->data.string;
    } else {
        uint32_t newbufsize = len + 2 * sizeof(uint32_t) + 1;
        uint8_t *str = (uint8_t *)(dst_alloc(vm, newbufsize) + 2 * sizeof(uint32_t));
        dst_memcpy(str, buf, len);
        dst_string_length(str) = len;
        dst_string_hash(str) = hash;
        str[len] = 0;
        return dst_cache_add_bucket(vm, wrap_string(str), bucket).data.string;
    }
}

static void inc_counter(uint8_t *digits, int base, int len) {
    int i;
    uint8_t carry = 1;
    for (i = len - 1; i >= 0; --i) {
        digits[i] += carry;
        carry = 0;
        if (digits[i] == base) {
            digits[i] = 0;
            carry = 1;
        }
    }
}

/* Generate a unique symbol */
const uint8_t *dst_string_bu(Dst *vm, const uint8_t *buf, uint32_t len) {
    static const char base64[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "+-";
    DstValue *bucket;
    uint32_t hash;
    uint8_t counter[6] = {63, 63, 63, 63, 63, 63};
    /* Leave spaces for 6 base 64 digits and two dashes. That means 64^6 possible symbols, which
     * is enough */
    uint32_t newlen = len + 8;
    uint32_t newbufsize = newlen + 2 * sizeof(uint32_t) + 1;
    uint8_t *str = (uint8_t *)(dst_alloc(vm, newbufsize) + 2 * sizeof(uint32_t));
    dst_string_length(str) = newlen;
    dst_memcpy(str, buf, len);
    str[len] = '-';
    str[len + 1] = '-';
    str[newlen] = 0;
    uint8_t *saltbuf = str + len + 2;
    int status = 1;
    while (status) {
        int i;
        inc_counter(counter, 64, 6);
        for (i = 0; i < 6; ++i)
            saltbuf[i] = base64[counter[i]];
        hash = dst_string_calchash(str, newlen);
        bucket = dst_cache_strfind(vm, str, newlen, hash, &status);
    }
    dst_string_hash(str) = hash;
    return dst_cache_add_bucket(vm, wrap_string(str), bucket).data.string;
}

/* Generate a unique string from a cstring */
const uint8_t *dst_string_cu(Dst *vm, const char *s) {
    uint32_t len = 0;
    while (s[len]) ++len;
    return dst_string_bu(vm, (const uint8_t *)s, len);
}

/* Load a c string */
const uint8_t *dst_string_c(Dst *vm, const char *str) {
    uint32_t len = 0;
    while (str[len]) ++len;
    return dst_string_b(vm, (const uint8_t *)str, len);
}

/* Load a c string and return it as a DstValue */
DstValue dst_string_cv(Dst *vm, const char *str) {
    DstValue ret;
    const uint8_t *data = dst_string_c(vm, str);
    ret.type = DST_STRING;
    ret.data.string = data;
    return ret;
}

/* Load a c string and return it as a (symbol) DstValue. */
DstValue dst_string_cvs(Dst *vm, const char *str) {
    DstValue ret;
    /* Only put strings in cache */
    const uint8_t *data = dst_string_c(vm, str);
    ret.type = DST_SYMBOL;
    ret.data.string = data;
    return ret;
}

/* Compares two strings */
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = dst_string_length(lhs);
    uint32_t ylen = dst_string_length(rhs);
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

/* Get a value out af an associated data structure.
 * Returns possible c error message, and NULL for no error. The
 * useful return value is written to out on success */
const char *dst_value_get(DstValue ds, DstValue key, DstValue *out) {
    DstInteger index;
    DstValue ret;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.data.integer, ds.data.array->count);
        if (index < 0) return "invalid array access";
        ret = ds.data.array->data[index];
        break;
    case DST_TUPLE:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.data.integer, dst_tuple_length(ds.data.tuple));
        if (index < 0) return "invalid tuple access";
        ret = ds.data.tuple[index];
        break;
    case DST_BYTEBUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.data.integer, ds.data.buffer->count);
        if (index < 0) return "invalid buffer access";
        ret.type = DST_INTEGER;
        ret.data.integer = ds.data.buffer->data[index];
        break;
    case DST_STRING:
    case DST_SYMBOL:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.data.integer, dst_string_length(ds.data.string));
        if (index < 0) return "invalid string access";
        ret.type = DST_INTEGER;
        ret.data.integer = ds.data.string[index];
        break;
    case DST_STRUCT:
        ret = dst_struct_get(ds.data.st, key);
        break;
    case DST_TABLE:
        ret = dst_table_get(ds.data.table, key);
        break;
    default:
        return "cannot get";
    }
    *out = ret;
    return NULL;
}

void dst_get(Dst *vm, uint32_t dest, uint32_t ds, uint32_t key) {
    DstValue dsv = dst_arg(vm, ds);
    DstValue keyv = dst_arg(vm, key);
    DstValue ret = keyv;
    const char *err = dst_value_get(dsv, keyv, &ret);
    if (err) {
        vm->flags = 1;
        vm->ret = dst_string_cv(vm, err);
    } else {
        dst_set_arg(vm, dest, ret);
    }

}

/* Set a value in an associative data structure. Returns possible
 * error message, and NULL if no error. */
const char *dst_value_set(Dst *vm, DstValue ds, DstValue key, DstValue value) {
    DstInteger index;
    switch (ds.type) {
    case DST_ARRAY:
        if (key.type != DST_INTEGER) return "expected integer key";
        index = dst_startrange(key.data.integer, ds.data.array->count);
        if (index < 0) return "invalid array access";
        ds.data.array->data[index] = value;
        break;
    case DST_BYTEBUFFER:
        if (key.type != DST_INTEGER) return "expected integer key";
        if (value.type != DST_INTEGER) return "expected integer value";
        index = dst_startrange(key.data.integer, ds.data.buffer->count);
        if (index < 0) return "invalid buffer access";
        ds.data.buffer->data[index] = (uint8_t) value.data.integer;
        break;
    case DST_TABLE:
        dst_table_put(vm, ds.data.table, key, value);
        break;
    default:
        return "cannot set";
    }
    return NULL;
}

void dst_set(Dst *vm, uint32_t ds, uint32_t key, uint32_t value) {
    DstValue dsv = dst_arg(vm, ds);
    DstValue keyv = dst_arg(vm, key);
    DstValue valuev = dst_arg(vm, value);
    const char *err = dst_value_set(vm, dsv, keyv, valuev);
    if (err) {
        vm->flags = 1;
        vm->ret = dst_string_cv(vm, err);
    }
}

int dst_next(Dst *vm, uint32_t dest, uint32_t ds, uint32_t key) {
    DstValue dsv = dst_arg(vm, ds);
    DstValue keyv = dst_arg(vm, key);
    DstValue ret = keyv;
    switch(dsv.type) {
        default:
            vm->ret = dst_string_cv(vm, "expected table or struct");
            vm->flags = 1;
            return 0;
        case DST_TABLE:
            ret = dst_table_next(dsv.data.table, keyv);
            break;
        case DST_STRUCT:
            ret = dst_struct_next(dsv.data.st, keyv);
            break;
    }
    dst_set_arg(vm, dest, ret);
    return ret.type != DST_NIL;
}

void dst_ensure(Dst *vm, uint32_t ds, uint32_t capacity) {
    DstValue x = dst_arg(vm, ds);
    switch (x.type) {
        default:
            dst_cerr(vm, "could not ensure capacity");
            break;
        case DST_ARRAY:
            dst_value_array_ensure(vm, x.data.array, capacity);
            break;
        case DST_BYTEBUFFER:
            dst_value_buffer_ensure(vm, x.data.buffer, capacity);
            break;
    }
}

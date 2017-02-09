#include "dict.h"
#include "value.h"
#include "gc.h"

/* Create a new dictionary */
Dictionary * DictNew(GC * gc, uint32_t capacity) {
    Dictionary * dict = GCAlloc(gc, sizeof(Dictionary));
    DictBucket ** buckets = GCZalloc(gc, capacity * sizeof(DictBucket *));
    dict->buckets = buckets;
    dict->capacity = capacity;
    dict->count = 0;
    return dict;
}

/* Resize the dictionary table. */
static void DictReHash(GC * gc, Dictionary * dict, uint32_t size) {
    DictBucket ** newBuckets = GCZalloc(gc, size * sizeof(DictBucket *));
    uint32_t i, count;
    for (i = 0, count = dict->capacity; i < count; ++i) {
        DictBucket * bucket = dict->buckets[i];
        while (bucket) {
            uint32_t index;
            DictBucket * next = bucket->next;
            index = ValueHash(&bucket->key) % size;
            bucket->next = newBuckets[index];
            newBuckets[index] = bucket;
            bucket = next;
        }
    }
    dict->buckets = newBuckets;
    dict->capacity = size;
}

/* Find the bucket that contains the given key */
static DictBucket * DictFind(Dictionary * dict, Value * key) {
    uint32_t index = ValueHash(key) % dict->capacity;
    DictBucket * bucket = dict->buckets[index];
    while (bucket) {
        if (ValueEqual(&bucket->key, key))
            return bucket;
        bucket = bucket->next;
    }
    return (DictBucket *)0;
}

/* Get a value out of the dictionary */
Value DictGet(Dictionary * dict, Value * key) {
    DictBucket * bucket = DictFind(dict, key);
    if (bucket) {
        return bucket->value;
    } else {
        Value nil;
        nil.type = TYPE_NIL;
        return nil;
    }
}

/* Remove an entry from the dictionary */
Value DictRemove(GC * gc, Dictionary * dict, Value * key) {
    DictBucket * bucket, * previous;
    uint32_t index = ValueHash(key) % dict->capacity;
    bucket = dict->buckets[index];
    previous = (DictBucket *)0;
    while (bucket) {
        if (ValueEqual(&bucket->key, key)) {
            if (previous) {
                previous->next = bucket->next;
            } else {
                dict->buckets[index] = bucket->next;
            }
            if (dict->count < dict->capacity / 4) {
                DictReHash(gc, dict, dict->capacity / 2);
            }
            --dict->count;
            return bucket->value;
        }
        previous = bucket;
        bucket = bucket->next;
    }
    /* Return nil if we found nothing */
    {
        Value nil;
        nil.type = TYPE_NIL;
        return nil;
    }
}

/* Put a value into the dictionary. Returns 1 if successful, 0 if out of memory.
 * The VM pointer is needed for memory allocation. */
void DictPut(GC * gc, Dictionary * dict, Value * key, Value * value) {
    DictBucket * bucket, * previous;
    uint32_t index = ValueHash(key) % dict->capacity;
    if (key->type == TYPE_NIL) return;
    /* Do a removal if value is nil */
    if (value->type == TYPE_NIL) {
        bucket = dict->buckets[index];
        previous = (DictBucket *)0;
        while (bucket) {
            if (ValueEqual(&bucket->key, key)) {
                if (previous) {
                    previous->next = bucket->next;
                } else {
                    dict->buckets[index] = bucket->next;
                }
                if (dict->count < dict->capacity / 4) {
                    DictReHash(gc, dict, dict->capacity / 2);
                }
                --dict->count;
                return;
            }
            previous = bucket;
            bucket = bucket->next;
        }
    } else {
        bucket = DictFind(dict, key);
        if (bucket) {
            bucket->value = *value;
        } else {
            if (dict->count >= 2 * dict->capacity) {
                DictReHash(gc, dict, 2 * dict->capacity);
            }
            bucket = GCAlloc(gc, sizeof(DictBucket));
            bucket->next = dict->buckets[index];
            bucket->value = *value;
            bucket->key = *key;
            dict->buckets[index] = bucket;
            ++dict->count;
        }
    }
}

/* Begin iteration through a dictionary */
void DictIterate(Dictionary * dict, DictionaryIterator * iterator) {
    iterator->index = 0;
    iterator->dict = dict;
    iterator->bucket = dict->buckets[0];
}

/* Provides a mechanism for iterating through a table. */
int DictIterateNext(DictionaryIterator * iterator, DictBucket ** bucket) {
    Dictionary * dict = iterator->dict;
    for (;;) {
        if (iterator->bucket) {
            *bucket = iterator->bucket;
            iterator->bucket = iterator->bucket->next;
            return 1;
        }
        if (++iterator->index >= dict->capacity) break;
        iterator->bucket = dict->buckets[iterator->index];
    }
    return 0;
}

#include "ds.h"
#include "value.h"
#include "vm.h"
#include <string.h>

/****/
/* Buffer functions */
/****/

/* Create a new Buffer */
Buffer * BufferNew(VM * vm, uint32_t capacity) {
    Buffer * buffer = VMAlloc(vm, sizeof(Buffer));
    uint8_t * data = VMAlloc(vm, sizeof(uint8_t) * capacity);
    buffer->data = data;
    buffer->count = 0;
    buffer->capacity = capacity;
    return buffer;
}

/* Ensure that the buffer has enough internal capacity */
void BufferEnsure(VM * vm, Buffer * buffer, uint32_t capacity) {
    uint8_t * newData;
    if (capacity <= buffer->capacity) return;
    newData = VMAlloc(vm, capacity * sizeof(uint8_t));
    memcpy(newData, buffer->data, buffer->count * sizeof(uint8_t));
    buffer->data = newData;
    buffer->capacity = capacity;
}

/* Get a byte from an index in the buffer */
int32_t BufferGet(Buffer * buffer, uint32_t index) {
    if (index < buffer->count) {
        return buffer->data[index];
    } else {
        return -1;
    }
}

/* Push a byte into the buffer */
void BufferPush(VM * vm, Buffer * buffer, uint8_t c) {
    if (buffer->count >= buffer->capacity) {
        BufferEnsure(vm, buffer, 2 * buffer->count);
    }
    buffer->data[buffer->count++] = c;
}

/* Push multiple bytes into the buffer */
void BufferAppendData(VM * vm, Buffer * buffer, uint8_t * string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        BufferEnsure(vm, buffer, 2 * newSize);
    }
    memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

/* Convert the buffer to a string */
uint8_t * BufferToString(VM * vm, Buffer * buffer) {
    uint8_t * data = VMAlloc(vm, buffer->count + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    VStringSize(data) = buffer->count;
    VStringHash(data) = 0;
    memcpy(data, buffer->data, buffer->count * sizeof(uint8_t));
    return data;
}

/****/
/* Array functions */
/****/

/* Creates a new array */
Array * ArrayNew(VM * vm, uint32_t capacity) {
    Array * array = VMAlloc(vm, sizeof(Array));
    Value * data = VMAlloc(vm, capacity * sizeof(Value));
    array->data = data;
    array->count = 0;
    array->capacity = capacity;
    return array;
}

/* Ensure the array has enough capacity for capacity elements */
void ArrayEnsure(VM * vm, Array * array, uint32_t capacity) {
    Value * newData;
    if (capacity <= array->capacity) return;
    newData = VMAlloc(vm, capacity * sizeof(Value));
    memcpy(newData, array->data, array->count * sizeof(Value));
    array->data = newData;
    array->capacity = capacity;
}

/* Get a value of an array with bounds checking. */
Value ArrayGet(Array * array, uint32_t index) {
    if (index < array->count) {
        return array->data[index];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

/* Try to set an index in the array. Return 1 if successful, 0
 * on failiure */
int ArraySet(Array * array, uint32_t index, Value x) {
    if (index < array->count) {
        array->data[index] = x;
        return 1;
    } else {
        return 0;
    }
}

/* Add an item to the end of the array */
void ArrayPush(VM * vm, Array * array, Value x) {
    if (array->count >= array->capacity) {
        ArrayEnsure(vm, array, 2 * array->count);
    }
    array->data[array->count++] = x;
}

/* Remove the last item from the Array and return it */
Value ArrayPop(Array * array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

/* Look at the last item in the Array */
Value ArrayPeek(Array * array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

/****/
/* Dictionary functions */
/****/

/* Create a new dictionary */
Dictionary * DictNew(VM * vm, uint32_t capacity) {
    Dictionary * dict = VMAlloc(vm, sizeof(Dictionary));
    DictBucket ** buckets = VMZalloc(vm, capacity * sizeof(DictBucket *));
    dict->buckets = buckets;
    dict->capacity = capacity;
    dict->count = 0;
    return dict;
}

/* Resize the dictionary table. */
static void DictReHash(VM * vm, Dictionary * dict, uint32_t size) {
    DictBucket ** newBuckets = VMZalloc(vm, size * sizeof(DictBucket *));
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
Value DictRemove(VM * vm, Dictionary * dict, Value * key) {
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
                DictReHash(vm, dict, dict->capacity / 2);
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
void DictPut(VM * vm, Dictionary * dict, Value * key, Value * value) {
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
                    DictReHash(vm, dict, dict->capacity / 2);
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
                DictReHash(vm, dict, 2 * dict->capacity);
            }
            bucket = VMAlloc(vm, sizeof(DictBucket));
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

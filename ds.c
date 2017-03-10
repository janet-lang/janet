#include "util.h"
#include "ds.h"
#include "value.h"
#include "vm.h"

/****/
/* Buffer functions */
/****/

/* Create a new Buffer */
GstBuffer *gst_buffer(Gst *vm, uint32_t capacity) {
    GstBuffer *buffer = gst_alloc(vm, sizeof(GstBuffer));
    uint8_t *data = gst_alloc(vm, sizeof(uint8_t) * capacity);
    buffer->data = data;
    buffer->count = 0;
    buffer->capacity = capacity;
    buffer->flags = 0;
    return buffer;
}

/* Ensure that the buffer has enough internal capacity */
void gst_buffer_ensure(Gst *vm, GstBuffer *buffer, uint32_t capacity) {
    uint8_t * newData;
    if (capacity <= buffer->capacity) return;
    newData = gst_alloc(vm, capacity * sizeof(uint8_t));
    gst_memcpy(newData, buffer->data, buffer->count * sizeof(uint8_t));
    buffer->data = newData;
    buffer->capacity = capacity;
}

/* Get a byte from an index in the buffer */
int gst_buffer_get(GstBuffer *buffer, uint32_t index) {
    if (index < buffer->count) {
        return buffer->data[index];
    } else {
        return -1;
    }
}

/* Push a byte into the buffer */
void gst_buffer_push(Gst *vm, GstBuffer * buffer, uint8_t c) {
    if (buffer->count >= buffer->capacity) {
        gst_buffer_ensure(vm, buffer, 2 * buffer->count);
    }
    buffer->data[buffer->count++] = c;
}

/* Push multiple bytes into the buffer */
void gst_buffer_append(Gst *vm, GstBuffer *buffer, uint8_t *string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        gst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    gst_memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

/* Convert the buffer to a string */
uint8_t *gst_buffer_to_string(Gst *vm, GstBuffer *buffer) {
    uint8_t *data = gst_alloc(vm, buffer->count + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    gst_string_length(data) = buffer->count;
    gst_string_hash(data) = 0;
    gst_memcpy(data, buffer->data, buffer->count * sizeof(uint8_t));
    return data;
}

/****/
/* Array functions */
/****/

/* Creates a new array */
GstArray *gst_array(Gst * vm, uint32_t capacity) {
    GstArray *array = gst_alloc(vm, sizeof(GstArray));
    GstValue *data = gst_alloc(vm, capacity * sizeof(GstValue));
    array->data = data;
    array->count = 0;
    array->capacity = capacity;
    array->flags = 0;
    return array;
}

/* Ensure the array has enough capacity for capacity elements */
void gst_array_ensure(Gst *vm, GstArray *array, uint32_t capacity) {
    GstValue *newData;
    if (capacity <= array->capacity) return;
    newData = gst_alloc(vm, capacity * sizeof(GstValue));
    gst_memcpy(newData, array->data, array->capacity * sizeof(GstValue));
    array->data = newData;
    array->capacity = capacity;
}

/* Get a value of an array with bounds checking. */
GstValue gst_array_get(GstArray *array, uint32_t index) {
    if (index < array->count) {
        return array->data[index];
    } else {
        GstValue v;
        v.type = GST_NIL;
        return v;
    }
}

/* Try to set an index in the array. Return 1 if successful, 0
 * on failiure */
int gst_array_set(GstArray *array, uint32_t index, GstValue x) {
    if (index < array->count) {
        array->data[index] = x;
        return 1;
    } else {
        return 0;
    }
}

/* Add an item to the end of the array */
void gst_array_push(Gst *vm, GstArray *array, GstValue x) {
    if (array->count >= array->capacity) {
        gst_array_ensure(vm, array, 2 * array->count);
    }
    array->data[array->count++] = x;
}

/* Remove the last item from the Array and return it */
GstValue gst_array_pop(GstArray *array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        GstValue v;
        v.type = GST_NIL;
        return v;
    }
}

/* Look at the last item in the Array */
GstValue gst_array_peek(GstArray *array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        GstValue v;
        v.type = GST_NIL;
        return v;
    }
}

/****/
/* Tuple functions */
/****/

/* Create a new emoty tuple of the given size. Expected to be
 * mutated immediately */
GstValue *gst_tuple(Gst *vm, uint32_t length) {
    char *data = gst_alloc(vm, 2 * sizeof(uint32_t) + length * sizeof(GstValue));
    GstValue *tuple = (GstValue *)(data + (2 * sizeof(uint32_t)));
    gst_tuple_length(tuple) = length;
    gst_tuple_hash(tuple) = 0;
    return tuple;
}

/****/
/* Userdata functions */
/****/

/* Create new userdata */
void *gst_userdata(Gst *vm, uint32_t size, GstObject *meta) {
    char *data = gst_alloc(vm, sizeof(GstUserdataHeader) + size);
    GstUserdataHeader *header = (GstUserdataHeader *)data;
    void *user = data + sizeof(GstUserdataHeader);
    header->size = size;
    header->meta = meta;
    return user;
}

/****/
/* Dictionary functions */
/****/

/* Create a new dictionary */
GstObject* gst_object(Gst *vm, uint32_t capacity) {
    GstObject *o = gst_alloc(vm, sizeof(GstObject));
    GstBucket **buckets = gst_zalloc(vm, capacity * sizeof(GstBucket *));
    o->buckets = buckets;
    o->capacity = capacity;
    o->count = 0;
    o->flags = 0;
    return o;
}

/* Resize the dictionary table. */
static void gst_object_rehash(Gst *vm, GstObject *o, uint32_t size) {
    GstBucket **newBuckets = gst_zalloc(vm, size * sizeof(GstBucket *));
    uint32_t i, count;
    for (i = 0, count = o->capacity; i < count; ++i) {
        GstBucket *bucket = o->buckets[i];
        while (bucket) {
            uint32_t index;
            GstBucket *next = bucket->next;
            index = gst_hash(bucket->key) % size;
            bucket->next = newBuckets[index];
            newBuckets[index] = bucket;
            bucket = next;
        }
    }
    o->buckets = newBuckets;
    o->capacity = size;
}

/* Find the bucket that contains the given key */
static GstBucket *gst_object_find(GstObject *o, GstValue key) {
    uint32_t index = gst_hash(key) % o->capacity;
    GstBucket *bucket = o->buckets[index];
    while (bucket) {
        if (gst_equals(bucket->key, key))
            return bucket;
        bucket = bucket->next;
    }
    return (GstBucket *)0;
}

/* Get a value out of the object */
GstValue gst_object_get(GstObject *o, GstValue key) {
    GstBucket *bucket = gst_object_find(o, key);
    if (bucket) {
        return bucket->value;
    } else {
        GstValue nil;
        nil.type = GST_NIL;
        return nil;
    }
}

/* Get a value of the object with a cstring key */
GstValue gst_object_get_cstring(GstObject *obj, const char *key) {
    const char *end = key;
    while (*end++);
    uint32_t len = end - key;
    uint32_t hash = gst_cstring_calchash((uint8_t *)key, len);
    uint32_t index = hash % obj->capacity;
    GstBucket *bucket = obj->buckets[index];
    while (bucket) {
        if (bucket->key.type == GST_STRING) {
            uint8_t *s = bucket->key.data.string;
            if (gst_string_length(s) == len) {
                if (!gst_string_hash(s))
                    gst_string_hash(s) = gst_string_calchash(s);
                if (gst_string_hash(s) == hash) {
                    uint32_t i;
                    for (i = 0; i < len; ++i)
                        if (s[i] != key[i])
                            goto notequal;
                    return bucket->value;
                }
            }
        }
notequal:
        bucket = bucket->next;
    }
    /* Return nil */
    {
        GstValue ret;
        ret.type = GST_NIL;
        return ret;
    }
}

/* Remove an entry from the dictionary */
GstValue gst_object_remove(Gst * vm, GstObject *o, GstValue key) {
    GstBucket *bucket, *previous;
    uint32_t index = gst_hash(key) % o->capacity;
    bucket = o->buckets[index];
    previous = (GstBucket *)0;
    while (bucket) {
        if (gst_equals(bucket->key, key)) {
            if (previous) {
                previous->next = bucket->next;
            } else {
                o->buckets[index] = bucket->next;
            }
            if (o->count < o->capacity / 4) {
                gst_object_rehash(vm, o, o->capacity / 2);
            }
            --o->count;
            return bucket->value;
        }
        previous = bucket;
        bucket = bucket->next;
    }
    /* Return nil if we found nothing */
    {
        GstValue nil;
        nil.type = GST_NIL;
        return nil;
    }
}

/* Put a value into the dictionary. Returns 1 if successful, 0 if out of memory.
 * The VM pointer is needed for memory allocation. */
void gst_object_put(Gst *vm, GstObject *o, GstValue key, GstValue value) {
    GstBucket *bucket, *previous;
    uint32_t index = gst_hash(key) % o->capacity;
    if (key.type == GST_NIL) return;
    /* Do a removal if value is nil */
    if (value.type == GST_NIL) {
        bucket = o->buckets[index];
        previous = (GstBucket *)0;
        while (bucket) {
            if (gst_equals(bucket->key, key)) {
                if (previous) {
                    previous->next = bucket->next;
                } else {
                    o->buckets[index] = bucket->next;
                }
                if (o->count < o->capacity / 4) {
                    gst_object_rehash(vm, o, o->capacity / 2);
                }
                --o->count;
                return;
            }
            previous = bucket;
            bucket = bucket->next;
        }
    } else {
        bucket = gst_object_find(o, key);
        if (bucket) {
            bucket->value = value;
        } else {
            if (o->count >= 2 * o->capacity) {
                gst_object_rehash(vm, o, 2 * o->capacity);
            }
            bucket = gst_alloc(vm, sizeof(GstBucket));
            bucket->next = o->buckets[index];
            bucket->value = value;
            bucket->key = key;
            o->buckets[index] = bucket;
            ++o->count;
        }
    }
}

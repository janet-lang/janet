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
void gst_buffer_append(Gst *vm, GstBuffer *buffer, const uint8_t *string, uint32_t length) {
    uint32_t newSize = buffer->count + length;
    if (newSize > buffer->capacity) {
        gst_buffer_ensure(vm, buffer, 2 * newSize);
    }
    gst_memcpy(buffer->data + buffer->count, string, length);
    buffer->count = newSize;
}

/* Convert the buffer to a string */
const uint8_t *gst_buffer_to_string(Gst *vm, GstBuffer *buffer) {
    return gst_string_b(vm, buffer->data, buffer->count);
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
/* Userdata functions */
/****/

/* Create new userdata */
void *gst_userdata(Gst *vm, uint32_t size, const GstValue *meta) {
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
    GstValue *data = gst_zalloc(vm, capacity * sizeof(GstValue));
    o->data = data;
    o->capacity = capacity;
    o->count = 0;
    o->parent = NULL;
    o->deleted = 0;
    return o;
}

/* Find the bucket that contains the given key. Will also return
 * bucket where key should go if not in object. */
static GstValue *gst_object_find(GstObject *o, GstValue key) {
    uint32_t index = (gst_hash(key) % (o->capacity / 2)) * 2;
    uint32_t i, j;
    uint32_t start[2], end[2];
    start[0] = index; end[0] = o->capacity;
    start[1] = 0; end[1] = index;
    for (j = 0; j < 2; ++j)
        for (i = start[j]; i < end[j]; i += 2) {
            if (o->data[i].type == GST_NIL) {
                if (o->data[i + 1].type == GST_NIL) {
                    /* Empty */
                    return o->data + i;
                }
            } else if (gst_equals(o->data[i], key)) {
                return o->data + i;
            }
        }
    return NULL;
}

/* Resize the dictionary table. */
static void gst_object_rehash(Gst *vm, GstObject *o, uint32_t size) {
    GstValue *olddata = o->data;
    GstValue *newdata = gst_zalloc(vm, size * sizeof(GstValue));
    uint32_t i, oldcapacity;
    oldcapacity = o->capacity;
    o->data = newdata;
    o->capacity = size;
    o->deleted = 0;
    for (i = 0; i < oldcapacity; i += 2) {
        if (olddata[i].type != GST_NIL) {
            GstValue *bucket = gst_object_find(o, olddata[i]);
            bucket[0] = olddata[i];
            bucket[1] = olddata[i + 1];
        }
    }
}

/* Get a value out of the object */
GstValue gst_object_get(GstObject *o, GstValue key) {
    GstValue *bucket = gst_object_find(o, key);
    if (bucket && bucket[0].type != GST_NIL) {
        return bucket[1];
    } else {
        GstValue nil;
        nil.type = GST_NIL;
        return nil;
    }
}

/* Remove an entry from the dictionary */
GstValue gst_object_remove(GstObject *o, GstValue key) {
    GstValue *bucket = gst_object_find(o, key);
    if (bucket && bucket[0].type != GST_NIL) {
        GstValue ret = bucket[1];
        o->count--;
        o->deleted++;
        bucket[0].type = GST_NIL;
        bucket[1].type = GST_BOOLEAN;
        return ret;
    } else {
        GstValue nil;
        nil.type = GST_NIL;
        return nil;
    }
}

/* Put a value into the object */
void gst_object_put(Gst *vm, GstObject *o, GstValue key, GstValue value) {
    if (key.type == GST_NIL) return;
    if (value.type == GST_NIL) {
        gst_object_remove(o, key);
    } else {
        GstValue *bucket = gst_object_find(o, key);
        if (bucket && bucket[0].type != GST_NIL) {
            bucket[1] = value;
        } else {
            if (!bucket || 4 * (o->count + o->deleted) >= o->capacity) {
                gst_object_rehash(vm, o, 4 * o->count + 6);
            }
            bucket = gst_object_find(o, key);
            bucket[0] = key;
            bucket[1] = value;
            ++o->count;
        }
    }
}

/* Find next key in an object. Returns nil if no next key. */
GstValue gst_object_next(GstObject *o, GstValue key) {
    GstValue ret;
    GstValue *bucket;
    if (key.type == GST_NIL)
       bucket = o->data - 2;
    else
       bucket = gst_object_find(o, key); 
    if (bucket && bucket[0].type != GST_NIL) {
        GstValue *nextbucket, *end;
        end = o->data + o->capacity;
        for (nextbucket = bucket + 2; nextbucket < end; nextbucket += 2) {
            if (nextbucket[0].type != GST_NIL)
                return nextbucket[0];
        }
    }
    ret.type = GST_NIL;
    return ret;
}


#include "dict.h"
#include "util.h"
#include "value.h"
#include "vm.h"

/****/
/* Bag implementation */
/****/

/* Find a kv pair in a bag */
static GstValue *gst_object_bag_find(GstDict *obj, GstValue key) {
    GstValue *start = obj->data;
    GstValue *end = obj->data + obj->count * 2;
    while (start < end) {
        if (gst_equals(*start, key))
            return start;
        start += 2;
    }
    return NULL;
}

/* Check for string equality */
static int str_equal_value(GstValue v, const char *str, uint32_t len, uint32_t hash) {
    uint32_t i;
    if (v.type != GST_STRING) return 0;
    if (gst_string_length(str) != len) return 0;
    if (!gst_string_hash(str))
        gst_string_hash(str) = gst_string_calchash((uint8_t *)str);
    if (gst_string_hash(str) != hash) return 0;
    for (i = 0; i < len; ++i)
        if (str[1] != v.data.string[i]) return 0;
    return 1;
}

/* Find key value pair with c string key */
static GstValue *gst_object_bag_findcstring(GstDict *obj, const char *key) {
    uint32_t len, hash;
    for (len = 0; key[len]; ++len);
    hash = gst_cstring_calchash((uint8_t *)key, len);
    GstValue *start = obj->data;
    GstValue *end = obj->data + obj->count * 2;
    while (start < end) {
        if (start->type == GST_STRING) {
            uint8_t *str = start->data.string;
            if (gst_string_length(str) == len) {
                if (!gst_string_hash(str))
                    gst_string_hash(str) = gst_string_calchash(str);
                if (gst_string_hash(str) == hash) {
                    return start
                }
            }
        }
        start += 2;
    }
    return NULL;
}

/* Remove a key from a bag */
static void gst_object_bag_remove(GstDict *obj, GstValue key) {
    GstValue *kv = gst_object_bag_find(obj, key);
    if (kv != NULL) {
        GstValue *lastKv = obj->data + --obj->count * 2;
        kv[0] = lastKv[0];
        kv[1] = lastKv[1];
    }
}

/* Add a key to a bag */
static void gst_object_bag_put(Gst *vm, GstDict *obj, GstValue key, GstValue value) {
    GstValue *kv = gst_object_bag_find(obj, key);
    if (kv != NULL) {
        /* Replace value */
        kv[1] = value;
    } else {
        /* Check for need to resize */
        if (obj->count + 1 > obj->capacity) {
            uint32_t newCap = 2 * obj->count + 2;
            GstValue *newData = gst_alloc(vm, sizeof(GstValue) * 2 * newCap);
            gst_memcpy(newData, obj->data, obj->count * 2 * sizeof(GstValue));
            obj->data = newData;
            obj->capacity = newCap;
        }
        /* Push to end */
        kv = obj->data + obj->count * 2;
        kv[0] = key;
        kv[1] = value;
        ++obj->count;
    }
}

/****/
/* Hashtable implementaion */
/****/

/* Add a key value pair to a given array. Returns if key successfully added. */ 
static void hash_putkv(GstValue *data, uint32_t cap, GstValue key, GstValue value) {
    GstValue *end = data + 2 * cap;
    GstValue *start = data + (gst_hash(key) % cap) * 2;
    GstValue *bucket;
    /* Check second half of array */
    for (bucket = start; bucket < end; bucket += 2) {
        if (bucket[0].type == GST_NIL) {
            bucket[0] = key;
            bucket[1] = value;
            return;
        }
    }
    /* Check first half of array */
    for (bucket = data; bucket < start; bucket += 2) {
        if (bucket[0].type == GST_NIL) {
            bucket[0] = key;
            bucket[1] = value;
            return;
        }
    }
    /* Should never reach here - data would be full */
}

/* Find a bucket in the hastable */
static GstValue *hash_findkv(GstValue *data, uint32_t cap, GstValue key, GstValue **out) {
    GstValue *end = data + 2 * cap;
    GstValue *start = data + (gst_hash(key) % cap) * 2;
    GstValue *bucket;
    /* Check second half of array */
    for (bucket = start; bucket < end; bucket += 2)
        if (bucket[0].type == GST_NIL)
            if (bucket[1].type == GST_BOOLEAN) /* Check if just marked deleted */
                continue;
            else {
                *out = bucket;
                return NULL;
            }
        else if (gst_equals(bucket[0], key))
            return bucket;
    /* Check first half of array */
    for (bucket = data; bucket < start; bucket += 2)
        if (bucket[0].type == GST_NIL)
            if (bucket[1].type == GST_BOOLEAN) /* Check if just marked deleted */
                continue;
            else {
                *out = bucket;
                return NULL;
            }
        else if (gst_equals(bucket[0], key))
            return bucket;
    /* Should never reach here - data would be full */
    *out = bucket;
    return NULL;
}

/* Resize internal hashtable. Also works if currently a bag. */
static void gst_object_rehash(Gst *vm, GstDict *obj, uint32_t capacity) {
    GstValue *toData, *fromBucket, *toBucket, *toStart *fromEnd, *toEnd;
    toData = gst_alloc(vm, capacity * 2 * sizeof(GstValue));
    toEnd = toData + 2 * capacity;
    fromBucket = obj->data;
    fromEnd = fromBucket + obj->count * 2;
    for (; fromBucket < fromEnd; fromBucket += 2) { 
        if (fromBucket[0].type == GST_NIL) continue;
        toStart = toData + (gst_hash(fromBucket[0]) % capacity) * 2;
        /* Check second half of array */
        for (toBucket = toStart; toBucket < toEnd; toBucket += 2) {
            if (toBucket[0].type == GST_NIL) {
                toBucket[0] = fromBucket[0];
                toBucket[1] = fromBucket[1];
                goto finish_put;
            }
        }
        /* Check first half of array */
        for (toBucket = toData; toBucket < toStart; toBucket += 2) {
            if (toBucket[0].type == GST_NIL) {
                toBucket[0] = fromBucket[0];
                toBucket[1] = fromBucket[1];
                goto finish_put;
            }
        }
        /* Error if we got here - backing array to small. */
        ;
        /* Continue. */
        finish_put: continue;
    }
    obj->capacity = capacity;
    obj->data = toData;
}

/****/
/* Interface */
/****/

/* Initialize a dictionary */
GstDict *gst_dict(Gst *vm, uint32_t capacity) {
    GstDict *dict = gst_alloc(vm, sizeof(GstDict));
    GstValue *data = gst_zalloc(vm, sizeof(GstValue) * 2 * capacity);
    dict->data = data;
    dict->capacity = capacity;
    dict->count = 0;
    dict->flags = (capacity < GST_OBJECT_BAG_THRESHOLD) ? GST_OBJECT_FLAG_ISBAG : 0;
    return dict;
}

/* Get item from dictionary */
GstValue gst_dict_get(GstDict *dict, GstValue key) {
    GstValue *bucket *notused;
    if (dict->flags & GST_OBJECT_FLAG_ISBAG) {
        bucket = gst_object_bag_find(dict, key);
    } else {
        bucket = hash_findkv(obj->data, obj->capacity, key, &notused);
    }
    if (bucket != NULL) {
        return bucket[1];
    } else {
        GstValue ret;
        ret.type = GST_NIL;
        return ret;
    }
}

/* Get item with c string key */
GstValue gst_dict_get_cstring(GstDict *dict, const char *key);

/* Add item to dictionary */
void gst_dict_put(Gst *vm, GstDict *obj, GstValue key, GstValue value) {
    if (obj->flags & GST_OBJECT_FLAG_ISBAG) {
        if (obj->count > GST_OBJECT_BAG_THRESHOLD) {
            /* Change to hashtable */
            obj->flags |= GST_OBJECT_FLAG_ISBAG;
            gst_object_rehash(vm, obj, 4 * obj->count);
            goto put_hash;
        }
        gst_object_bag_put(vm, obj, key, value);
    } else {
        GstValue *bucket, *out;
        put_hash:
        bucket = hash_findkv(obj->data, obj->capacity, key, &out);
        if (bucket != NULL) {
            bucket[1] = value;
        } else {
            /* Check for resize */
            if (obj->count + 1 > obj->capacity) {
                gst_object_rehash(vm, obj, 2 * (obj->count + 1));
                bucket = hash_findkv(obj->data, obj->capacity, key, &out);
            }
            out[0] = key;
            out[1] = value;
            ++obj->count;
        }
    }
}

/* Remove item from dictionary */
void gst_dict_remove(GstDict *obj, GstValue key) {
    if (obj->flags & GST_OBJECT_FLAG_ISBAG) {
        gst_object_bag_remove(obj, key);
    } else {
        GstValue *bucket, *out;
        bucket = hash_findkv(obj->data, obj->capacity, key, &out);
        if (bucket != NULL) {
            --obj->count;
            bucket[0].type = GST_NIL;
            bucket[1].type = GST_BOOLEAN;
        }
    }
}


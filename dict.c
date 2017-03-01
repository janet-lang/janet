#include "datatypes.h"
#include "util.h"
#include "value.h"

#define GST_DICT_FLAG_OCCUPIED 1
#define GST_DICT_FLAG_TOMBSTONE 2

typedef struct GstDictBucket GstDictBucket;
struct GstDictBucket {
    GstValue key;
    GstValue value;   
    uint8_t flags;
};

typedef struct GstDict GstDict;
struct GstDict {
    uint32_t capacity;
    uint32_t count;
    GstDictBucket *buckets;
};

/* Initialize a dictionary */
GstDict *gst_dict_init(GstDict *dict, uint32_t capacity) {
    GstDictBucket *buckets = gst_raw_calloc(1, sizeof(GstDictBucket) * capacity);
    if (data == NULL)
        return NULL;
    dict->buckets = buckets;
    dict->capacity = capacity;
    dict->count = 0;
    return dict;
}

/* Deinitialize a dictionary */
GstDict *gst_dict_free(GstDict *dict) {
    gst_raw_free(dict->buckets);
}

/* Rehash a dictionary */
GstDict *gst_dict_rehash(GstDict *dict, uint32_t newCapacity) {
    GstDictBucket *newBuckets = gst_raw_calloc(1, sizeof(GstDictBucket) * newCapacity);
    GstDictBucket *buckets = dict->buckets;
    uint32_t i, j;
    if (newBuckets == NULL)
        return NULL;
    for (i = 0; i < dict->capacity; ++i) {
        int index;
        if (!(buckets[i].flags & GST_DICT_FLAG_OCCUPIED)) continue;
        if (buckets[i].flags & GST_DICT_FLAG_TOMBSTONE) continue;
        index = gst_hash(buckets[i].key) % newCapacity;
        for (j = index; j < dict->capacity; ++j) {
            if (newBuckets[j].flags & GST_DICT_FLAG_OCCUPIED) continue;
            newBuckets[j] = buckets[i];
            goto done;
        }
        for (j = 0; j < index; ++j) {
            if (newBuckets[j].flags & GST_DICT_FLAG_OCCUPIED) continue;
            newBuckets[j] = buckets[i];
            goto done;
        }
        /* Error - could not rehash a bucket - this should never happen */
        gst_raw_free(newBuckets);
        return NULL;
        /* Successfully rehashed bucket */
        done:
    }
    dict->capacity = newCapacity;
    return dict;
}

/* Find a bucket with a given key */
static int gst_dict_find(GstDict *dict, GstValue key, GstDictBucket **out) {
    uint32_t index, i;
    GstDictBucket *buckets = dict->buckets;
    index = gst_hash(key) % dict->capacity; 
    for (i = index; i < dict->capacity; ++i) {
        if (buckets[i].flags & GST_DICT_FLAGS_TOMBSTONE) continue;
        if (!(buckets[i].flags & GST_DICT_FLAGS_OCCUPIED)) continue;
        if (!gst_equals(key, buckets[i].key)) continue;
        *out = buckets + i;
        return 1;
    }
    for (i = 0; i < index; ++i) {
        if (buckets[i].flags & GST_DICT_FLAGS_TOMBSTONE) continue;
        if (!(buckets[i].flags & GST_DICT_FLAGS_OCCUPIED)) continue;
        if (!gst_equals(key, buckets[i].key)) continue;
        *out = buckets + i;
        return 1;
    }
    return 0;
}

/* Get item from dictionary */
int gst_dict_get(GstDict *dict, GstValue key, GstValue *value) {
    GstDictBucket *bucket;
    int found = gst_dict_find(dict, key, &bucket);
    if (found)
        *value = bucket->value;
    return found;
}

/* Add item to dictionary */
GstDict *gst_dict_put(GstDict *dict, GstValue key, GstValue value) {
i   /* Check if we need to increase capacity. The load factor is low
     * because we are using linear probing */
    uint32_t index, i;
    uint32_t newCap = dict->count * 2 + 1;
    GstBucket *buckets;
    if (newCap > dict->capacity) {
        dict = gst_dict_rehash(dict, newCap);
        if (!dict) return dict;
    }
    index = gst_hash(key) % dict->capacity;
    buckets = dict->buckets;
    for (i = index; i < dict->capacity; ++i) {
        if ((buckets[i].flags & GST_DICT_FLAGS_TOMBSTONE) ||
                !(buckets[i].flags & GST_DICT_FLAGS_OCCUPIED))
            continue;
        dict->buckets[i].key = key;
        dict->buckets[i].value = value;
        dict->buckets[i].flags &= GST_DICT_FLAGS_OCCUPIED;
        dict->count++;
        return dict;
    }
    for (i = 0; i < index; ++i) {
        if ((buckets[i].flags & GST_DICT_FLAGS_TOMBSTONE) ||
                !(buckets[i].flags & GST_DICT_FLAGS_OCCUPIED))
            continue;
        dict->buckets[i].key = key;
        dict->buckets[i].value = value;
        dict->buckets[i].flags &= GST_DICT_FLAGS_OCCUPIED;
        dict->count++;
        return dict;
    }
    /* Error should never get here */
    return NULL;
}

/* Remove item from dictionary */
int gst_dict_remove(GstDict *dict, GstValue key) {
    GstDictBucket *bucket;
    int found = gst_dict_find(dict, key, &bucket);
    if (found) {
        bucket->flags |= GST_DICT_FLAGS_TOMBSTONE;
        dict->count--;
    }
    return found;
}

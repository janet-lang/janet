#ifndef dict_h_INCLUDED
#define dict_h_INCLUDED

#include "datatypes.h"

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
GstDict *gst_dict_init(GstDict *dict, uint32_t capacity);

/* Deinitialize a dictionary */
GstDict *gst_dict_free(GstDict *dict);

/* Rehash a dictionary */
GstDict *gst_dict_rehash(GstDict *dict, uint32_t newCapacity);

/* Get item from dictionary */
int gst_dict_get(GstDict *dict, GstValue key, GstValue *value);

/* Add item to dictionary */
GstDict *gst_dict_put(GstDict *dict, GstValue key, GstValue value);

/* Remove item from dictionary */
int gst_dict_remove(GstDict *dict, GstValue key);

#endif // dict_h_INCLUDED

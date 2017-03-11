#ifndef dict_h_INCLUDED
#define dict_h_INCLUDED

#include "datatypes.h"

/* Indicates object is implement as unsorted array of keypairs */
#define GST_OBJECT_FLAG_ISBAG (1 << 31)

/* Indicates object is immutable */
#define GST_OBJECT_IMMUTABLE (1 << 30)

/* Count at which the object goes from a linear search to a hash table */
#define GST_OBJECT_BAG_THRESHOLD 8

typedef struct GstDict GstDict;
struct GstDict {
    uint32_t capacity;
    uint32_t count;
    uint32_t flags;
    GstValue *data;
};

/* Initialize a dictionary */
GstDict *gst_dict(Gst *vm, uint32_t capacity);

/* Get item from dictionary */
GstValue gst_dict_get(GstDict *dict, GstValue key);

/* Get c string from object */
GstValue gst_dict_get_cstring(GstDict *dict, const char *key);

/* Add item to dictionary */
void gst_dict_put(Gst *vm, GstDict *dict, GstValue key, GstValue value);

/* Remove item from dictionary */
void gst_dict_remove(GstDict *dict, GstValue key);

#endif // dict_h_INCLUDED

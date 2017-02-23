#ifndef VALUE_H_1RJPQKFM
#define VALUE_H_1RJPQKFM

#include "datatypes.h"

/* Check for boolean truthiness */
int gst_truthy(GstValue x);

/* Compare two gst values. All gst values are comparable and strictly
 * ordered by default. Return 0 if equal, -1 if x is less than y, and
 * 1 and x is greater than y. */
int gst_compare(GstValue x, GstValue y);

/* Returns if two values are equal. */
int gst_equals(GstValue x, GstValue y);

/* Get a value from an associative gst object. Can throw errors. */
GstValue gst_get(Gst *vm, GstValue ds, GstValue key);

/* Set a value in an associative gst object. Can throw errors. */
void gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value);

/* Load a c style string into a gst value (copies data) */
GstValue gst_load_cstring(Gst *vm, const char *string);

/* Convert any gst value into a string */
uint8_t *gst_to_string(Gst *vm, GstValue x);

/* Generate a hash value for a gst object */
uint32_t gst_hash(GstValue x);

#endif /* end of include guard: VALUE_H_1RJPQKFM */

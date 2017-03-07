#ifndef ds_h_INCLUDED
#define ds_h_INCLUDED

#include "datatypes.h"

/*
 * Data type flags
 */
#define GST_DS_LOCKED 0x01

/****/
/* Buffer functions */
/****/

/* Create a new buffer */
GstBuffer *gst_buffer(Gst *vm, uint32_t capacity);

/* Ensure the buffer has enough capacity */
void gst_buffer_ensure(Gst *vm, GstBuffer *buffer, uint32_t capacity);

/* Get a value from the buffer */
int gst_buffer_get(GstBuffer *buffer, uint32_t index);

/* Push a value to the buffer */
void gst_buffer_push(Gst *vm, GstBuffer *buffer, uint8_t c);

/* Append a piece of memory to the buffer */
void gst_buffer_append(Gst *vm, GstBuffer *buffer, uint8_t *string, uint32_t length);

/* Convert the buffer to a string */
uint8_t *gst_buffer_to_string(Gst * vm, GstBuffer * buffer);

/* Define a push function for pushing a certain type to the buffer */
#define BUFFER_DEFINE(name, type) \
static void gst_buffer_push_##name(Gst * vm, GstBuffer * buffer, type x) { \
    union { type t; uint8_t bytes[sizeof(type)]; } u; \
    u.t = x; gst_buffer_append(vm, buffer, u.bytes, sizeof(type)); \
}

/****/
/* Array functions */
/****/

/* Create a new Array */
GstArray *gst_array(Gst *vm, uint32_t capacity);

/* Get a value of an array with bounds checking. Returns nil if
 * outside bounds. */
GstValue gst_array_get(GstArray *array, uint32_t index);

/* Set a value in the array. Does bounds checking but will not grow
 * or shrink the array */
int gst_array_set(GstArray *array, uint32_t index, GstValue x);

/* Ensure that the internal memory hash enough space for capacity items */
void gst_array_ensure(Gst *vm, GstArray *array, uint32_t capacity);

/* Set a value in an array. Will also append to the array if the index is
 * greater than the current max index. */
void gst_array_push(Gst *vm, GstArray *array, GstValue x);

/* Pop the last item in the array, or return NIL if empty */
GstValue gst_array_pop(GstArray *array);

/* Look at the top most item of an Array */
GstValue ArrayPeek(GstArray *array);

/****/
/* Tuple functions */
/* These really don't do all that much */
/****/

/* Create an empty tuple. It is expected to be mutated right after
 * creation. */
GstValue *gst_tuple(Gst *vm, uint32_t length);

/****/
/* Object functions */
/****/

/* Create a new object */
GstObject *gst_object(Gst *vm, uint32_t capacity);

/* Get a value out of the dictionary */
GstValue gst_object_get(GstObject *obj, GstValue key);

/* Get a Value from the dictionary, but remove it at the same
 * time. */
GstValue gst_object_remove(Gst *vm, GstObject *obj, GstValue key);

/* Put a value into the dictionary. Returns 1 if successful, 0 if out of memory.
 * The VM pointer is needed for memory allocation. */
void gst_object_put(Gst *vm, GstObject *obj, GstValue key, GstValue value);

#endif // ds_h_INCLUDED

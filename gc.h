#ifndef gc_h_INCLUDED
#define gc_h_INCLUDED

#include "datatypes.h"

/* Makr a value as reachable */
void gst_mark(Gst *vm, GstValue *x);

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void gst_sweep(Gst *vm);

/* Allocate a chunk memory that will be garbage collected. */
void *gst_alloc(Gst *vm, uint32_t size);

/* Allocate zeroed memory to be garbage collected */
void *gst_zalloc(Gst *vm, uint32_t size);

/* Run a collection */
void gst_collect(Gst *vm);

/* Run a collection if we have alloctaed enough memory since the last
 collection */
void gst_maybe_collect(Gst *vm);

/* Clear all memory */
void gst_clear_memory(Gst *vm);

#endif
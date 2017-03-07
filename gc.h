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

/* Separate memory container. This memory is not gced, but can be freed at once. This
 * is used in the compiler and parser to prevent memory leaks on errors. */
typedef void *GstManagedMemory;

/* Initialize managed memory */
void gst_mm_init(GstManagedMemory *mm);

/* Allocate some managed memory */
void *gst_mm_alloc(GstManagedMemory *mm, uint32_t size);

/* Intialize zeroed managed memory */
void *gst_mm_zalloc(GstManagedMemory *mm, uint32_t size);

/* Free a memory block used in managed memory */
void gst_mm_free(GstManagedMemory *mm, void *block);

/* Free all memory in managed memory */
void gst_mm_clear(GstManagedMemory *mm);

/* Analog to realloc */
void *gst_mm_realloc(GstManagedMemory *mm, void *block, uint32_t nsize);

#endif

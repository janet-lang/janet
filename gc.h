#ifndef GC_H_N8L3U4KK
#define GC_H_N8L3U4KK

#include "datatypes.h"

/* Initialize a GC */
void GCInit(GC * gc, uint32_t memoryInterval);

/* Iterate over all allocated memory, and frees memory that is not
 * marked as reachable */
void GCSweep(GC * gc);

/* Do a depth first search of the variables and mark all reachable memory.
 * Root variables are just everyting in the stack. */
void GCMark(GC * gc, Value * x);

/* Mark some raw memory as reachable */
void GCMarkMemory(GC * gc, void * memory);

/* Clean up all memory, including locked memory */
void GCClear(GC * gc);

/* Allocate some memory that is tracked for garbage collection */
void * GCAlloc(GC * gc, uint32_t size);

/* Allocate zeroed memory */
void * GCZalloc(GC * gc, uint32_t size);

/* Run a collection */
#define GCCollect(gc, root) \
    (GCMark((gc), (root)), GCSweep(gc), (gc)->nextCollection = 0)

/* Check if a collection needs to be run */
#define GCNeedsCollect(gc) \
    ((gc)->nextCollection >= (gc)->memoryInterval)

/* Run a collection if enough memory has been allocated since last collection */
#define GCMaybeCollect(gc, root) \
    (GCNeedsCollect(gc) ? GCCollect((gc), (root)) : 0)

#endif /* end of include guard: GC_H_N8L3U4KK */

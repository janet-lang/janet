#ifndef ARRAY_H_BW48JZSK
#define ARRAY_H_BW48JZSK

#include "datatypes.h"

/* Create a new Array */
Array * ArrayNew(GC * gc, uint32_t capacity);

/* Get a value of an array with bounds checking. Returns nil if
 * outside bounds. */
Value ArrayGet(Array * array, uint32_t index);

/* Set a value in the array. Does bounds checking but will not grow
 * or shrink the array */
int ArraySet(Array * array, uint32_t index, Value x);

/* Ensure that the internal memory hash enough space for capacity items */
void ArrayEnsure(GC * gc, Array * array, uint32_t capacity);

/* Set a value in an array. Will also append to the array if the index is
 * greater than the current max index. */
void ArrayPush(GC * gc, Array * array, Value x);

/* Pop the last item in the array, or return NIL if empty */
Value ArrayPop(Array * array);

/* Look at the top most item of an Array */
Value ArrayPeek(Array * array);

#endif /* end of include guard: ARRAY_H_BW48JZSK */

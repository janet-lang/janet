#ifndef DICT_H_YN2BKHUQ
#define DICT_H_YN2BKHUQ

#include "datatypes.h"

/* Create a new dictionary */
Dictionary * DictNew(GC * gc, uint32_t capacity);

/* Get a value out of the dictionary */
Value DictGet(Dictionary * dict, Value * key);

/* Get a Value from the dictionary, but remove it at the same
 * time. */
Value DictRemove(GC * gc, Dictionary * dict, Value * key);

/* Put a value into the dictionary. Returns 1 if successful, 0 if out of memory.
 * The VM pointer is needed for memory allocation. */
void DictPut(GC * gc, Dictionary * dict, Value * key, Value * value);

/* Begin iteration through a dictionary */
void DictIterate(Dictionary * dict, DictionaryIterator * iterator);

/* Provides a mechanism for iterating through a table. */
int DictIterateNext(DictionaryIterator * iterator, DictBucket ** bucket);

#endif /* end of include guard: DICT_H_YN2BKHUQ */

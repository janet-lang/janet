#ifndef ds_h_INCLUDED
#define ds_h_INCLUDED

#include "datatypes.h"

/****/
/* Buffer functions */
/****/

/* Create a new buffer */
Buffer * BufferNew(VM * vm, uint32_t capacity);

/* Ensure the buffer has enough capacity */
void BufferEnsure(VM * vm, Buffer * buffer, uint32_t capacity);

/* Get a value from the buffer */
int32_t BufferGet(Buffer * buffer, uint32_t index);

/* Push a value to the buffer */
void BufferPush(VM * vm, Buffer * buffer, uint8_t c);

/* Append a piece of memory to the buffer */
void BufferAppendData(VM * vm, Buffer * buffer, uint8_t * string, uint32_t length);

/* Convert the buffer to a string */
uint8_t * BufferToString(VM * vm, Buffer * buffer);

/* Define a push function for pushing a certain type to the buffer */
#define BufferDefine(name, type) \
static void BufferPush##name (VM * vm, Buffer * buffer, type x) { \
    union { type t; uint8_t bytes[sizeof(type)]; } u; \
    u.t = x; BufferAppendData(vm, buffer, u.bytes, sizeof(type)); \
}

/****/
/* Array functions */
/****/

/* Create a new Array */
Array * ArrayNew(VM * vm, uint32_t capacity);

/* Get a value of an array with bounds checking. Returns nil if
 * outside bounds. */
Value ArrayGet(Array * array, uint32_t index);

/* Set a value in the array. Does bounds checking but will not grow
 * or shrink the array */
int ArraySet(Array * array, uint32_t index, Value x);

/* Ensure that the internal memory hash enough space for capacity items */
void ArrayEnsure(VM * vm, Array * array, uint32_t capacity);

/* Set a value in an array. Will also append to the array if the index is
 * greater than the current max index. */
void ArrayPush(VM * vm, Array * array, Value x);

/* Pop the last item in the array, or return NIL if empty */
Value ArrayPop(Array * array);

/* Look at the top most item of an Array */
Value ArrayPeek(Array * array);

/****/
/* Dictionary functions */
/****/

/* Create a new dictionary */
Dictionary * DictNew(VM * vm, uint32_t capacity);

/* Get a value out of the dictionary */
Value DictGet(Dictionary * dict, Value key);

/* Get a Value from the dictionary, but remove it at the same
 * time. */
Value DictRemove(VM * vm, Dictionary * dict, Value key);

/* Put a value into the dictionary. Returns 1 if successful, 0 if out of memory.
 * The VM pointer is needed for memory allocation. */
void DictPut(VM * vm, Dictionary * dict, Value key, Value value);

/* Begin iteration through a dictionary */
void DictIterate(Dictionary * dict, DictionaryIterator * iterator);

/* Provides a mechanism for iterating through a table. */
int DictIterateNext(DictionaryIterator * iterator, DictBucket ** bucket);

#endif // ds_h_INCLUDED

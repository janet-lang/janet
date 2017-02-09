#include <string.h>
#include "array.h"
#include "gc.h"

/* Creates a new array */
Array * ArrayNew(GC * gc, uint32_t capacity) {
    Array * array = GCAlloc(gc, sizeof(Array));
    Value * data = GCAlloc(gc, capacity * sizeof(Value));
    array->data = data;
    array->count = 0;
    array->capacity = capacity;
    return array;
}

/* Ensure the array has enough capacity for capacity elements */
void ArrayEnsure(GC * gc, Array * array, uint32_t capacity) {
    Value * newData;
    if (capacity <= array->capacity) return;
    newData = GCAlloc(gc, capacity * sizeof(Value));
    memcpy(newData, array->data, array->count * sizeof(Value));
    array->data = newData;
    array->capacity = capacity;
}

/* Get a value of an array with bounds checking. */
Value ArrayGet(Array * array, uint32_t index) {
    if (index < array->count) {
        return array->data[index];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

/* Try to set an index in the array. Return 1 if successful, 0
 * on failiure */
int ArraySet(Array * array, uint32_t index, Value x) {
    if (index < array->count) {
        array->data[index] = x;
        return 1;
    } else {
        return 0;
    }
}

/* Add an item to the end of the array */
void ArrayPush(GC * gc, Array * array, Value x) {
    if (array->count >= array->capacity) {
        ArrayEnsure(gc, array, 2 * array->count);
    }
    array->data[array->count++] = x;
}

/* Remove the last item from the Array and return it */
Value ArrayPop(Array * array) {
    if (array->count) {
        return array->data[--array->count];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

/* Look at the last item in the Array */
Value ArrayPeek(Array * array) {
    if (array->count) {
        return array->data[array->count - 1];
    } else {
        Value v;
        v.type = TYPE_NIL;
        v.data.boolean = 0;
        return v;
    }
}

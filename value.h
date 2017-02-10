#ifndef VALUE_H_1RJPQKFM
#define VALUE_H_1RJPQKFM

#include "datatypes.h"

void ValuePrint(Value x, uint32_t indent);

int ValueCompare(Value x, Value y);

int ValueEqual(Value x, Value y);

Value ValueLoadCString(VM * vm, const char * string);

uint8_t * ValueToString(VM * vm, Value x);

uint32_t ValueHash(Value x);

#endif /* end of include guard: VALUE_H_1RJPQKFM */

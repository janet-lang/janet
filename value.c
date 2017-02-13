#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "value.h"
#include "ds.h"
#include "vm.h"

static uint8_t * LoadCString(VM * vm, const char * string, uint32_t len) {
    uint8_t * data = VMAlloc(vm, len + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    VStringHash(data) = 0;
    VStringSize(data) = len;
    memcpy(data, string, len);
    return data;
}

Value ValueLoadCString(VM * vm, const char * string) {
    Value ret;
    ret.type = TYPE_STRING;
    ret.data.string = LoadCString(vm, string, strlen(string));
    return ret;
}

static uint8_t * NumberToString(VM * vm, Number x) {
    static const uint32_t SIZE = 20;
    uint8_t * data = VMAlloc(vm, SIZE + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    snprintf((char *) data, SIZE, "%.17g", x);
    VStringHash(data) = 0;
    VStringSize(data) = strlen((char *) data);
    return data;
}

static const char * HEX_CHARACTERS = "0123456789abcdef";
#define HEX(i) (((uint8_t *) HEX_CHARACTERS)[(i)])

/* Returns a string description for a pointer */
static uint8_t * StringDescription(VM * vm, const char * title, uint32_t titlelen, void * pointer) {
    uint32_t len = 5 + titlelen + sizeof(void *) * 2;
    uint32_t i;
    uint8_t * data = VMAlloc(vm, len + 2 * sizeof(uint32_t));
    uint8_t * c;
    union {
        uint8_t bytes[sizeof(void *)];
        void * p;
    } buf;
    buf.p = pointer;
    data += 2 * sizeof(uint32_t);
    c = data;
    *c++ = '<';
    for (i = 0; i < titlelen; ++i) {
        *c++ = ((uint8_t *)title) [i];
    }
    *c++ = ' ';
    *c++ = '0';
    *c++ = 'x';
    for (i = sizeof(void *); i > 0; --i) {
        uint8_t byte = buf.bytes[i - 1];
        if (!byte) continue;
        *c++ = HEX(byte >> 4);
        *c++ = HEX(byte & 0xF);
    }
    *c++ = '>';
    VStringHash(data) = 0;
    VStringSize(data) = c - data;
    return data;
}

/* Returns a string pointer or NULL if could not allocate memory. */
uint8_t * ValueToString(VM * vm, Value x) {
    switch (x.type) {
        case TYPE_NIL:
            return LoadCString(vm, "nil", 3);
        case TYPE_BOOLEAN:
            if (x.data.boolean) {
                return LoadCString(vm, "true", 4);
            } else {
                return LoadCString(vm, "false", 5);
            }
        case TYPE_NUMBER:
            return NumberToString(vm, x.data.number);
        case TYPE_ARRAY:
            {
                uint32_t i;
				Buffer * b = BufferNew(vm, 40);
				BufferPush(vm, b, '(');
				for (i = 0; i < x.data.array->count; ++i) {
    				uint8_t * substr = ValueToString(vm, x.data.array->data[i]);
					BufferAppendData(vm, b, substr, VStringSize(substr));
					if (i < x.data.array->count - 1)
        				BufferPush(vm, b, ' ');
				}
				BufferPush(vm, b, ')');
				return BufferToString(vm, b);
            }
            return StringDescription(vm, "array", 5, x.data.pointer);
        case TYPE_STRING:
            return x.data.string;
        case TYPE_BYTEBUFFER:
            return StringDescription(vm, "buffer", 6, x.data.pointer);
        case TYPE_CFUNCTION:
            return StringDescription(vm, "cfunction", 9, x.data.pointer);
        case TYPE_FUNCTION:
            return StringDescription(vm, "function", 8, x.data.pointer);
        case TYPE_DICTIONARY:
            return StringDescription(vm, "dictionary", 10, x.data.pointer);
        case TYPE_THREAD:
            return StringDescription(vm, "thread", 6, x.data.pointer);
    }
    return NULL;
}

/* Simple hash function */
uint32_t djb2(const uint8_t * str) {
    const uint8_t * end = str + VStringSize(str);
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Check if two values are equal. This is strict equality with no conversion. */
int ValueEqual(Value x, Value y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
            case TYPE_NIL:
                result = 1;
                break;
            case TYPE_BOOLEAN:
                result = (x.data.boolean == y.data.boolean);
                break;
            case TYPE_NUMBER:
                result = (x.data.number == y.data.number);
                break;
                /* Assume that when strings are created, equal strings
                 * are set to the same string */
            case TYPE_STRING:
                if (x.data.string == y.data.string) {
                    result = 1;
                    break;
                }
                if (ValueHash(x) != ValueHash(y) ||
                        VStringSize(x.data.string) != VStringSize(y.data.string)) {
                    result = 0;
                    break;
                }
                if (!strncmp((char *) x.data.string, (char *) y.data.string, VStringSize(x.data.string))) {
                    result = 1;
                    break;
                }
                result = 0;
                break;
            case TYPE_ARRAY:
            case TYPE_BYTEBUFFER:
            case TYPE_CFUNCTION:
            case TYPE_DICTIONARY:
            case TYPE_FUNCTION:
            case TYPE_THREAD:
                /* compare pointers */
                result = (x.data.array == y.data.array);
                break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t ValueHash(Value x) {
    uint32_t hash = 0;
    switch (x.type) {
        case TYPE_NIL:
            hash = 0;
            break;
        case TYPE_BOOLEAN:
            hash = x.data.boolean;
            break;
        case TYPE_NUMBER:
            {
                union {
                    uint32_t hash;
                    Number number;
                } u;
                u.number = x.data.number;
                hash = u.hash;
            }
            break;
            /* String hashes */
        case TYPE_STRING:
            /* Assume 0 is not hashed. */
            if (VStringHash(x.data.string))
                hash = VStringHash(x.data.string);
            else
                hash = VStringHash(x.data.string) = djb2(x.data.string);
            break;
        case TYPE_ARRAY:
        case TYPE_BYTEBUFFER:
        case TYPE_CFUNCTION:
        case TYPE_DICTIONARY:
        case TYPE_FUNCTION:
        case TYPE_THREAD:
            /* Cast the pointer */
            {
                union {
                    void * pointer;
                    uint32_t hash;
                } u;
                u.pointer = x.data.pointer;
                hash = u.hash;
            }
            break;
    }
    return hash;
}

/* Compares x to y. If they are equal retuns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering. */
int ValueCompare(Value x, Value y) {
    if (x.type == y.type) {
        switch (x.type) {
            case TYPE_NIL:
                return 0;
            case TYPE_BOOLEAN:
                if (x.data.boolean == y.data.boolean) {
                    return 0;
                } else {
                    return x.data.boolean ? 1 : -1;
                }
            case TYPE_NUMBER:
                /* TODO: define behavior for NaN and infinties. */
                if (x.data.number == y.data.number) {
                    return 0;
                } else {
                    return x.data.number > y.data.number ? 1 : -1;
                }
            case TYPE_STRING:
                if (x.data.string == y.data.string) {
                    return 0;
                } else {
                    uint32_t xlen = VStringSize(x.data.string);
                    uint32_t ylen = VStringSize(y.data.string);
                    uint32_t len = xlen > ylen ? ylen : xlen;
                    uint32_t i;
                    for (i = 0; i < len; ++i) {
                        if (x.data.string[i] == y.data.string[i]) {
                            continue;
                        } else if (x.data.string[i] < y.data.string[i]) {
                            return 1; /* x is less then y */
                        } else {
                            return -1; /* y is less than x */
                        }
                    }
                    if (xlen == ylen) {
                        return 0;
                    } else {
                        return xlen < ylen ? -1 : 1;
                    }
                }
            case TYPE_ARRAY:
            case TYPE_BYTEBUFFER:
            case TYPE_CFUNCTION:
            case TYPE_FUNCTION:
            case TYPE_DICTIONARY:
            case TYPE_THREAD:
                if (x.data.string == y.data.string) {
                    return 0;
                } else {
                    return x.data.string > y.data.string ? 1 : -1;
                }
        }
    } else if (x.type < y.type) {
        return -1;
    }
    return 1;
}

/* Allow negative indexing to get from end of array like structure */
/* This probably isn't very fast - look at Lua conversion function.
 * I would like to keep this standard C for as long as possible, though. */
static int32_t ToIndex(Number raw, int64_t len) {
	int32_t toInt = raw;
	if ((Number) toInt == raw) {
		/* We were able to convert */
		if (toInt < 0) {	
    		/* Index from end */
			if (toInt < -len) return -1;	
			return len + toInt;
		} else {	
    		/* Normal indexing */
    		if (toInt >= len) return -1;
        	return toInt;
		}
	} else {
        return -1;
	}
}

/* Convert a number into a byte. */
static uint8_t NumberToByte(Number raw) {
	if (raw > 255) return 255;
	if (raw < 0) return 0;
	return (uint8_t) raw;
}

/* Get a value out af an associated data structure. Can throw VM error. */
Value ValueGet(VM * vm, Value ds, Value key) {
    int32_t index;
    Value ret;
	switch (ds.type) {
	case TYPE_ARRAY:
        VMAssertType(vm, key, TYPE_NUMBER);
		index = ToIndex(key.data.number, ds.data.array->count);
		if (index == -1) VMError(vm, "Invalid array access");
		return ds.data.array->data[index];
    case TYPE_BYTEBUFFER:
        VMAssertType(vm, key, TYPE_NUMBER);
        index = ToIndex(key.data.number, ds.data.buffer->count);
		if (index == -1) VMError(vm, "Invalid buffer access");
		ret.type = TYPE_NUMBER;
		ret.data.number = ds.data.buffer->data[index];
		break;
    case TYPE_STRING:
        VMAssertType(vm, key, TYPE_NUMBER);
        index = ToIndex(key.data.number, VStringSize(ds.data.string));
		if (index == -1) VMError(vm, "Invalid string access");
		ret.type = TYPE_NUMBER;
		ret.data.number = ds.data.string[index];
		break;
    case TYPE_DICTIONARY:
        return DictGet(ds.data.dict, key);
    default:
        VMError(vm, "Cannot get.");
	}
	return ret;
}

/* Set a value in an associative data structure. Can throw VM error. */
void ValueSet(VM * vm, Value ds, Value key, Value value) {
    int32_t index;
	switch (ds.type) {
	case TYPE_ARRAY:
        VMAssertType(vm, key, TYPE_NUMBER);
		index = ToIndex(key.data.number, ds.data.array->count);
		if (index == -1) VMError(vm, "Invalid array access");
		ds.data.array->data[index] = value;
		break;
    case TYPE_BYTEBUFFER:
        VMAssertType(vm, key, TYPE_NUMBER);
        VMAssertType(vm, value, TYPE_NUMBER);
        index = ToIndex(key.data.number, ds.data.buffer->count);
		if (index == -1) VMError(vm, "Invalid buffer access");
		ds.data.buffer->data[index] = NumberToByte(value.data.number);
		break;
    case TYPE_DICTIONARY:
        DictPut(vm, ds.data.dict, key, value);
        break;
    default:
        VMError(vm, "Cannot set.");
	}
}

#include "util.h"
#include "value.h"
#include "ds.h"
#include "vm.h"
#include <stdio.h>

/* Boolean truth definition */
int gst_truthy(GstValue v) {
    return v.type != GST_NIL && !(v.type == GST_BOOLEAN && !v.data.boolean);
}

static uint8_t * load_cstring(Gst *vm, const char *string, uint32_t len) {
    uint8_t *data = gst_alloc(vm, len + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    gst_string_hash(data) = 0;
    gst_string_length(data) = len;
    gst_memcpy(data, string, len);
    return data;
}

GstValue gst_load_cstring(Gst *vm, const char *string) {
    GstValue ret;
    ret.type = GST_STRING;
    ret.data.string = load_cstring(vm, string, strlen(string));
    return ret;
}

static uint8_t * number_to_string(Gst *vm, GstNumber x) {
    static const uint32_t SIZE = 20;
    uint8_t *data = gst_alloc(vm, SIZE + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    /* TODO - not depend on stdio */
    snprintf((char *) data, SIZE, "%.17g", x);
    gst_string_hash(data) = 0;
    gst_string_length(data) = strlen((char *) data);
    return data;
}

static const char *HEX_CHARACTERS = "0123456789abcdef";
#define HEX(i) (((uint8_t *) HEX_CHARACTERS)[(i)])

/* Returns a string description for a pointer */
static uint8_t *string_description(Gst *vm, const char *title, uint32_t titlelen, void *pointer) {
    uint32_t len = 5 + titlelen + sizeof(void *) * 2;
    uint32_t i;
    uint8_t *data = gst_alloc(vm, len + 2 * sizeof(uint32_t));
    uint8_t *c;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
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
    gst_string_hash(data) = 0;
    gst_string_length(data) = c - data;
    return data;
}

/* Returns a string pointer or NULL if could not allocate memory. */
uint8_t *gst_to_string(Gst *vm, GstValue x) {
    switch (x.type) {
        case GST_NIL:
            return load_cstring(vm, "nil", 3);
        case GST_BOOLEAN:
            if (x.data.boolean) {
                return load_cstring(vm, "true", 4);
            } else {
                return load_cstring(vm, "false", 5);
            }
        case GST_NUMBER:
            return number_to_string(vm, x.data.number);
        case GST_ARRAY:
            {
                uint32_t i;
				GstBuffer * b = gst_buffer(vm, 40);
				gst_buffer_push(vm, b, '(');
				for (i = 0; i < x.data.array->count; ++i) {
    				uint8_t * substr = gst_to_string(vm, x.data.array->data[i]);
					gst_buffer_append(vm, b, substr, gst_string_length(substr));
					if (i < x.data.array->count - 1)
        				gst_buffer_push(vm, b, ' ');
				}
				gst_buffer_push(vm, b, ')');
				return gst_buffer_to_string(vm, b);
            }
        case GST_STRING:
            return x.data.string;
        case GST_BYTEBUFFER:
            return string_description(vm, "buffer", 6, x.data.pointer);
        case GST_CFUNCTION:
            return string_description(vm, "cfunction", 9, x.data.pointer);
        case GST_FUNCTION:
            return string_description(vm, "function", 8, x.data.pointer);
        case GST_OBJECT:
            return string_description(vm, "object", 6, x.data.pointer);
        case GST_THREAD:
            return string_description(vm, "thread", 6, x.data.pointer);
    }
    return NULL;
}

/* Simple hash function */
uint32_t djb2(const uint8_t * str) {
    const uint8_t * end = str + gst_string_length(str);
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Check if two values are equal. This is strict equality with no conversion. */
int gst_equals(GstValue x, GstValue y) {
    int result = 0;
    if (x.type != y.type) {
        result = 0;
    } else {
        switch (x.type) {
            case GST_NIL:
                result = 1;
                break;
            case GST_BOOLEAN:
                result = (x.data.boolean == y.data.boolean);
                break;
            case GST_NUMBER:
                result = (x.data.number == y.data.number);
                break;
                /* Assume that when strings are created, equal strings
                 * are set to the same string */
            case GST_STRING:
                if (x.data.string == y.data.string) {
                    result = 1;
                    break;
                }
                if (gst_hash(x) != gst_hash(y) ||
                        gst_string_length(x.data.string) != gst_string_length(y.data.string)) {
                    result = 0;
                    break;
                }
                if (!strncmp((char *) x.data.string, (char *) y.data.string, gst_string_length(x.data.string))) {
                    result = 1;
                    break;
                }
                result = 0;
                break;
            default:
                /* compare pointers */
                result = (x.data.array == y.data.array);
                break;
        }
    }
    return result;
}

/* Computes a hash value for a function */
uint32_t gst_hash(GstValue x) {
    uint32_t hash = 0;
    switch (x.type) {
        case GST_NIL:
            hash = 0;
            break;
        case GST_BOOLEAN:
            hash = x.data.boolean;
            break;
        case GST_NUMBER:
            {
                union {
                    uint32_t hash;
                    GstNumber number;
                } u;
                u.number = x.data.number;
                hash = u.hash;
            }
            break;
            /* String hashes */
        case GST_STRING:
            /* Assume 0 is not hashed. */
            if (gst_string_hash(x.data.string))
                hash = gst_string_hash(x.data.string);
            else
                hash = gst_string_hash(x.data.string) = djb2(x.data.string);
            break;
        default:
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
int gst_compare(GstValue x, GstValue y) {
    if (x.type == y.type) {
        switch (x.type) {
            case GST_NIL:
                return 0;
            case GST_BOOLEAN:
                if (x.data.boolean == y.data.boolean) {
                    return 0;
                } else {
                    return x.data.boolean ? 1 : -1;
                }
            case GST_NUMBER:
                /* TODO: define behavior for NaN and infinties. */
                if (x.data.number == y.data.number) {
                    return 0;
                } else {
                    return x.data.number > y.data.number ? 1 : -1;
                }
            case GST_STRING:
                if (x.data.string == y.data.string) {
                    return 0;
                } else {
                    uint32_t xlen = gst_string_length(x.data.string);
                    uint32_t ylen = gst_string_length(y.data.string);
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
            default:
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
static int32_t to_index(GstNumber raw, int64_t len) {
	int32_t toInt = raw;
	if ((GstNumber) toInt == raw) {
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
static uint8_t to_byte(GstNumber raw) {
	if (raw > 255) return 255;
	if (raw < 0) return 0;
	return (uint8_t) raw;
}

/* Get a value out af an associated data structure. Can throw VM error. */
GstValue gst_get(Gst *vm, GstValue ds, GstValue key) {
    int32_t index;
    GstValue ret;
	switch (ds.type) {
	case GST_ARRAY:
       	gst_assert_type(vm, key, GST_NUMBER);
		index = to_index(key.data.number, ds.data.array->count);
		if (index == -1) gst_error(vm, "Invalid array access");
		return ds.data.array->data[index];
    case GST_BYTEBUFFER:
        gst_assert_type(vm, key, GST_NUMBER);
        index = to_index(key.data.number, ds.data.buffer->count);
		if (index == -1) gst_error(vm, "Invalid buffer access");
		ret.type = GST_NUMBER;
		ret.data.number = ds.data.buffer->data[index];
		break;
    case GST_STRING:
        gst_assert_type(vm, key, GST_NUMBER);
        index = to_index(key.data.number, gst_string_length(ds.data.string));
		if (index == -1) gst_error(vm, "Invalid string access");
		ret.type = GST_NUMBER;
		ret.data.number = ds.data.string[index];
		break;
    case GST_OBJECT:
        return gst_object_get(ds.data.object, key);
    default:
        gst_error(vm, "Cannot get.");
	}
	return ret;
}

/* Set a value in an associative data structure. Can throw VM error. */
void gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value) {
    int32_t index;
	switch (ds.type) {
	case GST_ARRAY:
       	gst_assert_type(vm, key, GST_NUMBER);
		index = to_index(key.data.number, ds.data.array->count);
		if (index == -1) gst_error(vm, "Invalid array access");
		ds.data.array->data[index] = value;
		break;
    case GST_BYTEBUFFER:
        gst_assert_type(vm, key, GST_NUMBER);
        gst_assert_type(vm, value, GST_NUMBER);
        index = to_index(key.data.number, ds.data.buffer->count);
		if (index == -1) gst_error(vm, "Invalid buffer access");
		ds.data.buffer->data[index] = to_byte(value.data.number);
		break;
    case GST_OBJECT:
        gst_object_put(vm, ds.data.object, key, value);
        break;
    default:
        gst_error(vm, "Cannot set.");
	}
}

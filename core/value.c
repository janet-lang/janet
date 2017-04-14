#include <gst/gst.h>
#include <stdio.h>

/* Boolean truth definition */
int gst_truthy(GstValue v) {
    return v.type != GST_NIL && !(v.type == GST_BOOLEAN && !v.data.boolean);
}

/* Temporary buffer size */
#define GST_BUFSIZE 36

static const uint8_t *number_to_string(Gst *vm, GstNumber x) {
    uint8_t buf[GST_BUFSIZE];
    /* TODO - not depend on stdio */
    int count = snprintf((char *) buf, GST_BUFSIZE, "%.21g", x);
    return gst_string_loadbuffer(vm, buf, (uint32_t) count);
}

static const char *HEX_CHARACTERS = "0123456789abcdef";
#define HEX(i) (((uint8_t *) HEX_CHARACTERS)[(i)])

/* Returns a string description for a pointer. Max titlelen is GST_BUFSIZE
 * - 5 - 2 * sizeof(void *). */
static const uint8_t *string_description(Gst *vm, const char *title, void *pointer) {
    uint8_t buf[GST_BUFSIZE];
    uint8_t *c = buf;
    uint32_t i;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
    } pbuf;

    pbuf.p = pointer;
    *c++ = '<';
    for (i = 0; title[i]; ++i)
        *c++ = ((uint8_t *)title) [i];
    *c++ = ' ';
    *c++ = '0';
    *c++ = 'x';
    for (i = sizeof(void *); i > 0; --i) {
        uint8_t byte = pbuf.bytes[i - 1];
        if (!byte) continue;
        *c++ = HEX(byte >> 4);
        *c++ = HEX(byte & 0xF);
    }
    *c++ = '>';
    return gst_string_loadbuffer(vm, buf, c - buf);
}

#undef GST_BUFSIZE

/* Returns a string pointer or NULL if could not allocate memory. */
const uint8_t *gst_to_string(Gst *vm, GstValue x) {
    switch (x.type) {
        case GST_NIL:
            return gst_cstring_to_string(vm, "nil");
        case GST_BOOLEAN:
            if (x.data.boolean) {
                return gst_cstring_to_string(vm, "true");
            } else {
                return gst_cstring_to_string(vm, "false");
            }
        case GST_NUMBER:
            return number_to_string(vm, x.data.number);
        case GST_ARRAY:
            return string_description(vm, "array", x.data.pointer);
        case GST_TUPLE:
            return string_description(vm, "tuple", x.data.pointer);
        case GST_OBJECT:
            return string_description(vm, "object", x.data.pointer);
        case GST_STRING:
            return x.data.string;
        case GST_BYTEBUFFER:
            return string_description(vm, "buffer", x.data.pointer);
        case GST_CFUNCTION:
            return string_description(vm, "cfunction", x.data.pointer);
        case GST_FUNCTION:
            return string_description(vm, "function", x.data.pointer);
        case GST_THREAD:
            return string_description(vm, "thread", x.data.pointer);
        case GST_USERDATA:
            return string_description(vm, "userdata", x.data.pointer);
        case GST_FUNCENV:
            return string_description(vm, "funcenv", x.data.pointer);
        case GST_FUNCDEF:
            return string_description(vm, "funcdef", x.data.pointer);
    }
    return NULL;
}

/* Simple hash function to get tuple hash */
static uint32_t tuple_calchash(GstValue *tuple) {
    uint32_t i;
    uint32_t count = gst_tuple_length(tuple);
    uint32_t hash = 5387;
    for (i = 0; i < count; ++i)
        hash = (hash << 5) + hash + gst_hash(tuple[i]);
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
            case GST_TUPLE:
                if (x.data.tuple == y.data.tuple) {
                    result = 1;
                    break;
                }
                if (gst_hash(x) != gst_hash(y) ||
                        gst_tuple_length(x.data.string) != gst_tuple_length(y.data.string)) {
                    result = 0;
                    break;
                }
                result = 1;
                {
                    uint32_t i;
                    for (i = 0; i < gst_tuple_length(x.data.tuple); ++i) {
                        if (!gst_equals(x.data.tuple[i], y.data.tuple[i])) {
                            result = 0;
                            break;
                        }
                    }
                }
                break;
            default:
                /* compare pointers */
                result = (x.data.pointer == y.data.pointer);
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
            hash = gst_string_hash(x.data.string);
            break;
        case GST_TUPLE:
            if (gst_tuple_hash(x.data.tuple))
                hash = gst_tuple_hash(x.data.tuple);
            else
                hash = gst_tuple_hash(x.data.tuple) = tuple_calchash(x.data.tuple);
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
                return gst_string_compare(x.data.string, y.data.string);
                /* Lower indices are most significant */
            case GST_TUPLE:
                {
                    uint32_t i;
                    uint32_t xlen = gst_tuple_length(x.data.tuple);
                    uint32_t ylen = gst_tuple_length(y.data.tuple);
                    uint32_t count = xlen < ylen ? xlen : ylen;
                    for (i = 0; i < count; ++i) {
                        int comp = gst_compare(x.data.tuple[i], y.data.tuple[i]);
                        if (comp != 0) return comp;
                    }
                    if (xlen < ylen)
                        return -1;
                    else if (xlen > ylen)
                        return 1;
                    return 0;
                }
                break;
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
        if (toInt < 0 && len > 0) { 
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

/* Get a value out af an associated data structure. 
 * Returns possible c error message, and NULL for no error. The
 * useful return value is written to out on success */
const char *gst_get(GstValue ds, GstValue key, GstValue *out) {
    int32_t index;
    GstValue ret;
    switch (ds.type) {
    case GST_ARRAY:
        if (key.type != GST_NUMBER) return "expected numeric key";
        index = to_index(key.data.number, ds.data.array->count);
        if (index == -1) return "invalid array access";
        ret = ds.data.array->data[index];
        break;
    case GST_TUPLE:
        if (key.type != GST_NUMBER) return "expected numeric key";
        index = to_index(key.data.number, gst_tuple_length(ds.data.tuple));
        if (index < 0) return "invalid tuple access";
        ret = ds.data.tuple[index];
        break;
    case GST_BYTEBUFFER:
        if (key.type != GST_NUMBER) return "expected numeric key";
        index = to_index(key.data.number, ds.data.buffer->count);
        if (index == -1) return "invalid buffer access";
        ret.type = GST_NUMBER;
        ret.data.number = ds.data.buffer->data[index];
        break;
    case GST_STRING:
        if (key.type != GST_NUMBER) return "expected numeric key";
        index = to_index(key.data.number, gst_string_length(ds.data.string));
        if (index == -1) return "invalid string access";
        ret.type = GST_NUMBER;
        ret.data.number = ds.data.string[index];
        break;
    case GST_OBJECT:
        ret = gst_object_get(ds.data.object, key);
        break;
    default:
       return "cannot get";
    }
    *out = ret;
    return NULL;
}

/* Set a value in an associative data structure. Returns possible
 * error message, and NULL if no error. */
const char *gst_set(Gst *vm, GstValue ds, GstValue key, GstValue value) {
    int32_t index;
    switch (ds.type) {
    case GST_ARRAY:
        if (key.type != GST_NUMBER) return "expected numeric key";
        index = to_index(key.data.number, ds.data.array->count);
        if (index == -1) return "invalid array access";
        ds.data.array->data[index] = value;
        break;
    case GST_BYTEBUFFER:
        if (key.type != GST_NUMBER) return "expected numeric key";
        if (value.type != GST_NUMBER) return "expected numeric value";
        index = to_index(key.data.number, ds.data.buffer->count);
        if (index == -1) return "invalid buffer access";
        ds.data.buffer->data[index] = to_byte(value.data.number);
        break;
    case GST_OBJECT:
        gst_object_put(vm, ds.data.object, key, value);
        break;
    default:
        return "cannot set";
    }
    return NULL;
}

/* Get the length of an object. Returns errors for invalid types */
int gst_length(Gst *vm, GstValue x, GstValue *len) {
    uint32_t length;
    switch (x.type) {
        default:
            vm->ret = gst_load_cstring(vm, "cannot get length");
            return GST_RETURN_ERROR;
        case GST_STRING:
            length = gst_string_length(x.data.string);
            break;
        case GST_ARRAY:
            length = x.data.array->count;
            break;
        case GST_BYTEBUFFER:
            length = x.data.buffer->count;
            break;
        case GST_TUPLE:
            length = gst_tuple_length(x.data.tuple);
            break;
        case GST_OBJECT:
            length = x.data.object->count;
            break;
    }
    /* Normal numeric return */
    len->type = GST_NUMBER;
    len->data.number = (GstNumber) length;
    return GST_RETURN_OK;
}


/*
* Copyright (c) 2017 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include "internal.h"
#include "cache.h"
#include "wrap.h"
#include "gc.h"

static const char *types[] = {
    "nil",
    "real",
    "integer",
    "boolean",
    "string",
    "symbol",
    "array",
    "tuple",
    "table",
    "struct",
    "thread",
    "buffer",
    "function",
    "cfunction",
    "userdata"
};

static const char base64[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* Calculate hash for string */
static uint32_t dst_string_calchash(const uint8_t *str, uint32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Begin building a string */
uint8_t *dst_string_begin(Dst *vm, uint32_t length) {
    char *data = dst_alloc(vm, DST_MEMORY_NONE, 2 * sizeof(uint32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(uint32_t));
    dst_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *dst_string_end(Dst *vm, uint8_t *str) {
    DstValue check = dst_string_calchash(str, dst_string_length(str));
    const uint8_t *ret = dst_cache_add(vm, dst_wrap_string(check)).as.string;
    gc_settype(dst_string_raw(ret), DST_MEMORY_STRING);
    return ret;
}

/* Load a buffer as a string */
static const uint8_t *dst_string(Dst *vm, const uint8_t *buf, uint32_t len) {
    uint32_t hash = dst_string_calchash(buf, len);
    int status = 0;
    DstValue *bucket = dst_cache_strfind(vm, buf, len, hash, &status);
    if (status) {
        return bucket->data.string;
    } else {
        uint32_t newbufsize = len + 2 * sizeof(uint32_t) + 1;
        uint8_t *str = (uint8_t *)(dst_alloc(vm, DST_MEMORY_STRING, newbufsize) + 2 * sizeof(uint32_t));
        dst_memcpy(str, buf, len);
        dst_string_length(str) = len;
        dst_string_hash(str) = hash;
        str[len] = 0;
        return dst_cache_add_bucket(vm, dst_wrap_string(str), bucket).as.string;
    }
}

/* Helper for creating a unique string. Increment an integer
 * represented as an array of integer digits. */
static void inc_counter(uint8_t *digits, int base, int len) {
    int i;
    uint8_t carry = 1;
    for (i = len - 1; i >= 0; --i) {
        digits[i] += carry;
        carry = 0;
        if (digits[i] == base) {
            digits[i] = 0;
            carry = 1;
        }
    }
}

/* Generate a unique symbol. This is used in the library function gensym. The
 * symbol string data does not have GC enabled on it yet. You must manuallyb enable
 * it later. */
const uint8_t *dst_string_unique(Dst *vm, const uint8_t *buf, uint32_t len) {
    DstValue *bucket;
    uint32_t hash;
    uint8_t counter[6] = {63, 63, 63, 63, 63, 63};
    /* Leave spaces for 6 base 64 digits and two dashes. That means 64^6 possible suffixes, which
     * is enough for resolving collisions. */
    uint32_t newlen = len + 8;
    uint32_t newbufsize = newlen + 2 * sizeof(uint32_t) + 1;
    uint8_t *str = (uint8_t *)(dst_alloc(vm, DST_MEMORY_STRING, newbufsize) + 2 * sizeof(uint32_t));
    dst_string_length(str) = newlen;
    memcpy(str, buf, len);
    str[len] = '-';
    str[len + 1] = '-';
    str[newlen] = 0;
    uint8_t *saltbuf = str + len + 2;
    int status = 1;
    while (status) {
        int i;
        inc_counter(counter, 64, 6);
        for (i = 0; i < 6; ++i)
            saltbuf[i] = base64[counter[i]];
        hash = dst_string_calchash(str, newlen);
        bucket = dst_cache_strfind(vm, str, newlen, hash, &status);
    }
    dst_string_hash(str) = hash;
    return dst_cache_add_bucket(vm, dst_wrap_string(str), bucket).as.string;
}

/* Generate a unique string from a cstring */
const uint8_t *dst_cstring_unique(Dst *vm, const char *s) {
    uint32_t len = 0;
    while (s[len]) ++len;
    return dst_string_unique(vm, (const uint8_t *)s, len);
}

/* Load a c string */
const uint8_t *dst_cstring(Dst *vm, const char *str) {
    uint32_t len = 0;
    while (str[len]) ++len;
    return dst_string(vm, (const uint8_t *)str, len);
}

/* Temporary buffer size */
#define DST_BUFSIZE 36

static uint32_t real_to_string_impl(uint8_t *buf, double x) {
    int count = snprintf((char *) buf, DST_BUFSIZE, "%.21gF", x);
    return (uint32_t) count;
}

static void real_to_string_b(Dst *vm, DstBuffer *buffer, double x) {
    dst_buffer_ensure_(vm, buffer, buffer->count + DST_BUFSIZE);
    buffer->count += real_to_string_impl(buffer->data + buffer->count, x);
}

static void real_to_string(Dst *vm, double x) {
    uint8_t buf[DST_BUFSIZE];
    dst_string(vm, buf, real_to_string_impl(buf, x));
}

static uint32_t integer_to_string_impl(uint8_t *buf, int64_t x) {
    int neg = 0;
    uint8_t *hi, *low;
    uint32_t count = 0;
    if (x == 0) {
        buf[0] = '0';
        return 1;
    }
    if (x < 0) {
        neg = 1;
        x = -x;
    }
    while (x > 0) {
        uint8_t digit = x % 10;
        buf[count++] = '0' + digit;
        x /= 10;
    }
    if (neg)
        buf[count++] = '-';
    /* Reverse */
    hi = buf + count - 1;
    low = buf;
    while (hi > low) {
        uint8_t temp = *low;
        *low++ = *hi;
        *hi-- = temp;
    }
    return count;
}

static void integer_to_string_b(Dst *vm, DstBuffer *buffer, int64_t x) {
    dst_buffer_extra(vm, buffer, DST_BUFSIZE);
    buffer->count += integer_to_string_impl(buffer->data + buffer->count, x);
}

static void integer_to_string(Dst *vm, int64_t x) {
    uint8_t buf[DST_BUFSIZE];
    dst_string(vm, buf, integer_to_string_impl(buf, x));
}

#define HEX(i) (((uint8_t *) base64)[(i)])

/* Returns a string description for a pointer. Truncates
 * title to 12 characters */
static uint32_t string_description_impl(uint8_t *buf, const char *title, void *pointer) {
    uint8_t *c = buf;
    uint32_t i;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
    } pbuf;

    pbuf.p = pointer;
    *c++ = '<';
    for (i = 0; title[i] && i < 12; ++i)
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
    return (uint32_t) (c - buf);
}

static void string_description_b(Dst *vm, DstBuffer *buffer, const char *title, void *pointer) {
    dst_buffer_ensure_(vm, buffer, buffer->count + DST_BUFSIZE);
    buffer->count += string_description_impl(buffer->data + buffer->count, title, pointer);
}

static const uint8_t *string_description(Dst *vm, const char *title, void *pointer) {
    uint8_t buf[DST_BUFSIZE];
    return dst_string(vm, buf, string_description_impl(buf, title, pointer));
}

#undef HEX
#undef DST_BUFSIZE

static uint32_t dst_escape_string_length(const uint8_t *str) {
    uint32_t len = 2;
    uint32_t i;
    for (i = 0; i < dst_string_length(str); ++i) {
        switch (str[i]) {
            case '"':
            case '\n':
            case '\r':
            case '\0':
                len += 2;
                break;
            default:
                len += 1;
                break;
        }
    }
    return len;
}

static void dst_escape_string_impl(uint8_t *buf, const uint8_t *str) {
    uint32_t i, j;
    buf[0] = '"';
    for (i = 0, j = 1; i < dst_string_length(str); ++i) {
        uint8_t c = str[i];
        switch (c) {
            case '"':
                buf[j++] = '\\';
                buf[j++] = '"';
                break;
            case '\n':
                buf[j++] = '\\';
                buf[j++] = 'n';
                break;
            case '\r':
                buf[j++] = '\\';
                buf[j++] = 'r';
                break;
            case '\0':
                buf[j++] = '\\';
                buf[j++] = '0';
                break;
            default:
                buf[j++] = c;
                break;
        }
    }
    buf[j++] = '"';
}

static void dst_escape_string_b(Dst *vm, DstBuffer *buffer, const uint8_t *str) {
    uint32_t len = dst_escape_string_length(str);
    dst_buffer_extra(vm, buffer, len);
    dst_escape_string_impl(buffer->data + buffer->count, str);
    buffer->count += len;
}

static const uint8_t *dst_escape_string(Dst *vm, const uint8_t *str) {
    uint32_t len = dst_escape_string_length(str);
    uint8_t *buf = dst_string_begin(vm, len);
    dst_escape_string_impl(buf, str);
    return dst_string_end(vm, buf);
}

/* Returns a string pointer with the description of the string */
static const uint8_t *dst_short_description(Dst *vm) {
    switch (x.type) {
    case DST_NIL:
        return dst_cstring(vm, "nil");
    case DST_BOOLEAN:
        if (x.as.boolean)
            return dst_cstring(vm, "true");
        else
            return dst_cstring(vm, "false");
    case DST_REAL:
        return real_to_string(vm, x.as.real);
    case DST_INTEGER:
        return integer_to_string(vm, x.as.integer);
    case DST_SYMBOL:
        return x.as.string;
    case DST_STRING:
        return dst_escape_string(vm, x.as.string);
    case DST_USERDATA:
        return string_description(vm, dst_udata_type(x.as.pointer)->name, x.as.pointer);
    default:
        return string_description(vm, types[x.type], x.as.pointer);
    }
}

static void dst_short_description_b(Dst *vm, DstBuffer *buffer, DstValue x) {
    switch (x.type) {
    case DST_NIL:
        dst_buffer_push_cstring(vm, buffer, "nil");
        return;
    case DST_BOOLEAN:
        if (x.as.boolean)
            dst_buffer_push_cstring(vm, buffer, "true");
        else
            dst_buffer_push_cstring(vm, buffer, "false");
        return;
    case DST_REAL:
        real_to_string_b(vm, buffer, x.as.real);
        return;
    case DST_INTEGER:
        integer_to_string_b(vm, buffer, x.as.integer);
        return;
    case DST_SYMBOL:
        dst_buffer_push_bytes(vm, buffer, x.as.string, dst_string_length(x.as.string));
        return;
    case DST_STRING:
        dst_escape_string_b(vm, buffer, x.as.string);
        return;
    case DST_USERDATA:
        string_description_b(vm, buffer, dst_udata_type(x.as.pointer)->name, x.as.pointer);
        return;
    default:
        string_description_b(vm, buffer, types[x.type], x.as.pointer);
        break;
    }
}

/* Static debug print helper */
static int64_t dst_description_helper(Dst *vm, DstBuffer *b, DstTable *seen, DstValue x, int64_t next, int depth) {
    DstValue check = dst_table_get(seen, x);
    if (check.type == DST_INTEGER) {
        dst_buffer_push_cstring(vm, b, "<cycle>");
    } else {
        const char *open;
        const char *close;
        uint32_t len, i;
        const DstValue *data;
        switch (x.type) {
            default:
                dst_short_description_b(vm, b, x);
                return next;
            case DST_STRUCT:
                open = "{"; close = "}";
                break;
            case DST_TABLE:
                open = "{"; close = "}";
                break;
            case DST_TUPLE:
                open = "("; close = ")";
                break;
            case DST_ARRAY:
                open = "["; close = "]";
                break;
        }
        dst_table_put(vm, seen, x, dst_wrap_integer(next++));
        dst_buffer_push_cstring(vm, b, open);
        if (depth == 0) {
            dst_buffer_push_cstring(vm, b, "...");
        } else if (dst_hashtable_view(x, &data, &len)) {
            int isfirst = 1;
            for (i = 0; i < len; i += 2) {
                if (data[i].type != DST_NIL) {
                    if (isfirst)
                        isfirst = 0;
                    else
                        dst_buffer_push_u8(vm, b, ' ');
                    next = dst_description_helper(vm, b, seen, data[i], next, depth - 1);
                    dst_buffer_push_u8(vm, b, ' ');
                    next = dst_description_helper(vm, b, seen, data[i + 1], next, depth - 1);
                }
            }
        } else if (dst_seq_view(x, &data, &len)) {
            for (i = 0; i < len; ++i) {
                next = dst_description_helper(vm, b, seen, data[i], next, depth - 1);
                if (i != len - 1)
                    dst_buffer_push_u8(vm, b, ' ');
            }
        }
        dst_buffer_push_cstring(vm, b, close);
    }
    return next;
}

/* Debug print. Returns a description of an object as a string. */
const uint8_t *dst_description(Dst *vm, DstValue x) {
    DstBuffer *buf = dst_buffer(vm, 10);
    DstTable *seen = dst_table(vm, 10);

    /* Only print description up to a depth of 4 */
    dst_description_helper(vm, buf, seen, x, 0, 4);

    return dst_string(vm, buf->data, buf->count);
}

/* Convert any value to a dst string */
const uint8_t *dst_to_string(Dst *vm, DstValue x) {
    DstValue x = dst_getv(vm, -1);
    switch (x.type) {
        default:
            return dst_description(vm, x);
        case DST_STRING:
        case DST_SYMBOL:
            return x.as.string;
        case DST_BUFFER:
            return dst_string(vm, x.as.buffer->data, x.as.buffer->count);
    }
}

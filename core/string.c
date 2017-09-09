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

/* Temporary buffer size */
#define DST_BUFSIZE 36

static const uint8_t *real_to_string(Dst *vm, DstReal x) {
    uint8_t buf[DST_BUFSIZE];
    int count = snprintf((char *) buf, DST_BUFSIZE, "%.21gF", x);
    return dst_string_b(vm, buf, (uint32_t) count);
}

static const uint8_t *integer_to_string(Dst *vm, DstInteger x) {
    uint8_t buf[DST_BUFSIZE];
    int neg = 0;
    uint8_t *hi, *low;
    uint32_t count = 0;
    if (x == 0) return dst_string_c(vm, "0");
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
    return dst_string_b(vm, buf, (uint32_t) count);
}

static const char *HEX_CHARACTERS = "0123456789abcdef";
#define HEX(i) (((uint8_t *) HEX_CHARACTERS)[(i)])

/* Returns a string description for a pointer. Max titlelen is DST_BUFSIZE
 * - 5 - 2 * sizeof(void *). */
static const uint8_t *string_description(Dst *vm, const char *title, void *pointer) {
    uint8_t buf[DST_BUFSIZE];
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
    return dst_string_b(vm, buf, c - buf);
}

/* Gets the string value of userdata. Allocates memory, so is slower than
 * string_description. */
static const uint8_t *string_udata(Dst *vm, const char *title, void *pointer) {
    uint32_t strlen = 0;
    uint8_t *c, *buf;
    uint32_t i;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
    } pbuf;

    while (title[strlen]) ++strlen;
    c = buf = dst_alloc(vm, strlen + 5 + 2 * sizeof(void *));
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
    return dst_string_b(vm, buf, c - buf);
}

#undef DST_BUFSIZE

void dst_escape_string(Dst *vm, DstBuffer *b, const uint8_t *str) {
    uint32_t i;
    dst_buffer_push(vm, b, '"');
    for (i = 0; i < dst_string_length(str); ++i) {
        uint8_t c = str[i];
        switch (c) {
            case '"':
                dst_buffer_push(vm, b, '\\');
                dst_buffer_push(vm, b, '"');
                break;
            case '\n':
                dst_buffer_push(vm, b, '\\');
                dst_buffer_push(vm, b, 'n');
                break;
            case '\r':
                dst_buffer_push(vm, b, '\\');
                dst_buffer_push(vm, b, 'r');
                break;
            case '\0':
                dst_buffer_push(vm, b, '\\');
                dst_buffer_push(vm, b, '0');
                break;
            default:
                dst_buffer_push(vm, b, c);
                break;
        }
    }
    dst_buffer_push(vm, b, '"');
}

/* Returns a string pointer with the description of the string */
const uint8_t *dst_short_description(Dst *vm, DstValue x) {
    switch (x.type) {
    case DST_NIL:
        return dst_string_c(vm, "nil");
    case DST_BOOLEAN:
        if (x.data.boolean)
            return dst_string_c(vm, "true");
        else
            return dst_string_c(vm, "false");
    case DST_REAL:
        return real_to_string(vm, x.data.real);
    case DST_INTEGER:
        return integer_to_string(vm, x.data.integer);
    case DST_ARRAY:
        return string_description(vm, "array", x.data.pointer);
    case DST_TUPLE:
        return string_description(vm, "tuple", x.data.pointer);
    case DST_STRUCT:
        return string_description(vm, "struct", x.data.pointer);
    case DST_TABLE:
        return string_description(vm, "table", x.data.pointer);
    case DST_SYMBOL:
        return x.data.string;
    case DST_STRING:
    {
        DstBuffer *buf = dst_buffer(vm, dst_string_length(x.data.string) + 4);
        dst_escape_string(vm, buf, x.data.string);
        return dst_buffer_to_string(vm, buf);
    }
    case DST_BYTEBUFFER:
        return string_description(vm, "buffer", x.data.pointer);
    case DST_CFUNCTION:
        return string_description(vm, "cfunction", x.data.pointer);
    case DST_FUNCTION:
        return string_description(vm, "function", x.data.pointer);
    case DST_THREAD:
        return string_description(vm, "thread", x.data.pointer);
    case DST_USERDATA:
        return string_udata(vm, dst_udata_type(x.data.pointer)->name, x.data.pointer);
    case DST_FUNCENV:
        return string_description(vm, "funcenv", x.data.pointer);
    case DST_FUNCDEF:
        return string_description(vm, "funcdef", x.data.pointer);
    }
}

static DstValue wrap_integer(DstInteger i) {
    DstValue v;
    v.type = DST_INTEGER;
    v.data.integer = i;
    return v;
}

/* Static debug print helper */
static DstInteger dst_description_helper(Dst *vm, DstBuffer *b, DstTable *seen, DstValue x, DstInteger next, int depth) {
    DstValue check = dst_table_get(seen, x);
    const uint8_t *str;
    /* Prevent a stack overflow */
    if (depth++ > DST_RECURSION_GUARD)
        return -1;
    if (check.type == DST_INTEGER) {
        str = integer_to_string(vm, check.data.integer);
        dst_buffer_append_cstring(vm, b, "<visited ");
        dst_buffer_append(vm, b, str, dst_string_length(str));
        dst_buffer_append_cstring(vm, b, ">");
    } else {
        uint8_t open, close;
        uint32_t len, i;
        const DstValue *data;
        switch (x.type) {
            default:
                str = dst_short_description(vm, x);
                dst_buffer_append(vm, b, str, dst_string_length(str));
                return next;
            case DST_STRING:
                dst_escape_string(vm, b, x.data.string);
                return next;
            case DST_SYMBOL:
                dst_buffer_append(vm, b, x.data.string, dst_string_length(x.data.string));
                return next;
            case DST_NIL:
                dst_buffer_append_cstring(vm, b, "nil");
                return next;
            case DST_BOOLEAN:
                dst_buffer_append_cstring(vm, b, x.data.boolean ? "true" : "false");
                return next;
            case DST_STRUCT:
                open = '<'; close = '>';
                break;
            case DST_TABLE:
                open = '{'; close = '}';
                break;
            case DST_TUPLE:
                open = '('; close = ')';
                break;
            case DST_ARRAY:
                open = '['; close = ']';
                break;
        }
        dst_table_put(vm, seen, x, wrap_integer(next++));
        dst_buffer_push(vm, b, open);
        if (dst_hashtable_view(x, &data, &len)) {
            int isfirst = 1;
            for (i = 0; i < len; i += 2) {
                if (data[i].type != DST_NIL) {
                    if (isfirst)
                        isfirst = 0;
                    else
                        dst_buffer_push(vm, b, ' ');
                    next = dst_description_helper(vm, b, seen, data[i], next, depth);
                    if (next == -1)
                        dst_buffer_append_cstring(vm, b, "...");
                    dst_buffer_push(vm, b, ' ');
                    next = dst_description_helper(vm, b, seen, data[i + 1], next, depth);
                    if (next == -1)
                        dst_buffer_append_cstring(vm, b, "...");
                }
            }
        } else if (dst_seq_view(x, &data, &len)) {
            for (i = 0; i < len; ++i) {
                next = dst_description_helper(vm, b, seen, data[i], next, depth);
                if (next == -1)
                    return -1;
                if (i != len - 1)
                    dst_buffer_push(vm, b, ' ');
            }
        }
        dst_buffer_push(vm, b, close);
    }
    return next;
}

/* Debug print. Returns a description of an object as a buffer. */
const uint8_t *dst_description(Dst *vm, DstValue x) {
    DstBuffer *buf = dst_buffer(vm, 10);
    dst_description_helper(vm, buf, dst_table(vm, 10), x, 0, 0);
    return dst_buffer_to_string(vm, buf);
}

const uint8_t *dst_to_string(Dst *vm, DstValue x) {
    if (x.type == DST_STRING || x.type == DST_SYMBOL) {
        return x.data.string;
    } else if (x.type == DST_BYTEBUFFER) {
        return dst_buffer_to_string(vm, x.data.buffer);
    } else {
        return dst_description(vm, x);
    }
}
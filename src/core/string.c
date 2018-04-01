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

#include <dst/dst.h>
#include "gc.h"
#include "util.h"

/* Begin building a string */
uint8_t *dst_string_begin(int32_t length) {
    char *data = dst_gcalloc(DST_MEMORY_STRING, 2 * sizeof(int32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    dst_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *dst_string_end(uint8_t *str) {
    dst_string_hash(str) = dst_string_calchash(str, dst_string_length(str));
    return str;
}

/* Load a buffer as a string */
const uint8_t *dst_string(const uint8_t *buf, int32_t len) {
    int32_t hash = dst_string_calchash(buf, len);
    char *data = dst_gcalloc(DST_MEMORY_STRING, 2 * sizeof(int32_t) + len + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    memcpy(str, buf, len);
    str[len] = 0;
    dst_string_length(str) = len;
    dst_string_hash(str) = hash;
    return str;
}

/* Compare two strings */
int dst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    int32_t xlen = dst_string_length(lhs);
    int32_t ylen = dst_string_length(rhs);
    int32_t len = xlen > ylen ? ylen : xlen;
    int32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less than y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

/* Compare a dst string with a piece of memory */
int dst_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash) {
    int32_t index;
    int32_t lhash = dst_string_hash(lhs);
    int32_t llen = dst_string_length(lhs);
    if (lhs == rhs)
        return 1;
    if (lhash != rhash || llen != rlen)
        return 0;
    for (index = 0; index < llen; index++) {
        if (lhs[index] != rhs[index])
            return 0;
    }
    return 1;
}

/* Check if two strings are equal */
int dst_string_equal(const uint8_t *lhs, const uint8_t *rhs) {
    return dst_string_equalconst(lhs, rhs,
            dst_string_length(rhs), dst_string_hash(rhs));
}

/* Load a c string */
const uint8_t *dst_cstring(const char *str) {
    int32_t len = 0;
    while (str[len]) ++len;
    return dst_string((const uint8_t *)str, len);
}

/* Temporary buffer size */
#define BUFSIZE 36

static int32_t real_to_string_impl(uint8_t *buf, double x) {
    /* Use 16 decimal places to ignore one ulp errors for now */
    int count = snprintf((char *) buf, BUFSIZE, "%.16gR", x);
    return (int32_t) count;
}

static void real_to_string_b(DstBuffer *buffer, double x) {
    dst_buffer_ensure(buffer, buffer->count + BUFSIZE);
    buffer->count += real_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *real_to_string(double x) {
    uint8_t buf[BUFSIZE];
    return dst_string(buf, real_to_string_impl(buf, x));
}

static int32_t integer_to_string_impl(uint8_t *buf, int32_t x) {
    int neg = 1;
    uint8_t *hi, *low;
    int32_t count = 0;
    if (x == 0) {
        buf[0] = '0';
        return 1;
    }
    if (x > 0) {
        neg = 0;
        x = -x;
    }
    while (x < 0) {
        uint8_t digit = (uint8_t) -(x % 10);
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

static void integer_to_string_b(DstBuffer *buffer, int32_t x) {
    dst_buffer_extra(buffer, BUFSIZE);
    buffer->count += integer_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *integer_to_string(int32_t x) {
    uint8_t buf[BUFSIZE];
    return dst_string(buf, integer_to_string_impl(buf, x));
}

#define HEX(i) (((uint8_t *) dst_base64)[(i)])

/* Returns a string description for a pointer. Truncates
 * title to 12 characters */
static int32_t string_description_impl(uint8_t *buf, const char *title, void *pointer) {
    uint8_t *c = buf;
    int32_t i;
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
#if defined(DST_64)
#define POINTSIZE 6
#else
#define POINTSIZE (sizeof(void *))
#endif
    for (i = POINTSIZE; i > 0; --i) {
        uint8_t byte = pbuf.bytes[i - 1];
        *c++ = HEX(byte >> 4);
        *c++ = HEX(byte & 0xF);
    }
    *c++ = '>';
    return (int32_t) (c - buf);
#undef POINTSIZE
}

static void string_description_b(DstBuffer *buffer, const char *title, void *pointer) {
    dst_buffer_ensure(buffer, buffer->count + BUFSIZE);
    buffer->count += string_description_impl(buffer->data + buffer->count, title, pointer);
}

/* Describes a pointer with a title (string_description("bork",  myp) returns 
 * a string "<bork 0x12345678>") */
static const uint8_t *string_description(const char *title, void *pointer) {
    uint8_t buf[BUFSIZE];
    return dst_string(buf, string_description_impl(buf, title, pointer));
}

#undef HEX
#undef BUFSIZE

/* TODO - add more characters to escape. 
 *
 * When more escapes are added, they must correspond
 * to dst_escape_string_impl exactly or a buffer overrun could occur. */
static int32_t dst_escape_string_length(const uint8_t *str, int32_t slen) {
    int32_t len = 2;
    int32_t i;
    for (i = 0; i < slen; ++i) {
        switch (str[i]) {
            case '"':
            case '\n':
            case '\r':
            case '\0':
            case '\\':
                len += 2;
                break;
            default:
                len += 1;
                break;
        }
    }
    return len;
}

static void dst_escape_string_impl(uint8_t *buf, const uint8_t *str, int32_t len) {
    int32_t i, j;
    buf[0] = '"';
    for (i = 0, j = 1; i < len; ++i) {
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
            case '\\':
                buf[j++] = '\\';
                buf[j++] = '\\';
                break;
            default:
                buf[j++] = c;
                break;
        }
    }
    buf[j++] = '"';
}

void dst_escape_string_b(DstBuffer *buffer, const uint8_t *str) {
    int32_t len = dst_string_length(str);
    int32_t elen = dst_escape_string_length(str, len);
    dst_buffer_extra(buffer, elen);
    dst_escape_string_impl(buffer->data + buffer->count, str, len);
    buffer->count += elen;
}

const uint8_t *dst_escape_string(const uint8_t *str) {
    int32_t len = dst_string_length(str);
    int32_t elen = dst_escape_string_length(str, len);
    uint8_t *buf = dst_string_begin(elen);
    dst_escape_string_impl(buf, str, len); 
    return dst_string_end(buf);
}

static void dst_escape_buffer_b(DstBuffer *buffer, DstBuffer *bx) {
    int32_t elen = dst_escape_string_length(bx->data, bx->count);
    dst_buffer_push_u8(buffer, '@');
    dst_buffer_extra(buffer, elen);
    dst_escape_string_impl(
            buffer->data + buffer->count,
            bx->data,
            bx->count);
    buffer->count += elen;
}

void dst_description_b(DstBuffer *buffer, Dst x) {
    switch (dst_type(x)) {
    case DST_NIL:
        dst_buffer_push_cstring(buffer, "nil");
        return;
    case DST_TRUE:
        dst_buffer_push_cstring(buffer, "true");
        return;
    case DST_FALSE:
        dst_buffer_push_cstring(buffer, "false");
        return;
    case DST_REAL:
        real_to_string_b(buffer, dst_unwrap_real(x));
        return;
    case DST_INTEGER:
        integer_to_string_b(buffer, dst_unwrap_integer(x));
        return;
    case DST_SYMBOL:
        dst_buffer_push_bytes(buffer, 
                dst_unwrap_string(x), 
                dst_string_length(dst_unwrap_string(x)));
        return;
    case DST_STRING:
        dst_escape_string_b(buffer, dst_unwrap_string(x));
        return;
    case DST_BUFFER:
        dst_escape_buffer_b(buffer, dst_unwrap_buffer(x));
        return;
    case DST_ABSTRACT:
        {
            const char *n = dst_abstract_type(dst_unwrap_abstract(x))->name;
            return string_description_b(buffer, 
                    n[0] == ':' ? n + 1 : n,
                    dst_unwrap_abstract(x));
        }
    default:
        string_description_b(buffer, dst_type_names[dst_type(x)] + 1, dst_unwrap_pointer(x));
        break;
    }
}

void dst_to_string_b(DstBuffer *buffer, Dst x) {
    switch (dst_type(x)) {
        default:
            dst_description_b(buffer, x);
            break;
        case DST_BUFFER:
            dst_buffer_push_bytes(buffer, 
                    dst_unwrap_buffer(x)->data,
                    dst_unwrap_buffer(x)->count);
            break;
        case DST_STRING:
        case DST_SYMBOL:
            dst_buffer_push_bytes(buffer, 
                    dst_unwrap_string(x),
                    dst_string_length(dst_unwrap_string(x)));
            break;
    }
}

const uint8_t *dst_description(Dst x) {
    switch (dst_type(x)) {
    case DST_NIL:
        return dst_cstring("nil");
    case DST_TRUE:
        return dst_cstring("true");
    case DST_FALSE:
        return dst_cstring("false");
    case DST_REAL:
        return real_to_string(dst_unwrap_real(x));
    case DST_INTEGER:
        return integer_to_string(dst_unwrap_integer(x));
    case DST_SYMBOL:
        return dst_unwrap_symbol(x);
    case DST_STRING:
        return dst_escape_string(dst_unwrap_string(x));
    case DST_BUFFER:
        {
            DstBuffer b;
            const uint8_t *ret;
            dst_buffer_init(&b, 3);
            dst_escape_buffer_b(&b, dst_unwrap_buffer(x));
            ret = dst_string(b.data, b.count);
            dst_buffer_deinit(&b);
            return ret;
        }
    case DST_ABSTRACT:
        {
            const char *n = dst_abstract_type(dst_unwrap_abstract(x))->name;
            return string_description( 
                    n[0] == ':' ? n + 1 : n,
                    dst_unwrap_abstract(x));
        }
    default:
        return string_description(dst_type_names[dst_type(x)] + 1, dst_unwrap_pointer(x));
    }
}

/* Convert any value to a dst string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *dst_to_string(Dst x) {
    switch (dst_type(x)) {
        default:
            return dst_description(x);
        case DST_BUFFER:
            return dst_string(dst_unwrap_buffer(x)->data, dst_unwrap_buffer(x)->count);
        case DST_STRING:
        case DST_SYMBOL:
            return dst_unwrap_string(x);
    }
}

/* Helper function for formatting strings. Useful for generating error messages and the like.
 * Similiar to printf, but specialized for operating with dst. */
const uint8_t *dst_formatc(const char *format, ...) {
    va_list args;
    int32_t len = 0;
    int32_t i;
    const uint8_t *ret;
    DstBuffer buffer;
    DstBuffer *bufp = &buffer;
    
    /* Calculate length */
    while (format[len]) len++;

    /* Initialize buffer */
    dst_buffer_init(bufp, len);

    /* Start args */
    va_start(args, format);

    /* Iterate length */
    for (i = 0; i < len; i++) {
        uint8_t c = format[i];
        switch (c) {
            default:
                dst_buffer_push_u8(bufp, c);
                break;
            case '%':
            {
                if (i + 1 >= len)
                    break;
                switch (format[++i]) {
                    default: 
                        dst_buffer_push_u8(bufp, format[i]);
                        break;
                    case 'f': 
                        real_to_string_b(bufp, va_arg(args, double));
                        break;
                    case 'd': 
                        integer_to_string_b(bufp, va_arg(args, int32_t));
                        break;
                    case 'S':
                    {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        dst_buffer_push_bytes(bufp, str, dst_string_length(str));
                        break;
                    }
                    case 's':
                        dst_buffer_push_cstring(bufp, va_arg(args, const char *));
                        break;
                    case 'c':
                        dst_buffer_push_u8(bufp, va_arg(args, long));
                        break;
                    case 'q':
                    {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        dst_escape_string_b(bufp, str);
                        break;
                    }
                    case 't':
                    {
                        dst_buffer_push_cstring(bufp, dst_type_names[va_arg(args, DstType)]);
                        break;
                    }
                    case 'V': 
                    {
                        dst_to_string_b(bufp, va_arg(args, Dst));
                        break;
                    }
                    case 'v': 
                    {
                        dst_description_b(bufp, va_arg(args, Dst));
                        break;
                    }
                }
            }
        }
    }

    va_end(args);

    ret = dst_string(buffer.data, buffer.count);
    dst_buffer_deinit(&buffer);
    return ret;
}

/* Print string to stdout */
void dst_puts(const uint8_t *str) {
    int32_t i;
    int32_t len = dst_string_length(str);
    for (i = 0; i < len; i++) {
        putc(str[i], stdout);
    }
}

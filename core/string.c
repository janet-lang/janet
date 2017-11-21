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
#include "cache.h"

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
uint8_t *dst_string_begin(uint32_t length) {
    char *data = dst_alloc(DST_MEMORY_NONE, 2 * sizeof(uint32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(uint32_t));
    dst_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *dst_string_end(uint8_t *str) {
    DstValue check;
    dst_string_hash(str) = dst_string_calchash(str, dst_string_length(str));
    check = dst_cache_add(dst_wrap_string(str));
    /* Don't tag the memory of the string builder directly. If the string is
     * already cached, we don't want the gc to remove it from cache when the original
     * string builder is gced (check will contained the cached string) */
    dst_gc_settype(dst_string_raw(check.as.string), DST_MEMORY_STRING);
    return check.as.string;
}

/* Load a buffer as a string */
const uint8_t *dst_string(const uint8_t *buf, uint32_t len) {
    uint32_t hash = dst_string_calchash(buf, len);
    int status = 0;
    DstValue *bucket = dst_cache_strfind(buf, len, hash, &status);
    if (status) {
        return bucket->as.string;
    } else {
        uint32_t newbufsize = len + 2 * sizeof(uint32_t) + 1;
        uint8_t *str = (uint8_t *)(dst_alloc(DST_MEMORY_STRING, newbufsize) + 2 * sizeof(uint32_t));
        memcpy(str, buf, len);
        dst_string_length(str) = len;
        dst_string_hash(str) = hash;
        str[len] = 0;
        return dst_cache_add_bucket(dst_wrap_string(str), bucket).as.string;
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
const uint8_t *dst_string_unique(const uint8_t *buf, uint32_t len) {
    DstValue *bucket;
    uint32_t hash;
    uint8_t counter[6] = {63, 63, 63, 63, 63, 63};
    /* Leave spaces for 6 base 64 digits and two dashes. That means 64^6 possible suffixes, which
     * is enough for resolving collisions. */
    uint32_t newlen = len + 8;
    uint32_t newbufsize = newlen + 2 * sizeof(uint32_t) + 1;
    uint8_t *str = (uint8_t *)(dst_alloc(DST_MEMORY_STRING, newbufsize) + 2 * sizeof(uint32_t));
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
        bucket = dst_cache_strfind(str, newlen, hash, &status);
    }
    dst_string_hash(str) = hash;
    return dst_cache_add_bucket(dst_wrap_string(str), bucket).as.string;
}

/* Generate a unique string from a cstring */
const uint8_t *dst_cstring_unique(const char *s) {
    uint32_t len = 0;
    while (s[len]) ++len;
    return dst_string_unique((const uint8_t *)s, len);
}

/* Load a c string */
const uint8_t *dst_cstring(const char *str) {
    uint32_t len = 0;
    while (str[len]) ++len;
    return dst_string((const uint8_t *)str, len);
}

/* Temporary buffer size */
#define DST_BUFSIZE 36

static uint32_t real_to_string_impl(uint8_t *buf, double x) {
    int count = snprintf((char *) buf, DST_BUFSIZE, "%.21gF", x);
    return (uint32_t) count;
}

static void real_to_string_b(DstBuffer *buffer, double x) {
    dst_buffer_ensure(buffer, buffer->count + DST_BUFSIZE);
    buffer->count += real_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *real_to_string(double x) {
    uint8_t buf[DST_BUFSIZE];
    return dst_string(buf, real_to_string_impl(buf, x));
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

static void integer_to_string_b(DstBuffer *buffer, int64_t x) {
    dst_buffer_extra(buffer, DST_BUFSIZE);
    buffer->count += integer_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *integer_to_string(int64_t x) {
    uint8_t buf[DST_BUFSIZE];
    return dst_string(buf, integer_to_string_impl(buf, x));
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

static void string_description_b(DstBuffer *buffer, const char *title, void *pointer) {
    dst_buffer_ensure(buffer, buffer->count + DST_BUFSIZE);
    buffer->count += string_description_impl(buffer->data + buffer->count, title, pointer);
}

/* Describes a pointer with a title (string_description("bork",  myp) returns 
 * a string "<bork 0x12345678>") */
static const uint8_t *string_description(const char *title, void *pointer) {
    uint8_t buf[DST_BUFSIZE];
    return dst_string(buf, string_description_impl(buf, title, pointer));
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

void dst_escape_string_b(DstBuffer *buffer, const uint8_t *str) {
    uint32_t len = dst_escape_string_length(str);
    dst_buffer_extra(buffer, len);
    dst_escape_string_impl(buffer->data + buffer->count, str);
    buffer->count += len;
}

const uint8_t *dst_escape_string(const uint8_t *str) {
    uint32_t len = dst_escape_string_length(str);
    uint8_t *buf = dst_string_begin(len);
    dst_escape_string_impl(buf, str);
    return dst_string_end(buf);
}

/* Returns a string pointer with the description of the string */
const uint8_t *dst_short_description(DstValue x) {
    switch (x.type) {
    case DST_NIL:
        return dst_cstring("nil");
    case DST_BOOLEAN:
        if (x.as.boolean)
            return dst_cstring("true");
        else
            return dst_cstring("false");
    case DST_REAL:
        return real_to_string(x.as.real);
    case DST_INTEGER:
        return integer_to_string(x.as.integer);
    case DST_SYMBOL:
        return x.as.string;
    case DST_STRING:
        return dst_escape_string(x.as.string);
    case DST_USERDATA:
        return string_description(dst_userdata_type(x.as.pointer)->name, x.as.pointer);
    default:
        return string_description(dst_type_names[x.type], x.as.pointer);
    }
}

void dst_short_description_b(DstBuffer *buffer, DstValue x) {
    switch (x.type) {
    case DST_NIL:
        dst_buffer_push_cstring(buffer, "nil");
        return;
    case DST_BOOLEAN:
        if (x.as.boolean)
            dst_buffer_push_cstring(buffer, "true");
        else
            dst_buffer_push_cstring(buffer, "false");
        return;
    case DST_REAL:
        real_to_string_b(buffer, x.as.real);
        return;
    case DST_INTEGER:
        integer_to_string_b(buffer, x.as.integer);
        return;
    case DST_SYMBOL:
        dst_buffer_push_bytes(buffer, x.as.string, dst_string_length(x.as.string));
        return;
    case DST_STRING:
        dst_escape_string_b(buffer, x.as.string);
        return;
    case DST_USERDATA:
        string_description_b(buffer, dst_userdata_type(x.as.pointer)->name, x.as.pointer);
        return;
    default:
        string_description_b(buffer, dst_type_names[x.type], x.as.pointer);
        break;
    }
}

/* Helper structure for stringifying deeply nested structures */
typedef struct DstPrinter DstPrinter;
struct DstPrinter {
    DstBuffer buffer;
    DstTable seen;
    uint32_t flags;
    int64_t next;
    uint32_t indent;
    uint32_t indent_size;
    uint32_t token_line_limit;
    uint32_t depth;
};

#define DST_PRINTFLAG_INDENT 1
#define DST_PRINTFLAG_TABLEPAIR 2
#define DST_PRINTFLAG_COLORIZE 4
#define DST_PRINTFLAG_ALLMAN 8

/* Go to next line for printer */
static void dst_print_indent(DstPrinter *p) {
    uint32_t i, len;
    len = p->indent_size * p->indent;
    for (i = 0; i < len; i++) {
        dst_buffer_push_u8(&p->buffer, ' ');
    }
}

/* Check if a value is a print atom (not a printable data structure) */
static int is_print_ds(DstValue v) {
    switch (v.type) {
        default: return 0;
        case DST_ARRAY:
        case DST_STRUCT:
        case DST_TUPLE:
        case DST_TABLE: return 1;
    }
}

/* VT100 Colors for types */
static const char *dst_type_colors[15] = {
    "\x1B[35m",
    "\x1B[33m",
    "\x1B[33m",
    "\x1B[35m",
    "\x1B[32m",
    "\x1B[36m",
    "",
    "",
    "",
    "",
    "\x1B[37m",
    "\x1B[37m",
    "\x1B[37m",
    "\x1B[37m",
    "\x1B[37m"
};

/* Forward declaration */
static void dst_description_helper(DstPrinter *p, DstValue x);

/* Print a hastable view inline */
static void dst_print_hashtable_inner(DstPrinter *p, const DstValue *data, uint32_t len, uint32_t cap) {
    uint32_t i;
    int doindent = 0;
    if (p->flags & DST_PRINTFLAG_INDENT) {
        if (len <= p->token_line_limit) {
            for (i = 0; i < cap; i += 2) {
                if (is_print_ds(data[i]) || is_print_ds(data[i + 1])) {
                    doindent = 1;
                    break;
                }
            }
        } else {
            doindent = 1;
        }
    }
    if (doindent) {
        dst_buffer_push_u8(&p->buffer, '\n');
        p->indent++;
        for (i = 0; i < cap; i += 2) {
            if (data[i].type != DST_NIL) {
                dst_print_indent(p);
                dst_description_helper(p, data[i]);
                dst_buffer_push_u8(&p->buffer, ' ');
                dst_description_helper(p, data[i + 1]);
                dst_buffer_push_u8(&p->buffer, '\n');
            }
        }
        p->indent--;
        dst_print_indent(p);
    } else {
        int isfirst = 1;
        for (i = 0; i < cap; i += 2) {
            if (data[i].type != DST_NIL) {
                if (isfirst)
                    isfirst = 0;
                else
                    dst_buffer_push_u8(&p->buffer, ' ');
                dst_description_helper(p, data[i]);
                dst_buffer_push_u8(&p->buffer, ' ');
                dst_description_helper(p, data[i + 1]);
            }
        }
    }
}

/* Help print a sequence */
static void dst_print_seq_inner(DstPrinter *p, const DstValue *data, uint32_t len) {
    uint32_t i;
    int doindent = 0;
    if (p->flags & DST_PRINTFLAG_INDENT) {
        if (len <= p->token_line_limit) {
            for (i = 0; i < len; ++i) {
                if (is_print_ds(data[i])) {
                    doindent = 1;
                    break;
                }
            }
        } else {
            doindent = 1;
        }
    }
    if (doindent) {
        dst_buffer_push_u8(&p->buffer, '\n');
        p->indent++;
        for (i = 0; i < len; ++i) {
            dst_print_indent(p);
            dst_description_helper(p, data[i]);
            dst_buffer_push_u8(&p->buffer, '\n');
        }
        p->indent--;
        dst_print_indent(p);
    } else {
        for (i = 0; i < len; ++i) {
            dst_description_helper(p, data[i]);
            if (i != len - 1)
                dst_buffer_push_u8(&p->buffer, ' ');
        }
    }
}

/* Static debug print helper */
static void dst_description_helper(DstPrinter *p, DstValue x) {
    p->depth--;
    DstValue check = dst_table_get(&p->seen, x);
    if (check.type == DST_INTEGER) {
        dst_buffer_push_cstring(&p->buffer, "<cycle ");
        integer_to_string_b(&p->buffer, check.as.integer);
        dst_buffer_push_cstring(&p->buffer, ">");
    } else {
        const char *open;
        const char *close;
        uint32_t len, cap;
        const DstValue *data;
        switch (x.type) {
            default:
                if (p->flags & DST_PRINTFLAG_COLORIZE) {
                    dst_buffer_push_cstring(&p->buffer, dst_type_colors[x.type]);
                    dst_short_description_b(&p->buffer, x);
                    dst_buffer_push_cstring(&p->buffer, "\x1B[0m");
                } else {
                    dst_short_description_b(&p->buffer, x);
                }
                p->depth++;
                return;
            case DST_STRUCT:
                open = "{"; close = "}";
                break;
            case DST_TABLE:
                open = "@{"; close = "}";
                break;
            case DST_TUPLE:
                open = "("; close = ")";
                break;
            case DST_ARRAY:
                open = "["; close = "]";
                break;
        }
        dst_table_put(&p->seen, x, dst_wrap_integer(p->next++));
        dst_buffer_push_cstring(&p->buffer, open);
        if (p->depth == 0) {
            dst_buffer_push_cstring(&p->buffer, "...");
        } else if (dst_hashtable_view(x, &data, &len, &cap)) {
            dst_print_hashtable_inner(p, data, len, cap);
        } else if (dst_seq_view(x, &data, &len)) {
            dst_print_seq_inner(p, data, len);
        }
        dst_buffer_push_cstring(&p->buffer, close);
    }
    /* Remove from seen as we know that printing completes, we
     * can print in multiple times and we know we are not recursing */
    dst_table_remove(&p->seen, x);
    p->depth++;
}

/* Init printer to defaults */
static void dst_printer_defaults(DstPrinter *p) {
    p->next = 0;
    p->flags = DST_PRINTFLAG_INDENT;
    p->depth = 4;
    p->indent = 0;
    p->indent_size = 2;
    p->token_line_limit = 5;
}

/* Debug print. Returns a description of an object as a string. */
const uint8_t *dst_description(DstValue x) {
    DstPrinter printer;
    const uint8_t *ret;

    dst_printer_defaults(&printer);
    dst_buffer_init(&printer.buffer, 0);
    dst_table_init(&printer.seen, 10);

    /* Only print description up to a depth of 4 */
    dst_description_helper(&printer, x);
    ret = dst_string(printer.buffer.data, printer.buffer.count);

    dst_buffer_deinit(&printer.buffer);
    dst_table_deinit(&printer.seen);

    return ret;
}

/* Convert any value to a dst string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *dst_to_string(DstValue x) {
    switch (x.type) {
        default:
            return dst_short_description(x);
        case DST_STRING:
        case DST_SYMBOL:
            return x.as.string;
    }
}

/* Helper function for formatting strings. Useful for generating error messages and the like.
 * Similiar to printf, but specialized for operating with dst. */
const uint8_t *dst_formatc(const char *format, ...) {
    va_list args;
    uint32_t len = 0;
    uint32_t i;
    const uint8_t *ret;
    DstPrinter printer;
    DstBuffer *bufp = &printer.buffer;
    
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
                        integer_to_string_b(bufp, va_arg(args, int64_t));
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
                        dst_buffer_push_u8(bufp, va_arg(args, int64_t));
                        break;
                    case 'p':
                    {
                        dst_printer_defaults(&printer);
                        dst_table_init(&printer.seen, 10);
                        /* Only print description up to a depth of 4 */
                        dst_description_helper(&printer, va_arg(args, DstValue));
                        dst_table_deinit(&printer.seen);
                        break;
                    }
                    case 'q':
                    {
                        const uint8_t *str = dst_to_string(va_arg(args, DstValue));
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
                        const uint8_t *str = dst_short_description(va_arg(args, DstValue));
                        dst_buffer_push_bytes(bufp, str, dst_string_length(str));
                        break;
                    }
                    case 'v': 
                    {
                        const uint8_t *str = dst_description(va_arg(args, DstValue));
                        dst_buffer_push_bytes(bufp, str, dst_string_length(str));
                        break;
                    }
                }
            }
        }
    }

    va_end(args);

    ret = dst_string(printer.buffer.data, printer.buffer.count);
    dst_buffer_deinit(&printer.buffer);
    return ret;
}

/* Print string to stdout */
void dst_puts(const uint8_t *str) {
    uint32_t i;
    uint32_t len = dst_string_length(str);
    for (i = 0; i < len; i++) {
        putc(str[i], stdout);
    }
}

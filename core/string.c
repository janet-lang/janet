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
#define DST_BUFSIZE 36

static int32_t real_to_string_impl(uint8_t *buf, double x) {
    int count = snprintf((char *) buf, DST_BUFSIZE, "%.17g", x);
    return (int32_t) count;
}

static void real_to_string_b(DstBuffer *buffer, double x) {
    dst_buffer_ensure(buffer, buffer->count + DST_BUFSIZE);
    buffer->count += real_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *real_to_string(double x) {
    uint8_t buf[DST_BUFSIZE];
    return dst_string(buf, real_to_string_impl(buf, x));
}

static int32_t integer_to_string_impl(uint8_t *buf, int32_t x) {
    int neg = 0;
    uint8_t *hi, *low;
    int32_t count = 0;
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

static void integer_to_string_b(DstBuffer *buffer, int32_t x) {
    dst_buffer_extra(buffer, DST_BUFSIZE);
    buffer->count += integer_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *integer_to_string(int32_t x) {
    uint8_t buf[DST_BUFSIZE];
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
    for (i = sizeof(void *); i > 0; --i) {
        uint8_t byte = pbuf.bytes[i - 1];
        if (!byte) continue;
        *c++ = HEX(byte >> 4);
        *c++ = HEX(byte & 0xF);
    }
    *c++ = '>';
    return (int32_t) (c - buf);
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

/* TODO - add more characters to escapes */
static int32_t dst_escape_string_length(const uint8_t *str) {
    int32_t len = 2;
    int32_t i;
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
    int32_t i, j;
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
    int32_t len = dst_escape_string_length(str);
    dst_buffer_extra(buffer, len);
    dst_escape_string_impl(buffer->data + buffer->count, str);
    buffer->count += len;
}

const uint8_t *dst_escape_string(const uint8_t *str) {
    int32_t len = dst_escape_string_length(str);
    uint8_t *buf = dst_string_begin(len);
    dst_escape_string_impl(buf, str);
    return dst_string_end(buf);
}

/* Returns a string pointer with the description of the string */
const uint8_t *dst_short_description(Dst x) {
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
        return dst_unwrap_string(x);
    case DST_STRING:
        return dst_escape_string(dst_unwrap_string(x));
    case DST_ABSTRACT:
        return string_description(
                dst_abstract_type(dst_unwrap_abstract(x))->name,
                dst_unwrap_abstract(x));
    default:
        return string_description(dst_type_names[dst_type(x)], dst_unwrap_pointer(x));
    }
}

void dst_short_description_b(DstBuffer *buffer, Dst x) {
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
    case DST_ABSTRACT:
        string_description_b(buffer, 
                dst_abstract_type(dst_unwrap_abstract(x))->name, 
                dst_unwrap_abstract(x));
        return;
    default:
        string_description_b(buffer, dst_type_names[dst_type(x)], dst_unwrap_pointer(x));
        break;
    }
}

/* Helper structure for stringifying nested structures */
typedef struct DstPrinter DstPrinter;
struct DstPrinter {
    DstBuffer *buffer;
    DstTable seen;
    uint32_t flags;
    uint32_t state;
    int32_t next;
    int32_t indent;
    int32_t indent_size;
    int32_t token_line_limit;
    int32_t depth;
};

#define DST_PRINTFLAG_INDENT 1
#define DST_PRINTFLAG_TABLEPAIR 2
#define DST_PRINTFLAG_COLORIZE 4
#define DST_PRINTFLAG_ALLMAN 8

/* Go to next line for printer */
static void dst_print_indent(DstPrinter *p) {
    int32_t i, len;
    len = p->indent_size * p->indent;
    for (i = 0; i < len; i++) {
        dst_buffer_push_u8(p->buffer, ' ');
    }
}

/* Check if a value is a print atom (not a printable data structure) */
static int is_print_ds(Dst v) {
    switch (dst_type(v)) {
        default: return 0;
        case DST_ARRAY:
        case DST_STRUCT:
        case DST_TUPLE:
        case DST_TABLE: return 1;
    }
}

/* VT100 Colors for types */
/* TODO - generalize into configurable headers and footers */
/*
    DST_NIL,
    DST_FALSE,
    DST_TRUE,
    DST_FIBER,
    DST_INTEGER,
    DST_REAL,
    DST_STRING,
    DST_SYMBOL,
    DST_ARRAY,
    DST_TUPLE,
    DST_TABLE,
    DST_STRUCT,
    DST_BUFFER,
    DST_FUNCTION,
    DST_CFUNCTION,
    DST_ABSTRACT
*/
static const char *dst_type_colors[16] = {
    "\x1B[35m",
    "\x1B[35m",
    "\x1B[35m",
    "",
    "\x1B[33m",
    "\x1B[33m",
    "\x1B[36m",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    ""
};

/* Forward declaration */
static void dst_description_helper(DstPrinter *p, Dst x);

/* Print a hastable view inline */
static void dst_print_hashtable_inner(DstPrinter *p, const DstKV *data, int32_t len, int32_t cap) {
    int32_t i;
    int doindent = 0;
    if (p->flags & DST_PRINTFLAG_INDENT) {
        if (len <= p->token_line_limit) {
            for (i = 0; i < cap; i++) {
                const DstKV *kv = data + i;
                if (is_print_ds(kv->key) ||
                    is_print_ds(kv->value)) {
                    doindent = 1;
                    break;
                }
            }
        } else {
            doindent = 1;
        }
    }
    if (doindent) {
        dst_buffer_push_u8(p->buffer, '\n');
        p->indent++;
        for (i = 0; i < cap; i++) {
            const DstKV *kv = data + i;
            if (!dst_checktype(kv->key, DST_NIL)) {
                dst_print_indent(p);
                dst_description_helper(p, kv->key);
                dst_buffer_push_u8(p->buffer, ' ');
                dst_description_helper(p, kv->value);
                dst_buffer_push_u8(p->buffer, '\n');
            }
        }
        p->indent--;
        dst_print_indent(p);
    } else {
        int isfirst = 1;
        for (i = 0; i < cap; i++) {
            const DstKV *kv = data + i;
            if (!dst_checktype(kv->key, DST_NIL)) {
                if (isfirst)
                    isfirst = 0;
                else
                    dst_buffer_push_u8(p->buffer, ' ');
                dst_description_helper(p, kv->key);
                dst_buffer_push_u8(p->buffer, ' ');
                dst_description_helper(p, kv->value);
            }
        }
    }
}

/* Help print a sequence */
static void dst_print_seq_inner(DstPrinter *p, const Dst *data, int32_t len) {
    int32_t i;
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
        dst_buffer_push_u8(p->buffer, '\n');
        p->indent++;
        for (i = 0; i < len; ++i) {
            dst_print_indent(p);
            dst_description_helper(p, data[i]);
            dst_buffer_push_u8(p->buffer, '\n');
        }
        p->indent--;
        dst_print_indent(p);
    } else {
        for (i = 0; i < len; ++i) {
            dst_description_helper(p, data[i]);
            if (i != len - 1)
                dst_buffer_push_u8(p->buffer, ' ');
        }
    }
}

/* Static debug print helper */
static void dst_description_helper(DstPrinter *p, Dst x) {
    const char *open;
    const char *close;
    int32_t len, cap;
    const Dst *data;
    const DstKV *kvs;
    Dst check;
    p->depth--;
    switch (dst_type(x)) {
        default:
            if (p->flags & DST_PRINTFLAG_COLORIZE) {
                dst_buffer_push_cstring(p->buffer, dst_type_colors[dst_type(x)]);
                dst_short_description_b(p->buffer, x);
                dst_buffer_push_cstring(p->buffer, "\x1B[0m");
            } else {
                dst_short_description_b(p->buffer, x);
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
    if (!p->state) {
        dst_table_init(&p->seen, 10);
        p->state = 1;
    }
    check = dst_table_get(&p->seen, x);
    if (dst_checktype(check, DST_INTEGER)) {
        dst_buffer_push_cstring(p->buffer, "<cycle ");
        integer_to_string_b(p->buffer, dst_unwrap_integer(check));
        dst_buffer_push_cstring(p->buffer, ">");
        return;
    }
    dst_table_put(&p->seen, x, dst_wrap_integer(p->next++));
    dst_buffer_push_cstring(p->buffer, open);
    if (p->depth == 0) {
        dst_buffer_push_cstring(p->buffer, "...");
    } else if (dst_hashtable_view(x, &kvs, &len, &cap)) {
        dst_print_hashtable_inner(p, kvs, len, cap);
    } else if (dst_seq_view(x, &data, &len)) {
        dst_print_seq_inner(p, data, len);
    }
    dst_buffer_push_cstring(p->buffer, close);
    /* Remove from seen as we know that printing completes, we
     * can print in multiple times and we know we are not recursing */
    dst_table_remove(&p->seen, x);
    p->depth++;
}

/* Init printer to defaults */
static void dst_printer_defaults(DstPrinter *p) {
    p->next = 0;
    p->flags = DST_PRINTFLAG_INDENT;
    p->depth = 10;
    p->indent = 0;
    p->indent_size = 2;
    p->token_line_limit = 5;
}

/* Debug print. Returns a description of an object as a string. */
const uint8_t *dst_description(Dst x) {
    DstPrinter printer;
    const uint8_t *ret;

    DstBuffer buffer;
    dst_printer_defaults(&printer);
    printer.state = 0;
    dst_buffer_init(&buffer, 0);
    printer.buffer = &buffer;

    /* Only print description up to a depth of 4 */
    dst_description_helper(&printer, x);
    ret = dst_string(buffer.data, buffer.count);

    dst_buffer_deinit(&buffer);
    if (printer.state)
        dst_table_deinit(&printer.seen);
    return ret;
}

/* Convert any value to a dst string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *dst_to_string(Dst x) {
    switch (dst_type(x)) {
        default:
            return dst_short_description(x);
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
    DstPrinter printer;
    DstBuffer buffer;
    DstBuffer *bufp = &buffer;
    printer.state = 0;
    
    /* Calculate length */
    while (format[len]) len++;

    /* Initialize buffer */
    dst_buffer_init(bufp, len);
    printer.buffer = bufp;

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
                    case 'v':
                    {
                        dst_printer_defaults(&printer);
                        dst_description_helper(&printer, va_arg(args, Dst));
                        break;
                    }
                    case 'C':
                    {
                        dst_printer_defaults(&printer);
                        printer.flags |= DST_PRINTFLAG_COLORIZE;
                        dst_description_helper(&printer, va_arg(args, Dst));
                        break;
                    }
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
                        dst_short_description_b(bufp, va_arg(args, Dst));
                        break;
                    }
                }
            }
        }
    }

    va_end(args);

    ret = dst_string(buffer.data, buffer.count);
    dst_buffer_deinit(&buffer);
    if (printer.state)
        dst_table_deinit(&printer.seen);
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

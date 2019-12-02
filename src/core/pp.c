/*
* Copyright (c) 2019 Calvin Rose
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

#include <string.h>
#include <ctype.h>

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#include "state.h"
#endif

/* Implements a pretty printer for Janet. The pretty printer
 * is simple and not that flexible, but fast. */

/* Temporary buffer size */
#define BUFSIZE 64

static void number_to_string_b(JanetBuffer *buffer, double x) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    /* Use int32_t range for valid integers because that is the
     * range most integer-expecting functions in the C api use. */
    const char *fmt = (x == floor(x) &&
                       x <= ((double) INT32_MAX) &&
                       x >= ((double) INT32_MIN)) ? "%.0f" : "%g";
    int count = snprintf((char *) buffer->data + buffer->count, BUFSIZE, fmt, x);
    buffer->count += count;
}

/* expects non positive x */
static int count_dig10(int32_t x) {
    int result = 1;
    for (;;) {
        if (x > -10) return result;
        if (x > -100) return result + 1;
        if (x > -1000) return result + 2;
        if (x > -10000) return result + 3;
        x /= 10000;
        result += 4;
    }
}

static void integer_to_string_b(JanetBuffer *buffer, int32_t x) {
    janet_buffer_extra(buffer, BUFSIZE);
    uint8_t *buf = buffer->data + buffer->count;
    int32_t neg = 0;
    int32_t len = 0;
    if (x == 0) {
        buf[0] = '0';
        buffer->count++;
        return;
    }
    if (x > 0) {
        x = -x;
    } else {
        neg = 1;
        *buf++ = '-';
    }
    len = count_dig10(x);
    buf += len;
    while (x) {
        uint8_t digit = (uint8_t) - (x % 10);
        *(--buf) = '0' + digit;
        x /= 10;
    }
    buffer->count += len + neg;
}

#define HEX(i) (((uint8_t *) janet_base64)[(i)])

/* Returns a string description for a pointer. Truncates
 * title to 32 characters */
static void string_description_b(JanetBuffer *buffer, const char *title, void *pointer) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    uint8_t *c = buffer->data + buffer->count;
    int32_t i;
    union {
        uint8_t bytes[sizeof(void *)];
        void *p;
    } pbuf;

    pbuf.p = pointer;
    *c++ = '<';
    /* Maximum of 32 bytes for abstract type name */
    for (i = 0; title[i] && i < 32; ++i)
        *c++ = ((uint8_t *)title) [i];
    *c++ = ' ';
    *c++ = '0';
    *c++ = 'x';
#if defined(JANET_64)
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
    buffer->count = (int32_t)(c - buffer->data);
#undef POINTSIZE
}

#undef HEX
#undef BUFSIZE

static void janet_escape_string_impl(JanetBuffer *buffer, const uint8_t *str, int32_t len) {
    janet_buffer_push_u8(buffer, '"');
    for (int32_t i = 0; i < len; ++i) {
        uint8_t c = str[i];
        switch (c) {
            case '"':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\\"", 2);
                break;
            case '\n':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\n", 2);
                break;
            case '\r':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\r", 2);
                break;
            case '\0':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\0", 2);
                break;
            case '\f':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\f", 2);
                break;
            case '\v':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\v", 2);
                break;
            case 27:
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\e", 2);
                break;
            case '\\':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\\\", 2);
                break;
            default:
                if (c < 32 || c > 127) {
                    uint8_t buf[4];
                    buf[0] = '\\';
                    buf[1] = 'x';
                    buf[2] = janet_base64[(c >> 4) & 0xF];
                    buf[3] = janet_base64[c & 0xF];
                    janet_buffer_push_bytes(buffer, buf, 4);
                } else {
                    janet_buffer_push_u8(buffer, c);
                }
                break;
        }
    }
    janet_buffer_push_u8(buffer, '"');
}

static void janet_escape_string_b(JanetBuffer *buffer, const uint8_t *str) {
    janet_escape_string_impl(buffer, str, janet_string_length(str));
}

static void janet_escape_buffer_b(JanetBuffer *buffer, JanetBuffer *bx) {
    janet_buffer_push_u8(buffer, '@');
    janet_escape_string_impl(buffer, bx->data, bx->count);
}

void janet_description_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        case JANET_NIL:
            janet_buffer_push_cstring(buffer, "nil");
            return;
        case JANET_BOOLEAN:
            janet_buffer_push_cstring(buffer,
                                      janet_unwrap_boolean(x) ? "true" : "false");
            return;
        case JANET_NUMBER:
            number_to_string_b(buffer, janet_unwrap_number(x));
            return;
        case JANET_KEYWORD:
            janet_buffer_push_u8(buffer, ':');
        /* fallthrough */
        case JANET_SYMBOL:
            janet_buffer_push_bytes(buffer,
                                    janet_unwrap_string(x),
                                    janet_string_length(janet_unwrap_string(x)));
            return;
        case JANET_STRING:
            janet_escape_string_b(buffer, janet_unwrap_string(x));
            return;
        case JANET_BUFFER: {
            JanetBuffer *b = janet_unwrap_buffer(x);
            if (b == buffer) {
                /* Ensures buffer won't resize while escaping */
                janet_buffer_ensure(b, 5 * b->count + 3, 1);
            }
            janet_escape_buffer_b(buffer, b);
            return;
        }
        case JANET_ABSTRACT: {
            void *p = janet_unwrap_abstract(x);
            const JanetAbstractType *at = janet_abstract_type(p);
            if (at->tostring) {
                at->tostring(p, buffer);
            } else {
                const char *n = at->name;
                string_description_b(buffer, n, janet_unwrap_abstract(x));
            }
            return;
        }
        case JANET_CFUNCTION: {
            Janet check = janet_table_get(janet_vm_registry, x);
            if (janet_checktype(check, JANET_SYMBOL)) {
                janet_buffer_push_cstring(buffer, "<cfunction ");
                janet_buffer_push_bytes(buffer,
                                        janet_unwrap_symbol(check),
                                        janet_string_length(janet_unwrap_symbol(check)));
                janet_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
        case JANET_FUNCTION: {
            JanetFunction *fun = janet_unwrap_function(x);
            JanetFuncDef *def = fun->def;
            if (def->name) {
                const uint8_t *n = def->name;
                janet_buffer_push_cstring(buffer, "<function ");
                janet_buffer_push_bytes(buffer, n, janet_string_length(n));
                janet_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
    fallthrough:
        default:
            string_description_b(buffer, janet_type_names[janet_type(x)], janet_unwrap_pointer(x));
            break;
    }
}

void janet_to_string_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        default:
            janet_description_b(buffer, x);
            break;
        case JANET_BUFFER: {
            JanetBuffer *to = janet_unwrap_buffer(x);
            /* Prevent resizing buffer while appending */
            if (buffer == to) janet_buffer_extra(buffer, to->count);
            janet_buffer_push_bytes(buffer, to->data, to->count);
            break;
        }
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            janet_buffer_push_bytes(buffer,
                                    janet_unwrap_string(x),
                                    janet_string_length(janet_unwrap_string(x)));
            break;
    }
}

const uint8_t *janet_description(Janet x) {
    JanetBuffer b;
    janet_buffer_init(&b, 10);
    janet_description_b(&b, x);
    const uint8_t *ret = janet_string(b.data, b.count);
    janet_buffer_deinit(&b);
    return ret;
}

/* Convert any value to a janet string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *janet_to_string(Janet x) {
    switch (janet_type(x)) {
        default: {
            JanetBuffer b;
            janet_buffer_init(&b, 10);
            janet_to_string_b(&b, x);
            const uint8_t *ret = janet_string(b.data, b.count);
            janet_buffer_deinit(&b);
            return ret;
        }
        case JANET_BUFFER:
            return janet_string(janet_unwrap_buffer(x)->data, janet_unwrap_buffer(x)->count);
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            return janet_unwrap_string(x);
    }
}

/* Hold state for pretty printer. */
struct pretty {
    JanetBuffer *buffer;
    int depth;
    int indent;
    int flags;
    int32_t bufstartlen;
    JanetTable seen;
};

static void print_newline(struct pretty *S, int just_a_space) {
    int i;
    if (just_a_space || (S->flags & JANET_PRETTY_ONELINE)) {
        janet_buffer_push_u8(S->buffer, ' ');
        return;
    }
    janet_buffer_push_u8(S->buffer, '\n');
    for (i = 0; i < S->indent; i++) {
        janet_buffer_push_u8(S->buffer, ' ');
    }
}

/* Color coding for types */
static const char janet_cycle_color[] = "\x1B[36m";
static const char *janet_pretty_colors[] = {
    "\x1B[32m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[35m",
    "\x1B[34m",
    "\x1B[33m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m"
    "\x1B[35m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m"
};

#define JANET_PRETTY_DICT_ONELINE 4
#define JANET_PRETTY_IND_ONELINE 10

/* Helper for pretty printing */
static void janet_pretty_one(struct pretty *S, Janet x, int is_dict_value) {
    /* Add to seen */
    switch (janet_type(x)) {
        case JANET_NIL:
        case JANET_NUMBER:
        case JANET_SYMBOL:
        case JANET_BOOLEAN:
            break;
        default: {
            Janet seenid = janet_table_get(&S->seen, x);
            if (janet_checktype(seenid, JANET_NUMBER)) {
                if (S->flags & JANET_PRETTY_COLOR) {
                    janet_buffer_push_cstring(S->buffer, janet_cycle_color);
                }
                janet_buffer_push_cstring(S->buffer, "<cycle ");
                integer_to_string_b(S->buffer, janet_unwrap_integer(seenid));
                janet_buffer_push_u8(S->buffer, '>');
                if (S->flags & JANET_PRETTY_COLOR) {
                    janet_buffer_push_cstring(S->buffer, "\x1B[0m");
                }
                return;
            } else {
                janet_table_put(&S->seen, x, janet_wrap_integer(S->seen.count));
                break;
            }
        }
    }

    switch (janet_type(x)) {
        default: {
            const char *color = janet_pretty_colors[janet_type(x)];
            if (color && (S->flags & JANET_PRETTY_COLOR)) {
                janet_buffer_push_cstring(S->buffer, color);
            }
            if (janet_checktype(x, JANET_BUFFER) && janet_unwrap_buffer(x) == S->buffer) {
                janet_buffer_ensure(S->buffer, S->buffer->count + S->bufstartlen * 4 + 3, 1);
                janet_buffer_push_u8(S->buffer, '@');
                janet_escape_string_impl(S->buffer, S->buffer->data, S->bufstartlen);
            } else {
                janet_description_b(S->buffer, x);
            }
            if (color && (S->flags & JANET_PRETTY_COLOR)) {
                janet_buffer_push_cstring(S->buffer, "\x1B[0m");
            }
            break;
        }
        case JANET_ARRAY:
        case JANET_TUPLE: {
            int32_t i = 0, len = 0;
            const Janet *arr = NULL;
            int isarray = janet_checktype(x, JANET_ARRAY);
            janet_indexed_view(x, &arr, &len);
            int hasbrackets = !isarray && (janet_tuple_flag(arr) & JANET_TUPLE_FLAG_BRACKETCTOR);
            const char *startstr = isarray ? "@[" : hasbrackets ? "[" : "(";
            const char endchar = isarray ? ']' : hasbrackets ? ']' : ')';
            janet_buffer_push_cstring(S->buffer, startstr);
            S->depth--;
            S->indent += 2;
            if (S->depth == 0) {
                janet_buffer_push_cstring(S->buffer, "...");
            } else {
                if (!isarray && !(S->flags & JANET_PRETTY_ONELINE) && len >= JANET_PRETTY_IND_ONELINE)
                    janet_buffer_push_u8(S->buffer, ' ');
                if (is_dict_value && len >= JANET_PRETTY_IND_ONELINE) print_newline(S, 0);
                for (i = 0; i < len; i++) {
                    if (i) print_newline(S, len < JANET_PRETTY_IND_ONELINE);
                    janet_pretty_one(S, arr[i], 0);
                }
            }
            S->indent -= 2;
            S->depth++;
            janet_buffer_push_u8(S->buffer, endchar);
            break;
        }
        case JANET_STRUCT:
        case JANET_TABLE: {
            int istable = janet_checktype(x, JANET_TABLE);
            janet_buffer_push_cstring(S->buffer, istable ? "@" : "{");

            /* For object-like tables, print class name */
            if (istable) {
                JanetTable *t = janet_unwrap_table(x);
                JanetTable *proto = t->proto;
                if (NULL != proto) {
                    Janet name = janet_table_get(proto, janet_csymbolv(":name"));
                    if (janet_checktype(name, JANET_SYMBOL)) {
                        const uint8_t *sym = janet_unwrap_symbol(name);
                        janet_buffer_push_bytes(S->buffer, sym, janet_string_length(sym));
                    }
                }
                janet_buffer_push_cstring(S->buffer, "{");
            }

            S->depth--;
            S->indent += 2;
            if (S->depth == 0) {
                janet_buffer_push_cstring(S->buffer, "...");
            } else {
                int32_t i = 0, len = 0, cap = 0;
                int first_kv_pair = 1;
                const JanetKV *kvs = NULL;
                janet_dictionary_view(x, &kvs, &len, &cap);
                if (!istable && len >= JANET_PRETTY_DICT_ONELINE)
                    janet_buffer_push_u8(S->buffer, ' ');
                if (is_dict_value && len >= JANET_PRETTY_DICT_ONELINE) print_newline(S, 0);
                for (i = 0; i < cap; i++) {
                    if (!janet_checktype(kvs[i].key, JANET_NIL)) {
                        if (first_kv_pair) {
                            first_kv_pair = 0;
                        } else {
                            print_newline(S, len < JANET_PRETTY_DICT_ONELINE);
                        }
                        janet_pretty_one(S, kvs[i].key, 0);
                        janet_buffer_push_u8(S->buffer, ' ');
                        janet_pretty_one(S, kvs[i].value, 1);
                    }
                }
            }
            S->indent -= 2;
            S->depth++;
            janet_buffer_push_u8(S->buffer, '}');
            break;
        }
    }
    /* Remove from seen */
    janet_table_remove(&S->seen, x);
    return;
}

static JanetBuffer *janet_pretty_(JanetBuffer *buffer, int depth, int flags, Janet x, int32_t startlen) {
    struct pretty S;
    if (NULL == buffer) {
        buffer = janet_buffer(0);
    }
    S.buffer = buffer;
    S.depth = depth;
    S.indent = 0;
    S.flags = flags;
    S.bufstartlen = startlen;
    janet_table_init(&S.seen, 10);
    janet_pretty_one(&S, x, 0);
    janet_table_deinit(&S.seen);
    return S.buffer;
}

/* Helper for printing a janet value in a pretty form. Not meant to be used
 * for serialization or anything like that. */
JanetBuffer *janet_pretty(JanetBuffer *buffer, int depth, int flags, Janet x) {
    return janet_pretty_(buffer, depth, flags, x, buffer ? buffer->count : 0);
}

static const char *typestr(Janet x) {
    JanetType t = janet_type(x);
    return (t == JANET_ABSTRACT)
           ? janet_abstract_type(janet_unwrap_abstract(x))->name
           : janet_type_names[t];
}

static void pushtypes(JanetBuffer *buffer, int types) {
    int first = 1;
    int i = 0;
    while (types) {
        if (1 & types) {
            if (first) {
                first = 0;
            } else {
                janet_buffer_push_u8(buffer, '|');
            }
            janet_buffer_push_cstring(buffer, janet_type_names[i]);
        }
        i++;
        types >>= 1;
    }
}

void janet_formatb(JanetBuffer *bufp, const char *format, va_list args) {
    for (const char *c = format; *c; c++) {
        switch (*c) {
            default:
                janet_buffer_push_u8(bufp, *c);
                break;
            case '%': {
                if (c[1] == '\0')
                    break;
                switch (*++c) {
                    default:
                        janet_buffer_push_u8(bufp, *c);
                        break;
                    case 'f':
                        number_to_string_b(bufp, va_arg(args, double));
                        break;
                    case 'd':
                        integer_to_string_b(bufp, va_arg(args, long));
                        break;
                    case 'S': {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        janet_buffer_push_bytes(bufp, str, janet_string_length(str));
                        break;
                    }
                    case 's':
                        janet_buffer_push_cstring(bufp, va_arg(args, const char *));
                        break;
                    case 'c':
                        janet_buffer_push_u8(bufp, (uint8_t) va_arg(args, long));
                        break;
                    case 'q': {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        janet_escape_string_b(bufp, str);
                        break;
                    }
                    case 't': {
                        janet_buffer_push_cstring(bufp, typestr(va_arg(args, Janet)));
                        break;
                    }
                    case 'T': {
                        int types = va_arg(args, long);
                        pushtypes(bufp, types);
                        break;
                    }
                    case 'V': {
                        janet_to_string_b(bufp, va_arg(args, Janet));
                        break;
                    }
                    case 'v': {
                        janet_description_b(bufp, va_arg(args, Janet));
                        break;
                    }
                    case 'p': {
                        janet_pretty(bufp, 4, 0, va_arg(args, Janet));
                        break;
                    }
                    case 'P': {
                        janet_pretty(bufp, 4, JANET_PRETTY_COLOR, va_arg(args, Janet));
                        break;
                    }
                }
            }
        }
    }
}

/* Helper function for formatting strings. Useful for generating error messages and the like.
 * Similar to printf, but specialized for operating with janet. */
const uint8_t *janet_formatc(const char *format, ...) {
    va_list args;
    const uint8_t *ret;
    JanetBuffer buffer;
    int32_t len = 0;

    /* Calculate length, init buffer and args */
    while (format[len]) len++;
    janet_buffer_init(&buffer, len);
    va_start(args, format);

    /* Run format */
    janet_formatb(&buffer, format, args);

    /* Iterate length */
    va_end(args);

    ret = janet_string(buffer.data, buffer.count);
    janet_buffer_deinit(&buffer);
    return ret;
}

/*
 * code adapted from lua/lstrlib.c http://lua.org
 */

#define MAX_ITEM  256
#define FMT_FLAGS "-+ #0"
#define MAX_FORMAT 32

static const char *scanformat(
    const char *strfrmt,
    char *form,
    char width[3],
    char precision[3]) {
    const char *p = strfrmt;
    memset(width, '\0', 3);
    memset(precision, '\0', 3);
    while (*p != '\0' && strchr(FMT_FLAGS, *p) != NULL)
        p++; /* skip flags */
    if ((size_t)(p - strfrmt) >= sizeof(FMT_FLAGS) / sizeof(char))
        janet_panic("invalid format (repeated flags)");
    if (isdigit((int)(*p)))
        width[0] = *p++; /* skip width */
    if (isdigit((int)(*p)))
        width[1] = *p++; /* (2 digits at most) */
    if (*p == '.') {
        p++;
        if (isdigit((int)(*p)))
            precision[0] = *p++; /* skip precision */
        if (isdigit((int)(*p)))
            precision[1] = *p++; /* (2 digits at most) */
    }
    if (isdigit((int)(*p)))
        janet_panic("invalid format (width or precision too long)");
    *(form++) = '%';
    memcpy(form, strfrmt, ((p - strfrmt) + 1) * sizeof(char));
    form += (p - strfrmt) + 1;
    *form = '\0';
    return p;
}

/* Shared implementation between string/format and
 * buffer/format */
void janet_buffer_format(
    JanetBuffer *b,
    const char *strfrmt,
    int32_t argstart,
    int32_t argc,
    Janet *argv) {
    size_t sfl = strlen(strfrmt);
    const char *strfrmt_end = strfrmt + sfl;
    int32_t arg = argstart;
    int32_t startlen = b->count;
    while (strfrmt < strfrmt_end) {
        if (*strfrmt != '%')
            janet_buffer_push_u8(b, (uint8_t) * strfrmt++);
        else if (*++strfrmt == '%')
            janet_buffer_push_u8(b, (uint8_t) * strfrmt++); /* %% */
        else { /* format item */
            char form[MAX_FORMAT], item[MAX_ITEM];
            char width[3], precision[3];
            int nb = 0; /* number of bytes in added item */
            if (++arg >= argc)
                janet_panic("not enough values for format");
            strfrmt = scanformat(strfrmt, form, width, precision);
            switch (*strfrmt++) {
                case 'c': {
                    nb = snprintf(item, MAX_ITEM, form, (int)
                                  janet_getinteger(argv, arg));
                    break;
                }
                case 'd':
                case 'i':
                case 'o':
                case 'u':
                case 'x':
                case 'X': {
                    int32_t n = janet_getinteger(argv, arg);
                    nb = snprintf(item, MAX_ITEM, form, n);
                    break;
                }
                case 'a':
                case 'A':
                case 'e':
                case 'E':
                case 'f':
                case 'g':
                case 'G': {
                    double d = janet_getnumber(argv, arg);
                    nb = snprintf(item, MAX_ITEM, form, d);
                    break;
                }
                case 's': {
                    const uint8_t *s = janet_getstring(argv, arg);
                    int32_t l = janet_string_length(s);
                    if (form[2] == '\0')
                        janet_buffer_push_bytes(b, s, l);
                    else {
                        if (l != (int32_t) strlen((const char *) s))
                            janet_panic("string contains zeros");
                        if (!strchr(form, '.') && l >= 100) {
                            janet_panic("no precision and string is too long to be formatted");
                        } else {
                            nb = snprintf(item, MAX_ITEM, form, s);
                        }
                    }
                    break;
                }
                case 'V': {
                    janet_to_string_b(b, argv[arg]);
                    break;
                }
                case 'v': {
                    janet_description_b(b, argv[arg]);
                    break;
                }
                case 'Q':
                case 'q':
                case 'P':
                case 'p': { /* janet pretty , precision = depth */
                    int depth = atoi(precision);
                    if (depth < 1)
                        depth = 4;
                    char c = strfrmt[-1];
                    int has_color = (c == 'P') || (c == 'Q');
                    int has_oneline = (c == 'Q') || (c == 'q');
                    int flags = 0;
                    flags |= has_color ? JANET_PRETTY_COLOR : 0;
                    flags |= has_oneline ? JANET_PRETTY_ONELINE : 0;
                    janet_pretty_(b, depth, flags, argv[arg], startlen);
                    break;
                }
                default: {
                    /* also treat cases 'nLlh' */
                    janet_panicf("invalid conversion '%s' to 'format'",
                                 form);
                }
            }
            if (nb >= MAX_ITEM)
                janet_panicf("format buffer overflow", form);
            if (nb > 0)
                janet_buffer_push_bytes(b, (uint8_t *) item, nb);
        }
    }
}

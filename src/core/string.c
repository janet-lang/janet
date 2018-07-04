/*
* Copyright (c) 2018 Calvin Rose
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
#include "state.h"

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
#define BUFSIZE 64

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
    /* Maximum of 32 bytes for abstract type name */
    for (i = 0; title[i] && i < 32; ++i)
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
                if (str[i] < 32 || str[i] > 127)
                    len += 4;
                else
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
                if (c < 32 || c > 127) {
                    buf[j++] = '\\';
                    buf[j++] = 'h';
                    buf[j++] = dst_base64[(c >> 4) & 0xF];
                    buf[j++] = dst_base64[c & 0xF];
                } else {
                    buf[j++] = c;
                }
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
    case DST_CFUNCTION:
        {
            Dst check = dst_table_get(dst_vm_registry, x);
            if (dst_checktype(x, DST_SYMBOL)) {
                dst_buffer_push_cstring(buffer, "<cfunction ");
                dst_buffer_push_bytes(buffer,
                        dst_unwrap_symbol(check),
                        dst_string_length(dst_unwrap_symbol(check)));
                dst_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
    fallthrough:
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
    case DST_CFUNCTION:
        {
            Dst check = dst_table_get(dst_vm_registry, x);
            if (dst_checktype(check, DST_SYMBOL)) {
                return dst_formatc("<cfunction %V>", check);
            }
            goto fallthrough;
        }
    fallthrough:
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
                        dst_buffer_push_cstring(bufp, dst_type_names[va_arg(args, DstType)] + 1);
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

/* Knuth Morris Pratt Algorithm */

struct kmp_state {
    int32_t i;
    int32_t j;
    int32_t textlen;
    int32_t patlen;
    int32_t *lookup;
    const uint8_t *text;
    const uint8_t *pat;
};

static void kmp_init(
        struct kmp_state *s,
        const uint8_t *text, int32_t textlen,
        const uint8_t *pat, int32_t patlen) {
    int32_t *lookup = calloc(patlen, sizeof(int32_t));
    if (!lookup) {
        DST_OUT_OF_MEMORY;
    }
    s->lookup = lookup;
    s->i = 0;
    s->j = 0;
    s->text = text;
    s->pat = pat;
    s->textlen = textlen;
    s->patlen = patlen;
    /* Init state machine */
    {
        int32_t i, j;
        for (i = 1, j = 0; i < patlen; i++) {
            while (j && pat[j] != pat[i]) j = lookup[j - 1];
            if (pat[j] == pat[i]) j++;
            lookup[i] = j;
        }
    }
}

static void kmp_deinit(struct kmp_state *state) {
    free(state->lookup);
}

static int32_t kmp_next(struct kmp_state *state) {
    int32_t i = state->i;
    int32_t j = state->j;
    int32_t textlen = state->textlen;
    int32_t patlen = state->patlen;
    const uint8_t *text = state->text;
    const uint8_t *pat = state->pat;
    int32_t *lookup = state->lookup;
    while (i < textlen) {
        if (text[i] == pat[j]) {
            if (j == patlen - 1) {
                state->i = i + 1;
                state->j = lookup[j];
                return i - j;
            } else {
                i++;
                j++;
            }
        } else {
            if (j > 0) {
                j = lookup[j - 1];
            } else {
                i++;
            }
        }
    }
    return -1;
}

/* CFuns */

static int cfun_slice(DstArgs args) {
    const uint8_t *data;
    int32_t len, start, end;
    const uint8_t *ret;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 3);
    DST_ARG_BYTES(data, len, args, 0);
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (dst_checktype(args.v[1], DST_INTEGER)) {
        start = dst_unwrap_integer(args.v[1]);
    } else {
        DST_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (dst_checktype(args.v[2], DST_INTEGER)) {
        end = dst_unwrap_integer(args.v[2]);
    } else {
        DST_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = dst_string(data + start, end - start);
    } else {
        ret = dst_cstring("");
    }
    DST_RETURN_STRING(args, ret);
}

static int cfun_repeat(DstArgs args) {
    const uint8_t *data;
    uint8_t *newbuf, *p, *end;
    int32_t len, rep;
    int64_t mulres;
    DST_FIXARITY(args, 2);
    DST_ARG_BYTES(data, len, args, 0);
    DST_ARG_INTEGER(rep, args, 1);
    if (rep < 0) {
        DST_THROW(args, "expected non-negative number of repetitions");
    } else if (rep == 0) {
        DST_RETURN_CSTRING(args, "");
    }
    mulres = (int64_t) rep * len;
    if (mulres > INT32_MAX) {
        DST_THROW(args, "result string is too long");
    }
    newbuf = dst_string_begin((int32_t) mulres);
    end = newbuf + mulres;
    for (p = newbuf; p < end; p += len) {
        memcpy(p, data, len);
    }
    DST_RETURN_STRING(args, dst_string_end(newbuf));
}

static int cfun_bytes(DstArgs args) {
    const uint8_t *str;
    int32_t strlen, i;
    Dst *tup;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(str, strlen, args, 0);
    tup = dst_tuple_begin(strlen);
    for (i = 0; i < strlen; i++) {
        tup[i] = dst_wrap_integer((int32_t) str[i]);
    }
    DST_RETURN_TUPLE(args, dst_tuple_end(tup));
}

static int cfun_frombytes(DstArgs args) {
    int32_t i;
    uint8_t *buf;
    for (i = 0; i < args.n; i++) {
        DST_CHECK(args, i, DST_INTEGER);
    }
    buf = dst_string_begin(args.n);
    for (i = 0; i < args.n; i++) {
        int32_t c;
        DST_ARG_INTEGER(c, args, i);
        buf[i] = c & 0xFF;
    }
    DST_RETURN_STRING(args, dst_string_end(buf));
}

static int cfun_asciilower(DstArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(str, len, args, 0);
    buf = dst_string_begin(len);
    for (i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c >= 65 && c <= 90) {
            buf[i] = c + 32;
        } else {
            buf[i] = c;
        }
    }
    DST_RETURN_STRING(args, dst_string_end(buf));
}

static int cfun_asciiupper(DstArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(str, len, args, 0);
    buf = dst_string_begin(len);
    for (i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c >= 97 && c <= 122) {
            buf[i] = c - 32;
        } else {
            buf[i] = c;
        }
    }
    DST_RETURN_STRING(args, dst_string_end(buf));
}

static int cfun_reverse(DstArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i, j;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(str, len, args, 0);
    buf = dst_string_begin(len);
    for (i = 0, j = len - 1; i < len; i++, j--) {
        buf[i] = str[j];
    }
    DST_RETURN_STRING(args, dst_string_end(buf));
}

static int findsetup(DstArgs args, struct kmp_state *s, int32_t extra) {
    const uint8_t *text, *pat;
    int32_t textlen, patlen, start;
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3 + extra);
    DST_ARG_BYTES(pat, patlen, args, 0);
    DST_ARG_BYTES(text, textlen, args, 1);
    if (args.n >= 3) {
        DST_ARG_INTEGER(start, args, 2);
        if (start < 0) {
            DST_THROW(args, "expected non-negative start index");
        }
    } else {
        start = 0;
    }
    kmp_init(s, text, textlen, pat, patlen);
    s->i = start;
    return DST_SIGNAL_OK;
}

static int cfun_find(DstArgs args) {
    int32_t result;
    struct kmp_state state;
    int status = findsetup(args, &state, 0);
    if (status) return status;
    result = kmp_next(&state);
    kmp_deinit(&state);
    DST_RETURN(args, result < 0
            ? dst_wrap_nil()
            : dst_wrap_integer(result));
}

static int cfun_findall(DstArgs args) {
    int32_t result;
    DstArray *array;
    struct kmp_state state;
    int status = findsetup(args, &state, 0);
    if (status) return status;
    array = dst_array(0);
    while ((result = kmp_next(&state)) >= 0) {
        dst_array_push(array, dst_wrap_integer(result));
    }
    kmp_deinit(&state);
    DST_RETURN_ARRAY(args, array);
}

struct replace_state {
    struct kmp_state kmp;
    const uint8_t *subst;
    int32_t substlen;
};

static int replacesetup(DstArgs args, struct replace_state *s) {
    const uint8_t *text, *pat, *subst;
    int32_t textlen, patlen, substlen, start;
    DST_MINARITY(args, 3);
    DST_MAXARITY(args, 4);
    DST_ARG_BYTES(pat, patlen, args, 0);
    DST_ARG_BYTES(subst, substlen, args, 1);
    DST_ARG_BYTES(text, textlen, args, 2);
    if (args.n == 4) {
        DST_ARG_INTEGER(start, args, 3);
        if (start < 0) {
            DST_THROW(args, "expected non-negative start index");
        }
    } else {
        start = 0;
    }
    kmp_init(&s->kmp, text, textlen, pat, patlen);
    s->kmp.i = start;
    s->subst = subst;
    s->substlen = substlen;
    return DST_SIGNAL_OK;
}

static int cfun_replace(DstArgs args) {
    int32_t result;
    struct replace_state s;
    uint8_t *buf;
    int status = replacesetup(args, &s);
    if (status) return status;
    result = kmp_next(&s.kmp);
    if (result < 0) {
        kmp_deinit(&s.kmp);
        DST_RETURN_STRING(args, dst_string(s.kmp.text, s.kmp.textlen));
    }
    buf = dst_string_begin(s.kmp.textlen - s.kmp.patlen + s.substlen);
    memcpy(buf, s.kmp.text, result);
    memcpy(buf + result, s.subst, s.substlen);
    memcpy(buf + result + s.substlen,
            s.kmp.text + result + s.kmp.patlen,
            s.kmp.textlen - result - s.kmp.patlen);
    kmp_deinit(&s.kmp);
    DST_RETURN_STRING(args, dst_string_end(buf));
}

static int cfun_replaceall(DstArgs args) {
    int32_t result;
    struct replace_state s;
    DstBuffer b;
    const uint8_t *ret;
    int32_t lastindex = 0;
    int status = replacesetup(args, &s);
    if (status) return status;
    dst_buffer_init(&b, s.kmp.textlen);
    while ((result = kmp_next(&s.kmp)) >= 0) {
        dst_buffer_push_bytes(&b, s.kmp.text + lastindex, result - lastindex);
        dst_buffer_push_bytes(&b, s.subst, s.substlen);
        lastindex = result + s.kmp.patlen;
    }
    dst_buffer_push_bytes(&b, s.kmp.text + lastindex, s.kmp.textlen - lastindex);
    ret = dst_string(b.data, b.count);
    dst_buffer_deinit(&b);
    kmp_deinit(&s.kmp);
    DST_RETURN_STRING(args, ret);
}

static int cfun_split(DstArgs args) {
    int32_t result;
    DstArray *array;
    struct kmp_state state;
    int32_t limit = -1, lastindex = 0;
    if (args.n == 4) {
        DST_ARG_INTEGER(limit, args, 3);
    }
    int status = findsetup(args, &state, 1);
    if (status) return status;
    array = dst_array(0);
    while ((result = kmp_next(&state)) >= 0 && limit--) {
        const uint8_t *slice = dst_string(state.text + lastindex, result - lastindex);
        dst_array_push(array, dst_wrap_string(slice));
        lastindex = result + state.patlen;
    }
    {
        const uint8_t *slice = dst_string(state.text + lastindex, state.textlen - lastindex);
        dst_array_push(array, dst_wrap_string(slice));
    }
    kmp_deinit(&state);
    DST_RETURN_ARRAY(args, array);
}

static int cfun_checkset(DstArgs args) {
    const uint8_t *set, *str;
    int32_t setlen, strlen, i;
    uint32_t bitset[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    DST_MINARITY(args, 2);
    DST_MAXARITY(args, 3);
    DST_ARG_BYTES(set, setlen, args, 0);
    DST_ARG_BYTES(str, strlen, args, 1);
    /* Populate set */
    for (i = 0; i < setlen; i++) {
        int index = set[i] >> 5;
        uint32_t mask = 1 << (set[i] & 7);
        bitset[index] |= mask;
    }
    if (args.n == 3) {
        int invert;
        DST_ARG_BOOLEAN(invert, args, 2);
        if (invert) {
            for (i = 0; i < 8; i++)
                bitset[i] = ~bitset[i];
        }
    }
    /* Check set */
    for (i = 0; i < strlen; i++) {
        int index = str[i] >> 5;
        uint32_t mask = 1 << (str[i] & 7);
        if (!(bitset[index] & mask)) {
            DST_RETURN_FALSE(args);
        }
    }
    DST_RETURN_TRUE(args);
}

static int cfun_join(DstArgs args) {
    const Dst *parts;
    const uint8_t *joiner;
    uint8_t *buf, *out;
    int32_t joinerlen, partslen, finallen, i;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_INDEXED(parts, partslen, args, 0);
    if (args.n == 2) {
        DST_ARG_BYTES(joiner, joinerlen, args, 1);
    } else {
        joiner = NULL;
        joinerlen = 0;
    }
    /* Check args */
    finallen = 0;
    for (i = 0; i < partslen; i++) {
        const uint8_t *chunk;
        int32_t chunklen = 0;
        if (!dst_chararray_view(parts[i], &chunk, &chunklen)) {
            DST_THROW(args, "expected string|symbol|buffer");
        }
        if (i) finallen += joinerlen;
        finallen += chunklen;
    }
    out = buf = dst_string_begin(finallen);
    for (i = 0; i < partslen; i++) {
        const uint8_t *chunk = NULL;
        int32_t chunklen = 0;
        if (i) {
            memcpy(out, joiner, joinerlen);
            out += joinerlen;
        }
        dst_chararray_view(parts[i], &chunk, &chunklen);
        memcpy(out, chunk, chunklen);
        out += chunklen;
    }
    DST_RETURN_STRING(args, dst_string_end(buf));

}

static const DstReg cfuns[] = {
    {"string.slice", cfun_slice},
    {"string.repeat", cfun_repeat},
    {"string.bytes", cfun_bytes},
    {"string.from-bytes", cfun_frombytes},
    {"string.ascii-lower", cfun_asciilower},
    {"string.ascii-upper", cfun_asciiupper},
    {"string.reverse", cfun_reverse},
    {"string.find", cfun_find},
    {"string.find-all", cfun_findall},
    {"string.replace", cfun_replace},
    {"string.replace-all", cfun_replaceall},
    {"string.split", cfun_split},
    {"string.check-set", cfun_checkset},
    {"string.join", cfun_join},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_string(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

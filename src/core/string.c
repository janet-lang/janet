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

#include <janet/janet.h>
#include "gc.h"
#include "util.h"
#include "state.h"

/* Begin building a string */
uint8_t *janet_string_begin(int32_t length) {
    char *data = janet_gcalloc(JANET_MEMORY_STRING, 2 * sizeof(int32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    janet_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *janet_string_end(uint8_t *str) {
    janet_string_hash(str) = janet_string_calchash(str, janet_string_length(str));
    return str;
}

/* Load a buffer as a string */
const uint8_t *janet_string(const uint8_t *buf, int32_t len) {
    int32_t hash = janet_string_calchash(buf, len);
    char *data = janet_gcalloc(JANET_MEMORY_STRING, 2 * sizeof(int32_t) + len + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    memcpy(str, buf, len);
    str[len] = 0;
    janet_string_length(str) = len;
    janet_string_hash(str) = hash;
    return str;
}

/* Compare two strings */
int janet_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    int32_t xlen = janet_string_length(lhs);
    int32_t ylen = janet_string_length(rhs);
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

/* Compare a janet string with a piece of memory */
int janet_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash) {
    int32_t index;
    int32_t lhash = janet_string_hash(lhs);
    int32_t llen = janet_string_length(lhs);
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
int janet_string_equal(const uint8_t *lhs, const uint8_t *rhs) {
    return janet_string_equalconst(lhs, rhs,
            janet_string_length(rhs), janet_string_hash(rhs));
}

/* Load a c string */
const uint8_t *janet_cstring(const char *str) {
    int32_t len = 0;
    while (str[len]) ++len;
    return janet_string((const uint8_t *)str, len);
}

/* Temporary buffer size */
#define BUFSIZE 64

static int32_t real_to_string_impl(uint8_t *buf, double x) {
    int count = snprintf((char *) buf, BUFSIZE, "%.17gr", x);
    return (int32_t) count;
}

static void real_to_string_b(JanetBuffer *buffer, double x) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    buffer->count += real_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *real_to_string(double x) {
    uint8_t buf[BUFSIZE];
    return janet_string(buf, real_to_string_impl(buf, x));
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

static int32_t integer_to_string_impl(uint8_t *buf, int32_t x) {
    int32_t neg = 0;
    int32_t len = 0;
    if (x == 0) {
        buf[0] = '0';
        return 1;
    } else if (x > 0) {
        x = -x;
    } else {
        neg = 1;
        *buf++ = '-';
    }
    len = count_dig10(x);
    buf += len;
    while (x) {
        uint8_t digit = (uint8_t) -(x % 10);
        *(--buf) = '0' + digit;
        x /= 10;
    }
    return len + neg;
}

static void integer_to_string_b(JanetBuffer *buffer, int32_t x) {
    janet_buffer_extra(buffer, BUFSIZE);
    buffer->count += integer_to_string_impl(buffer->data + buffer->count, x);
}

static const uint8_t *integer_to_string(int32_t x) {
    uint8_t buf[BUFSIZE];
    return janet_string(buf, integer_to_string_impl(buf, x));
}

#define HEX(i) (((uint8_t *) janet_base64)[(i)])

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
    return (int32_t) (c - buf);
#undef POINTSIZE
}

static void string_description_b(JanetBuffer *buffer, const char *title, void *pointer) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    buffer->count += string_description_impl(buffer->data + buffer->count, title, pointer);
}

/* Describes a pointer with a title (string_description("bork",  myp) returns
 * a string "<bork 0x12345678>") */
static const uint8_t *string_description(const char *title, void *pointer) {
    uint8_t buf[BUFSIZE];
    return janet_string(buf, string_description_impl(buf, title, pointer));
}

#undef HEX
#undef BUFSIZE

/* TODO - add more characters to escape.
 *
 * When more escapes are added, they must correspond
 * to janet_escape_string_impl exactly or a buffer overrun could occur. */
static int32_t janet_escape_string_length(const uint8_t *str, int32_t slen) {
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

static void janet_escape_string_impl(uint8_t *buf, const uint8_t *str, int32_t len) {
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
                    buf[j++] = 'x';
                    buf[j++] = janet_base64[(c >> 4) & 0xF];
                    buf[j++] = janet_base64[c & 0xF];
                } else {
                    buf[j++] = c;
                }
                break;
        }
    }
    buf[j++] = '"';
}

void janet_escape_string_b(JanetBuffer *buffer, const uint8_t *str) {
    int32_t len = janet_string_length(str);
    int32_t elen = janet_escape_string_length(str, len);
    janet_buffer_extra(buffer, elen);
    janet_escape_string_impl(buffer->data + buffer->count, str, len);
    buffer->count += elen;
}

const uint8_t *janet_escape_string(const uint8_t *str) {
    int32_t len = janet_string_length(str);
    int32_t elen = janet_escape_string_length(str, len);
    uint8_t *buf = janet_string_begin(elen);
    janet_escape_string_impl(buf, str, len);
    return janet_string_end(buf);
}

static void janet_escape_buffer_b(JanetBuffer *buffer, JanetBuffer *bx) {
    int32_t elen = janet_escape_string_length(bx->data, bx->count);
    janet_buffer_push_u8(buffer, '@');
    janet_buffer_extra(buffer, elen);
    janet_escape_string_impl(
            buffer->data + buffer->count,
            bx->data,
            bx->count);
    buffer->count += elen;
}

void janet_description_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
    case JANET_NIL:
        janet_buffer_push_cstring(buffer, "nil");
        return;
    case JANET_TRUE:
        janet_buffer_push_cstring(buffer, "true");
        return;
    case JANET_FALSE:
        janet_buffer_push_cstring(buffer, "false");
        return;
    case JANET_REAL:
        real_to_string_b(buffer, janet_unwrap_real(x));
        return;
    case JANET_INTEGER:
        integer_to_string_b(buffer, janet_unwrap_integer(x));
        return;
    case JANET_SYMBOL:
        janet_buffer_push_bytes(buffer,
                janet_unwrap_string(x),
                janet_string_length(janet_unwrap_string(x)));
        return;
    case JANET_STRING:
        janet_escape_string_b(buffer, janet_unwrap_string(x));
        return;
    case JANET_BUFFER:
        janet_escape_buffer_b(buffer, janet_unwrap_buffer(x));
        return;
    case JANET_ABSTRACT:
        {
            const char *n = janet_abstract_type(janet_unwrap_abstract(x))->name;
            string_description_b(buffer,
                n[0] == ':' ? n + 1 : n,
                janet_unwrap_abstract(x));
			return;
        }
    case JANET_CFUNCTION:
        {
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
    case JANET_FUNCTION:
        {
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
        string_description_b(buffer, janet_type_names[janet_type(x)] + 1, janet_unwrap_pointer(x));
        break;
    }
}

void janet_to_string_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        default:
            janet_description_b(buffer, x);
            break;
        case JANET_BUFFER:
            janet_buffer_push_bytes(buffer,
                    janet_unwrap_buffer(x)->data,
                    janet_unwrap_buffer(x)->count);
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
            janet_buffer_push_bytes(buffer,
                    janet_unwrap_string(x),
                    janet_string_length(janet_unwrap_string(x)));
            break;
    }
}

const uint8_t *janet_description(Janet x) {
    switch (janet_type(x)) {
    case JANET_NIL:
        return janet_cstring("nil");
    case JANET_TRUE:
        return janet_cstring("true");
    case JANET_FALSE:
        return janet_cstring("false");
    case JANET_REAL:
        return real_to_string(janet_unwrap_real(x));
    case JANET_INTEGER:
        return integer_to_string(janet_unwrap_integer(x));
    case JANET_SYMBOL:
        return janet_unwrap_symbol(x);
    case JANET_STRING:
        return janet_escape_string(janet_unwrap_string(x));
    case JANET_BUFFER:
        {
            JanetBuffer b;
            const uint8_t *ret;
            janet_buffer_init(&b, 3);
            janet_escape_buffer_b(&b, janet_unwrap_buffer(x));
            ret = janet_string(b.data, b.count);
            janet_buffer_deinit(&b);
            return ret;
        }
    case JANET_ABSTRACT:
        {
            const char *n = janet_abstract_type(janet_unwrap_abstract(x))->name;
            return string_description(
                    n[0] == ':' ? n + 1 : n,
                    janet_unwrap_abstract(x));
        }
    case JANET_CFUNCTION:
        {
            Janet check = janet_table_get(janet_vm_registry, x);
            if (janet_checktype(check, JANET_SYMBOL)) {
                return janet_formatc("<cfunction %V>", check);
            }
            goto fallthrough;
        }
    case JANET_FUNCTION:
        {
            JanetFunction *fun = janet_unwrap_function(x);
            JanetFuncDef *def = fun->def;
            if (def->name) {
                return janet_formatc("<function %S>", def->name);
            }
            goto fallthrough;
        }
    fallthrough:
    default:
        return string_description(janet_type_names[janet_type(x)] + 1, janet_unwrap_pointer(x));
    }
}

/* Convert any value to a janet string. Similar to description, but
 * strings, symbols, and buffers will return their content. */
const uint8_t *janet_to_string(Janet x) {
    switch (janet_type(x)) {
        default:
            return janet_description(x);
        case JANET_BUFFER:
            return janet_string(janet_unwrap_buffer(x)->data, janet_unwrap_buffer(x)->count);
        case JANET_STRING:
        case JANET_SYMBOL:
            return janet_unwrap_string(x);
    }
}

/* Helper function for formatting strings. Useful for generating error messages and the like.
 * Similiar to printf, but specialized for operating with janet. */
const uint8_t *janet_formatc(const char *format, ...) {
    va_list args;
    int32_t len = 0;
    int32_t i;
    const uint8_t *ret;
    JanetBuffer buffer;
    JanetBuffer *bufp = &buffer;

    /* Calculate length */
    while (format[len]) len++;

    /* Initialize buffer */
    janet_buffer_init(bufp, len);

    /* Start args */
    va_start(args, format);

    /* Iterate length */
    for (i = 0; i < len; i++) {
        uint8_t c = format[i];
        switch (c) {
            default:
                janet_buffer_push_u8(bufp, c);
                break;
            case '%':
            {
                if (i + 1 >= len)
                    break;
                switch (format[++i]) {
                    default:
                        janet_buffer_push_u8(bufp, format[i]);
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
                        janet_buffer_push_bytes(bufp, str, janet_string_length(str));
                        break;
                    }
                    case 's':
                        janet_buffer_push_cstring(bufp, va_arg(args, const char *));
                        break;
                    case 'c':
                        janet_buffer_push_u8(bufp, (uint8_t) va_arg(args, long));
                        break;
                    case 'q':
                    {
                        const uint8_t *str = va_arg(args, const uint8_t *);
                        janet_escape_string_b(bufp, str);
                        break;
                    }
                    case 't':
                    {
                        janet_buffer_push_cstring(bufp, janet_type_names[va_arg(args, JanetType)] + 1);
                        break;
                    }
                    case 'V':
                    {
                        janet_to_string_b(bufp, va_arg(args, Janet));
                        break;
                    }
                    case 'v':
                    {
                        janet_description_b(bufp, va_arg(args, Janet));
                        break;
                    }
                }
            }
        }
    }

    va_end(args);

    ret = janet_string(buffer.data, buffer.count);
    janet_buffer_deinit(&buffer);
    return ret;
}

/* Print string to stdout */
void janet_puts(const uint8_t *str) {
    int32_t i;
    int32_t len = janet_string_length(str);
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
        JANET_OUT_OF_MEMORY;
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

static void kmp_seti(struct kmp_state *state, int32_t i) {
    state->i = i;
    state->j = 0;
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

static int cfun_slice(JanetArgs args) {
    const uint8_t *data;
    int32_t len, start, end;
    const uint8_t *ret;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 3);
    JANET_ARG_BYTES(data, len, args, 0);
    /* Get start */
    if (args.n < 2) {
        start = 0;
    } else if (janet_checktype(args.v[1], JANET_INTEGER)) {
        start = janet_unwrap_integer(args.v[1]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    /* Get end */
    if (args.n < 3) {
        end = -1;
    } else if (janet_checktype(args.v[2], JANET_INTEGER)) {
        end = janet_unwrap_integer(args.v[2]);
    } else {
        JANET_THROW(args, "expected integer");
    }
    if (start < 0) start = len + start;
    if (end < 0) end = len + end + 1;
    if (end >= start) {
        ret = janet_string(data + start, end - start);
    } else {
        ret = janet_cstring("");
    }
    JANET_RETURN_STRING(args, ret);
}

static int cfun_repeat(JanetArgs args) {
    const uint8_t *data;
    uint8_t *newbuf, *p, *end;
    int32_t len, rep;
    int64_t mulres;
    JANET_FIXARITY(args, 2);
    JANET_ARG_BYTES(data, len, args, 0);
    JANET_ARG_INTEGER(rep, args, 1);
    if (rep < 0) {
        JANET_THROW(args, "expected non-negative number of repetitions");
    } else if (rep == 0) {
        JANET_RETURN_CSTRING(args, "");
    }
    mulres = (int64_t) rep * len;
    if (mulres > INT32_MAX) {
        JANET_THROW(args, "result string is too long");
    }
    newbuf = janet_string_begin((int32_t) mulres);
    end = newbuf + mulres;
    for (p = newbuf; p < end; p += len) {
        memcpy(p, data, len);
    }
    JANET_RETURN_STRING(args, janet_string_end(newbuf));
}

static int cfun_bytes(JanetArgs args) {
    const uint8_t *str;
    int32_t strlen, i;
    Janet *tup;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(str, strlen, args, 0);
    tup = janet_tuple_begin(strlen);
    for (i = 0; i < strlen; i++) {
        tup[i] = janet_wrap_integer((int32_t) str[i]);
    }
    JANET_RETURN_TUPLE(args, janet_tuple_end(tup));
}

static int cfun_frombytes(JanetArgs args) {
    int32_t i;
    uint8_t *buf;
    for (i = 0; i < args.n; i++) {
        JANET_CHECK(args, i, JANET_INTEGER);
    }
    buf = janet_string_begin(args.n);
    for (i = 0; i < args.n; i++) {
        int32_t c;
        JANET_ARG_INTEGER(c, args, i);
        buf[i] = c & 0xFF;
    }
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static int cfun_asciilower(JanetArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(str, len, args, 0);
    buf = janet_string_begin(len);
    for (i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c >= 65 && c <= 90) {
            buf[i] = c + 32;
        } else {
            buf[i] = c;
        }
    }
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static int cfun_asciiupper(JanetArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(str, len, args, 0);
    buf = janet_string_begin(len);
    for (i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c >= 97 && c <= 122) {
            buf[i] = c - 32;
        } else {
            buf[i] = c;
        }
    }
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static int cfun_reverse(JanetArgs args) {
    const uint8_t *str;
    uint8_t *buf;
    int32_t len, i, j;
    JANET_FIXARITY(args, 1);
    JANET_ARG_BYTES(str, len, args, 0);
    buf = janet_string_begin(len);
    for (i = 0, j = len - 1; i < len; i++, j--) {
        buf[i] = str[j];
    }
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static int findsetup(JanetArgs args, struct kmp_state *s, int32_t extra) {
    const uint8_t *text, *pat;
    int32_t textlen, patlen, start;
    JANET_MINARITY(args, 2);
    JANET_MAXARITY(args, 3 + extra);
    JANET_ARG_BYTES(pat, patlen, args, 0);
    JANET_ARG_BYTES(text, textlen, args, 1);
    if (args.n >= 3) {
        JANET_ARG_INTEGER(start, args, 2);
        if (start < 0) {
            JANET_THROW(args, "expected non-negative start index");
        }
    } else {
        start = 0;
    }
    kmp_init(s, text, textlen, pat, patlen);
    s->i = start;
    return JANET_SIGNAL_OK;
}

static int cfun_find(JanetArgs args) {
    int32_t result;
    struct kmp_state state;
    int status = findsetup(args, &state, 0);
    if (status) return status;
    result = kmp_next(&state);
    kmp_deinit(&state);
    JANET_RETURN(args, result < 0
            ? janet_wrap_nil()
            : janet_wrap_integer(result));
}

static int cfun_findall(JanetArgs args) {
    int32_t result;
    JanetArray *array;
    struct kmp_state state;
    int status = findsetup(args, &state, 0);
    if (status) return status;
    array = janet_array(0);
    while ((result = kmp_next(&state)) >= 0) {
        janet_array_push(array, janet_wrap_integer(result));
    }
    kmp_deinit(&state);
    JANET_RETURN_ARRAY(args, array);
}

struct replace_state {
    struct kmp_state kmp;
    const uint8_t *subst;
    int32_t substlen;
};

static int replacesetup(JanetArgs args, struct replace_state *s) {
    const uint8_t *text, *pat, *subst;
    int32_t textlen, patlen, substlen, start;
    JANET_MINARITY(args, 3);
    JANET_MAXARITY(args, 4);
    JANET_ARG_BYTES(pat, patlen, args, 0);
    JANET_ARG_BYTES(subst, substlen, args, 1);
    JANET_ARG_BYTES(text, textlen, args, 2);
    if (args.n == 4) {
        JANET_ARG_INTEGER(start, args, 3);
        if (start < 0) {
            JANET_THROW(args, "expected non-negative start index");
        }
    } else {
        start = 0;
    }
    kmp_init(&s->kmp, text, textlen, pat, patlen);
    s->kmp.i = start;
    s->subst = subst;
    s->substlen = substlen;
    return JANET_SIGNAL_OK;
}

static int cfun_replace(JanetArgs args) {
    int32_t result;
    struct replace_state s;
    uint8_t *buf;
    int status = replacesetup(args, &s);
    if (status) return status;
    result = kmp_next(&s.kmp);
    if (result < 0) {
        kmp_deinit(&s.kmp);
        JANET_RETURN_STRING(args, janet_string(s.kmp.text, s.kmp.textlen));
    }
    buf = janet_string_begin(s.kmp.textlen - s.kmp.patlen + s.substlen);
    memcpy(buf, s.kmp.text, result);
    memcpy(buf + result, s.subst, s.substlen);
    memcpy(buf + result + s.substlen,
            s.kmp.text + result + s.kmp.patlen,
            s.kmp.textlen - result - s.kmp.patlen);
    kmp_deinit(&s.kmp);
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static int cfun_replaceall(JanetArgs args) {
    int32_t result;
    struct replace_state s;
    JanetBuffer b;
    const uint8_t *ret;
    int32_t lastindex = 0;
    int status = replacesetup(args, &s);
    if (status) return status;
    janet_buffer_init(&b, s.kmp.textlen);
    while ((result = kmp_next(&s.kmp)) >= 0) {
        janet_buffer_push_bytes(&b, s.kmp.text + lastindex, result - lastindex);
        janet_buffer_push_bytes(&b, s.subst, s.substlen);
        lastindex = result + s.kmp.patlen;
        kmp_seti(&s.kmp, lastindex);
    }
    janet_buffer_push_bytes(&b, s.kmp.text + lastindex, s.kmp.textlen - lastindex);
    ret = janet_string(b.data, b.count);
    janet_buffer_deinit(&b);
    kmp_deinit(&s.kmp);
    JANET_RETURN_STRING(args, ret);
}

static int cfun_split(JanetArgs args) {
    int32_t result;
    JanetArray *array;
    struct kmp_state state;
    int32_t limit = -1, lastindex = 0;
    if (args.n == 4) {
        JANET_ARG_INTEGER(limit, args, 3);
    }
    int status = findsetup(args, &state, 1);
    if (status) return status;
    array = janet_array(0);
    while ((result = kmp_next(&state)) >= 0 && limit--) {
        const uint8_t *slice = janet_string(state.text + lastindex, result - lastindex);
        janet_array_push(array, janet_wrap_string(slice));
        lastindex = result + state.patlen;
    }
    {
        const uint8_t *slice = janet_string(state.text + lastindex, state.textlen - lastindex);
        janet_array_push(array, janet_wrap_string(slice));
    }
    kmp_deinit(&state);
    JANET_RETURN_ARRAY(args, array);
}

static int cfun_checkset(JanetArgs args) {
    const uint8_t *set, *str;
    int32_t setlen, strlen, i;
    uint32_t bitset[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    JANET_MINARITY(args, 2);
    JANET_MAXARITY(args, 3);
    JANET_ARG_BYTES(set, setlen, args, 0);
    JANET_ARG_BYTES(str, strlen, args, 1);
    /* Populate set */
    for (i = 0; i < setlen; i++) {
        int index = set[i] >> 5;
        uint32_t mask = 1 << (set[i] & 7);
        bitset[index] |= mask;
    }
    if (args.n == 3) {
        int invert;
        JANET_ARG_BOOLEAN(invert, args, 2);
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
            JANET_RETURN_FALSE(args);
        }
    }
    JANET_RETURN_TRUE(args);
}

static int cfun_join(JanetArgs args) {
    const Janet *parts;
    const uint8_t *joiner;
    uint8_t *buf, *out;
    int32_t joinerlen, partslen, finallen, i;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_INDEXED(parts, partslen, args, 0);
    if (args.n == 2) {
        JANET_ARG_BYTES(joiner, joinerlen, args, 1);
    } else {
        joiner = NULL;
        joinerlen = 0;
    }
    /* Check args */
    finallen = 0;
    for (i = 0; i < partslen; i++) {
        const uint8_t *chunk;
        int32_t chunklen = 0;
        if (!janet_bytes_view(parts[i], &chunk, &chunklen)) {
            JANET_THROW(args, "expected string|symbol|buffer");
        }
        if (i) finallen += joinerlen;
        finallen += chunklen;
    }
    out = buf = janet_string_begin(finallen);
    for (i = 0; i < partslen; i++) {
        const uint8_t *chunk = NULL;
        int32_t chunklen = 0;
        if (i) {
            memcpy(out, joiner, joinerlen);
            out += joinerlen;
        }
        janet_bytes_view(parts[i], &chunk, &chunklen);
        memcpy(out, chunk, chunklen);
        out += chunklen;
    }
    JANET_RETURN_STRING(args, janet_string_end(buf));
}

static struct formatter {
    const char *lead;
    const char *f1;
    const char *f2;
} formatters[] = {
    {":g", "%g", "%.*g"},
    {":G", "%G", "%.*G"},
    {":e", "%e", "%.*e"},
    {":E", "%E", "%.*E"},
    {":f", "%f", "%.*f"},
    {":F", "%F", "%.*F"}
};

static int cfun_number(JanetArgs args) {
    struct formatter fmter = formatters[0];
    char buf[100];
    double x;
    int formatNargs = 1;
    int32_t precision = 0;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 4);
    JANET_ARG_NUMBER(x, args, 0);
    if (args.n >= 2) {
        const uint8_t *flag;
        JANET_ARG_SYMBOL(flag, args, 1);
        for (int i = 0; i < 6; i++) {
            struct formatter fmttest = formatters[i];
            if (!janet_cstrcmp(flag, fmttest.lead)) {
                fmter = fmttest;
                break;
            }
        }
    }

    if (args.n >= 3) {
        JANET_ARG_INTEGER(precision, args, 2);
        formatNargs++;
    }

    if (formatNargs == 1) {
        snprintf(buf, sizeof(buf), fmter.f1, x);
    } else if (formatNargs == 2) {
        snprintf(buf, sizeof(buf), fmter.f2, precision, x);
    }

    JANET_RETURN_CSTRING(args, buf);
}

static const JanetReg cfuns[] = {
    {"string.slice", cfun_slice,
        "(string.slice bytes [,start=0 [,end=(length str)]])\n\n"
        "Returns a substring from a byte sequence. The substring is from "
        "index start inclusive to index end exclusive. All indexing "
        "is from 0. 'start' and 'end' can also be negative to indicate indexing "
        "from the end of the string."
    },
    {"string.repeat", cfun_repeat,
        "(string.repeat bytes n)\n\n"
        "Returns a string that is n copies of bytes concatenated."
    },
    {"string.bytes", cfun_bytes,
        "(string.bytes str)\n\n"
        "Returns an array of integers that are the byte values of the string."
    },
    {"string.from-bytes", cfun_frombytes,
        "(string.from-bytes byte-array)\n\n"
        "Creates a string from an array of integers with byte values. All integers "
        "will be coerced to the range of 1 byte 0-255."
    },
    {"string.ascii-lower", cfun_asciilower,
        "(string.ascii-lower str)\n\n"
        "Returns a new string where all bytes are replaced with the "
        "lowercase version of themselves in ascii. Does only a very simple "
        "case check, meaning no unicode support."
    },
    {"string.ascii-upper", cfun_asciiupper,
        "(string.ascii-upper str)\n\n"
        "Returns a new string where all bytes are replaced with the "
        "uppercase version of themselves in ascii. Does only a very simple "
        "case check, meaning no unicode support."
    },
    {"string.reverse", cfun_reverse,
        "(string.reverse str)\n\n"
        "Returns a string that is the reversed version of str."
    },
    {"string.find", cfun_find,
        "(string.find patt str)\n\n"
        "Searches for the first instance of pattern patt in string "
        "str. Returns the index of the first character in patt if found, "
        "otherwise returns nil."
    },
    {"string.find-all", cfun_findall,
        "(string.find patt str)\n\n"
        "Searches for all instances of pattern patt in string "
        "str. Returns an array of all indices of found patterns. Overlapping "
        "instances of the pattern are not counted, meaning a byte in string "
        "will only contribute to finding at most on occurrence of pattern. If no "
        "occurrences are found, will return an empty array."
    },
    {"string.replace", cfun_replace,
        "(string.replace patt subst str)\n\n"
        "Replace the first occurrence of patt with subst in the the string str. "
        "Will return the new string if patt is found, otherwise returns str."
    },
    {"string.replace-all", cfun_replaceall,
        "(string.replace-all patt subst str)\n\n"
        "Replace all instances of patt with subst in the string str. "
        "Will return the new string if patt is found, otherwise returns str."
    },
    {"string.split", cfun_split,
        "(string.split delim str)\n\n"
        "Splits a string str with delimiter delim and returns an array of "
        "substrings. The substrings will not contain the delimiter delim. If delim "
        "is not found, the returned array will have one element."
    },
    {"string.check-set", cfun_checkset,
        "(string.check-set set str)\n\n"
        "Checks if any of the bytes in the string set appear in the string str. "
        "Returns true if some bytes in set do appear in str, false if no bytes do."
    },
    {"string.join", cfun_join,
        "(string.join parts [,sep])\n\n"
        "Joins an array of strings into one string, optionally separated by "
        "a separator string sep."
    },
    {"string.number", cfun_number,
        "(string.number x [,format [,maxlen [,precision]]])\n\n"
        "Formats a number as string. The format parameter indicates how "
        "to display the number, either as floating point, scientific, or "
        "whichever representation is shorter. format can be:\n\n"
        "\t:g - (default) shortest representation with lowercase e.\n"
        "\t:G - shortest representation with uppercase E.\n"
        "\t:e - scientific with lowercase e.\n"
        "\t:E - scientific with uppercase E.\n"
        "\t:f - floating point representation.\n"
        "\t:F - same as :f\n\n"
        "The programmer can also specify the max length of the output string "
        "and the precision (number of places after decimal) in the output number. "
        "Returns a string representation of x."
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
int janet_lib_string(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}

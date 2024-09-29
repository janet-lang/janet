/*
* Copyright (c) 2024 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#include "state.h"
#include <math.h>
#endif

#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <float.h>

/* Implements a pretty printer for Janet. The pretty printer
 * is simple and not that flexible, but fast. */

/* Temporary buffer size */
#define BUFSIZE 64

/* Preprocessor hacks */
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static void number_to_string_b(JanetBuffer *buffer, double x) {
    janet_buffer_ensure(buffer, buffer->count + BUFSIZE, 2);
    const char *fmt = (x == floor(x) &&
                       x <= JANET_INTMAX_DOUBLE &&
                       x >= JANET_INTMIN_DOUBLE) ? "%.0f" : ("%." STR(DBL_DIG) "g");
    int count;
    if (x == 0.0) {
        /* Prevent printing of '-0' */
        count = 1;
        buffer->data[buffer->count] = '0';
    } else {
        count = snprintf((char *) buffer->data + buffer->count, BUFSIZE, fmt, x);
    }
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
    for (i = 0; i < 32 && title[i]; ++i)
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
            case '\a':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\a", 2);
                break;
            case '\b':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\b", 2);
                break;
            case 27:
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\e", 2);
                break;
            case '\\':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\\\", 2);
                break;
            case '\t':
                janet_buffer_push_bytes(buffer, (const uint8_t *)"\\t", 2);
                break;
            default:
                if (c < 32 || c > 126) {
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
    if (bx == buffer) {
        /* Ensures buffer won't resize while escaping */
        janet_buffer_ensure(bx, bx->count + 5 * bx->count + 3, 1);
    }
    janet_buffer_push_u8(buffer, '@');
    janet_escape_string_impl(buffer, bx->data, bx->count);
}

void janet_to_string_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        case JANET_NIL:
            janet_buffer_push_cstring(buffer, "");
            break;
        case JANET_BOOLEAN:
            janet_buffer_push_cstring(buffer,
                                      janet_unwrap_boolean(x) ? "true" : "false");
            break;
        case JANET_NUMBER:
            number_to_string_b(buffer, janet_unwrap_number(x));
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            janet_buffer_push_bytes(buffer,
                                    janet_unwrap_string(x),
                                    janet_string_length(janet_unwrap_string(x)));
            break;
        case JANET_BUFFER: {
            JanetBuffer *to = janet_unwrap_buffer(x);
            /* Prevent resizing buffer while appending */
            if (buffer == to) janet_buffer_extra(buffer, to->count);
            janet_buffer_push_bytes(buffer, to->data, to->count);
            break;
        }
        case JANET_ABSTRACT: {
            JanetAbstract p = janet_unwrap_abstract(x);
            const JanetAbstractType *t = janet_abstract_type(p);
            if (t->tostring != NULL) {
                t->tostring(p, buffer);
            } else {
                string_description_b(buffer, t->name, p);
            }
        }
        return;
        case JANET_CFUNCTION: {
            JanetCFunRegistry *reg = janet_registry_get(janet_unwrap_cfunction(x));
            if (NULL != reg) {
                janet_buffer_push_cstring(buffer, "<cfunction ");
                if (NULL != reg->name_prefix) {
                    janet_buffer_push_cstring(buffer, reg->name_prefix);
                    janet_buffer_push_u8(buffer, '/');
                }
                janet_buffer_push_cstring(buffer, reg->name);
                janet_buffer_push_u8(buffer, '>');
                break;
            }
            goto fallthrough;
        }
        case JANET_FUNCTION: {
            JanetFunction *fun = janet_unwrap_function(x);
            JanetFuncDef *def = fun->def;
            if (def == NULL) {
                janet_buffer_push_cstring(buffer, "<incomplete function>");
                break;
            }
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

/* See parse.c for full table */

/* Check if a symbol or keyword contains no symbol characters */
static int contains_bad_chars(const uint8_t *sym, int issym) {
    int32_t len = janet_string_length(sym);
    if (len && issym && sym[0] >= '0' && sym[0] <= '9') return 1;
    if (!janet_valid_utf8(sym, len)) return 1;
    for (int32_t i = 0; i < len; i++) {
        if (!janet_is_symbol_char(sym[i])) return 1;
    }
    return 0;
}

void janet_description_b(JanetBuffer *buffer, Janet x) {
    switch (janet_type(x)) {
        default:
            break;
        case JANET_NIL:
            janet_buffer_push_cstring(buffer, "nil");
            return;
        case JANET_KEYWORD:
            janet_buffer_push_u8(buffer, ':');
            break;
        case JANET_STRING:
            janet_escape_string_b(buffer, janet_unwrap_string(x));
            return;
        case JANET_BUFFER: {
            JanetBuffer *b = janet_unwrap_buffer(x);
            janet_escape_buffer_b(buffer, b);
            return;
        }
        case JANET_ABSTRACT: {
            JanetAbstract p = janet_unwrap_abstract(x);
            const JanetAbstractType *t = janet_abstract_type(p);
            if (t->tostring != NULL) {
                janet_buffer_push_cstring(buffer, "<");
                janet_buffer_push_cstring(buffer, t->name);
                janet_buffer_push_cstring(buffer, " ");
                t->tostring(p, buffer);
                janet_buffer_push_cstring(buffer, ">");
            } else {
                string_description_b(buffer, t->name, p);
            }
            return;
        }
    }
    janet_to_string_b(buffer, x);
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
    int32_t *keysort_buffer;
    int32_t keysort_capacity;
    int32_t keysort_start;
    JanetTable seen;
};

/* Print jdn format */
static int print_jdn_one(struct pretty *S, Janet x, int depth) {
    if (depth == 0) return 1;
    switch (janet_type(x)) {
        case JANET_NIL:
        case JANET_BOOLEAN:
        case JANET_BUFFER:
        case JANET_STRING:
            janet_description_b(S->buffer, x);
            break;
        case JANET_NUMBER:
            janet_buffer_ensure(S->buffer, S->buffer->count + BUFSIZE, 2);
            double num = janet_unwrap_number(x);
            if (isnan(num)) return 1;
            if (isinf(num)) return 1;
            janet_buffer_dtostr(S->buffer, num);
            break;
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            if (contains_bad_chars(janet_unwrap_keyword(x), janet_type(x) == JANET_SYMBOL)) return 1;
            janet_description_b(S->buffer, x);
            break;
        case JANET_TUPLE: {
            JanetTuple t = janet_unwrap_tuple(x);
            int isb = janet_tuple_flag(t) & JANET_TUPLE_FLAG_BRACKETCTOR;
            janet_buffer_push_u8(S->buffer, isb ? '[' : '(');
            for (int32_t i = 0; i < janet_tuple_length(t); i++) {
                if (i) janet_buffer_push_u8(S->buffer, ' ');
                if (print_jdn_one(S, t[i], depth - 1)) return 1;
            }
            janet_buffer_push_u8(S->buffer, isb ? ']' : ')');
        }
        break;
        case JANET_ARRAY: {
            janet_table_put(&S->seen, x, janet_wrap_true());
            JanetArray *a = janet_unwrap_array(x);
            janet_buffer_push_cstring(S->buffer, "@[");
            for (int32_t i = 0; i < a->count; i++) {
                if (i) janet_buffer_push_u8(S->buffer, ' ');
                if (print_jdn_one(S, a->data[i], depth - 1)) return 1;
            }
            janet_buffer_push_u8(S->buffer, ']');
        }
        break;
        case JANET_TABLE: {
            janet_table_put(&S->seen, x, janet_wrap_true());
            JanetTable *tab = janet_unwrap_table(x);
            janet_buffer_push_cstring(S->buffer, "@{");
            int isFirst = 1;
            for (int32_t i = 0; i < tab->capacity; i++) {
                const JanetKV *kv = tab->data + i;
                if (janet_checktype(kv->key, JANET_NIL)) continue;
                if (!isFirst) janet_buffer_push_u8(S->buffer, ' ');
                isFirst = 0;
                if (print_jdn_one(S, kv->key, depth - 1)) return 1;
                janet_buffer_push_u8(S->buffer, ' ');
                if (print_jdn_one(S, kv->value, depth - 1)) return 1;
            }
            janet_buffer_push_u8(S->buffer, '}');
        }
        break;
        case JANET_STRUCT: {
            JanetStruct st = janet_unwrap_struct(x);
            janet_buffer_push_u8(S->buffer, '{');
            int isFirst = 1;
            for (int32_t i = 0; i < janet_struct_capacity(st); i++) {
                const JanetKV *kv = st + i;
                if (janet_checktype(kv->key, JANET_NIL)) continue;
                if (!isFirst) janet_buffer_push_u8(S->buffer, ' ');
                isFirst = 0;
                if (print_jdn_one(S, kv->key, depth - 1)) return 1;
                janet_buffer_push_u8(S->buffer, ' ');
                if (print_jdn_one(S, kv->value, depth - 1)) return 1;
            }
            janet_buffer_push_u8(S->buffer, '}');
        }
        break;
        default:
            return 1;
    }
    return 0;
}

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
static const char janet_class_color[] = "\x1B[34m";
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
    "\x1B[36m",
    "\x1B[35m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m",
    "\x1B[36m"
};

#define JANET_PRETTY_DICT_ONELINE 4
#define JANET_PRETTY_IND_ONELINE 10
#define JANET_PRETTY_DICT_LIMIT 30
#define JANET_PRETTY_ARRAY_LIMIT 160

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
                if (len > JANET_PRETTY_ARRAY_LIMIT && !(S->flags & JANET_PRETTY_NOTRUNC)) {
                    for (i = 0; i < 3; i++) {
                        if (i) print_newline(S, 0);
                        janet_pretty_one(S, arr[i], 0);
                    }
                    print_newline(S, 0);
                    janet_buffer_push_cstring(S->buffer, "...");
                    for (i = 0; i < 3; i++) {
                        print_newline(S, 0);
                        janet_pretty_one(S, arr[len - 3 + i], 0);
                    }
                } else {
                    for (i = 0; i < len; i++) {
                        if (i) print_newline(S, len < JANET_PRETTY_IND_ONELINE);
                        janet_pretty_one(S, arr[i], 0);
                    }
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

            /* For object-like tables, print class name */
            if (istable) {
                JanetTable *t = janet_unwrap_table(x);
                JanetTable *proto = t->proto;
                janet_buffer_push_cstring(S->buffer, "@");
                if (NULL != proto) {
                    Janet name = janet_table_get(proto, janet_ckeywordv("_name"));
                    const uint8_t *n;
                    int32_t len;
                    if (janet_bytes_view(name, &n, &len)) {
                        if (S->flags & JANET_PRETTY_COLOR) {
                            janet_buffer_push_cstring(S->buffer, janet_class_color);
                        }
                        janet_buffer_push_bytes(S->buffer, n, len);
                        if (S->flags & JANET_PRETTY_COLOR) {
                            janet_buffer_push_cstring(S->buffer, "\x1B[0m");
                        }
                    }
                }
            } else {
                JanetStruct st = janet_unwrap_struct(x);
                JanetStruct proto = janet_struct_proto(st);
                if (NULL != proto) {
                    Janet name = janet_struct_get(proto, janet_ckeywordv("_name"));
                    const uint8_t *n;
                    int32_t len;
                    if (janet_bytes_view(name, &n, &len)) {
                        if (S->flags & JANET_PRETTY_COLOR) {
                            janet_buffer_push_cstring(S->buffer, janet_class_color);
                        }
                        janet_buffer_push_bytes(S->buffer, n, len);
                        if (S->flags & JANET_PRETTY_COLOR) {
                            janet_buffer_push_cstring(S->buffer, "\x1B[0m");
                        }
                    }
                }
            }
            janet_buffer_push_cstring(S->buffer, "{");

            S->depth--;
            S->indent += 2;
            if (S->depth == 0) {
                janet_buffer_push_cstring(S->buffer, "...");
            } else {
                int32_t i = 0, len = 0, cap = 0;
                const JanetKV *kvs = NULL;
                janet_dictionary_view(x, &kvs, &len, &cap);
                if (!istable && !(S->flags & JANET_PRETTY_ONELINE) && len >= JANET_PRETTY_DICT_ONELINE)
                    janet_buffer_push_u8(S->buffer, ' ');
                if (is_dict_value && len >= JANET_PRETTY_DICT_ONELINE) print_newline(S, 0);
                int32_t ks_start = S->keysort_start;

                /* Ensure buffer is large enough to sort keys. */
                int truncated = 0;
                int64_t mincap = (int64_t) len + (int64_t) ks_start;
                if (mincap > INT32_MAX) {
                    truncated = 1;
                    len = 0;
                    mincap = ks_start;
                }

                if (S->keysort_capacity < mincap) {
                    if (mincap >= INT32_MAX / 2) {
                        S->keysort_capacity = INT32_MAX;
                    } else {
                        S->keysort_capacity = (int32_t)(mincap * 2);
                    }
                    S->keysort_buffer = janet_srealloc(S->keysort_buffer, sizeof(int32_t) * S->keysort_capacity);
                    if (NULL == S->keysort_buffer) {
                        JANET_OUT_OF_MEMORY;
                    }
                }

                janet_sorted_keys(kvs, cap, S->keysort_buffer == NULL ? NULL : S->keysort_buffer + ks_start);
                S->keysort_start += len;
                if (!(S->flags & JANET_PRETTY_NOTRUNC) && (len > JANET_PRETTY_DICT_LIMIT)) {
                    len = JANET_PRETTY_DICT_LIMIT;
                    truncated = 1;
                }

                for (i = 0; i < len; i++) {
                    if (i) print_newline(S, len < JANET_PRETTY_DICT_ONELINE);
                    int32_t j = S->keysort_buffer[i + ks_start];
                    janet_pretty_one(S, kvs[j].key, 0);
                    janet_buffer_push_u8(S->buffer, ' ');
                    janet_pretty_one(S, kvs[j].value, 1);
                }

                if (truncated) {
                    print_newline(S, 0);
                    janet_buffer_push_cstring(S->buffer, "...");
                }

                S->keysort_start = ks_start;
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
    S.keysort_capacity = 0;
    S.keysort_buffer = NULL;
    S.keysort_start = 0;
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

static JanetBuffer *janet_jdn_(JanetBuffer *buffer, int depth, Janet x, int32_t startlen) {
    struct pretty S;
    if (NULL == buffer) {
        buffer = janet_buffer(0);
    }
    S.buffer = buffer;
    S.depth = depth;
    S.indent = 0;
    S.flags = 0;
    S.bufstartlen = startlen;
    S.keysort_capacity = 0;
    S.keysort_buffer = NULL;
    S.keysort_start = 0;
    janet_table_init(&S.seen, 10);
    int res = print_jdn_one(&S, x, depth);
    janet_table_deinit(&S.seen);
    if (res) {
        janet_panic("could not print to jdn format");
    }
    return S.buffer;
}

JanetBuffer *janet_jdn(JanetBuffer *buffer, int depth, Janet x) {
    return janet_jdn_(buffer, depth, x, buffer ? buffer->count : 0);
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
                janet_buffer_push_cstring(buffer, (types == 1) ? " or " : ", ");
            }
            janet_buffer_push_cstring(buffer, janet_type_names[i]);
        }
        i++;
        types >>= 1;
    }
}

/*
 * code adapted from lua/lstrlib.c http://lua.org
 */

#define MAX_ITEM  256
#define FMT_FLAGS "-+ #0"
#define FMT_REPLACE_INTTYPES "diouxX"
#define MAX_FORMAT 32

struct FmtMapping {
    char c;
    const char *mapping;
};

/* Janet uses fixed width integer types for most things, so map
 * format specifiers to these fixed sizes */
static const struct FmtMapping format_mappings[] = {
    {'D', PRId64},
    {'I', PRIi64},
    {'d', PRId64},
    {'i', PRIi64},
    {'o', PRIo64},
    {'u', PRIu64},
    {'x', PRIx64},
    {'X', PRIX64},
};

static const char *get_fmt_mapping(char c) {
    for (size_t i = 0; i < (sizeof(format_mappings) / sizeof(struct FmtMapping)); i++) {
        if (format_mappings[i].c == c)
            return format_mappings[i].mapping;
    }
    janet_assert(0, "bad format mapping");
}

static const char *scanformat(
    const char *strfrmt,
    char *form,
    char width[3],
    char precision[3]) {
    const char *p = strfrmt;

    /* Parse strfrmt */
    memset(width, '\0', 3);
    memset(precision, '\0', 3);
    while (*p != '\0' && strchr(FMT_FLAGS, *p) != NULL)
        p++; /* skip flags */
    if ((size_t)(p - strfrmt) >= sizeof(FMT_FLAGS)) janet_panic("invalid format (repeated flags)");
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

    /* Write to form - replace characters with fixed size stuff */
    *(form++) = '%';
    const char *p2 = strfrmt;
    while (p2 <= p) {
        char *loc = strchr(FMT_REPLACE_INTTYPES, *p2);
        if (loc != NULL && *loc != '\0') {
            const char *mapping = get_fmt_mapping(*p2++);
            size_t len = strlen(mapping);
            memcpy(form, mapping, len);
            form += len;
        } else {
            *(form++) = *(p2++);
        }
    }
    *form = '\0';

    return p;
}

void janet_formatbv(JanetBuffer *b, const char *format, va_list args) {
    const char *format_end = format + strlen(format);
    const char *c = format;
    int32_t startlen = b->count;
    while (c < format_end) {
        if (*c != '%') {
            janet_buffer_push_u8(b, (uint8_t) *c++);
        } else if (*++c == '%') {
            janet_buffer_push_u8(b, (uint8_t) *c++);
        } else {
            char form[MAX_FORMAT], item[MAX_ITEM];
            char width[3], precision[3];
            int nb = 0; /* number of bytes in added item */
            c = scanformat(c, form, width, precision);
            switch (*c++) {
                case 'c': {
                    int n = va_arg(args, int);
                    nb = snprintf(item, MAX_ITEM, form, n);
                    break;
                }
                case 'd':
                case 'i': {
                    int64_t n = (int64_t) va_arg(args, int32_t);
                    nb = snprintf(item, MAX_ITEM, form, n);
                    break;
                }
                case 'D':
                case 'I': {
                    int64_t n = va_arg(args, int64_t);
                    nb = snprintf(item, MAX_ITEM, form, n);
                    break;
                }
                case 'x':
                case 'X':
                case 'o':
                case 'u': {
                    uint64_t n = va_arg(args, uint64_t);
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
                    double d = va_arg(args, double);
                    nb = snprintf(item, MAX_ITEM, form, d);
                    break;
                }
                case 's':
                case 'S': {
                    const char *str = va_arg(args, const char *);
                    int32_t len = c[-1] == 's'
                                  ? (int32_t) strlen(str)
                                  : janet_string_length((JanetString) str);
                    if (form[2] == '\0')
                        janet_buffer_push_bytes(b, (const uint8_t *) str, len);
                    else {
                        if (len != (int32_t) strlen((const char *) str))
                            janet_panic("string contains zeros");
                        if (!strchr(form, '.') && len >= 100) {
                            janet_panic("no precision and string is too long to be formatted");
                        } else {
                            nb = snprintf(item, MAX_ITEM, form, str);
                        }
                    }
                    break;
                }
                case 'V':
                    janet_to_string_b(b, va_arg(args, Janet));
                    break;
                case 'v':
                    janet_description_b(b, va_arg(args, Janet));
                    break;
                case 't':
                    janet_buffer_push_cstring(b, typestr(va_arg(args, Janet)));
                    break;
                case 'T': {
                    int types = va_arg(args, int);
                    pushtypes(b, types);
                    break;
                }
                case 'M':
                case 'm':
                case 'N':
                case 'n':
                case 'Q':
                case 'q':
                case 'P':
                case 'p': { /* janet pretty , precision = depth */
                    int depth = atoi(precision);
                    if (depth < 1) depth = JANET_RECURSION_GUARD;
                    char d = c[-1];
                    int has_color = (d == 'P') || (d == 'Q') || (d == 'M') || (d == 'N');
                    int has_oneline = (d == 'Q') || (d == 'q') || (d == 'N') || (d == 'n');
                    int has_notrunc = (d == 'M') || (d == 'm') || (d == 'N') || (d == 'n');
                    int flags = 0;
                    flags |= has_color ? JANET_PRETTY_COLOR : 0;
                    flags |= has_oneline ? JANET_PRETTY_ONELINE : 0;
                    flags |= has_notrunc ? JANET_PRETTY_NOTRUNC : 0;
                    janet_pretty_(b, depth, flags, va_arg(args, Janet), startlen);
                    break;
                }
                case 'j': {
                    int depth = atoi(precision);
                    if (depth < 1)
                        depth = JANET_RECURSION_GUARD;
                    janet_jdn_(b, depth, va_arg(args, Janet), startlen);
                    break;
                }
                default: {
                    /* also treat cases 'nLlh' */
                    janet_panicf("invalid conversion '%s' to 'format'",
                                 form);
                }
            }
            if (nb >= MAX_ITEM)
                janet_panic("format buffer overflow");
            if (nb > 0)
                janet_buffer_push_bytes(b, (uint8_t *) item, nb);
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
    janet_formatbv(&buffer, format, args);

    /* Iterate length */
    va_end(args);

    ret = janet_string(buffer.data, buffer.count);
    janet_buffer_deinit(&buffer);
    return ret;
}

JanetBuffer *janet_formatb(JanetBuffer *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    janet_formatbv(buffer, format, args);
    va_end(args);
    return buffer;
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
                case 'D':
                case 'I':
                case 'd':
                case 'i': {
                    int64_t n = janet_getinteger64(argv, arg);
                    nb = snprintf(item, MAX_ITEM, form, n);
                    break;
                }
                case 'x':
                case 'X':
                case 'o':
                case 'u': {
                    uint64_t n = janet_getuinteger64(argv, arg);
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
                    JanetByteView bytes = janet_getbytes(argv, arg);
                    const uint8_t *s = bytes.bytes;
                    int32_t l = bytes.len;
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
                case 't':
                    janet_buffer_push_cstring(b, typestr(argv[arg]));
                    break;
                case 'M':
                case 'm':
                case 'N':
                case 'n':
                case 'Q':
                case 'q':
                case 'P':
                case 'p': { /* janet pretty , precision = depth */
                    int depth = atoi(precision);
                    if (depth < 1) depth = JANET_RECURSION_GUARD;
                    char d = strfrmt[-1];
                    int has_color = (d == 'P') || (d == 'Q') || (d == 'M') || (d == 'N');
                    int has_oneline = (d == 'Q') || (d == 'q') || (d == 'N') || (d == 'n');
                    int has_notrunc = (d == 'M') || (d == 'm') || (d == 'N') || (d == 'n');
                    int flags = 0;
                    flags |= has_color ? JANET_PRETTY_COLOR : 0;
                    flags |= has_oneline ? JANET_PRETTY_ONELINE : 0;
                    flags |= has_notrunc ? JANET_PRETTY_NOTRUNC : 0;
                    janet_pretty_(b, depth, flags, argv[arg], startlen);
                    break;
                }
                case 'j': {
                    int depth = atoi(precision);
                    if (depth < 1)
                        depth = JANET_RECURSION_GUARD;
                    janet_jdn_(b, depth, argv[arg], startlen);
                    break;
                }
                default: {
                    /* also treat cases 'nLlh' */
                    janet_panicf("invalid conversion '%s' to 'format'",
                                 form);
                }
            }
            if (nb >= MAX_ITEM)
                janet_panic("format buffer overflow");
            if (nb > 0)
                janet_buffer_push_bytes(b, (uint8_t *) item, nb);
        }
    }
}

#undef HEX
#undef BUFSIZE

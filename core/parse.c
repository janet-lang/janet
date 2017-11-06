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

/* Get an integer power of 10 */
static double exp10(int power) {
    if (power == 0) return 1;
    if (power > 0) {
        double result = 10;
        int currentPower = 1;
        while (currentPower * 2 <= power) {
            result = result * result;
            currentPower *= 2;
        }
        return result * exp10(power - currentPower);
    } else {
        return 1 / exp10(-power);
    }
}

/* Read an integer */
static int read_integer(const uint8_t *string, const uint8_t *end, int64_t *ret) {
    int sign = 1, x = 0;
    int64_t accum = 0;
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        x = *string;
        if (x < '0' || x > '9') return 0;
        x -= '0';
        accum = accum * 10 + x;
        ++string;
    }
    *ret = accum * sign;
    return 1;
}

/* Read a real from a string. Returns if successfuly
 * parsed a real from the enitre input string.
 * If returned 1, output is int ret.*/
static int read_real(const uint8_t *string, const uint8_t *end, double *ret, int forceInt) {
    int sign = 1, x = 0;
    double accum = 0, exp = 1, place = 1;
    /* Check the sign */
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        if (*string == '.' && !forceInt) {
            place = 0.1;
        } else if (!forceInt && (*string == 'e' || *string == 'E')) {
            /* Read the exponent */
            ++string;
            if (string >= end) return 0;
            if (!read_real(string, end, &exp, 1))
                return 0;
            exp = exp10(exp);
            break;
        } else {
            x = *string;
            if (x < '0' || x > '9') return 0;
            x -= '0';
            if (place < 1) {
                accum += x * place;
                place *= 0.1;
            } else {
                accum *= 10;
                accum += x;
            }
        }
        ++string;
    }
    *ret = accum * sign * exp;
    return 1;
}


/* Checks if a string slice is equal to a string constant */
static int check_str_const(const char *ref, const uint8_t *start, const uint8_t *end) {
    while (*ref && start < end) {
        if (*ref != *(char *)start) return 0;
        ++ref;
        ++start;
    }
    return !*ref && start == end;
}

/* Quote a value */
static DstValue quote(DstValue x) {
    DstValue sym = dst_wrap_symbol(dst_cstring("quote"));
    DstValue *t = dst_tuple_begin(2);
    t[0] = sym;
    t[1] = x;
    return dst_wrap_tuple(dst_tuple_end(t));
}

/* Check if a character is whitespace */
static int is_whitespace(uint8_t c) {
    return c == ' ' 
        || c == '\t'
        || c == '\n'
        || c == '\r'
        || c == '\0'
        || c == ',';
}

/* Check if a character is a valid symbol character */
/* TODO - wlloe utf8 - shouldn't be difficult, err on side
 * of inclusivity */
static int is_symbol_char(uint8_t c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= ':') return 1;
    if (c >= '<' && c <= '@') return 1;
    if (c >= '*' && c <= '/') return 1;
    if (c >= '#' && c <= '&') return 1;
    if (c == '_') return 1;
    if (c == '^') return 1;
    if (c == '!') return 1;
    return 0;
}

/* Get hex digit from a letter */
static int to_hex(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    } else if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    } else {
        return -1;
    }
}

typedef struct {
    DstFiber *fiber;
    const uint8_t *end;
    const char *errmsg;
    enum {
        DST_PARSE_OK,
        DST_PARSE_ERROR,
        DST_PARSE_UNEPECTED_EOS
    } status;
} ParseArgs;

/* Entry point of the recursive descent parser */
static const uint8_t *parse_recur(
    ParseArgs *args,
    const uint8_t *src,
    uint32_t recur) {

    const uint8_t *end = args->end;
    uint32_t qcount = 0;
    DstValue ret;

    /* Prevent stack overflow */
    if (recur == 0) goto too_much_recur;

    /* Trim leading whitespace and count quotes */
    while (src < end && (is_whitespace(*src) || *src == '\'')) {
        if (*src == '\'') {
            ++qcount;
        }
        ++src;
    }

    /* Check for end of source */
    if (src >= end) goto unexpected_eos;

    /* Detect token type based on first character */
    switch (*src) {

        /* Numbers, symbols, simple literals */
        default: {
            double real;
            int64_t integer;
            const uint8_t *tokenend = src;
            if (!is_symbol_char(*src)) goto unexpected_character;
            while (tokenend < end && is_symbol_char(*tokenend))
                tokenend++;
            if (tokenend >= end) goto unexpected_eos;
            if (read_integer(src, tokenend, &integer)) {
                ret = dst_wrap_integer(integer);
            } else if (read_real(src, tokenend, &real, 0)) {
                ret = dst_wrap_real(real);
            } else if (check_str_const("nil", src, tokenend)) {
                ret = dst_wrap_nil();
            } else if (check_str_const("false", src, tokenend)) {
                ret = dst_wrap_boolean(0);
            } else if (check_str_const("true", src, tokenend)) {
                ret = dst_wrap_boolean(1);
            } else {
                if (*src >= '0' && *src <= '9') {
                    goto sym_nodigits;
                } else {
                    ret = dst_wrap_symbol(dst_string(src, tokenend - src));
                }
            }
            src = tokenend;
            break;
        }

        case ':': {
            const uint8_t *tokenend = ++src;
            while (tokenend < end && is_symbol_char(*tokenend))
                tokenend++;
            if (tokenend >= end) goto unexpected_eos;
            ret = dst_wrap_string(dst_string(src, tokenend - src));
            src = tokenend;
            break;
        }

        /* String literals */
        case '"': {
            const uint8_t *strend = ++src;
            const uint8_t *strstart = src;
            uint32_t len = 0;
            int containsEscape = 0;
            /* Preprocess string to check for escapes and string end */
            while (strend < end && *strend != '"') {
                len++;
                if (*strend++ == '\\') {
                    containsEscape = 1;
                    if (strend >= end) goto unexpected_eos;
                    if (*strend == 'h') {
                        strend += 2;
                        if (strend >= end) goto unexpected_eos;
                    } else {
                        strend++;
                    }
                }
            }
            if (containsEscape) {
                uint8_t *buf = dst_string_begin(len);
                uint8_t *write = buf;
                while (src < strend) {
                    if (*src == '\\') {
                        src++;
                        switch (*++src) {
                            case 'n': *write++ = '\n'; break;
                            case 'r': *write++ = '\r'; break;
                            case 't': *write++ = '\t'; break;
                            case 'f': *write++ = '\f'; break;
                            case '0': *write++ = '\0'; break;
                            case '"': *write++ = '"'; break;
                            case '\'': *write++ = '\''; break;
                            case 'z': *write++ = '\0'; break;
                            case 'e': *write++ = 27; break;
                            case 'h': {
                                int d1 = to_hex(*src++);
                                int d2 = to_hex(*src++);
                                if (d1 < 0 || d2 < 0) goto invalid_hex;
                                *write++ = 16 * d1 + d2;
                                break;
                            }
                            default:
                                goto unknown_strescape;
                        }
                    } else {
                        *write++ = *src++;
                    }
                }
                ret = dst_wrap_string(dst_string_end(buf));
            } else {
                ret = dst_wrap_string(dst_string(strstart, strend - strstart));
                src = strend + 1;
            }
            break;
        }

        /* Data Structure literals */
        case '(':
        case '[':
        case '{': {
            uint32_t n = 0, i = 0;
            uint8_t close;
            switch (*src++) {
                case '[': close = ']'; break;
                case '{': close = '}'; break;
                default: close = ')'; break;
            }
            /* Recursively parse inside literal */
            while (*src != close) {
                src = parse_recur(args, src, recur - 1);
                if (args->errmsg || !src) return src;
                n++;
                /* Trim trailing whitespace */
                while (src < end && (is_whitespace(*src)))
                    ++src;
            }
            src++;
            switch (close) {
                case ')':
                case ']':
                {
                    DstValue *tup = dst_tuple_begin(n);
                    for (i = n; i > 0; i--)
                        tup[i - 1] = dst_fiber_popvalue(args->fiber);
                    ret = dst_wrap_tuple(dst_tuple_end(tup));
                    break;
                }
                case '}':
                {
                    if (n & 1) goto struct_oddargs;
                    DstValue *st = dst_struct_begin(n >> 1);
                    for (i = n; i > 0; i -= 2) {
                        DstValue val = dst_fiber_popvalue(args->fiber);
                        DstValue key = dst_fiber_popvalue(args->fiber);
                        dst_struct_put(st, key, val);
                    }
                    ret = dst_wrap_struct(dst_struct_end(st));
                    break;
                }
            }
            break;
        }
    }

    /* Quote the returned value qcount times */
    while (qcount--) ret = quote(ret);

    /* Push the result to the stack */
    dst_fiber_push(args->fiber, ret);

    /* Return the new source position for further calls */
    return src;

    /* Errors below */

    unexpected_eos:
    args->errmsg = "unexpected end of source";
    args->status = DST_PARSE_UNEXPECTED_EOS;
    return NULL;

    unexpected_character:
    args->errmsg = "unexpected character";
    args->status = DST_PARSE_ERROR;
    return src;

    sym_nodigits:
    args->errmsg = "symbols cannot start with digits";
    args->status = DST_PARSE_ERROR;
    return src;

    struct_oddargs:
    args->errmsg = "struct literal needs an even number of arguments";
    args->status = DST_PARSE_ERROR;
    return src;

    unknown_strescape:
    args->errmsg = "unknown string escape sequence";
    args->status = DST_PARSE_ERROR;
    return src;

    invalid_hex:
    args->errmsg = "invalid hex escape in string";
    args->status = DST_PARSE_ERROR;
    return src;

    too_much_recur:
    args->errmsg = "recursed too deeply in parsing";
    args->status = DST_PARSE_ERROR;
    return src;
}

/* Parse an array of bytes. Return value in the fiber return value. */
int dst_parse(const uint8_t *src, uint32_t len, const uint8_t **newsrc) {
    ParseArgs args;
    DstFiber *fiber = dst_vm_fiber;

    /* Save original stack top */
    uint32_t oldstacktop = fiber->stacktop;

    args.fiber = fiber;
    args.status = DST_PARSE_OK;
    args.end = src + len;
    args.errmsg = NULL;

    src = parse_recur(&args, src, DST_RECURSION_GUARD);
    if (NULL != newsrc) *newsrc = src;

    if (args.errmsg) {
        fiber->ret = dst_wrap_string(dst_cstring(args.errmsg));
    } else {
        fiber->ret = dst_fiber_popvalue(fiber);
    }

    /* Reset stacktop */
    fiber->stacktop = oldstacktop;

    return args.status;
}

/* Parse a c string */
int dst_parsec(const char *src, const char **newsrc) {
    uint32_t len = 0;
    const uint8_t *ns = NULL;
    int status;
    while (src[len]) ++len;
    status = dst_parse((const uint8_t *)src, len, &ns);
    if (newsrc) {
        *newsrc = (const char *)ns;
    }
    return status;
}

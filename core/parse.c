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
static void quote(Dst *vm) {
    dst_cstring(vm, "quote");
    dst_hoist(vm, -2);
    dst_tuple(vm, 2);
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
    Dst *vm;
    const uint8_t *end;
    const char *errmsg;
    int64_t sourcemap;
    int status;
} ParseArgs;

/* Entry point of the recursive descent parser */
static const uint8_t *parse_recur(
    ParseArgs *args,
    const uint8_t *src,
    uint32_t recur) {

    Dst *vm = args->vm;
    const uint8_t *end = args->end;
    uint32_t qcount = 0;

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
                dst_integer(vm, integer);
            } else if (read_real(src, tokenend, &real, 0)) {
                dst_real(vm, real);
            } else if (check_str_const("nil", src, tokenend)) {
                dst_nil(vm);
            } else if (check_str_const("false", src, tokenend)) {
                dst_false(vm);
            } else if (check_str_const("true", src, tokenend)) {
                dst_true(vm);
            } else {
                if (*src >= '0' && *src <= '9') {
                    goto sym_nodigits;
                } else {
                    dst_symbol(vm, src, tokenend - src);
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
            dst_string(vm, src, tokenend - src);
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
                uint8_t *buf = dst_string_begin(vm, len);
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
                dst_string_end(vm, buf);
            } else {
                dst_string(vm, strstart, strend - strstart);
                src = strend + 1;
            }
            break;
        }

        /* Data Structure literals */
        case '(':
        case '[':
        case '{': {
            uint32_t n = 0;
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
                    dst_tuple(vm, n);
                    break;
                case ']':
                    dst_arrayn(vm, n);
                    break;
                case '}':
                    if (n & 1) goto struct_oddargs;
                    dst_struct(vm, n/2);
                    break;
            }
            break;
        }
    }

    /* Quote the returned value qcount times */
    while (qcount--) quote(vm);

    /* Return the new source position for further calls */
    return src;

    /* Errors below */

    unexpected_eos:
    args->errmsg = "unexpected end of source";
    args->status = PARSE_UNEXPECTED_EOS;
    return NULL;

    unexpected_character:
    args->errmsg = "unexpected character";
    args->status = PARSE_ERROR;
    return src;

    sym_nodigits:
    args->errmsg = "symbols cannot start with digits";
    args->status = PARSE_ERROR;
    return src;

    struct_oddargs:
    args->errmsg = "struct literal needs an even number of arguments";
    args->status = PARSE_ERROR;
    return src;

    unknown_strescape:
    args->errmsg = "unknown string escape sequence";
    args->status = PARSE_ERROR;
    return src;

    invalid_hex:
    args->errmsg = "invalid hex escape in string";
    args->status = PARSE_ERROR;
    return src;

    too_much_recur:
    args->errmsg = "recursed too deeply in parsing";
    args->status = PARSE_ERROR;
    return src;
}

/* Parse an array of bytes */
int dst_parseb(Dst *vm, const uint8_t *src, const uint8_t **newsrc, uint32_t len) {
    ParseArgs args;
    uint32_t nargs;

    /* Create a source map */
    dst_table(vm, 10);

    nargs = dst_stacksize(vm);

    args.vm = vm;
    args.status = PARSE_OK;
    args.end = src + len;
    args.errmsg = NULL;
    args.sourcemap = dst_normalize_index(vm, -1);

    src = parse_recur(&args, src, 2048);
    if (newsrc) *newsrc = src;

    if (args.errmsg) {
        /* Unwind the stack */
        dst_trimstack(vm, nargs);
        dst_cstring(vm, args.errmsg);
    }

    return args.status;
}

/* Parse a c string */
int dst_parsec(Dst *vm, const char *src, const char **newsrc) {
    uint32_t len = 0;
    const uint8_t *ns = NULL;
    int status;
    while (src[len]) ++len;
    status = dst_parseb(vm, (const uint8_t *)src, &ns, len);
    if (newsrc) {
        *newsrc = (const char *)ns;
    }
    return status;
}

/* Parse a DST char seq (Buffer, String, Symbol) */
int dst_parse(Dst *vm)  {
    int status;
    uint32_t len;
    const uint8_t *bytes = dst_bytes(vm, -1, &len);
    status = dst_parseb(vm, bytes, NULL, len);
    dst_swap(vm, -1, -2);
    dst_pop(vm);
    return status;
}

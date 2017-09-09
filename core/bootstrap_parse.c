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
#include "bootstrap.h"

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
static DstValue quote(Dst *vm, DstValue x) {
    DstValue *tuple = dst_tuple_begin(vm, 2);
    tuple[0] = dst_string_cvs(vm, "quote");
    tuple[1] = x;
    return dst_wrap_tuple(dst_tuple_end(vm, tuple));
}

/* Check if a character is whitespace */
static int is_whitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0' || c == ',';
}

/* Check if a character is a valid symbol character */
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
    const char **errmsg;
    int status;
} ParseArgs;

/* Entry point of recursive descent parser */
static const uint8_t *parse(
    ParseArgs *args,
    const uint8_t *src,
    uint32_t recur) {

    Dst *vm = args->vm;
    const uint8_t *end = args->end;
    uint32_t qcount = 0;
    uint32_t retindex = dst_args(vm);

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
            DstReal real;
            DstInteger integer;
            const uint8_t *tokenend = src;
            if (!is_symbol_char(*src)) goto unexpected_character;
            dst_setsize(vm, retindex + 1);
            while (tokenend < end && is_symbol_char(*tokenend))
                tokenend++;
            if (tokenend >= end) goto unexpected_eos;
            if (dst_read_integer(src, tokenend, &integer)) {
                dst_set_integer(vm, retindex, integer);
            } else if (dst_read_real(src, tokenend, &real, 0)) {
                dst_set_real(vm, retindex, real);
            } else if (check_str_const("nil", src, tokenend)) {
                dst_nil(vm, retindex);
            } else if (check_str_const("false", src, tokenend)) {
                dst_false(vm, retindex);
            } else if (check_str_const("true", src, tokenend)) {
                dst_true(vm, retindex);
            } else {
                if (*src >= '0' && *src <= '9') {
                    goto sym_nodigits;
                } else {
                    dst_symbol(vm, retindex, src, tokenend - src);
                }
            }
            src = tokenend;
            break;
        }

        case ':': {
            const uint8_t *tokenend = ++src;
            dst_setsize(vm, retindex + 1);
            while (tokenend < end && is_symbol_char(*tokenend))
                tokenend++;
            if (tokenend >= end) goto unexpected_eos;
            dst_string(vm, retindex, src, tokenend - src);
            src = tokenend;
            break;
        }

        /* String literals */
        case '"': {
            const uint8_t *strend = ++src;
            uint32_t len = 0;
            int containsEscape = 0;
            /* Preprocess string to check for escapes and string end */
            while (strend < end && *strend != '"') {
                len++;
                if (*strend++ == '\\') {
                    constainsEscape = 1;
                    if (strend >= end) goto unexpected_eos;
                    if (*strend == 'h') {
                        strend += 2;
                        if (strend >= end) goto unexpected_eos;
                    }
                }
            }
            if (containsEscape) {
                uint8_t *buf = dst_string_begin(vm, len);
                uint8_t *write = buf;
                const uint8_t *scan = src;
                while (scan < strend) {
                    if (*scan == '\\') {
                        scan++;
                        switch (*++scan) {
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
                                int d1 = to_hex(scan[0]);
                                int d2 = to_hex(scan[1]);
                                if (d1 < 0 || d2 < 0) goto invalid_hex;
                                *write = 16 * d1 + d2;
                                break;
                            }
                            default:
                                goto unknown_strescape;
                        }
                    } else {
                        *write++ = *scan++;
                    }
                }
                dst_string_end(vm, retindex, buf);
            } else {
                dst_string(vm, retindex, src, strend - src);
            }
            src = strend + 1;
            break;
        }

        /* Data Structure literals */
        case: '(':
        case: '[':
        case: '{': {
            uint8_t close;
            uint32_t tmpindex;
            switch (*src++) {
                case '(': close = ')'; break;
                case '[': close = ']'; break;
                case '{': close = '}'; break;
                default: close = ')'; break;
            }
            /* Recursively parse inside literal */
            while (*src != close) {
                src = parse(args, src, recur - 1);
                if (*(args->errmsg) || !src) return src;
            }
            src++;
            tmpindex = dst_args(vm);
            dst_push_space(vm, 1);
            switch (close) {
                case ')':
                    dst_tuple_n(vm, tmpindex, retindex, tmpindex - retindex);
                    break;
                case ']':
                    dst_array_n(vm, tmpindex, retindex, tmpindex - retindex);
                    break;
                case '}':
                    if ((tmpindex - retindex) % 2) goto struct_oddargs;
                    dst_struct_n(vm, tmpindex, retindex, tmpindex - retindex);
                    break;
            }
            dst_move(vm, retindex, tmpindex);
            dst_setsize(vm, retindex + 1);
            break;
        }
    }

    /* Quote the returned value qcount times */
    while (qcount--) {
        dst_set_arg(vm, retindex, quote(vm, dst_arg(vm, retindex)));
    }

    /* Return the new source position for further calls */
    return src;

    /* Errors below */

    unexpected_eos:
    *(args->errmsg) = "unexpected end of source";
    args->status = PARSE_UNEXPECTED_EOS;
    return NULL;

    unexpected_character:
    *(args->errmsg) = "unexpected character";
    args->status = PARSE_ERROR;
    return src;

    sym_nodigits:
    *(args->errmsg) = "symbols cannot start with digits";
    args->status = PARSE_ERROR;
    return src;

    struct_oddargs:
    *(args->errmsg) = "struct literal needs an even number of arguments";
    args->status = PARSE_ERROR;
    return src;

    unknown_strescape:
    *(args->errmsg) = "unknown string escape sequence";
    args->status = PARSE_ERROR;
    return src;

    invalid_hex:
    *(args->errmsg) = "invalid hex escape in string";
    args->status = PARSE_ERROR;
    return src;

    too_much_recur:
    *(args->errmsg) = "recursed too deeply in parsing";
    args->status = PARSE_ERROR;
    return src;
}

/* Parse an array of bytes */
int dst_parseb(Dst *vm, uint32_t dest, const uint8_t *src, const uint8_t **newsrc, uint32_t len) {
    ParseArgs args;
    uint32_t toploc = dst_args(vm);
    const uint8_t *srcrest;

    args.vm = vm;
    args.status = PARSE_OK;
    args.end = src + len;
    args.errmsg = NULL;

    srcrest = parse(&args, src, 2048) || src;
    if (newsrc) {
        *newsrc = srcrest;
    }
    if (args.errmsg) {
        /* Error */
        dst_cstring(vm, dest, args.errmsg);
    } else {
        /* Success */
        dst_move(vm, dest, toploc);
    }
    dst_setsize(vm, toploc);
    return args.status;
}

/* Parse a c string */
int dst_parsec(Dst *vm, uint32_t dest, const char *src) {
    uint32_t len = 0;
    while (src[len]) ++len;
    return dst_parseb(vm, dest, (const uint8_t *)src, NULL, len);
}

/* Parse a DST char seq (Buffer, String, Symbol) */
int dst_parse(Dst *vm, uint32_t dest, uint32_t src) {
    uint32_t len;
    const uint8_t *bytes = dst_bytes(vm, src, &len);
    return dst_parseb(vm, dest, bytes, NULL, len);
}

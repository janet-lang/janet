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
#include <stdlib.h>
#include <errno.h>

/*****************/
/* JSON Decoding */
/*****************/

/* Check if a character is whitespace */
static int white(uint8_t c) {
    return c == '\t' || c == '\n' || c == ' ' || c == '\r';
}

/* Skip whitespace */
static void skipwhite(const char **p) {
    const char *cp = *p;
    for (;;) {
        if (white(*cp))
            cp++;
        else
            break;
    }
    *p = cp;
}

/* Get a hex digit value */
static int hexdig(char dig) {
    if (dig >= '0' && dig <= '9')
        return dig - '0';
    if (dig >= 'a' && dig <= 'f')
        return 10 + dig - 'a';
    if (dig >= 'A' && dig <= 'F')
        return 10 + dig - 'A';
    return -1;
}

/* Read the hex value for a unicode escape */
static char *decode_utf16_escape(const char *p, uint32_t *outpoint) {
    if (!p[0] || !p[1] || !p[2] || !p[3])
        return "unexpected end of source";
    int d1 = hexdig(p[0]);
    int d2 = hexdig(p[1]);
    int d3 = hexdig(p[2]);
    int d4 = hexdig(p[3]);
    if (d1 < 0 || d2 < 0 || d3 < 0 || d4 < 0)
        return "invalid hex digit";
    *outpoint = d4 | (d3 << 4) | (d2 << 8) | (d1 << 12);
    return NULL;
}

/* Parse a string */
const char *decode_string(const char **p, Dst *out) {
    DstBuffer *buffer = dst_buffer(0);
    const char *cp = *p;
    while (*cp != '"') {
        uint8_t b = (uint8_t) *cp;
        if (b < 32) return "invalid character in string";
        if (b == '\\') {
            cp++;
            switch(*cp) {
                default:
                    return "unknown string escape";
                case 'b':
                    b = '\b';
                    break;
                case 'f':
                    b = '\f';
                    break;
                case 'n':
                    b = '\n';
                    break;
                case 'r':
                    b = '\r';
                    break;
                case 't':
                    b = '\t';
                    break;
                case '"':
                    b = '"';
                    break;
                case '\\':
                    b = '\\';
                    break;
                case 'u':
                    {
                        /* Get codepoint and check for surrogate pair */
                        uint32_t codepoint;
                        const char *err = decode_utf16_escape(cp + 1, &codepoint);
                        if (err) return err;
                        if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                            return "unexpected utf-16 low surrogate";
                        } else if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            if (cp[5] != '\\') return "expected utf-16 low surrogate pair";
                            if (cp[6] != 'u') return "expected utf-16 low surrogate pair";
                            uint32_t lowsur;
                            const char *err = decode_utf16_escape(cp + 7, &lowsur);
                            if (err) return err;
                            if (lowsur < 0xDC00 || lowsur > 0xDFFF)
                                return "expected utf-16 low surrogate pair";
                            codepoint = ((codepoint - 0xD800) << 10) +
                                (lowsur - 0xDC00) + 0x10000;
                            cp += 11;
                        } else {
                            cp += 5;
                        }
                        /* Write codepoint */
                        if (codepoint <= 0x7F) {
                            dst_buffer_push_u8(buffer, codepoint);
                        } else if (codepoint <= 0x7FF) {
                            dst_buffer_push_u8(buffer, ((codepoint >>  6) & 0x1F) | 0xC0);
                            dst_buffer_push_u8(buffer, ((codepoint >>  0) & 0x3F) | 0x80);
                        } else if (codepoint <= 0xFFFF) {
                            dst_buffer_push_u8(buffer, ((codepoint >> 12) & 0x0F) | 0xE0);
                            dst_buffer_push_u8(buffer, ((codepoint >>  6) & 0x3F) | 0x80);
                            dst_buffer_push_u8(buffer, ((codepoint >>  0) & 0x3F) | 0x80);
                        } else {
                            dst_buffer_push_u8(buffer, ((codepoint >> 18) & 0x07) | 0xF0);
                            dst_buffer_push_u8(buffer, ((codepoint >> 12) & 0x3F) | 0x80);
                            dst_buffer_push_u8(buffer, ((codepoint >>  6) & 0x3F) | 0x80);
                            dst_buffer_push_u8(buffer, ((codepoint >>  0) & 0x3F) | 0x80);
                        }
                    }
                    continue;
            }
        }
        dst_buffer_push_u8(buffer, b);
        cp++;
    }
    *out = dst_stringv(buffer->data, buffer->count);
    *p = cp + 1;
    return NULL;
}

static const char *decode_one(const char **p, Dst *out, int depth) {

    /* Prevent stack overflow */
    if (depth > DST_RECURSION_GUARD) goto recurdepth;

    /* Skip leading whitepspace */
    skipwhite(p);

    /* Main switch */
    switch (**p) {
        default:
            goto badchar;
        case '\0':
            goto eos;
        /* Numbers */
        case '-': case '0': case '1' : case '2': case '3' : case '4':
        case '5': case '6': case '7' : case '8': case '9':
            {
                errno = 0;
                char *end = NULL;
                double x = strtod(*p, &end);
                if (end == *p) goto badnum;
                *p = end;
                *out = dst_wrap_real(x);
                break;
            }
        /* false, null, true */
        case 'f':
            {
                const char *cp = *p;
                if (cp[1] != 'a' || cp[2] != 'l' || cp[3] != 's' || cp[4] != 'e')
                    goto badident;
                *out = dst_wrap_false();
                *p = cp + 5;
                break;
            }
        case 'n':
            {
                const char *cp = *p;

                if (cp[1] != 'u' || cp[2] != 'l' || cp[3] != 'l')
                    goto badident;
                *out = dst_wrap_nil();
                *p = cp + 4;
                break;
            }
        case 't':
            {
                const char *cp = *p;
                if (cp[1] != 'r' || cp[2] != 'u' || cp[3] != 'e')
                    goto badident;
                *out = dst_wrap_true();
                *p = cp + 4;
                break;
            }
        /* String */
        case '"':
            {
                const char *cp = *p + 1;
                const char *start = cp;
                while (*cp >= 32 && *cp != '"' && *cp != '\\')
                    cp++;
                /* Only use a buffer for strings with escapes, else just copy
                 * memory from source */
                if (*cp == '\\') {
                    *p = *p + 1;
                    const char *err = decode_string(p, out);
                    if (err) return err;
                    break;
                }
                if (*cp != '"') goto badchar;
                *p = cp + 1;
                *out = dst_stringv((const uint8_t *)start, cp - start);
                break;
            }
        /* Array */
        case '[':
            {
                *p = *p + 1;
                DstArray *array = dst_array(0);
                const char *err;
                Dst subval;
                skipwhite(p);
                while (**p != ']') {
                    err = decode_one(p, &subval, depth + 1);
                    if (err) return err;
                    dst_array_push(array, subval);
                    skipwhite(p);
                    if (**p == ']') break;
                    if (**p != ',') goto wantcomma;
                    *p = *p + 1;
                }
                *p = *p + 1;
                *out = dst_wrap_array(array);
            }
            break;
        /* Object */
        case '{':
            {
                *p = *p + 1;
                DstTable *table = dst_table(0);
                const char *err;
                Dst subkey, subval;
                skipwhite(p);
                while (**p != '}') {
                    skipwhite(p);
                    if (**p != '"') goto wantstring;
                    err = decode_one(p, &subkey, depth + 1);
                    if (err) return err;
                    skipwhite(p);
                    if (**p != ':') goto wantcolon;
                    *p = *p + 1;
                    err = decode_one(p, &subval, depth + 1);
                    if (err) return err;
                    dst_table_put(table, subkey, subval);
                    skipwhite(p);
                    if (**p == '}') break;
                    if (**p != ',') goto wantcomma;
                    *p = *p + 1;
                }
                *p = *p + 1;
                *out = dst_wrap_table(table);
                break;
            }
    }

    /* Good return */
    return NULL;

    /* Errors */
recurdepth:
    return "recured too deeply";
eos:
    return "unexpected end of source";
badident:
    return "bad identifier";
badnum:
    return "bad number";
wantcomma:
    return "expected comma";
wantcolon:
    return "expected colon";
badchar:
    return "unexpected character";
wantstring:
    return "expected json string";
}

static int json_decode(DstArgs args) {
    Dst ret;
    DST_FIXARITY(args, 1);
    const char *err;
    const char *start;
    const char *p;
    if (dst_checktype(args.v[0], DST_BUFFER)) {
        DstBuffer *buffer = dst_unwrap_buffer(args.v[0]);
        /* Ensure 0 padded */
        dst_buffer_push_u8(buffer, 0);
        start = p = (const char *)buffer->data;
        err = decode_one(&p, &ret, 0);
        buffer->count--;
    } else {
        const uint8_t *bytes;
        int32_t len;
        DST_ARG_BYTES(bytes, len, args, 0);
        start = p = (const char *)bytes;
        err = decode_one(&p, &ret, 0);
    }
    /* Check trailing values */
    if (!err) {
        skipwhite(&p);
        if (*p)
            err = "unexpected extra token";
    }
    if (err) {
        DST_THROWV(args, dst_wrap_string(dst_formatc(
                    "decode error at postion %d: %s",
                    p - start,
                    err)));
    }
    DST_RETURN(args, ret);
}

/*****************/
/* JSON Encoding */
/*****************/

typedef struct {
    DstBuffer *buffer;
    int32_t indent;
    const uint8_t *tab;
    const uint8_t *newline;
    int32_t tablen;
    int32_t newlinelen;
} Encoder;

static const char *encode_newline(Encoder *e) {
    if (dst_buffer_push_bytes(e->buffer, e->newline, e->newlinelen))
        return "buffer overflow";
    for (int32_t i = 0; i < e->indent; i++)
        if (dst_buffer_push_bytes(e->buffer, e->tab, e->tablen))
            return "buffer overflow";
    return NULL;
}

static const char *encode_one(Encoder *e, Dst x, int depth) {
    switch(dst_type(x)) {
        default:
            goto badtype;
        case DST_NIL:
            {
                if (dst_buffer_push_cstring(e->buffer, "null"))
                    goto overflow;
            }
            break;
        case DST_FALSE:
            {
                if (dst_buffer_push_cstring(e->buffer, "false"))
                    goto overflow;
            }
            break;
        case DST_TRUE:
            {
                if (dst_buffer_push_cstring(e->buffer, "true"))
                    goto overflow;
            }
            break;
        case DST_INTEGER:
            {
                char cbuf[20];
                sprintf(cbuf, "%d", dst_unwrap_integer(x));
                if (dst_buffer_push_cstring(e->buffer, cbuf))
                    goto overflow;
            }
            break;
        case DST_REAL:
            {
                char cbuf[25];
                sprintf(cbuf, "%.17g", dst_unwrap_real(x));
                if (dst_buffer_push_cstring(e->buffer, cbuf))
                    goto overflow;
            }
            break;
        case DST_STRING:
        case DST_SYMBOL:
        case DST_BUFFER:
            {
                const uint8_t *bytes;
                const uint8_t *c;
                const uint8_t *end;
                int32_t len;
                dst_bytes_view(x, &bytes, &len);
                if (dst_buffer_push_u8(e->buffer, '"')) goto overflow;
                c = bytes;
                end = bytes + len;
                while (c < end) {

                    /* get codepoint */
                    uint32_t codepoint;
                    if (*c < 0x80) {
                        /* one byte */
                        codepoint = *c++;
                    } else if (*c < 0xE0) {
                        /* two bytes */
                        if (c + 2 > end) goto overflow;
                        codepoint = ((c[0] & 0x1F) << 6) |
                            (c[1] & 0x3F);
                        c += 2;
                    } else if (*c < 0xF0) {
                        /* three bytes */
                        if (c + 3 > end) goto overflow;
                        codepoint = ((c[0] & 0x0F) << 12) |
                            ((c[1] & 0x3F) << 6) |
                            (c[2] & 0x3F);
                        c += 3;
                    } else if (*c < 0xF8) {
                        /* four bytes */
                        if (c + 4 > end) goto overflow;
                        codepoint = ((c[0] & 0x07) << 18) |
                            ((c[1] & 0x3F) << 12) |
                            ((c[3] & 0x3F) << 6) |
                            (c[3] & 0x3F);
                        c += 4;
                    } else {
                        /* invalid */
                        goto invalidutf8;
                    }

                    /* write codepoint */
                    if (codepoint > 0x1F && codepoint < 0x80) {
                        /* Normal, no escape */
                        if (codepoint == '\\' || codepoint == '"')
                            if (dst_buffer_push_u8(e->buffer, '\\'))
                                goto overflow;
                        if (dst_buffer_push_u8(e->buffer, (uint8_t) codepoint))
                            goto overflow;
                    } else if (codepoint < 0x10000) {
                        /* One unicode escape */
                        uint8_t buf[6];
                        buf[0] = '\\';
                        buf[1] = 'u';
                        buf[2] = (codepoint >> 12) & 0xF;
                        buf[3] = (codepoint >> 8) & 0xF;
                        buf[4] = (codepoint >> 4) & 0xF;
                        buf[5] = codepoint & 0xF;
                        if (dst_buffer_push_bytes(e->buffer, buf, sizeof(buf)))
                            goto overflow;
                    } else {
                        /* Two unicode escapes (surrogate pair) */
                        uint32_t hi, lo;
                        uint8_t buf[12];
                        hi = ((codepoint - 0x10000) >> 10) + 0xD800;
                        lo = ((codepoint - 0x10000) & 0x3FF) + 0xDC00;
                        buf[0] = '\\';
                        buf[1] = 'u';
                        buf[2] = (hi >> 12) & 0xF;
                        buf[3] = (hi >> 8) & 0xF;
                        buf[4] = (hi >> 4) & 0xF;
                        buf[5] = hi & 0xF;
                        buf[6] = '\\';
                        buf[7] = 'u';
                        buf[8] = (lo >> 12) & 0xF;
                        buf[9] = (lo >> 8) & 0xF;
                        buf[10] = (lo >> 4) & 0xF;
                        buf[11] = lo & 0xF;
                        if (dst_buffer_push_bytes(e->buffer, buf, sizeof(buf)))
                            goto overflow;
                    }
                }
                if (dst_buffer_push_u8(e->buffer, '"')) goto overflow;
            }
            break;
        case DST_TUPLE:
        case DST_ARRAY:
            {
                const char *err;
                const Dst *items;
                int32_t len;
                dst_indexed_view(x, &items, &len);
                if (dst_buffer_push_u8(e->buffer, '[')) goto overflow;
                e->indent++;
                if ((err = encode_newline(e))) return err;
                for (int32_t i = 0; i < len; i++) {
                    if ((err = encode_newline(e))) return err;
                    if ((err = encode_one(e, items[i], depth + 1)))
                        return err;
                    if (dst_buffer_push_u8(e->buffer, ','))
                        goto overflow;
                }
                e->indent--;
                if (e->buffer->data[e->buffer->count - 1] == ',') {
                    e->buffer->count--;
                    if ((err = encode_newline(e))) return err;
                }
                if (dst_buffer_push_u8(e->buffer, ']')) goto overflow;
            }
            break;
        case DST_TABLE:
        case DST_STRUCT:
            {
                const char *err;
                const DstKV *kvs;
                int32_t count, capacity;
                dst_dictionary_view(x, &kvs, &count, &capacity);
                if (dst_buffer_push_u8(e->buffer, '{')) goto overflow;
                e->indent++;
                for (int32_t i = 0; i < capacity; i++) {
                    if (dst_checktype(kvs[i].key, DST_NIL))
                        continue;
                    if (!dst_checktype(kvs[i].key, DST_STRING))
                        return "only strings keys are allowed in objects";
                    if ((err = encode_newline(e))) return err;
                    if ((err = encode_one(e, kvs[i].key, depth + 1)))
                        return err;
                    const char *sep = e->tablen ? ": " : ":";
                    if (dst_buffer_push_cstring(e->buffer, sep))
                        goto overflow;
                    if ((err = encode_one(e, kvs[i].value, depth + 1)))
                        return err;
                    if (dst_buffer_push_u8(e->buffer, ','))
                        goto overflow;
                }
                e->indent--;
                if (e->buffer->data[e->buffer->count - 1] == ',') {
                    e->buffer->count--;
                    if ((err = encode_newline(e))) return err;
                }
                if (dst_buffer_push_u8(e->buffer, '}')) goto overflow;
            }
            break;
    }
    return NULL;

    /* Errors */
overflow:
    return "buffer overflow";
badtype:
    return "type not supported";
invalidutf8:
    return "string contains invalid utf-8";
}

static int json_encode(DstArgs args) {
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 3);
    Encoder e;
    e.indent = 0;
    e.buffer = dst_buffer(10);
    e.tab = NULL;
    e.newline = NULL;
    e.tablen = 0;
    e.newlinelen = 0;
    if (args.n >= 2) {
        DST_ARG_BYTES(e.tab, e.tablen, args, 1);
        if (args.n >= 3) {
            DST_ARG_BYTES(e.newline, e.newlinelen, args, 2);
        } else {
            e.newline = (const uint8_t *)"\n";
            e.newlinelen = 1;
        }
    }
    const char *err = encode_one(&e, args.v[0], 0);
    if (err) DST_THROW(args, err);
    DST_RETURN_BUFFER(args, e.buffer);
}

/****************/
/* Module Entry */
/****************/

static const DstReg cfuns[] = {
    {"encode", json_encode},
    {"decode", json_decode},
    {NULL, NULL}
};

DST_MODULE_ENTRY(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

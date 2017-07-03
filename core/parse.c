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

#include <gst/gst.h>

static const char UNEXPECTED_CLOSING_DELIM[] = "unexpected closing delimiter";

/* Handle error in parsing */
#define p_error(p, e) ((p)->error = (e), (p)->status = GST_PARSER_ERROR)

/* Get the top ParseState in the parse stack */
static GstParseState *parser_peek(GstParser *p) {
    if (!p->count) {
        return NULL;
    }
    return p->data + (p->count - 1);
}

/* Remove the top state from the ParseStack */
static GstParseState *parser_pop(GstParser * p) {
    if (!p->count) {
        p_error(p, "parser stack underflow");
        return NULL;
    }
    return p->data + --p->count;
}

/* Quote a value */
static GstValue quote(GstParser *p, GstValue x) {
    GstValue *tuple = gst_tuple_begin(p->vm, 2);
    tuple[0] = gst_string_cvs(p->vm, "quote");
    tuple[1] = x;
    return gst_wrap_tuple(gst_tuple_end(p->vm, tuple));
}

/* Add a new, empty ParseState to the ParseStack. */
static void parser_push(GstParser *p, ParseType type, uint8_t character) {
    GstParseState *top;
    if (p->count >= p->cap) {
        uint32_t newCap = 2 * p->count + 2;
        GstParseState *data = gst_alloc(p->vm, newCap * sizeof(GstParseState));
        gst_memcpy(data, p->data, p->cap * sizeof(GstParseState));
        p->data = data;
        p->cap = newCap;
    }
    ++p->count;
    top = parser_peek(p);
    if (!top) return;
    top->type = type;
    top->quoteCount = p->quoteCount;
    p->quoteCount = 0;
    switch (type) {
        case PTYPE_STRING:
            top->buf.string.state = STRING_STATE_BASE;
            top->buf.string.buffer = gst_buffer(p->vm, 10);
            break;
        case PTYPE_TOKEN:
            top->buf.string.buffer = gst_buffer(p->vm, 10);
            break;
        case PTYPE_FORM:
            top->buf.form.array = gst_array(p->vm, 10);
            if (character == '(') top->buf.form.endDelimiter = ')';
            if (character == '[') top->buf.form.endDelimiter = ']';
            if (character == '{') top->buf.form.endDelimiter = '}';
            break;
    }
}

/* Append a value to the top-most state in the Parser's stack. */
static void parser_append(GstParser *p, GstValue x) {
    GstParseState *oldtop = parser_pop(p);
    GstParseState *top = parser_peek(p);
    while (oldtop->quoteCount--)
        x = quote(p, x);
    if (!top) {
        p->value = x;
        p->status = GST_PARSER_FULL;
        return;
    }
    switch (top->type) {
        case PTYPE_FORM:
            gst_array_push(p->vm, top->buf.form.array, x);
            break;
        default:
            p_error(p, "expected container type");
            break;
    }
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

/* Checks if a string slice is equal to a string constant */
static int check_str_const(const char *ref, const uint8_t *start, const uint8_t *end) {
    while (*ref && start < end) {
        if (*ref != *(char *)start) return 0;
        ++ref;
        ++start;
    }
    return !*ref && start == end;
}

/* Build from the token buffer */
static GstValue build_token(GstParser *p, GstBuffer *buf) {
    GstValue x;
    GstReal real;
    GstInteger integer;
    uint8_t *data = buf->data;
    uint8_t *back = data + buf->count;
    if (gst_read_integer(data, back, &integer)) {
        x.type = GST_INTEGER;
        x.data.integer = integer;
    } else if (gst_read_real(data, back, &real, 0)) {
        x.type = GST_REAL;
        x.data.real = real;
    } else if (check_str_const("nil", data, back)) {
        x.type = GST_NIL;
        x.data.boolean = 0;
    } else if (check_str_const("false", data, back)) {
        x.type = GST_BOOLEAN;
        x.data.boolean = 0;
    } else if (check_str_const("true", data, back)) {
        x.type = GST_BOOLEAN;
        x.data.boolean = 1;
    } else {
        if (buf->data[0] >= '0' && buf->data[0] <= '9') {
            p_error(p, "symbols cannot start with digits");
            x.type = GST_NIL;
        } else {
            if (buf->data[0] == ':' && buf->count >= 2) {
                x.type = GST_STRING;
                x.data.string = gst_string_b(p->vm, buf->data + 1, buf->count - 1);
            } else {
                x.type = GST_SYMBOL;
                x.data.string = gst_buffer_to_string(p->vm, buf);
            }
        }
    }
    return x;
}

/* Handle parsing a token */
static int token_state(GstParser *p, uint8_t c) {
    GstParseState *top = parser_peek(p);
    GstBuffer *buf = top->buf.string.buffer;
    if (is_whitespace(c) || c == ')' || c == ']' || c == '}') {
        parser_append(p, build_token(p, buf));
        return !(c == ')' || c == ']' || c == '}');
    } else if (is_symbol_char(c)) {
        gst_buffer_push(p->vm, buf, c);
        return 1;
    } else {
        p_error(p, "expected symbol character");
        return 1;
    }
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

/* Handle parsing a string literal */
static int string_state(GstParser *p, uint8_t c) {
    int digit;
    GstParseState *top = parser_peek(p);
    switch (top->buf.string.state) {
        case STRING_STATE_BASE:
            if (c == '\\') {
                top->buf.string.state = STRING_STATE_ESCAPE;
            } else if (c == '"') {
                GstValue x;
                x.type = GST_STRING;
                x.data.string = gst_buffer_to_string(p->vm, top->buf.string.buffer);
                parser_append(p, x);
            } else {
                gst_buffer_push(p->vm, top->buf.string.buffer, c);
            }
            break;
        case STRING_STATE_ESCAPE:
            {
                uint8_t next;
                switch (c) {
                    case 'n': next = '\n'; break;
                    case 'r': next = '\r'; break;
                    case 't': next = '\t'; break;
                    case 'f': next = '\f'; break;
                    case '0': next = '\0'; break;
                    case '"': next = '"'; break;
                    case '\'': next = '\''; break;
                    case 'z': next = '\0'; break;
                    case 'e': next = 27; break;
                    case 'h':
                        top->buf.string.state = STRING_STATE_ESCAPE_HEX;
                        top->buf.string.count = 0;
                        top->buf.string.accum = 0;
                        return 1;
                    case 'u':
                        top->buf.string.state = STRING_STATE_ESCAPE_HEX;
                        top->buf.string.count = 0;
                        top->buf.string.accum = 0;
                        return 1;
                    default:
                        p_error(p, "unknown string escape sequence");
                        return 1;
                }
                gst_buffer_push(p->vm, top->buf.string.buffer, next);
                top->buf.string.state = STRING_STATE_BASE;
            }
            break;
        case STRING_STATE_ESCAPE_HEX:
            digit = to_hex(c);
            if (digit < 0) {
                p_error(p, "invalid hexidecimal digit");
                return 1;
            } else {
                top->buf.string.accum *= 16;
                top->buf.string.accum += digit;
            }
            top->buf.string.accum += digit;
            if (++top->buf.string.count == 2) {
                gst_buffer_push(p->vm, top->buf.string.buffer, top->buf.string.accum);
                top->buf.string.state = STRING_STATE_BASE;
            }
            break;
        case STRING_STATE_ESCAPE_UNICODE:
            break;
    }
    return 1;
}

/* Root state of the parser */
static int root_state(GstParser *p, uint8_t c) {
    if (is_whitespace(c)) return 1;
    p->status = GST_PARSER_PENDING;
    if (c == ']' || c == ')' || c == '}') {
        p_error(p, UNEXPECTED_CLOSING_DELIM);
        return 1;
    }
    if (c == '(' || c == '[' || c == '{') {
        parser_push(p, PTYPE_FORM, c);
        return 1;
    }
    if (c == '"') {
        parser_push(p, PTYPE_STRING, c);
        return 1;
    }
    if (c == '\'') {
        p->quoteCount++;
        return 1;
    }
    if (is_symbol_char(c)) {
        parser_push(p, PTYPE_TOKEN, c);
        return 0;
    }
    p_error(p, "unexpected character");
    return 1;
}

/* Handle parsing a form */
static int form_state(GstParser *p, uint8_t c) {
    GstParseState *top = parser_peek(p);
    if (c == top->buf.form.endDelimiter) {
        GstArray *array = top->buf.form.array;
        GstValue x;
        if (c == ']') {
            x.type = GST_ARRAY;
            x.data.array = array;
        } else if (c == ')') {
            GstValue *tup;
            tup = gst_tuple_begin(p->vm, array->count);
            gst_memcpy(tup, array->data, array->count * sizeof(GstValue));
            x.type = GST_TUPLE;
            x.data.tuple = gst_tuple_end(p->vm, tup);
        } else { /* c == '{' */
            uint32_t i;
            if (array->count % 2 != 0) {
                p_error(p, "table literal must have even number of elements");
                return 1;
            }
            x.type = GST_TABLE;
            x.data.table = gst_table(p->vm, array->count);
            for (i = 0; i < array->count; i += 2) {
                gst_table_put(p->vm, x.data.table, array->data[i], array->data[i + 1]);
            }
        }
        parser_append(p, x);
        return 1;
    }
    return root_state(p, c);
}

/* Handle a character */
void gst_parse_byte(GstParser *p, uint8_t c) {
    int done = 0;
    /* Update position in source */
    if (c == '\n') {
        p->line++;
        p->index = 0;
        p->comment = GST_PCOMMENT_EXPECTING;
    } else {
        ++p->index;
    }
    /* Check comments */
    switch (p->comment) {
        case GST_PCOMMENT_NOT:
            break;
        case GST_PCOMMENT_EXPECTING:
            if (c == '#') {
                p->comment = GST_PCOMMENT_INSIDE;
                return;
            } else if (!is_whitespace(c)) {
                p->comment = GST_PCOMMENT_NOT;
            }
            break;
        case GST_PCOMMENT_INSIDE:
            return;
    }
    /* Dispatch character to state */
    while (!done) {
        GstParseState *top = parser_peek(p);
        if (!top) {
            done = root_state(p, c);
        } else {
            switch (top->type) {
                case PTYPE_TOKEN:
                    done = token_state(p, c);
                    break;
                case PTYPE_FORM:
                    done = form_state(p, c);
                    break;
                case PTYPE_STRING:
                    done = string_state(p, c);
                    break;
            }
        }
    }
}

/* Parse a C style string. The first value encountered when parsed is put
 * in p->value. The string variable is then updated to the next char that
 * was not read. Returns 1 if any values were read, otherwise returns 0.
 * Returns the number of bytes read.
 */
int gst_parse_cstring(GstParser *p, const char *string) {
    int bytesRead = 0;
    if (!string)
        return 0;
    while ((p->status == GST_PARSER_PENDING || p->status == GST_PARSER_ROOT)
            && (string[bytesRead] != '\0')) {
        gst_parse_byte(p, string[bytesRead++]);
    }
    return bytesRead;
}

/* Parse a gst string */
int gst_parse_string(GstParser *p, const uint8_t *string) {
    uint32_t i;
    if (!string)
        return 0;
    for (i = 0; i < gst_string_length(string); ++i) {
        if (p->status != GST_PARSER_PENDING && p->status != GST_PARSER_ROOT) break;
        gst_parse_byte(p, string[i]);
    }
    return i;
}

/* Check if a parser has a value that needs to be handled. If
 * so, the parser will not parse any more input until that value
 * is consumed. */
int gst_parse_hasvalue(GstParser *p) {
    return p->status == GST_PARSER_FULL;
}

/* Gets a value from the parser */
GstValue gst_parse_consume(GstParser *p) {
    p->status = GST_PARSER_ROOT;
    return p->value;
}

/* Parser initialization (memory allocation) */
void gst_parser(GstParser *p, Gst *vm) {
    p->vm = vm;
    GstParseState *data = gst_alloc(vm, sizeof(GstParseState) * 10);
    p->cap = 10;
    p->data = data;
    p->count = 0;
    p->index = 0;
    p->line = 1;
    p->quoteCount = 0;
    p->error = NULL;
    p->status = GST_PARSER_ROOT;
    p->value.type = GST_NIL;
    p->comment = GST_PCOMMENT_EXPECTING;
}
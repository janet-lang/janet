#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "datatypes.h"
#include "ds.h"
#include "parse.h"
#include "vm.h"

static const char UNEXPECTED_CLOSING_DELIM[] = "Unexpected closing delimiter";

/* The type of a ParseState */
typedef enum ParseType {
    PTYPE_ROOT,
    PTYPE_ARRAY,
    PTYPE_FORM,
    PTYPE_DICTIONARY,
    PTYPE_STRING,
    PTYPE_TOKEN
} ParseType;

/* Contain a parse state that goes on the parse stack */
struct ParseState {
    ParseType type;
    union {
        Array * array;
        struct {
            Dictionary * dict;
            Value key;
            int keyFound;
        } dictState;
        struct {
            Buffer * buffer;
            enum {
                STRING_STATE_BASE,
                STRING_STATE_ESCAPE,
                STRING_STATE_ESCAPE_UNICODE,
                STRING_STATE_ESCAPE_HEX
            } state;
        } string;
    } buf;
};

/* Handle error in parsing */
#define PError(p, e) ((p)->error = (e), (p)->status = PARSER_ERROR)

/* Get the top ParseState in the parse stack */
static ParseState * ParserPeek(Parser * p) {
    if (!p->count) {
        PError(p, "Parser stack underflow. (Peek)");
        return NULL;
    }
    return p->data + p->count - 1;
}

/* Remove the top state from the ParseStack */
static ParseState * ParserPop(Parser * p) {
    if (!p->count) {
        PError(p, "Parser stack underflow. (Pop)");
        return NULL;
    }
    return p->data + --p->count;
}

/* Add a new, empty ParseState to the ParseStack. */
static void ParserPush(Parser *p, ParseType type) {
    ParseState * top;
    if (p->count >= p->cap) {
        uint32_t newCap = 2 * p->count;
        ParseState * data = VMAlloc(p->vm, newCap);
        p->data = data;
        p->cap = newCap;
    }
    ++p->count;
    top = ParserPeek(p);
    if (!top) return;
    top->type = type;
    switch (type) {
        case PTYPE_ROOT:
            break;
        case PTYPE_STRING:
            top->buf.string.state = STRING_STATE_BASE;
        case PTYPE_TOKEN:
            top->buf.string.buffer = BufferNew(p->vm, 10);
            break;
        case PTYPE_ARRAY:
        case PTYPE_FORM:
            top->buf.array = ArrayNew(p->vm, 10);
            break;
        case PTYPE_DICTIONARY:
            top->buf.dictState.dict = DictNew(p->vm, 10);
            top->buf.dictState.keyFound = 0;
            break;
    }
}

/* Append a value to the top-most state in the Parser's stack. */
static void ParserTopAppend(Parser * p, Value x) {
    ParseState * top = ParserPeek(p);
    if (!top) return;
    switch (top->type) {
        case PTYPE_ROOT:
            p->value = x;
            p->status = PARSER_FULL;
            break;
        case PTYPE_ARRAY:
        case PTYPE_FORM:
            ArrayPush(p->vm, top->buf.array, x);
            break;
        case PTYPE_DICTIONARY:
            if (top->buf.dictState.keyFound) {
                DictPut(p->vm, top->buf.dictState.dict, top->buf.dictState.key, x);
            } else {
                top->buf.dictState.key = x;
            }
            top->buf.dictState.keyFound = !top->buf.dictState.keyFound;
            break;
        default:
            PError(p, "Expected container type.");
            break;
    }
}

/* Check if a character is whitespace */
static int isWhitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0' || c == ',';
}

/* Check if a character is a valid symbol character */
static int isSymbolChar(uint8_t c) {
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

/* Read a number from a string */
static int ParseReadNumber(const uint8_t * string, const uint8_t * end, double * ret, int forceInt) {
    int sign = 1, x = 0;
    double accum = 0, exp = 1, place = 1;
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
            ++string;
            if (!ParseReadNumber(string, end, &exp, 1))
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
static int checkStrConst(const char * ref, const uint8_t * start, const uint8_t * end) {
    while (*ref && start < end) {
        if (*ref != *(char *)start) return 0;
        ++ref;
        ++start;
    }
    return !*ref && start == end;
}

/* Handle parsing generic input */
static int ParserMainState(Parser * p, uint8_t c) {
    if (c == '(') {
        ParserPush(p, PTYPE_FORM);
        return 1;
    }
    if (c == '[') {
        ParserPush(p, PTYPE_ARRAY);
        return 1;
    }
    if (c == '{') {
        ParserPush(p, PTYPE_DICTIONARY);
        return 1;
    }
    if (c == '"') {
        ParserPush(p, PTYPE_STRING);
        return 1;
    }
    if (isWhitespace(c)) return 1;
    if (isSymbolChar(c)) {
        ParserPush(p, PTYPE_TOKEN);
        return 0;
    }
    PError(p, "Unexpected character.");
    return 1;
}

/* Build from the token buffer */
static Value ParserBuildTokenBuffer(Parser * p, Buffer * buf) {
    Value x;
    Number number;
    uint8_t * data = buf->data;
    uint8_t * back = data + buf->count;
    if (ParseReadNumber(data, back, &number, 0)) {
        x.type = TYPE_NUMBER;
        x.data.number = number;
    } else if (checkStrConst("nil", data, back)) {
        x.type = TYPE_NIL;
        x.data.boolean = 0;
    } else if (checkStrConst("false", data, back)) {
        x.type = TYPE_BOOLEAN;
        x.data.boolean = 0;
    } else if (checkStrConst("true", data, back)) {
        x.type = TYPE_BOOLEAN;
        x.data.boolean = 1;
    } else {
        if (buf->data[0] >= '0' && buf->data[0] <= '9') {
            PError(p, "Symbols cannot start with digits.");
            x.type = TYPE_NIL;
        } else {
            x.type = TYPE_SYMBOL;
            x.data.string = BufferToString(p->vm, buf);
        }
    }
    return x;
}

/* Handle parsing a token */
static int ParserTokenState(Parser * p, uint8_t c) {
    ParseState * top = ParserPeek(p);
    Buffer * buf = top->buf.string.buffer;
    if (isWhitespace(c) || c == ')' || c == ']' || c == '}') {
        ParserPop(p);
        ParserTopAppend(p, ParserBuildTokenBuffer(p, buf));
        return !(c == ')' || c == ']' || c == '}');
    } else if (isSymbolChar(c)) {
        BufferPush(p->vm, buf, c);
        return 1;
    } else {
        PError(p, "Expected symbol character.");
        return 1;
    }
}

/* Handle parsing a string literal */
static int ParserStringState(Parser * p, uint8_t c) {
    ParseState * top = ParserPeek(p);
    switch (top->buf.string.state) {
        case STRING_STATE_BASE:
            if (c == '\\') {
                top->buf.string.state = STRING_STATE_ESCAPE;
            } else if (c == '"') {
                Value x;
                x.type = TYPE_STRING;
                x.data.string = BufferToString(p->vm, top->buf.string.buffer);
                ParserPop(p);
                ParserTopAppend(p, x);
            } else {
                BufferPush(p->vm, top->buf.string.buffer, c);
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
                    default:
                              PError(p, "Unknown string escape sequence.");
                              return 1;
                }
                BufferPush(p->vm, top->buf.string.buffer, next);
                top->buf.string.state = STRING_STATE_BASE;
            }
            break;
        case STRING_STATE_ESCAPE_HEX:
            break;
        case STRING_STATE_ESCAPE_UNICODE:
            break;
    }
    return 1;
}

/* Handle parsing a form */
static int ParserFormState(Parser * p, uint8_t c) {
    if (c == ')') {
        ParseState * top = ParserPop(p);
        Array * array = top->buf.array;
        Value x;
        x.type = TYPE_FORM;
        x.data.array = array;
        ParserTopAppend(p, x);
        return 1;
    } else if (c == ']' || c == '}') {
        PError(p, UNEXPECTED_CLOSING_DELIM);
        return 1;
    } else {
        return ParserMainState(p, c);
    }
}

/* Handle parsing an array */
static int ParserArrayState(Parser * p, uint8_t c) {
    if (c == ']') {
        ParseState * top = ParserPop(p);
        Array * array = top->buf.array;
        Value x;
        x.type = TYPE_ARRAY;
        x.data.array = array;
        ParserTopAppend(p, x);
        return 1;
    } else if (c == ')' || c == '}') {
        PError(p, UNEXPECTED_CLOSING_DELIM);
        return 1;
    } else {
        return ParserMainState(p, c);
    }
}

/* Handle parsing a dictionary */
static int ParserDictState(Parser * p, uint8_t c) {
    if (c == '}') {
        ParseState * top = ParserPop(p);
        if (!top->buf.dictState.keyFound) {
            Value x;
            x.type = TYPE_DICTIONARY;
            x.data.dict = top->buf.dictState.dict;
            ParserTopAppend(p, x);
            return 1;
        } else {
            PError(p, "Odd number of items in dictionary literal.");
            return 1;
        }
    } else if (c == ')' || c == ']') {
        PError(p, UNEXPECTED_CLOSING_DELIM);
        return 1;
    } else {
        return ParserMainState(p, c);
    }
}

/* Root state of the parser */
static int ParserRootState(Parser * p, uint8_t c) {
    if (c == ']' || c == ')' || c == '}') {
        PError(p, UNEXPECTED_CLOSING_DELIM);
        return 1;
    } else {
        return ParserMainState(p, c);
    }
}

/* Handle a character */
static int ParserDispatchChar(Parser * p, uint8_t c) {
    int done = 0;
    while (!done && p->status == PARSER_PENDING) {
        ParseState * top = ParserPeek(p);
        switch (top->type) {
            case PTYPE_ROOT:
                done = ParserRootState(p, c);
                break;
            case PTYPE_TOKEN:
                done = ParserTokenState(p, c);
                break;
            case PTYPE_FORM:
                done = ParserFormState(p, c);
                break;
            case PTYPE_ARRAY:
                done = ParserArrayState(p, c);
                break;
            case PTYPE_STRING:
                done = ParserStringState(p, c);
                break;
            case PTYPE_DICTIONARY:
                done = ParserDictState(p, c);
                break;
        }
    }
    ++p->index;
    return !done;
}

/* Parse a C style string. The first value encountered when parsed is put
 * in p->value. The string variable is then updated to the next char that
 * was not read. Returns 1 if any values were read, otherwise returns 0.
 * Returns the number of bytes read.
 */
int ParserParseCString(Parser * p, const char * string) {
    int bytesRead = 0;
    p->status = PARSER_PENDING;
    while ((p->status == PARSER_PENDING) && (string[bytesRead] != '\0')) {
        ParserDispatchChar(p, string[bytesRead++]);
    }
    return bytesRead;
}

/* Parser initialization (memory allocation) */
void ParserInit(Parser * p, VM * vm) {
    p->vm = vm;
    ParseState * data = VMAlloc(vm, sizeof(ParseState) * 10);
    p->data = data;
    p->count = 0;
    p->cap = 10;
    p->index = 0;
    p->error = NULL;
    p->status = PARSER_PENDING;
    p->value.type = TYPE_NIL;
    ParserPush(p, PTYPE_ROOT);
}

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

#include <janet/janet.h>
#include <string.h>
#include "util.h"
#include "vector.h"

/*
 * Runtime
 */

/* opcodes for peg vm */
typedef enum {
    RULE_LITERAL,      /* [len, bytes...] */
    RULE_NCHAR,        /* [n] */
    RULE_NOTNCHAR,     /* [n] */
    RULE_RANGE,        /* [lo | hi << 16 (1 word)] */
    RULE_SET,          /* [bitmap (8 words)] */
    RULE_LOOK,         /* [offset, rule] */
    RULE_CHOICE,       /* [len, rules...] */
    RULE_SEQUENCE,     /* [len, rules...] */
    RULE_IFNOT,        /* [rule_a, rule_b (a if not b)] */
    RULE_NOT,          /* [rule] */
    RULE_BETWEEN,      /* [lo, hi, rule] */
    RULE_CAPTURE,      /* [rule] */
    RULE_POSITION,     /* [] */
    RULE_ARGUMENT,     /* [argument-index] */
    RULE_REPINDEX,     /* [capture-index] */
    RULE_BACKINDEX,    /* [capture-index] */
    RULE_CONSTANT,     /* [constant] */
    RULE_SUBSTITUTE,   /* [rule] */
    RULE_GROUP,        /* [rule] */
    RULE_REPLACE,      /* [rule, constant] */
    RULE_MATCHTIME,    /* [rule, constant] */
} Opcode;

/* Hold captured patterns and match state */
typedef struct {
    const uint8_t *text_start;
    const uint8_t *text_end;
    const uint8_t *subst_end;
    const uint32_t *bytecode;
    const Janet *constants;
    JanetArray *captures;
    JanetBuffer *scratch;
    const Janet *extrav;
    int32_t extrac;
    int32_t depth;
    enum {
        PEG_MODE_NORMAL,
        PEG_MODE_SUBSTITUTE,
        PEG_MODE_NOCAPTURE
    } mode;
} PegState;

/* Allow backtrack with captures. We need
 * to save state at branches, and then reload
 * if one branch fails and try a new branch. */
typedef struct {
    const uint8_t *subst_end;
    int32_t cap;
    int32_t scratch;
} CapState;

/* Save the current capture state */
static CapState cap_save(PegState *s) {
    CapState cs;
    cs.subst_end = s->subst_end;
    cs.scratch = s->scratch->count;
    cs.cap = s->captures->count;
    return cs;
}

/* Load a saved capture state in the case of failure */
static void cap_load(PegState *s, CapState cs) {
    s->subst_end = cs.subst_end;
    s->scratch->count = cs.scratch;
    s->captures->count = cs.cap;
}

/* Add a capture */
static void pushcap(PegState *s,
        Janet capture,
        const uint8_t *text,
        const uint8_t *result) {
    if (s->mode == PEG_MODE_SUBSTITUTE) {
        janet_buffer_push_bytes(s->scratch, s->subst_end,
                (int32_t)(text - s->subst_end));
        janet_to_string_b(s->scratch, capture);
        s->subst_end = result;
    } else if (s->mode == PEG_MODE_NORMAL) {
        janet_array_push(s->captures, capture);
    }
}

/* Prevent stack overflow */
#define down1(s) do { \
    if (0 == --((s)->depth)) janet_panic("peg/match recursed too deeply"); \
} while (0)
#define up1(s) ((s)->depth++)

/* Evaluate a peg rule */
static const uint8_t *peg_rule(
        PegState *s,
        const uint32_t *rule,
        const uint8_t *text) {
tail:
    switch(*rule & 0x1F) {
        default:
            janet_panic("unexpected opcode");
            return NULL;
        case RULE_LITERAL:
            {
                uint32_t len = rule[1];
                if (text + len > s->text_end) return NULL;
                return memcmp(text, rule + 2, len) ? NULL : text + len;
            }
        case RULE_NCHAR:
            {
                uint32_t n = rule[1];
                return (text + n > s->text_end) ? NULL : text + n;
            }
        case RULE_NOTNCHAR:
            {
                uint32_t n = rule[1];
                return (text + n > s->text_end) ? text : NULL;
            }
        case RULE_RANGE:
            {
                uint8_t lo = rule[1] & 0xFF;
                uint8_t hi = (rule[1] >> 16) & 0xFF;
                return (text < s->text_end &&
                        text[0] >= lo &&
                        text[0] <= hi)
                    ? text + 1
                    : NULL;
            }
        case RULE_SET:
            {
                uint32_t word = rule[1 + (text[0] >> 5)];
                uint32_t mask = (uint32_t)1 << (text[0] & 0x1F);
                return (text < s->text_end && (word & mask))
                    ? text + 1
                    : NULL;
            }
        case RULE_LOOK:
            {
                text += ((int32_t *)rule)[1];
                if (text < s->text_start || text > s->text_end) return NULL;
                int oldmode = s->mode;
                s->mode = PEG_MODE_NOCAPTURE;
                down1(s);
                const uint8_t *result = peg_rule(s, s->bytecode + rule[2], text);
                up1(s);
                s->mode = oldmode;
                return result ? text : NULL;
            }
        case RULE_CHOICE:
            {
                uint32_t len = rule[1];
                const uint32_t *args = rule + 2;
                if (len == 0) return NULL;
                down1(s);
                CapState cs = cap_save(s);
                for (uint32_t i = 0; i < len - 1; i++) {
                    const uint8_t *result = peg_rule(s, s->bytecode + args[i], text);
                    if (result) {
                        up1(s);
                        return result;
                    }
                    cap_load(s, cs);
                }
                up1(s);
                rule = s->bytecode + args[len - 1];
                goto tail;
            }
        case RULE_SEQUENCE:
            {
                uint32_t len = rule[1];
                const uint32_t *args = rule + 2;
                if (len == 0) return text;
                down1(s);
                for (uint32_t i = 0; text && i < len - 1; i++)
                    text = peg_rule(s, s->bytecode + args[i], text);
                up1(s);
                if (!text) return NULL;
                rule = s->bytecode + args[len - 1];
                goto tail;
            }
        case RULE_IFNOT:
            {
                const uint32_t *rule_a = s->bytecode + rule[1];
                const uint32_t *rule_b = s->bytecode + rule[2];
                int oldmode = s->mode;
                s->mode = PEG_MODE_NOCAPTURE;
                down1(s);
                const uint8_t *result = peg_rule(s, rule_b, text);
                up1(s);
                s->mode = oldmode;
                if (result) return NULL;
                rule = rule_a;
                goto tail;
            }
        case RULE_NOT:
            {
                const uint32_t *rule_a = s->bytecode + rule[1];
                int oldmode = s->mode;
                s->mode = PEG_MODE_NOCAPTURE;
                down1(s);
                const uint8_t *result = peg_rule(s, rule_a, text);
                up1(s);
                s->mode = oldmode;
                return (result) ? NULL : text;
            }
        case RULE_BETWEEN:
            {
                uint32_t lo = rule[1];
                uint32_t hi = rule[2];
                const uint32_t *rule_a = s->bytecode + rule[3];
                uint32_t captured = 0;
                const uint8_t *next_text;
                CapState cs = cap_save(s);
                down1(s);
                while (captured < hi && (next_text = peg_rule(s, rule_a, text))) {
                    captured++;
                    text = next_text;
                }
                up1(s);
                if (captured < lo) {
                    cap_load(s, cs);
                    return NULL;
                }
                return text;
            }
        case RULE_POSITION:
            {
                pushcap(s, janet_wrap_number((double)(text - s->text_start)), text, text);
                return text;
            }
        case RULE_ARGUMENT:
            {
                int32_t index = ((int32_t *)rule)[1];
                Janet capture = (index >= s->extrac) ? janet_wrap_nil() : s->extrav[index];
                pushcap(s, capture, text, text);
                return text;
            }
        case RULE_REPINDEX:
            {
                int32_t index = ((int32_t *)rule)[1];
                if (index >= s->captures->count)
                    janet_panic("invalid capture index");
                Janet capture = s->captures->data[index];
                pushcap(s, capture, text, text);
                return text;
            }
        case RULE_BACKINDEX:
            {
                int32_t index = ((int32_t *)rule)[1];
                if (index >= s->captures->count)
                    janet_panic("invalid capture index");
                Janet capture = s->captures->data[s->captures->count - 1 - index];
                pushcap(s, capture, text, text);
                return text;
            }
        case RULE_CONSTANT:
            {
                pushcap(s, s->constants[rule[1]], text, text);
                return text;
            }
        case RULE_CAPTURE:
            {
                int oldmode = s->mode;
                if (oldmode == PEG_MODE_NOCAPTURE) {
                    rule = s->bytecode + rule[1];
                    goto tail;
                }
                if (oldmode == PEG_MODE_SUBSTITUTE) s->mode = PEG_MODE_NOCAPTURE;
                down1(s);
                const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
                up1(s);
                s->mode = oldmode;
                if (!result) return NULL;
                if (oldmode != PEG_MODE_SUBSTITUTE)
                    pushcap(s, janet_stringv(text, (int32_t)(result - text)), text, result);
                return result;
            }
        case RULE_SUBSTITUTE:
        case RULE_GROUP:
        case RULE_REPLACE:
            {
                /* In no-capture mode, all captures simply become their matching pattern */
                int oldmode = s->mode;
                if (oldmode == PEG_MODE_NOCAPTURE) {
                    rule = s->bytecode + rule[1];
                    goto tail;
                }

                /* Save previous state. Will use this to reload state before
                 * pushing grammar. Each of these rules pushes exactly 1 new
                 * capture, regardless of the sub rule. */
                CapState cs = cap_save(s);

                /* Set sub mode as needed. Modes affect how captures are recorded (pushed to stack,
                 * pushed to byte buffer, or ignored) */
                if (rule[0] == RULE_GROUP) s->mode = PEG_MODE_NORMAL;
                if (rule[0] == RULE_REPLACE) s->mode = PEG_MODE_NORMAL;
                if (rule[0] == RULE_SUBSTITUTE) {
                    s->mode = PEG_MODE_SUBSTITUTE;
                    s->subst_end = text;
                }

                down1(s);
                const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
                up1(s);
                s->mode = oldmode;
                if (!result) return NULL;

                /* The replacement capture */
                Janet cap;

                /* Figure out what to push based on opcode */
                if (rule[0] == RULE_GROUP) {
                    int32_t num_sub_captures = s->captures->count - cs.cap;
                    JanetArray *sub_captures = janet_array(num_sub_captures);
                    memcpy(sub_captures->data,
                            s->captures->data + cs.cap,
                            sizeof(Janet) * num_sub_captures);
                    sub_captures->count = num_sub_captures;
                    cap = janet_wrap_array(sub_captures);

                } else if (rule[0] == RULE_SUBSTITUTE) {
                    janet_buffer_push_bytes(s->scratch, s->subst_end,
                            (int32_t)(result - s->subst_end));
                    cap = janet_stringv(s->scratch->data + cs.scratch,
                            s->scratch->count - cs.scratch);

                } else { /* RULE_REPLACE */
                    Janet constant = s->constants[rule[2]];
                    int32_t nbytes = (int32_t)(result - text);
                    switch (janet_type(constant)) {
                        default:
                            cap = constant;
                            break;
                        case JANET_STRUCT:
                            cap = janet_struct_get(janet_unwrap_struct(constant),
                                    janet_stringv(text, nbytes));
                            break;
                        case JANET_TABLE:
                            cap = janet_table_get(janet_unwrap_table(constant),
                                    janet_stringv(text, nbytes));
                            break;
                        case JANET_CFUNCTION:
                            janet_array_push(s->captures,
                                    janet_stringv(text, nbytes));
                            JanetCFunction cfunc = janet_unwrap_cfunction(constant);
                            cap = cfunc(s->captures->count - cs.cap,
                                    s->captures->data + cs.cap);
                            break;
                        case JANET_FUNCTION:
                            janet_array_push(s->captures,
                                    janet_stringv(text, nbytes));
                            cap = janet_call(janet_unwrap_function(constant),
                                    s->captures->count - cs.cap,
                                    s->captures->data + cs.cap);
                            break;
                    }
                }

                /* Reset old state and then push capture */
                cap_load(s, cs);
                pushcap(s, cap, text, result);
                return result;
            }
        case RULE_MATCHTIME:
            {
                int oldmode = s->mode;
                CapState cs = cap_save(s);
                s->mode = PEG_MODE_NORMAL;
                down1(s);
                const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
                up1(s);
                s->mode = oldmode;
                if (!result) return NULL;

                /* Add matched text */
                pushcap(s, janet_stringv(text, (int32_t)(result - text)), text, result);

                /* Now check captures with provided function */
                int32_t argc = s->captures->count - cs.cap;
                Janet *argv = s->captures->data + cs.cap;
                Janet fval = s->constants[rule[2]];
                Janet cap;
                if (janet_checktype(fval, JANET_FUNCTION)) {
                    cap = janet_call(janet_unwrap_function(fval), argc, argv);
                } else {
                    JanetCFunction cfun = janet_unwrap_cfunction(fval);
                    cap = cfun(argc, argv);
                }

                /* Capture failed */
                if (janet_checktype(cap, JANET_NIL)) return NULL;

                /* Capture worked, so use new capture */
                cap_load(s, cs);
                pushcap(s, cap, text, result);

                return result;
            }
    }
}

/*
 * Compilation
 */

typedef struct {
    JanetTable *grammar;
    JanetTable *memoized;
    Janet *constants;
    uint32_t *bytecode;
    Janet form;
    int depth;
} Builder;

/* Forward declaration to allow recursion */
static uint32_t compile1(Builder *b, Janet peg);

/*
 * Emission
 */

static uint32_t emit_constant(Builder *b, Janet c) {
    uint32_t cindex = (uint32_t) janet_v_count(b->constants);
    janet_v_push(b->constants, c);
    return cindex;
}

/* Reserve space in bytecode for a rule. When a special emits a rule,
 * it must place that rule immediately on the bytecode stack. This lets
 * the compiler know where the rule is going to be before it is complete,
 * allowing recursive rules. */
typedef struct {
    Builder *builder;
    uint32_t index;
    int32_t size;
} Reserve;

static Reserve reserve(Builder *b, int32_t size) {
    Reserve r;
    r.index = janet_v_count(b->bytecode);
    r.builder = b;
    r.size = size;
    for (int32_t i = 0; i < size; i++)
        janet_v_push(b->bytecode, 0);
    return r;
}

/* Emit a rule in the builder. Returns the index of the new rule */
static void emit_rule(Reserve r, int32_t op, int32_t n, const uint32_t *body) {
    janet_assert(r.size == n + 1, "bad reserve");
    r.builder->bytecode[r.index] = op;
    memcpy(r.builder->bytecode + r.index + 1, body, n * sizeof(uint32_t));
}

/* For RULE_LITERAL */
static void emit_bytes(Builder *b, uint32_t op, int32_t len, const uint8_t *bytes) {
    uint32_t next_rule = janet_v_count(b->bytecode);
    janet_v_push(b->bytecode, op);
    janet_v_push(b->bytecode, len);
    int32_t words = ((len + 3) >> 2);
    for (int32_t i = 0; i < words; i++)
        janet_v_push(b->bytecode, 0);
    memcpy(b->bytecode + next_rule + 2, bytes, len);
}

/* For fixed arity rules of arities 1, 2, and 3 */
static void emit_1(Reserve r, uint32_t op, uint32_t arg) {
    return emit_rule(r, op, 1, &arg);
}
static void emit_2(Reserve r, uint32_t op, uint32_t arg1, uint32_t arg2) {
    uint32_t arr[2] = {arg1, arg2};
    return emit_rule(r, op, 2, arr);
}
static void emit_3(Reserve r, uint32_t op, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t arr[3] = {arg1, arg2, arg3};
    return emit_rule(r, op, 3, arr);
}

/*
 * Errors
 */

static void builder_cleanup(Builder *b) {
    janet_v_free(b->constants);
    janet_v_free(b->bytecode);
}

static void peg_panic(Builder *b, const char *msg) {
    builder_cleanup(b);
    janet_panicf("grammar error in %p, %s", b->form, msg);
}

#define peg_panicf(b,...) peg_panic((b), (const char *) janet_formatc(__VA_ARGS__))

static void peg_fixarity(Builder *b, int32_t argc, int32_t arity) {
    if (argc != arity) {
        peg_panicf(b, "expected %d argument%s, got %d%",
                arity,
                arity == 1 ? "" : "s",
                argc);
    }
}

static void peg_arity(Builder *b, int32_t arity, int32_t min, int32_t max) {
    if (min >= 0 && arity < min)
        peg_panicf(b, "arity mismatch, expected at least %d, got %d", min, arity);
    if (max >= 0 && arity > max)
        peg_panicf(b, "arity mismatch, expected at most %d, got %d", max, arity);
}

static const uint8_t *peg_getset(Builder *b, Janet x) {
    if (!janet_checktype(x, JANET_STRING))
        peg_panicf(b, "expected string for character set");
    const uint8_t *str = janet_unwrap_string(x);
    return str;
}

static const uint8_t *peg_getrange(Builder *b, Janet x) {
    if (!janet_checktype(x, JANET_STRING))
        peg_panicf(b, "expected string for character range");
    const uint8_t *str = janet_unwrap_string(x);
    if (janet_string_length(str) != 2)
        peg_panicf(b, "expected string to have length 2, got %v", x);
    if (str[1] < str[0])
        peg_panicf(b, "range %v is empty", x);
    return str;
}

static int32_t peg_getinteger(Builder *b, Janet x) {
    if (!janet_checkint(x))
        peg_panicf(b, "expected integer, got %v", x);
    return janet_unwrap_integer(x);
}

static int32_t peg_getnat(Builder *b, Janet x) {
    int32_t i = peg_getinteger(b, x);
    if (i < 0)
        peg_panicf(b, "expected non-negative integer, got %v", x);
    return i;
}

/*
 * Specials
 */

static void bitmap_set(uint32_t *bitmap, uint8_t c) {
    bitmap[c >> 5] |= ((uint32_t)1) << (c & 0x1F);
}

static void spec_range(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 1, -1);
    if (argc == 1) {
        Reserve r = reserve(b, 2);
        const uint8_t *str = peg_getrange(b, argv[0]);
        uint32_t arg = str[0] | (str[1] << 16);
        emit_1(r, RULE_RANGE, arg);
    } else {
        /* Compile as a set */
        Reserve r = reserve(b, 9);
        uint32_t bitmap[8] = {0};
        for (int32_t i = 0; i < argc; i++) {
            const uint8_t *str = peg_getrange(b, argv[i]);
            for (uint32_t c = str[0]; c <= str[1]; c++)
                bitmap_set(bitmap, c);
        }
        emit_rule(r, RULE_SET, 8, bitmap);
    }
}

static void spec_set(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 9);
    const uint8_t *str = peg_getset(b, argv[0]);
    uint32_t bitmap[8] = {0};
    for (int32_t i = 0; i < janet_string_length(str); i++)
        bitmap_set(bitmap, str[i]);
    emit_rule(r, RULE_SET, 8, bitmap);
}

static void spec_look(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 1, 2);
    Reserve r = reserve(b, 3);
    int32_t rulearg = argc == 2 ? 1 : 0;
    int32_t offset = argc == 2 ? peg_getinteger(b, argv[0]) : 0;
    uint32_t subrule = compile1(b, argv[rulearg]);
    emit_2(r, RULE_LOOK, (uint32_t) offset, subrule);
}

/* Rule of the form [len, rules...] */
static void spec_variadic(Builder *b, int32_t argc, const Janet *argv, uint32_t op) {
    uint32_t rule = janet_v_count(b->bytecode);
    janet_v_push(b->bytecode, op);
    janet_v_push(b->bytecode, argc);
    for (int32_t i = 0; i < argc; i++)
        janet_v_push(b->bytecode, 0);
    for (int32_t i = 0; i < argc; i++) {
        uint32_t rulei = compile1(b, argv[i]);
        b->bytecode[rule + 2 + i] = rulei;
    }
}

static void spec_choice(Builder *b, int32_t argc, const Janet *argv) {
    spec_variadic(b, argc, argv, RULE_CHOICE);
}
static void spec_sequence(Builder *b, int32_t argc, const Janet *argv) {
    spec_variadic(b, argc, argv, RULE_SEQUENCE);
}

static void spec_ifnot(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t rule_a = compile1(b, argv[0]);
    uint32_t rule_b = compile1(b, argv[1]);
    emit_2(r, RULE_IFNOT, rule_a, rule_b);
}

/* Rule of the form [rule] */
static void spec_onerule(Builder *b, int32_t argc, const Janet *argv, uint32_t op) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 2);
    uint32_t rule = compile1(b, argv[0]);
    emit_1(r, op, rule);
}

static void spec_not(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_NOT);
}
static void spec_capture(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_CAPTURE);
}
static void spec_substitute(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_SUBSTITUTE);
}
static void spec_group(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_GROUP);
}

static void spec_exponent(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 4);
    int32_t n = peg_getinteger(b, argv[1]);
    uint32_t subrule = compile1(b, argv[0]);
    if (n < 0) {
        emit_3(r, RULE_BETWEEN, 0, -n, subrule);
    } else {
        emit_3(r, RULE_BETWEEN, n, UINT32_MAX, subrule);
    }
}

static void spec_between(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 3);
    Reserve r = reserve(b, 4);
    int32_t lo = peg_getnat(b, argv[0]);
    int32_t hi = peg_getnat(b, argv[1]);
    uint32_t subrule = compile1(b, argv[2]);
    emit_3(r, RULE_BETWEEN, lo, hi, subrule);
}

static void spec_position(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 0);
    Reserve r = reserve(b, 1);
    (void) argv;
    emit_rule(r, RULE_POSITION, 0, NULL);
}

static void spec_reference(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 2);
    int32_t index = peg_getinteger(b, argv[0]);
    if (index < 0) {
        emit_1(r, RULE_BACKINDEX, -index);
    } else {
        emit_1(r, RULE_REPINDEX, index);
    }
}

static void spec_argument(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 2);
    int32_t index = peg_getnat(b, argv[0]);
    emit_1(r, RULE_ARGUMENT, index);
}

static void spec_constant(Builder *b, int32_t argc, const Janet *argv) {
    janet_fixarity(argc, 1);
    Reserve r = reserve(b, 2);
    emit_1(r, RULE_CONSTANT, emit_constant(b, argv[0]));
}

static void spec_replace(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t subrule = compile1(b, argv[0]);
    uint32_t constant = emit_constant(b, argv[1]);
    emit_2(r, RULE_REPLACE, subrule, constant);
}

/* For some and any, really just short-hand for (^ rule n) */
static void spec_repeater(Builder *b, int32_t argc, const Janet *argv, int32_t min) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 4);
    uint32_t subrule = compile1(b, argv[0]);
    emit_3(r, RULE_BETWEEN, min, UINT32_MAX, subrule);
}

static void spec_some(Builder *b, int32_t argc, const Janet *argv) {
    spec_repeater(b, argc, argv, 1);
}
static void spec_any(Builder *b, int32_t argc, const Janet *argv) {
    spec_repeater(b, argc, argv, 0);
}

static void spec_atleast(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 4);
    int32_t n = peg_getnat(b, argv[0]);
    uint32_t subrule = compile1(b, argv[1]);
    emit_3(r, RULE_BETWEEN, n, UINT32_MAX, subrule);
}

static void spec_atmost(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 4);
    int32_t n = peg_getnat(b, argv[0]);
    uint32_t subrule = compile1(b, argv[1]);
    emit_3(r, RULE_BETWEEN, 0, n, subrule);
}

static void spec_matchtime(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t subrule = compile1(b, argv[0]);
    Janet fun = argv[1];
    if (!janet_checktype(fun, JANET_FUNCTION) &&
            !janet_checktype(fun, JANET_CFUNCTION)) {
        peg_panicf(b, "expected function|cfunction, got %v", fun);
    }
    uint32_t cindex = emit_constant(b, fun);
    emit_2(r, RULE_MATCHTIME, subrule, cindex);
}

/* Special compiler form */
typedef void (*Special)(Builder *b, int32_t argc, const Janet *argv);
typedef struct {
    const char *name;
    Special special;
} SpecialPair;

static const SpecialPair specials[] = {
    {"!", spec_not},
    {"*", spec_sequence},
    {"+", spec_choice},
    {"-", spec_ifnot},
    {"/", spec_replace},
    {">", spec_look},
    {"^", spec_exponent},
    {"any", spec_any},
    {"argument", spec_argument},
    {"at-least", spec_atleast},
    {"at-most", spec_atmost},
    {"backref", spec_reference},
    {"between", spec_between},
    {"capture", spec_capture},
    {"choice", spec_choice},
    {"cmt", spec_matchtime},
    {"constant", spec_constant},
    {"group", spec_group},
    {"if-not", spec_ifnot},
    {"look", spec_look},
    {"not", spec_not},
    {"position", spec_position},
    {"range", spec_range},
    {"replace", spec_replace},
    {"sequence", spec_sequence},
    {"set", spec_set},
    {"some", spec_some},
    {"substitute", spec_substitute},
    {"|", spec_substitute},
};

/* Compile a janet value into a rule and return the rule index. */
static uint32_t compile1(Builder *b, Janet peg) {

    /* Check for already compiled rules */
    Janet check = janet_table_get(b->memoized, peg);
    if (!janet_checktype(check, JANET_NIL)) {
        uint32_t rule = (uint32_t) janet_unwrap_number(check);
        return rule;
    }

    /* Keep track of the form being compiled for error purposes */
    Janet old_form = b->form;
    b->form = peg;

    /* Check depth */
    if (b->depth-- == 0) {
        peg_panic(b, "peg grammar recursed too deeply");
    }

    /* The final rule to return */
    uint32_t rule = janet_v_count(b->bytecode);
    if (!janet_checktype(peg, JANET_KEYWORD) &&
            !janet_checktype(peg, JANET_STRUCT)) {
        janet_table_put(b->memoized, peg, janet_wrap_number(rule));
    }

    switch (janet_type(peg)) {
        default:
            peg_panicf(b, "unexpected peg source");
            return 0;
        case JANET_NUMBER:
            {
                int32_t n = peg_getinteger(b, peg);
                Reserve r = reserve(b, 2);
                if (n < 0) {
                    emit_1(r, RULE_NOTNCHAR, -n);
                } else {
                    emit_1(r, RULE_NCHAR, n);
                }
                break;
            }
        case JANET_STRING:
            {
                const uint8_t *str = janet_unwrap_string(peg);
                int32_t len = janet_string_length(str);
                emit_bytes(b, RULE_LITERAL, len, str);
                break;
            }
        case JANET_KEYWORD:
            {
                Janet check = janet_table_get(b->grammar, peg);
                if (janet_checktype(check, JANET_NIL))
                    peg_panicf(b, "unknown rule");
                rule = compile1(b, check);
                break;
            }
        case JANET_STRUCT:
            {
                JanetTable *grammar = janet_struct_to_table(janet_unwrap_struct(peg));
                grammar->proto = b->grammar;
                b->grammar = grammar;
                Janet main_rule = janet_table_get(grammar, janet_ckeywordv("main"));
                if (janet_checktype(main_rule, JANET_NIL))
                    peg_panicf(b, "grammar requires :main rule");
                rule = compile1(b, main_rule);
                b->grammar = grammar->proto;
                break;
            }
        case JANET_TUPLE:
            {
                const Janet *tup = janet_unwrap_tuple(peg);
                int32_t len = janet_tuple_length(tup);
                if (len == 0) peg_panic(b, "tuple in grammar must have non-zero length");
                if (!janet_checktype(tup[0], JANET_SYMBOL))
                    peg_panicf(b, "expected grammar command, found %v", tup[0]);
                const uint8_t *sym = janet_unwrap_symbol(tup[0]);
                const SpecialPair *sp = janet_strbinsearch(
                        &specials,
                        sizeof(specials)/sizeof(SpecialPair),
                        sizeof(SpecialPair),
                        sym);
                if (!sp)
                    peg_panicf(b, "unknown special %S", sym);
                sp->special(b, len - 1, tup + 1);
                break;
            }
    }

    /* Increase depth again */
    b->depth++;
    b->form = old_form;
    return rule;
}

/*
 * Post-Compilation
 */

typedef struct {
    uint32_t *bytecode;
    Janet *constants;
    uint32_t main_rule;
    uint32_t num_constants;
} Peg;

static int peg_mark(void *p, size_t size) {
    (void) size;
    Peg *peg = (Peg *)p;
    for (uint32_t i = 0; i < peg->num_constants; i++)
        janet_mark(peg->constants[i]);
    return 0;
}

static JanetAbstractType peg_type = {
    "core/peg",
    NULL,
    peg_mark
};

/* Convert Builder to Peg (Janet Abstract Value) */
static Peg *make_peg(Builder *b, uint32_t main_rule) {
    size_t bytecode_size = janet_v_count(b->bytecode) * sizeof(uint32_t);
    size_t constants_size = janet_v_count(b->constants) * sizeof(Janet);
    size_t total_size = bytecode_size + constants_size + sizeof(Peg);
    char *mem = janet_abstract(&peg_type, total_size);
    Peg *peg = (Peg *)mem;
    peg->bytecode = (uint32_t *)(mem + sizeof(Peg));
    peg->constants = (Janet *)(mem + sizeof(Peg) + bytecode_size);
    peg->num_constants = janet_v_count(b->constants);
    peg->main_rule = main_rule;
    memcpy(peg->bytecode, b->bytecode, bytecode_size);
    memcpy(peg->constants, b->constants, constants_size);
    return peg;
}

/* Compiler entry point */
static Peg *compile_peg(Janet x) {
    Builder builder;
    builder.grammar = janet_table(0);
    builder.memoized = janet_table(0);
    builder.constants = NULL;
    builder.bytecode = NULL;
    builder.form = x;
    builder.depth = JANET_RECURSION_GUARD;
    uint32_t main_rule = compile1(&builder, x);
    Peg *peg = make_peg(&builder, main_rule);
    builder_cleanup(&builder);
    return peg;
}

/*
 * C Functions
 */

static Janet cfun_compile(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    Peg *peg = compile_peg(argv[0]);
    return janet_wrap_abstract(peg);
}

static Janet cfun_match(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, -1);
    Peg *peg;
    if (janet_checktype(argv[0], JANET_ABSTRACT) &&
            janet_abstract_type(janet_unwrap_abstract(argv[0])) == &peg_type) {
        peg = janet_unwrap_abstract(argv[0]);
    } else {
        peg = compile_peg(argv[0]);
    }
    JanetByteView bytes = janet_getbytes(argv, 1);
    int32_t start;
    PegState s;
    if (argc > 2) {
        start = janet_gethalfrange(argv, 2, bytes.len, "offset");
        s.extrac = argc - 3;
        s.extrav = argv + 3;
    } else {
        start = 0;
        s.extrac = 0;
        s.extrav = NULL;
    }
    s.mode = PEG_MODE_NORMAL;
    s.text_start = bytes.bytes;
    s.text_end = bytes.bytes + bytes.len;
    s.depth = JANET_RECURSION_GUARD;
    s.captures = janet_array(0);
    s.scratch = janet_buffer(10);

    s.constants = peg->constants;
    s.bytecode = peg->bytecode;
    const uint8_t *result = peg_rule(&s, s.bytecode + peg->main_rule, bytes.bytes + start);
    return result ? janet_wrap_array(s.captures) : janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"peg/compile", cfun_compile,
        "(peg/compile peg)\n\n"
            "Compiles a peg source data structure into a <core/peg>. This will speed up matching "
            "if the same peg will be used multiple times."
    },
    {"peg/match", cfun_match,
        "(peg/match peg text [,start=0])\n\n"
            "Match a Parsing Expression Grammar to a byte string and return an array of captured values. "
            "Returns nil if text does not match the language defined by peg. The syntax of PEGs are very "
            "similar to those defined by LPeg, and have similar capabilities. Still WIP."
    },
    {NULL, NULL, NULL}
};

/* Load the peg module */
void janet_lib_peg(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
}

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

/* Potential opcodes for peg vm. 
 * These are not yet implemented, but efficiently express the current semantics
 * of the current implementation.
typedef enum {
    POP_LITERAL,      [len, bytes...]
    POP_NCHAR,        [n]
    POP_RANGE,        [lo | hi << 16 (1 word)]
    POP_SET,          [bitmap (8 words)]
    POP_LOOK,         [offset, rule]
    POP_CHOICE,       [len, rules...]
    POP_SEQUENCE,     [len, rules...]
    POP_IFNOT,        [a, b (a if not b)]
    POP_NOT,          [a] 
    POP_ATLEAST,      [n, rule]
    POP_BETWEEN,      [lo, hi, rule]
    POP_CAPTURE,      [rule]
    POP_POSITION,     []
    POP_SUBSTITUTE,   [rule]
    POP_GROUP,        [rule]
    POP_CONSTANT,     [constant]
    POP_REPLACE,      [constant]
    POP_REPINDEX,     [capture index]
    POP_ARGUMENT      [argument index]
} Opcode;
*/

/* TODO
 * - Compilation - compile peg to binary form - one grammar, patterns reference each other by index
 *   and bytecode "opcodes" identify primitive patterns and pattern "constructors". Main pattern is
 *   pattern index 0. The logic of patterns would not change much, but we could elide arity checking,
 *   expensive keyword lookups, and unused patterns. We could also combine certain pattern types into
 *   more efficient types. */

/* Hold captured patterns and match state */
typedef struct {
    const uint8_t *text_start;
    const uint8_t *text_end;
    const uint8_t *subst_end;
    JanetTable *grammar;
    JanetArray *captures;
    JanetBuffer *scratch;
    const Janet *extrav;
    int32_t extrac;
    int32_t depth;
    enum {
        PEG_NORMAL,
        PEG_SUBSTITUTE,
        PEG_NOCAPTURE
    } mode;
} State;

/* Forward declaration */
static int32_t match(State *s, Janet peg, const uint8_t *text);

/* Special matcher form */
typedef int32_t (*Matcher)(State *s, int32_t argc, const Janet *argv, const uint8_t *text);

/* A "Matcher" is a function that is used to match a pattern at an anchored position. It takes some
 * optional arguments, and returns either the number of bytes matched, or -1 for no match. It can also
 * append values to the capture array of State *s, panic on bad arguments, and call match recursively. */

/*
 * Primitive Pattern Types
 */

/* Match a character range */
static int32_t match_range(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    if (s->text_end <= text)
        return -1;
    for (int32_t i = 0; i < argc; i++) {
        const uint8_t *range = janet_getstring(argv, i);
        int32_t length = janet_string_length(range);
        if (length != 2) janet_panicf("arguments to range must have length 2");
        uint8_t lo = range[0];
        uint8_t hi = range[1];
        if (text[0] >= lo && text[0] <= hi) return 1;
    }
    return -1;
}

/* Match 1 of any character in argv[0] */
static int32_t match_set(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    const uint8_t *set = janet_getstring(argv, 0);
    int32_t len = janet_string_length(set);
    if (s->text_end <= text) return -1;
    for (int32_t i = 0; i < len; i++)
        if (set[i] == text[0]) return 1;
    return -1;
}

/*
 * Look ahead/behind
 */

/* Match 0 length if match argv[0] */
static int32_t match_lookahead(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    return match(s, argv[0], text) >= 0 ? 0 : -1;
}

/* Match 0 length if match argv[0] at text + offset */
static int32_t match_lookoffset(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);
    text += janet_getinteger(argv, 0);
    if (text < s->text_start || text > s->text_end)
        return -1;
    return match(s, argv[1], text) >= 0 ? 0 : -1;
}

/*
 * Combining Pattern Types
 */

/* Match the first of argv[0], argv[1], argv[2], ... */
static int32_t match_choice(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    for (int32_t i = 0; i < argc; i++) {
        int32_t result = match(s, argv[i], text);
        if (result >= 0) return result;
    }
    return -1;
}

/* Match argv[0] then argv[1] then argv[2] ... Fail if any match fails. */
static int32_t match_sequence(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    int32_t traversed = 0;
    for (int32_t i = 0; i < argc; i++) {
        int32_t result = match(s, argv[i], text + traversed);
        if (result < 0) return -1;
        traversed += result;
    }
    return traversed;
}

/* Match argv[0] if not argv[1] */
static int32_t match_minus(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);
    if (match(s, argv[1], text) < 0)
        return match(s, argv[0], text);
    return -1;
}

/* Match zero length if not match argv[0] */
static int32_t match_not(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    if (match(s, argv[0], text) < 0)
        return 0;
    return -1;
}

/* Match at least argv[0] repetitions of argv[1]. Will match as many repetitions as possible. */
static int32_t match_atleast(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);
    int32_t n = janet_getinteger(argv, 0);
    int32_t captured = 0;
    int32_t total_length = 0;
    int32_t result;
    /* Greedy match until match fails */
    while ((result = match(s, argv[1], text + total_length)) > 0) {
        captured++;
        total_length += result;
    }
    return captured >= n ? total_length : -1;
}

/* Match at most argv[0] repetitions of argv[1]. Will match as many repetitions as possible. */
static int32_t match_atmost(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);
    int32_t n = janet_getinteger(argv, 0);
    int32_t captured = 0;
    int32_t total_length = 0;
    int32_t result;
    /* Greedy match until match fails or n captured */
    while (captured < n && (result = match(s, argv[1], text + total_length)) > 0) {
        captured++;
        total_length += result;
    }
    /* always matches */
    return total_length;
}

/* Match between argv[0] and argv[1] repetitions of argv[2]. Will match as many repetitions as possible. */
static int32_t match_between(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 3);
    int32_t lo = janet_getinteger(argv, 0);
    int32_t hi = janet_getinteger(argv, 1);
    int32_t captured = 0;
    int32_t total_length = 0;
    int32_t result;
    /* Greedy match until match fails or n captured */
    while (captured < hi && (result = match(s, argv[2], text + total_length)) > 0) {
        captured++;
        total_length += result;
    }
    /* always matches */
    return captured >= lo ? total_length : -1;
}

/*
 * Captures
 */

static void push_capture(State *s, Janet capture, const uint8_t *text, int32_t nbytes) {
    if (s->mode == PEG_SUBSTITUTE) {
        /* Substitution mode, append as string to scratch buffer */
        /* But first append in-between text */
        janet_buffer_push_bytes(s->scratch, s->subst_end, text - s->subst_end);
        janet_to_string_b(s->scratch, capture);
        s->subst_end = text + nbytes;
    } else if (s->mode == PEG_NORMAL) {
        /* Normal mode, append to captures */
        janet_array_push(s->captures, capture);
    }
}

/* Capture a value */
static int32_t match_capture(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    int oldmode = s->mode;
    /* We can't have overlapping captures during substitution, so we can
     * turn off the child captures if subsituting */
    if (s->mode == PEG_SUBSTITUTE) s->mode = PEG_NOCAPTURE;
    int32_t result = match(s, argv[0], text);
    s->mode = oldmode;
    if (result < 0) return -1;
    push_capture(s, janet_stringv(text, result), text, result);
    return result;
}

/* Capture position */
static int32_t match_position(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 0);
    (void) argv;
    push_capture(s, janet_wrap_number(text - s->text_start), text, 0);
    return 0;
}

/* Capture group */
static int32_t match_group(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    int32_t old_count = s->captures->count;

    int oldmode = s->mode;
    if (oldmode != PEG_NOCAPTURE) s->mode = PEG_NORMAL;
    int32_t result = match(s, argv[0], text);
    s->mode = oldmode;
    if (result < 0) return -1;

    if (oldmode != PEG_NOCAPTURE) {
        /* Collect sub-captures into an array by popping new values off of the capture stack,
         * and then putting them in a new array. Then, push the new array back onto the capture stack. */
        int32_t num_sub_captures = s->captures->count - old_count;
        JanetArray *sub_captures = janet_array(num_sub_captures);
        memcpy(sub_captures->data, s->captures->data + old_count, sizeof(Janet) * num_sub_captures);
        sub_captures->count = num_sub_captures;
        s->captures->count = old_count;
        push_capture(s, janet_wrap_array(sub_captures), text, result);
    }

    return result;
}

/* Capture a constant */
static int32_t match_capture_constant(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    push_capture(s, argv[0], text, 0);
    return 0;
}

/* Capture nth extra argument to peg/match */
static int32_t match_capture_arg(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    int32_t n = janet_getargindex(argv, 0, s->extrac, "n");
    push_capture(s, s->extrav[n], text, 0);
    return 0;
}

/* Capture replace */
static int32_t match_replace(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);

    int oldmode = s->mode;
    int32_t old_count = s->captures->count;
    if (oldmode == PEG_SUBSTITUTE) s->mode = PEG_NORMAL;
    int32_t result = match(s, argv[0], text);
    s->mode = oldmode;

    if (result < 0) return -1;
    if (oldmode == PEG_NOCAPTURE) return result;

    Janet capture;
    switch (janet_type(argv[1])) {
        default:
            capture = argv[1];
            break;
        case JANET_STRUCT:
            capture = janet_struct_get(janet_unwrap_struct(argv[1]), janet_stringv(text, result));
            break;
        case JANET_TABLE:
            capture = janet_table_get(janet_unwrap_table(argv[1]), janet_stringv(text, result));
            break;
        case JANET_CFUNCTION:
            {
                janet_array_push(s->captures, janet_stringv(text, result));
                JanetCFunction cfunc = janet_unwrap_cfunction(argv[1]);
                capture = cfunc(s->captures->count - old_count, s->captures->data + old_count);
                break;
            }
        case JANET_FUNCTION:
            {
                janet_array_push(s->captures, janet_stringv(text, result));
                capture = janet_call(janet_unwrap_function(argv[1]),
                        s->captures->count - old_count, s->captures->data + old_count);
                break;
            }
        case JANET_NUMBER:
            {
                int32_t index = janet_getargindex(argv, 1, s->captures->count, "capture");
                capture = s->captures->data[index];
                break;
            }
    }
    s->captures->count = old_count;
    push_capture(s, capture, text, result);
    return result;
}

/* Substitution capture */
static int32_t match_substitute(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);

    /* Save old scratch state */
    int32_t old_count = s->scratch->count;
    const uint8_t *old_subst_end = s->subst_end;

    /* Prepare for collecting in scratch */
    s->subst_end = text;

    int oldmode = s->mode;
    if (oldmode != PEG_NOCAPTURE) s->mode = PEG_SUBSTITUTE;
    int32_t result = match(s, argv[0], text);
    s->mode = oldmode;

    if (result < 0) return -1;

    if (oldmode != PEG_NOCAPTURE) {
        /* Push remaining text to scratch buffer */
        janet_buffer_push_bytes(s->scratch, s->subst_end, text - s->subst_end + result);
        /* Pop last section of scratch buffer and push a string capture */
        janet_array_push(s->captures,
                janet_stringv(s->scratch->data + old_count, s->scratch->count - old_count));
    }

    /* Reset scratch buffer and subst_end */
    s->scratch->count = old_count;
    s->subst_end = old_subst_end;

    return result;
}

/* Lookup for special forms */
typedef struct {
    const char *name;
    Matcher matcher;
} MatcherPair;
static const MatcherPair specials[] = {
    {"*", match_sequence},
    {"+", match_choice},
    {"-", match_minus},
    {"/", match_replace},
    {"<-", match_capture},
    {"<-arg", match_capture_arg},
    {"<-c", match_capture_constant},
    {"<-g", match_group},
    {"<-p", match_position},
    {"<-s", match_substitute},
    {">", match_lookahead},
    {">>", match_lookoffset},
    {"at-least", match_atleast},
    {"at-most", match_atmost},
    {"between", match_between},
    {"not", match_not},
    {"range", match_range},
    {"set", match_set}
};

/* Check if the string matches the pattern at the given point. Returns a negative number
 * if no match, else the number of characters matched against. */
static int32_t match(State *s, Janet peg, const uint8_t *text) {
    switch(janet_type(peg)) {
        default:
            janet_panicf("unexpected element in peg: %v", peg);
            return -1;
        case JANET_NUMBER:
            /* Match n characters */
            {
                if (!janet_checkint(peg))
                    janet_panicf("numbers in peg must be integers, got %v", peg);
                int32_t n = janet_unwrap_integer(peg);
                if (n < 0) /* Invert pattern */
                    return (text - n > s->text_end) ? 0 : -1;
                return (text + n > s->text_end) ? -1 : n;
            }
        case JANET_STRING:
            /* Match a sequence of bytes */
            {
                const uint8_t *str = janet_unwrap_string(peg);
                int32_t len = janet_string_length(str);
                if (text + len > s->text_end) return -1;
                return memcmp(text, str, len) ? -1 : len;
            }
        case JANET_TUPLE:
            /* Match a special command */
            {
                const Janet *items;
                int32_t len;
                janet_indexed_view(peg, &items, &len);
                janet_arity(len, 1, -1);
                if (!janet_checktype(items[0], JANET_SYMBOL))
                    janet_panicf("expected symbol for name of command");
                const uint8_t *sym = janet_unwrap_symbol(items[0]);
                const MatcherPair *mp = janet_strbinsearch(
                        &specials,
                        sizeof(specials)/sizeof(MatcherPair),
                        sizeof(MatcherPair),
                        sym);
                if (!mp) janet_panicf("unknown special form %p", peg);
                if (s->depth-- == 0)
                    janet_panic("recursed too deeply");

                /* Save old state in case of failure */
                int32_t old_capture_count = s->captures->count;
                int32_t old_scratch_count = s->scratch->count;
                const uint8_t *old_subst_end = s->subst_end;
                int32_t result =  mp->matcher(s, len - 1, items + 1, text);

                /* Reset old state on failure */
                if (result < 0) {
                    s->captures->count = old_capture_count;
                    s->scratch->count = old_scratch_count;
                    s->subst_end = old_subst_end;
                }
                s->depth++;
                return result;
            }
        case JANET_KEYWORD:
            /* Look up a rule */
            return match(s, janet_table_get(s->grammar, peg), text);
        case JANET_STRUCT:
            /* Specify a grammar */
            {
                JanetTable *grammar = janet_struct_to_table(janet_unwrap_struct(peg));
                grammar->proto = s->grammar;

                /* Run main rule with grammar set */
                s->grammar = grammar;
                int32_t result = match(s, janet_table_get(grammar, janet_ckeywordv("main")), text);
                s->grammar = grammar->proto;

                return result;
            }
    }
}

/* C Functions */

static Janet cfun_match(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, -1);
    JanetByteView bytes = janet_getbytes(argv, 1);
    int32_t start;
    State s;
    if (argc > 2) {
        start = janet_gethalfrange(argv, 2, bytes.len, "offset");
        s.extrac = argc - 3;
        s.extrav = argv + 3;
    } else {
        start = 0;
        s.extrac = 0;
        s.extrav = NULL;
    }
    s.mode = PEG_NORMAL;
    s.text_start = bytes.bytes;
    s.text_end = bytes.bytes + bytes.len;
    s.depth = JANET_RECURSION_GUARD;
    s.grammar = NULL;
    s.captures = janet_array(0);
    s.scratch = janet_buffer(0);
    int32_t result = match(&s, argv[0], bytes.bytes + start);
    return result >= 0 ? janet_wrap_array(s.captures) : janet_wrap_nil();
}

static const JanetReg cfuns[] = {
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

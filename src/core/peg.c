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

/* TODO
 * - tail recursion in patterns (allow for self referential patterns that don't decrease depth)
 * - Captures - need to account for possible (likely) recursion and not overwrite previous captures
 * - Compilation - compile peg to binary form - one grammar, patterns reference each other by index
 *   and bytecode "opcodes" identify primitive patterns and pattern "constructors". Main pattern is 
 *   pattern index 0.
 * - Investigate more primitive pattern types - Kleene star and variations.
 * - Possibly allow referencing captures from grammars or arbitrary code execution in
 *   patterns for flexible usage. */

/* My flags man */
#define PEG_CAPTURE 0x1

/* Hold captured patterns and match state */
typedef struct {
    int32_t depth;
    const uint8_t *text_start;
    const uint8_t *text_end;
    JanetTable *grammar;
    int flags;
} State;

/* Forward declaration */
static int32_t match(State *s, Janet peg, const uint8_t *text);

/* Special matcher form */
typedef int32_t (*Matcher)(State *s, int32_t argc, const Janet *argv, const uint8_t *text);
typedef struct {
    const char *name;
    Matcher matcher;
} MatcherPair;

/* Match a character range */
int32_t match_range(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
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
int32_t match_set(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    const uint8_t *set = janet_getstring(argv, 0);
    int32_t len = janet_string_length(set);
    if (s->text_end <= text) return -1;
    for (int32_t i = 0; i < len; i++)
        if (set[i] == text[0]) return 1;
    return -1;
}

/* Match the first of argv[0], argv[1], argv[2], ... */
int32_t match_choice(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    for (int32_t i = 0; i < argc; i++) {
        int32_t result = match(s, argv[i], text);
        if (result >= 0) return result;
    }
    return -1;
}

/* Match argv[0] then argv[1] then argv[2] ... Fail if any match fails. */
int32_t match_sequence(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    int32_t traversed = 0;
    for (int32_t i = 0; i < argc; i++) {
        if (text + traversed >= s->text_end) return -1;
        int32_t result = match(s, argv[i], text + traversed);
        if (result < 0) return -1;
        traversed += result;
    }
    return traversed;
}

/* Match argv[0] if not argv[1] */
int32_t match_minus(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 2);
    if (match(s, argv[1], text) < 0)
        return match(s, argv[0], text);
    return -1;
}

/* Match zero length if not match argv[0] */
int32_t match_not(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    if (match(s, argv[0], text) < 0)
        return 0;
    return -1;
}

/* Match 0 length if match argv[0] */
int32_t match_lookahead(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
    janet_fixarity(argc, 1);
    return match(s, argv[0], text) >= 0 ? 0 : -1;
}

/* Match at least argv[0] repetitions of argv[1]. Will match as many repetitions as possible. */
int32_t match_atleast(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
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
int32_t match_atmost(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
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
int32_t match_between(State *s, int32_t argc, const Janet *argv, const uint8_t *text) {
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

/* Lookup for special forms */
static const MatcherPair specials[] = {
    {"*", match_sequence},
    {"+", match_choice},
    {"-", match_minus},
    {">", match_lookahead},
    {"at-least", match_atleast},
    {"at-most", match_atmost},
    {"between", match_between},
    {"not", match_not},
    {"range", match_range},
    {"set", match_set}
};

/* Check if the string matches the pattern at the given point. Returns a negative number
 * if no match, else the number of charcters matched against. */
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
                return (s->text_end >= text + n) ? n : -1;
            }
        case JANET_STRING:
            /* Match a sequence of bytes */
            {
                const uint8_t *str = janet_unwrap_string(peg);
                int32_t len = janet_string_length(str);
                if (text + len > s->text_end) return 0;
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
                if (!mp) janet_panicf("unknown special form %v", peg);
                if (s->depth-- == 0)
                    janet_panic("recursed too deeply");
                int32_t result =  mp->matcher(s, len - 1, items + 1, text);
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
    janet_fixarity(argc, 2);
    JanetByteView bytes = janet_getbytes(argv, 1);
    State s;
    s.text_start = bytes.bytes;
    s.text_end = bytes.bytes + bytes.len;
    s.depth = JANET_RECURSION_GUARD;
    s.grammar = NULL;
    int32_t result = match(&s, argv[0], bytes.bytes);
    return janet_wrap_boolean(result >= 0);
}

static const JanetReg cfuns[] = {
    {"peg/match", cfun_match, NULL},
    {NULL, NULL, NULL}
};

/* Load the peg module */
void janet_lib_peg(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
}

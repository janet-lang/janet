/*
* Copyright (c) 2025 Calvin Rose
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
#include <string.h>
#include "util.h"
#include "vector.h"
#include "util.h"
#endif

#ifdef JANET_PEG

/*
 * Runtime
 */

/* Hold captured patterns and match state */
typedef struct {
    const uint8_t *text_start;
    const uint8_t *text_end;
    /* text_end can be restricted by some rules, but
       outer_text_end will always contain the real end of
       input, which we need to generate a line mapping */
    const uint8_t *outer_text_end;
    const uint32_t *bytecode;
    const Janet *constants;
    JanetArray *captures;
    JanetBuffer *scratch;
    JanetBuffer *tags;
    JanetArray *tagged_captures;
    const Janet *extrav;
    int32_t *linemap;
    int32_t extrac;
    int32_t depth;
    int32_t linemaplen;
    int32_t has_backref;
    enum {
        PEG_MODE_NORMAL,
        PEG_MODE_ACCUMULATE
    } mode;
} PegState;

/* Allow backtrack with captures. We need
 * to save state at branches, and then reload
 * if one branch fails and try a new branch. */
typedef struct {
    int32_t cap;
    int32_t tcap;
    int32_t scratch;
} CapState;

/* Save the current capture state */
static CapState cap_save(PegState *s) {
    CapState cs;
    cs.scratch = s->scratch->count;
    cs.cap = s->captures->count;
    cs.tcap = s->tagged_captures->count;
    return cs;
}

/* Load a saved capture state in the case of failure */
static void cap_load(PegState *s, CapState cs) {
    s->scratch->count = cs.scratch;
    s->captures->count = cs.cap;
    s->tags->count = cs.tcap;
    s->tagged_captures->count = cs.tcap;
}

/* Load a saved capture state in the case of success. Keeps
 * tagged captures around for backref. */
static void cap_load_keept(PegState *s, CapState cs) {
    s->scratch->count = cs.scratch;
    s->captures->count = cs.cap;
}

/* Add a capture */
static void pushcap(PegState *s, Janet capture, uint32_t tag) {
    if (s->mode == PEG_MODE_ACCUMULATE) {
        janet_to_string_b(s->scratch, capture);
    }
    if (s->mode == PEG_MODE_NORMAL) {
        janet_array_push(s->captures, capture);
    }
    if (s->has_backref) {
        janet_array_push(s->tagged_captures, capture);
        janet_buffer_push_u8(s->tags, tag);
    }
}

/* Lazily generate line map to get line and column information for PegState.
 * line and column are 1-indexed. */
typedef struct {
    int32_t line;
    int32_t col;
} LineCol;
static LineCol get_linecol_from_position(PegState *s, int32_t position) {
    /* Generate if not made yet */
    if (s->linemaplen < 0) {
        int32_t newline_count = 0;
        for (const uint8_t *c = s->text_start; c < s->outer_text_end; c++) {
            if (*c == '\n') newline_count++;
        }
        int32_t *mem = janet_smalloc(sizeof(int32_t) * newline_count);
        size_t index = 0;
        for (const uint8_t *c = s->text_start; c < s->outer_text_end; c++) {
            if (*c == '\n') mem[index++] = (int32_t)(c - s->text_start);
        }
        s->linemaplen = newline_count;
        s->linemap = mem;
    }
    /* Do binary search for line. Slightly modified from classic binary search:
     * - if we find that our current character is a line break, just return immediately.
     *   a newline character is consider to be on the same line as the character before
     *   (\n is line terminator, not line separator).
     * - in the not-found case, we still want to find the greatest-indexed newline that
     *   is before position. we use that to calculate the line and column.
     * - in the case that lo = 0 and s->linemap[0] is still greater than position, we
     *   are on the first line and our column is position + 1. */
    int32_t hi = s->linemaplen; /* hi is greater than the actual line */
    int32_t lo = 0; /* lo is less than or equal to the actual line */
    LineCol ret;
    while (lo + 1 < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (s->linemap[mid] >= position) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    /* first line case */
    if (s->linemaplen == 0 || (lo == 0 && s->linemap[0] >= position)) {
        ret.line = 1;
        ret.col = position + 1;
    } else {
        ret.line = lo + 2;
        ret.col = position - s->linemap[lo];
    }
    return ret;
}

/* Convert a uint64_t to a int64_t by wrapping to a maximum number of bytes */
static int64_t peg_convert_u64_s64(uint64_t from, int width) {
    int shift = 8 * (8 - width);
    return ((int64_t)(from << shift)) >> shift;
}

/* Prevent stack overflow */
#define down1(s) do { \
    if (0 == --((s)->depth)) janet_panic("peg/match recursed too deeply"); \
} while (0)
#define up1(s) ((s)->depth++)

/* Evaluate a peg rule
 * Pre-conditions: s is in a valid state
 * Post-conditions: If there is a match, returns a pointer to the next text.
 * All captures on the capture stack are valid. If there is no match,
 * returns NULL. Extra captures from successful child expressions can be
 * left on the capture stack.
 */
static const uint8_t *peg_rule(
    PegState *s,
    const uint32_t *rule,
    const uint8_t *text) {
tail:
    switch (*rule) {
        default:
            janet_panic("unexpected opcode");
            return NULL;

        case RULE_LITERAL: {
            uint32_t len = rule[1];
            if (text + len > s->text_end) return NULL;
            return memcmp(text, rule + 2, len) ? NULL : text + len;
        }

        case RULE_NCHAR: {
            uint32_t n = rule[1];
            return (text + n > s->text_end) ? NULL : text + n;
        }

        case RULE_NOTNCHAR: {
            uint32_t n = rule[1];
            return (text + n > s->text_end) ? text : NULL;
        }

        case RULE_RANGE: {
            uint8_t lo = rule[1] & 0xFF;
            uint8_t hi = (rule[1] >> 16) & 0xFF;
            return (text < s->text_end &&
                    text[0] >= lo &&
                    text[0] <= hi)
                   ? text + 1
                   : NULL;
        }

        case RULE_SET: {
            if (text >= s->text_end) return NULL;
            uint32_t word = rule[1 + (text[0] >> 5)];
            uint32_t mask = (uint32_t)1 << (text[0] & 0x1F);
            return (word & mask)
                   ? text + 1
                   : NULL;
        }

        case RULE_LOOK: {
            text += ((int32_t *)rule)[1];
            if (text < s->text_start || text > s->text_end) return NULL;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[2], text);
            up1(s);
            text -= ((int32_t *)rule)[1];
            return result ? text : NULL;
        }

        case RULE_CHOICE: {
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

        case RULE_SEQUENCE: {
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

        case RULE_IF: {
            const uint32_t *rule_a = s->bytecode + rule[1];
            const uint32_t *rule_b = s->bytecode + rule[2];
            down1(s);
            const uint8_t *result = peg_rule(s, rule_a, text);
            up1(s);
            if (!result) return NULL;
            rule = rule_b;
            goto tail;
        }
        case RULE_IFNOT: {
            const uint32_t *rule_a = s->bytecode + rule[1];
            const uint32_t *rule_b = s->bytecode + rule[2];
            down1(s);
            CapState cs = cap_save(s);
            const uint8_t *result = peg_rule(s, rule_a, text);
            if (!!result) {
                up1(s);
                return NULL;
            } else {
                cap_load(s, cs);
                up1(s);
                rule = rule_b;
                goto tail;
            }
        }

        case RULE_NOT: {
            const uint32_t *rule_a = s->bytecode + rule[1];
            down1(s);
            CapState cs = cap_save(s);
            const uint8_t *result = peg_rule(s, rule_a, text);
            if (result) {
                up1(s);
                return NULL;
            } else {
                cap_load(s, cs);
                up1(s);
                return text;
            }
        }

        case RULE_THRU:
        case RULE_TO: {
            const uint32_t *rule_a = s->bytecode + rule[1];
            const uint8_t *next_text = NULL;
            CapState cs = cap_save(s);
            down1(s);
            while (text <= s->text_end) {
                CapState cs2 = cap_save(s);
                next_text = peg_rule(s, rule_a, text);
                if (next_text) {
                    if (rule[0] == RULE_TO) cap_load(s, cs2);
                    break;
                }
                cap_load(s, cs2);
                text++;
            }
            up1(s);
            if (text > s->text_end) {
                cap_load(s, cs);
                return NULL;
            }
            return rule[0] == RULE_TO ? text : next_text;
        }

        case RULE_BETWEEN: {
            uint32_t lo = rule[1];
            uint32_t hi = rule[2];
            const uint32_t *rule_a = s->bytecode + rule[3];
            uint32_t captured = 0;
            const uint8_t *next_text;
            CapState cs = cap_save(s);
            down1(s);
            while (captured < hi) {
                CapState cs2 = cap_save(s);
                next_text = peg_rule(s, rule_a, text);
                if (!next_text || next_text == text) {
                    cap_load(s, cs2);
                    break;
                }
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

        /* Capturing rules */

        case RULE_GETTAG: {
            uint32_t search = rule[1];
            uint32_t tag = rule[2];
            for (int32_t i = s->tags->count - 1; i >= 0; i--) {
                if (s->tags->data[i] == search) {
                    pushcap(s, s->tagged_captures->data[i], tag);
                    return text;
                }
            }
            return NULL;
        }

        case RULE_POSITION: {
            pushcap(s, janet_wrap_number((double)(text - s->text_start)), rule[1]);
            return text;
        }

        case RULE_LINE: {
            LineCol lc = get_linecol_from_position(s, (int32_t)(text - s->text_start));
            pushcap(s, janet_wrap_number((double)(lc.line)), rule[1]);
            return text;
        }

        case RULE_COLUMN: {
            LineCol lc = get_linecol_from_position(s, (int32_t)(text - s->text_start));
            pushcap(s, janet_wrap_number((double)(lc.col)), rule[1]);
            return text;
        }

        case RULE_ARGUMENT: {
            int32_t index = ((int32_t *)rule)[1];
            Janet capture = (index >= s->extrac) ? janet_wrap_nil() : s->extrav[index];
            pushcap(s, capture, rule[2]);
            return text;
        }

        case RULE_CONSTANT: {
            pushcap(s, s->constants[rule[1]], rule[2]);
            return text;
        }

        case RULE_CAPTURE: {
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (!result) return NULL;
            /* Specialized pushcap - avoid intermediate string creation */
            if (!s->has_backref && s->mode == PEG_MODE_ACCUMULATE) {
                janet_buffer_push_bytes(s->scratch, text, (int32_t)(result - text));
            } else {
                uint32_t tag = rule[2];
                pushcap(s, janet_stringv(text, (int32_t)(result - text)), tag);
            }
            return result;
        }

        case RULE_CAPTURE_NUM: {
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (!result) return NULL;
            /* check number parsing */
            double x = 0.0;
            int32_t base = (int32_t) rule[2];
            if (janet_scan_number_base(text, (int32_t)(result - text), base, &x)) return NULL;
            /* Specialized pushcap - avoid intermediate string creation */
            if (!s->has_backref && s->mode == PEG_MODE_ACCUMULATE) {
                janet_buffer_push_bytes(s->scratch, text, (int32_t)(result - text));
            } else {
                uint32_t tag = rule[3];
                pushcap(s, janet_wrap_number(x), tag);
            }
            return result;
        }

        case RULE_ACCUMULATE: {
            uint32_t tag = rule[2];
            int oldmode = s->mode;
            if (!tag && oldmode == PEG_MODE_ACCUMULATE) {
                rule = s->bytecode + rule[1];
                goto tail;
            }
            CapState cs = cap_save(s);
            s->mode = PEG_MODE_ACCUMULATE;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            s->mode = oldmode;
            if (!result) return NULL;
            Janet cap = janet_stringv(s->scratch->data + cs.scratch,
                                      s->scratch->count - cs.scratch);
            cap_load_keept(s, cs);
            pushcap(s, cap, tag);
            return result;
        }

        case RULE_DROP: {
            CapState cs = cap_save(s);
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (!result) return NULL;
            cap_load(s, cs);
            return result;
        }

        case RULE_ONLY_TAGS: {
            CapState cs = cap_save(s);
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (!result) return NULL;
            cap_load_keept(s, cs);
            return result;
        }

        case RULE_GROUP: {
            uint32_t tag = rule[2];
            int oldmode = s->mode;
            CapState cs = cap_save(s);
            s->mode = PEG_MODE_NORMAL;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            s->mode = oldmode;
            if (!result) return NULL;
            int32_t num_sub_captures = s->captures->count - cs.cap;
            JanetArray *sub_captures = janet_array(num_sub_captures);
            safe_memcpy(sub_captures->data,
                        s->captures->data + cs.cap,
                        sizeof(Janet) * num_sub_captures);
            sub_captures->count = num_sub_captures;
            cap_load_keept(s, cs);
            pushcap(s, janet_wrap_array(sub_captures), tag);
            return result;
        }

        case RULE_NTH: {
            uint32_t nth = rule[1];
            if (nth > INT32_MAX) nth = INT32_MAX;
            uint32_t tag = rule[3];
            int oldmode = s->mode;
            CapState cs = cap_save(s);
            s->mode = PEG_MODE_NORMAL;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[2], text);
            up1(s);
            s->mode = oldmode;
            if (!result) return NULL;
            int32_t num_sub_captures = s->captures->count - cs.cap;
            Janet cap;
            if (num_sub_captures > (int32_t) nth) {
                cap = s->captures->data[cs.cap + nth];
            } else {
                return NULL;
            }
            cap_load_keept(s, cs);
            pushcap(s, cap, tag);
            return result;
        }

        case RULE_SUB: {
            const uint8_t *text_start = text;
            const uint32_t *rule_window = s->bytecode + rule[1];
            const uint32_t *rule_subpattern = s->bytecode + rule[2];
            down1(s);
            const uint8_t *window_end = peg_rule(s, rule_window, text);
            up1(s);
            if (!window_end) {
                return NULL;
            }
            const uint8_t *saved_end = s->text_end;
            s->text_end = window_end;
            down1(s);
            const uint8_t *next_text = peg_rule(s, rule_subpattern, text_start);
            up1(s);
            s->text_end = saved_end;

            if (!next_text) {
                return NULL;
            }

            return window_end;
        }

        case RULE_SPLIT: {
            const uint8_t *saved_end = s->text_end;
            const uint32_t *rule_separator = s->bytecode + rule[1];
            const uint32_t *rule_subpattern = s->bytecode + rule[2];

            const uint8_t *chunk_start = text;
            const uint8_t *chunk_end = NULL;

            while (text <= saved_end) {
                /* Find next split (or end of text) */
                CapState cs = cap_save(s);
                down1(s);
                while (text <= saved_end) {
                    chunk_end = text;
                    const uint8_t *check = peg_rule(s, rule_separator, text);
                    cap_load(s, cs);
                    if (check) {
                        text = check;
                        break;
                    }
                    text++;
                }
                up1(s);

                /* Match between splits */
                s->text_end = chunk_end;
                down1(s);
                const uint8_t *subpattern_end = peg_rule(s, rule_subpattern, chunk_start);
                up1(s);
                s->text_end = saved_end;
                if (!subpattern_end) return NULL; /* Don't match anything */

                /* Ensure forward progress */
                if (text == chunk_start) return NULL;
                chunk_start = text;
            }

            s->text_end = saved_end;
            return s->text_end;
        }

        case RULE_REPLACE:
        case RULE_MATCHTIME: {
            uint32_t tag = rule[3];
            int oldmode = s->mode;
            CapState cs = cap_save(s);
            s->mode = PEG_MODE_NORMAL;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            s->mode = oldmode;
            if (!result) return NULL;

            Janet cap = janet_wrap_nil();
            Janet constant = s->constants[rule[2]];
            switch (janet_type(constant)) {
                default:
                    cap = constant;
                    break;
                case JANET_STRUCT:
                    if (s->captures->count) {
                        cap = janet_struct_get(janet_unwrap_struct(constant),
                                               s->captures->data[s->captures->count - 1]);
                    }
                    break;
                case JANET_TABLE:
                    if (s->captures->count) {
                        cap = janet_table_get(janet_unwrap_table(constant),
                                              s->captures->data[s->captures->count - 1]);
                    }
                    break;
                case JANET_CFUNCTION:
                    cap = janet_unwrap_cfunction(constant)(s->captures->count - cs.cap,
                                                           s->captures->data + cs.cap);
                    break;
                case JANET_FUNCTION:
                    cap = janet_call(janet_unwrap_function(constant),
                                     s->captures->count - cs.cap,
                                     s->captures->data + cs.cap);
                    break;
            }
            cap_load_keept(s, cs);
            if (rule[0] == RULE_MATCHTIME && !janet_truthy(cap)) return NULL;
            pushcap(s, cap, tag);
            return result;
        }

        case RULE_ERROR: {
            int oldmode = s->mode;
            s->mode = PEG_MODE_NORMAL;
            int32_t old_cap = s->captures->count;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            s->mode = oldmode;
            if (!result) return NULL;
            if (s->captures->count > old_cap) {
                /* Throw last capture */
                janet_panicv(s->captures->data[s->captures->count - 1]);
            } else {
                /* Throw generic error */
                int32_t start = (int32_t)(text - s->text_start);
                LineCol lc = get_linecol_from_position(s, start);
                janet_panicf("match error at line %d, column %d", lc.line, lc.col);
            }
            return NULL;
        }

        case RULE_BACKMATCH: {
            uint32_t search = rule[1];
            for (int32_t i = s->tags->count - 1; i >= 0; i--) {
                if (s->tags->data[i] == search) {
                    Janet capture = s->tagged_captures->data[i];
                    if (!janet_checktype(capture, JANET_STRING))
                        return NULL;
                    const uint8_t *bytes = janet_unwrap_string(capture);
                    int32_t len = janet_string_length(bytes);
                    if (text + len > s->text_end)
                        return NULL;
                    return memcmp(text, bytes, len) ? NULL : text + len;
                }
            }
            return NULL;
        }

        case RULE_LENPREFIX: {
            int oldmode = s->mode;
            s->mode = PEG_MODE_NORMAL;
            const uint8_t *next_text;
            CapState cs = cap_save(s);
            down1(s);
            next_text = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (NULL == next_text) return NULL;
            s->mode = oldmode;
            int32_t num_sub_captures = s->captures->count - cs.cap;
            Janet lencap;
            if (num_sub_captures <= 0 ||
                    (lencap = s->captures->data[cs.cap], !janet_checkint(lencap))) {
                cap_load(s, cs);
                return NULL;
            }
            int32_t nrep = janet_unwrap_integer(lencap);
            /* drop captures from len pattern */
            cap_load(s, cs);
            for (int32_t i = 0; i < nrep; i++) {
                down1(s);
                next_text = peg_rule(s, s->bytecode + rule[2], next_text);
                up1(s);
                if (NULL == next_text) {
                    cap_load(s, cs);
                    return NULL;
                }
            }
            return next_text;
        }

        case RULE_READINT: {
            uint32_t tag = rule[2];
            uint32_t signedness = rule[1] & 0x10;
            uint32_t endianness = rule[1] & 0x20;
            int width = (int)(rule[1] & 0xF);
            if (text + width > s->text_end) return NULL;
            uint64_t accum = 0;
            if (endianness) {
                /* BE */
                for (int i = 0; i < width; i++) accum = (accum << 8) | text[i];
            } else {
                /* LE */
                for (int i = width - 1; i >= 0; i--) accum = (accum << 8) | text[i];
            }

            Janet capture_value;
            /* We can only parse integeres of greater than 6 bytes reliable if int-types are enabled.
             * Otherwise, we may lose precision, so 6 is the maximum size when int-types are disabled. */
#ifdef JANET_INT_TYPES
            if (width > 6) {
                if (signedness) {
                    capture_value = janet_wrap_s64(peg_convert_u64_s64(accum, width));
                } else {
                    capture_value = janet_wrap_u64(accum);
                }
            } else
#endif
            {
                double double_value;
                if (signedness) {
                    double_value = (double)(peg_convert_u64_s64(accum, width));
                } else {
                    double_value = (double)accum;
                }
                capture_value = janet_wrap_number(double_value);
            }

            pushcap(s, capture_value, tag);
            return text + width;
        }

        case RULE_UNREF: {
            int32_t tcap = s->tags->count;
            down1(s);
            const uint8_t *result = peg_rule(s, s->bytecode + rule[1], text);
            up1(s);
            if (!result) return NULL;
            int32_t final_tcap = s->tags->count;
            /* Truncate tagged captures to not include items of the given tag */
            int32_t w = tcap;
            /* If no tag is given, drop ALL tagged captures */
            if (rule[2]) {
                for (int32_t i = tcap; i < final_tcap; i++) {
                    if (s->tags->data[i] != (0xFF & rule[2])) {
                        s->tags->data[w] = s->tags->data[i];
                        s->tagged_captures->data[w] = s->tagged_captures->data[i];
                        w++;
                    }
                }
            }
            s->tags->count = w;
            s->tagged_captures->count = w;
            return result;
        }

    }
}

/*
 * Compilation
 */

typedef struct {
    JanetTable *grammar;
    JanetTable *default_grammar;
    JanetTable *tags;
    Janet *constants;
    uint32_t *bytecode;
    Janet form;
    int depth;
    uint32_t nexttag;
    int has_backref;
} Builder;

/* Forward declaration to allow recursion */
static uint32_t peg_compile1(Builder *b, Janet peg);

/*
 * Errors
 */

static void builder_cleanup(Builder *b) {
    janet_v_free(b->constants);
    janet_v_free(b->bytecode);
}

JANET_NO_RETURN static void peg_panic(Builder *b, const char *msg) {
    builder_cleanup(b);
    janet_panicf("grammar error in %p, %s", b->form, msg);
}

#define peg_panicf(b,...) peg_panic((b), (const char *) janet_formatc(__VA_ARGS__))

static void peg_fixarity(Builder *b, int32_t argc, int32_t arity) {
    if (argc != arity) {
        peg_panicf(b, "expected %d argument%s, got %d",
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
        peg_panic(b, "expected string for character set");
    const uint8_t *str = janet_unwrap_string(x);
    return str;
}

static const uint8_t *peg_getrange(Builder *b, Janet x) {
    if (!janet_checktype(x, JANET_STRING))
        peg_panic(b, "expected string for character range");
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
 * Emission
 */

static uint32_t emit_constant(Builder *b, Janet c) {
    uint32_t cindex = (uint32_t) janet_v_count(b->constants);
    janet_v_push(b->constants, c);
    return cindex;
}

static uint32_t emit_tag(Builder *b, Janet t) {
    if (!janet_checktype(t, JANET_KEYWORD))
        peg_panicf(b, "expected keyword for capture tag, got %v", t);
    Janet check = janet_table_get(b->tags, t);
    if (janet_checktype(check, JANET_NIL)) {
        uint32_t tag = b->nexttag++;
        if (tag > 255) {
            peg_panic(b, "too many tags - up to 255 tags are supported per peg");
        }
        Janet val = janet_wrap_number(tag);
        janet_table_put(b->tags, t, val);
        return tag;
    } else {
        return (uint32_t) janet_unwrap_number(check);
    }
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
    emit_rule(r, op, 1, &arg);
}
static void emit_2(Reserve r, uint32_t op, uint32_t arg1, uint32_t arg2) {
    uint32_t arr[2] = {arg1, arg2};
    emit_rule(r, op, 2, arr);
}
static void emit_3(Reserve r, uint32_t op, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t arr[3] = {arg1, arg2, arg3};
    emit_rule(r, op, 3, arr);
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
    uint32_t subrule = peg_compile1(b, argv[rulearg]);
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
        uint32_t rulei = peg_compile1(b, argv[i]);
        b->bytecode[rule + 2 + i] = rulei;
    }
}

static void spec_choice(Builder *b, int32_t argc, const Janet *argv) {
    spec_variadic(b, argc, argv, RULE_CHOICE);
}
static void spec_sequence(Builder *b, int32_t argc, const Janet *argv) {
    spec_variadic(b, argc, argv, RULE_SEQUENCE);
}

/* For (if a b) and (if-not a b) */
static void spec_branch(Builder *b, int32_t argc, const Janet *argv, uint32_t rule) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t rule_a = peg_compile1(b, argv[0]);
    uint32_t rule_b = peg_compile1(b, argv[1]);
    emit_2(r, rule, rule_a, rule_b);
}

static void spec_if(Builder *b, int32_t argc, const Janet *argv) {
    spec_branch(b, argc, argv, RULE_IF);
}
static void spec_ifnot(Builder *b, int32_t argc, const Janet *argv) {
    spec_branch(b, argc, argv, RULE_IFNOT);
}
static void spec_lenprefix(Builder *b, int32_t argc, const Janet *argv) {
    spec_branch(b, argc, argv, RULE_LENPREFIX);
}

static void spec_between(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 3);
    Reserve r = reserve(b, 4);
    int32_t lo = peg_getnat(b, argv[0]);
    int32_t hi = peg_getnat(b, argv[1]);
    uint32_t subrule = peg_compile1(b, argv[2]);
    emit_3(r, RULE_BETWEEN, lo, hi, subrule);
}

static void spec_repeater(Builder *b, int32_t argc, const Janet *argv, int32_t min) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 4);
    uint32_t subrule = peg_compile1(b, argv[0]);
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
    uint32_t subrule = peg_compile1(b, argv[1]);
    emit_3(r, RULE_BETWEEN, n, UINT32_MAX, subrule);
}

static void spec_atmost(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 4);
    int32_t n = peg_getnat(b, argv[0]);
    uint32_t subrule = peg_compile1(b, argv[1]);
    emit_3(r, RULE_BETWEEN, 0, n, subrule);
}

static void spec_opt(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 4);
    uint32_t subrule = peg_compile1(b, argv[0]);
    emit_3(r, RULE_BETWEEN, 0, 1, subrule);
}

static void spec_repeat(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 4);
    int32_t n = peg_getnat(b, argv[0]);
    uint32_t subrule = peg_compile1(b, argv[1]);
    emit_3(r, RULE_BETWEEN, n, n, subrule);
}

/* Rule of the form [rule] */
static void spec_onerule(Builder *b, int32_t argc, const Janet *argv, uint32_t op) {
    peg_fixarity(b, argc, 1);
    Reserve r = reserve(b, 2);
    uint32_t rule = peg_compile1(b, argv[0]);
    emit_1(r, op, rule);
}

static void spec_not(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_NOT);
}
static void spec_error(Builder *b, int32_t argc, const Janet *argv) {
    if (argc == 0) {
        Reserve r = reserve(b, 2);
        uint32_t rule = peg_compile1(b, janet_wrap_number(0));
        emit_1(r, RULE_ERROR, rule);
    } else {
        spec_onerule(b, argc, argv, RULE_ERROR);
    }
}
static void spec_to(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_TO);
}
static void spec_thru(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_THRU);
}
static void spec_drop(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_DROP);
}
static void spec_only_tags(Builder *b, int32_t argc, const Janet *argv) {
    spec_onerule(b, argc, argv, RULE_ONLY_TAGS);
}

/* Rule of the form [rule, tag] */
static void spec_cap1(Builder *b, int32_t argc, const Janet *argv, uint32_t op) {
    peg_arity(b, argc, 1, 2);
    Reserve r = reserve(b, 3);
    uint32_t tag = (argc == 2) ? emit_tag(b, argv[1]) : 0;
    uint32_t rule = peg_compile1(b, argv[0]);
    emit_2(r, op, rule, tag);
}

static void spec_capture(Builder *b, int32_t argc, const Janet *argv) {
    spec_cap1(b, argc, argv, RULE_CAPTURE);
}
static void spec_accumulate(Builder *b, int32_t argc, const Janet *argv) {
    spec_cap1(b, argc, argv, RULE_ACCUMULATE);
}
static void spec_group(Builder *b, int32_t argc, const Janet *argv) {
    spec_cap1(b, argc, argv, RULE_GROUP);
}
static void spec_unref(Builder *b, int32_t argc, const Janet *argv) {
    spec_cap1(b, argc, argv, RULE_UNREF);
}

static void spec_nth(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 2, 3);
    Reserve r = reserve(b, 4);
    uint32_t nth = peg_getnat(b, argv[0]);
    uint32_t rule = peg_compile1(b, argv[1]);
    uint32_t tag = (argc == 3) ? emit_tag(b, argv[2]) : 0;
    emit_3(r, RULE_NTH, nth, rule, tag);
}

static void spec_capture_number(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 1, 3);
    Reserve r = reserve(b, 4);
    uint32_t base = 0;
    if (argc >= 2) {
        if (!janet_checktype(argv[1], JANET_NIL)) {
            if (!janet_checkint(argv[1])) goto error;
            base = (uint32_t) janet_unwrap_integer(argv[1]);
            if (base < 2 || base > 36) goto error;
        }
    }
    uint32_t tag = (argc == 3) ? emit_tag(b, argv[2]) : 0;
    uint32_t rule = peg_compile1(b, argv[0]);
    emit_3(r, RULE_CAPTURE_NUM, rule, base, tag);
    return;
error:
    peg_panicf(b, "expected integer between 2 and 36, got %v", argv[1]);
}

static void spec_reference(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 1, 2);
    Reserve r = reserve(b, 3);
    uint32_t search = emit_tag(b, argv[0]);
    uint32_t tag = (argc == 2) ? emit_tag(b, argv[1]) : 0;
    b->has_backref = 1;
    emit_2(r, RULE_GETTAG, search, tag);
}

static void spec_tag1(Builder *b, int32_t argc, const Janet *argv, uint32_t op) {
    peg_arity(b, argc, 0, 1);
    Reserve r = reserve(b, 2);
    uint32_t tag = (argc) ? emit_tag(b, argv[0]) : 0;
    (void) argv;
    emit_1(r, op, tag);
}

static void spec_position(Builder *b, int32_t argc, const Janet *argv) {
    spec_tag1(b, argc, argv, RULE_POSITION);
}
static void spec_line(Builder *b, int32_t argc, const Janet *argv) {
    spec_tag1(b, argc, argv, RULE_LINE);
}
static void spec_column(Builder *b, int32_t argc, const Janet *argv) {
    spec_tag1(b, argc, argv, RULE_COLUMN);
}

static void spec_backmatch(Builder *b, int32_t argc, const Janet *argv) {
    b->has_backref = 1;
    spec_tag1(b, argc, argv, RULE_BACKMATCH);
}

static void spec_argument(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 1, 2);
    Reserve r = reserve(b, 3);
    uint32_t tag = (argc == 2) ? emit_tag(b, argv[1]) : 0;
    int32_t index = peg_getnat(b, argv[0]);
    emit_2(r, RULE_ARGUMENT, index, tag);
}

static void spec_constant(Builder *b, int32_t argc, const Janet *argv) {
    janet_arity(argc, 1, 2);
    Reserve r = reserve(b, 3);
    uint32_t tag = (argc == 2) ? emit_tag(b, argv[1]) : 0;
    emit_2(r, RULE_CONSTANT, emit_constant(b, argv[0]), tag);
}

static void spec_replace(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 2, 3);
    Reserve r = reserve(b, 4);
    uint32_t subrule = peg_compile1(b, argv[0]);
    uint32_t constant = emit_constant(b, argv[1]);
    uint32_t tag = (argc == 3) ? emit_tag(b, argv[2]) : 0;
    emit_3(r, RULE_REPLACE, subrule, constant, tag);
}

static void spec_matchtime(Builder *b, int32_t argc, const Janet *argv) {
    peg_arity(b, argc, 2, 3);
    Reserve r = reserve(b, 4);
    uint32_t subrule = peg_compile1(b, argv[0]);
    Janet fun = argv[1];
    if (!janet_checktype(fun, JANET_FUNCTION) &&
            !janet_checktype(fun, JANET_CFUNCTION)) {
        peg_panicf(b, "expected function or cfunction, got %v", fun);
    }
    uint32_t tag = (argc == 3) ? emit_tag(b, argv[2]) : 0;
    uint32_t cindex = emit_constant(b, fun);
    emit_3(r, RULE_MATCHTIME, subrule, cindex, tag);
}

static void spec_sub(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t subrule1 = peg_compile1(b, argv[0]);
    uint32_t subrule2 = peg_compile1(b, argv[1]);
    emit_2(r, RULE_SUB, subrule1, subrule2);
}

static void spec_split(Builder *b, int32_t argc, const Janet *argv) {
    peg_fixarity(b, argc, 2);
    Reserve r = reserve(b, 3);
    uint32_t subrule1 = peg_compile1(b, argv[0]);
    uint32_t subrule2 = peg_compile1(b, argv[1]);
    emit_2(r, RULE_SPLIT, subrule1, subrule2);
}

#ifdef JANET_INT_TYPES
#define JANET_MAX_READINT_WIDTH 8
#else
#define JANET_MAX_READINT_WIDTH 6
#endif

static void spec_readint(Builder *b, int32_t argc, const Janet *argv, uint32_t mask) {
    peg_arity(b, argc, 1, 2);
    Reserve r = reserve(b, 3);
    uint32_t tag = (argc == 2) ? emit_tag(b, argv[1]) : 0;
    int32_t width = peg_getnat(b, argv[0]);
    if ((width < 0) || (width > JANET_MAX_READINT_WIDTH)) {
        peg_panicf(b, "width must be between 0 and %d, got %d", JANET_MAX_READINT_WIDTH, width);
    }
    emit_2(r, RULE_READINT, mask | ((uint32_t) width), tag);
}

static void spec_uint_le(Builder *b, int32_t argc, const Janet *argv) {
    spec_readint(b, argc, argv, 0x0u);
}
static void spec_int_le(Builder *b, int32_t argc, const Janet *argv) {
    spec_readint(b, argc, argv, 0x10u);
}
static void spec_uint_be(Builder *b, int32_t argc, const Janet *argv) {
    spec_readint(b, argc, argv, 0x20u);
}
static void spec_int_be(Builder *b, int32_t argc, const Janet *argv) {
    spec_readint(b, argc, argv, 0x30u);
}

/* Special compiler form */
typedef void (*Special)(Builder *b, int32_t argc, const Janet *argv);
typedef struct {
    const char *name;
    Special special;
} SpecialPair;

/* Keep in lexical order (vim :sort works well) */
static const SpecialPair peg_specials[] = {
    {"!", spec_not},
    {"$", spec_position},
    {"%", spec_accumulate},
    {"*", spec_sequence},
    {"+", spec_choice},
    {"->", spec_reference},
    {"/", spec_replace},
    {"<-", spec_capture},
    {">", spec_look},
    {"?", spec_opt},
    {"accumulate", spec_accumulate},
    {"any", spec_any},
    {"argument", spec_argument},
    {"at-least", spec_atleast},
    {"at-most", spec_atmost},
    {"backmatch", spec_backmatch},
    {"backref", spec_reference},
    {"between", spec_between},
    {"capture", spec_capture},
    {"choice", spec_choice},
    {"cmt", spec_matchtime},
    {"column", spec_column},
    {"constant", spec_constant},
    {"drop", spec_drop},
    {"error", spec_error},
    {"group", spec_group},
    {"if", spec_if},
    {"if-not", spec_ifnot},
    {"int", spec_int_le},
    {"int-be", spec_int_be},
    {"lenprefix", spec_lenprefix},
    {"line", spec_line},
    {"look", spec_look},
    {"not", spec_not},
    {"nth", spec_nth},
    {"number", spec_capture_number},
    {"only-tags", spec_only_tags},
    {"opt", spec_opt},
    {"position", spec_position},
    {"quote", spec_capture},
    {"range", spec_range},
    {"repeat", spec_repeat},
    {"replace", spec_replace},
    {"sequence", spec_sequence},
    {"set", spec_set},
    {"some", spec_some},
    {"split", spec_split},
    {"sub", spec_sub},
    {"thru", spec_thru},
    {"to", spec_to},
    {"uint", spec_uint_le},
    {"uint-be", spec_uint_be},
    {"unref", spec_unref},
};

/* Compile a janet value into a rule and return the rule index. */
static uint32_t peg_compile1(Builder *b, Janet peg) {

    /* Keep track of the form being compiled for error purposes */
    Janet old_form = b->form;
    JanetTable *old_grammar = b->grammar;
    b->form = peg;

    /* Resolve keyword references */
    int i = JANET_RECURSION_GUARD;
    JanetTable *grammar = old_grammar;
    for (; i > 0 && janet_checktype(peg, JANET_KEYWORD); --i) {
        Janet nextPeg = janet_table_get_ex(grammar, peg, &grammar);
        if (!grammar || janet_checktype(nextPeg, JANET_NIL)) {
            nextPeg = (b->default_grammar == NULL)
                      ? janet_wrap_nil()
                      : janet_table_get(b->default_grammar, peg);
            if (janet_checktype(nextPeg, JANET_NIL)) {
                peg_panic(b, "unknown rule");
            }
        }
        peg = nextPeg;
        b->form = peg;
        b->grammar = grammar;
    }
    if (i == 0)
        peg_panic(b, "reference chain too deep");

    /* Check cache - for tuples we check only the local cache, as
     * in a different grammar, the same tuple can compile to a different
     * rule - for example, (+ :a :b) depends on whatever :a and :b are bound to. */
    Janet check = janet_checktype(peg, JANET_TUPLE)
                  ? janet_table_rawget(grammar, peg)
                  : janet_table_get(grammar, peg);
    if (!janet_checktype(check, JANET_NIL)) {
        b->form = old_form;
        b->grammar = old_grammar;
        return (uint32_t) janet_unwrap_number(check);
    }

    /* Check depth */
    if (b->depth-- == 0)
        peg_panic(b, "peg grammar recursed too deeply");

    /* The final rule to return */
    uint32_t rule = janet_v_count(b->bytecode);

    /* Add to cache. Do not cache structs, as we don't yet know
     * what rule they will return! We can just as effectively cache
     * the structs main rule. */
    if (!janet_checktype(peg, JANET_STRUCT)) {
        JanetTable *which_grammar = grammar;
        /* If we are a primitive pattern, add to the global cache (root grammar table) */
        if (!janet_checktype(peg, JANET_TUPLE)) {
            while (which_grammar->proto)
                which_grammar = which_grammar->proto;
        }
        janet_table_put(which_grammar, peg, janet_wrap_number(rule));
    }

    switch (janet_type(peg)) {
        default:
            peg_panic(b, "unexpected peg source");
            return 0;

        case JANET_BOOLEAN: {
            int n = janet_unwrap_boolean(peg);
            Reserve r = reserve(b, 2);
            emit_1(r, n ? RULE_NCHAR : RULE_NOTNCHAR, 0);
            break;
        }
        case JANET_NUMBER: {
            int32_t n = peg_getinteger(b, peg);
            Reserve r = reserve(b, 2);
            if (n < 0) {
                emit_1(r, RULE_NOTNCHAR, -n);
            } else {
                emit_1(r, RULE_NCHAR, n);
            }
            break;
        }
        case JANET_STRING: {
            const uint8_t *str = janet_unwrap_string(peg);
            int32_t len = janet_string_length(str);
            emit_bytes(b, RULE_LITERAL, len, str);
            break;
        }
        case JANET_TABLE: {
            /* Build grammar table */
            JanetTable *new_grammar = janet_table_clone(janet_unwrap_table(peg));
            new_grammar->proto = grammar;
            b->grammar = grammar = new_grammar;
            /* Run the main rule */
            Janet main_rule = janet_table_rawget(grammar, janet_ckeywordv("main"));
            if (janet_checktype(main_rule, JANET_NIL))
                peg_panic(b, "grammar requires :main rule");
            rule = peg_compile1(b, main_rule);
            break;
        }
        case JANET_STRUCT: {
            /* Build grammar table */
            const JanetKV *st = janet_unwrap_struct(peg);
            JanetTable *new_grammar = janet_table(2 * janet_struct_capacity(st));
            for (int32_t i = 0; i < janet_struct_capacity(st); i++) {
                if (janet_checktype(st[i].key, JANET_KEYWORD)) {
                    janet_table_put(new_grammar, st[i].key, st[i].value);
                }
            }
            new_grammar->proto = grammar;
            b->grammar = grammar = new_grammar;
            /* Run the main rule */
            Janet main_rule = janet_table_rawget(grammar, janet_ckeywordv("main"));
            if (janet_checktype(main_rule, JANET_NIL))
                peg_panic(b, "grammar requires :main rule");
            rule = peg_compile1(b, main_rule);
            break;
        }
        case JANET_TUPLE: {
            const Janet *tup = janet_unwrap_tuple(peg);
            int32_t len = janet_tuple_length(tup);
            if (len == 0) peg_panic(b, "tuple in grammar must have non-zero length");
            if (janet_checkint(tup[0])) {
                int32_t n = janet_unwrap_integer(tup[0]);
                if (n < 0) {
                    peg_panicf(b, "expected non-negative integer, got %d", n);
                }
                spec_repeat(b, len, tup);
                break;
            }
            if (!janet_checktype(tup[0], JANET_SYMBOL))
                peg_panicf(b, "expected grammar command, found %v", tup[0]);
            const uint8_t *sym = janet_unwrap_symbol(tup[0]);
            const SpecialPair *sp = janet_strbinsearch(
                                        &peg_specials,
                                        sizeof(peg_specials) / sizeof(SpecialPair),
                                        sizeof(SpecialPair),
                                        sym);
            if (sp) {
                sp->special(b, len - 1, tup + 1);
            } else {
                peg_panicf(b, "unknown special %S", sym);
            }
            break;
        }
    }

    /* Increase depth again */
    b->depth++;
    b->form = old_form;
    b->grammar = old_grammar;
    return rule;
}

/*
 * Post-Compilation
 */

static int peg_mark(void *p, size_t size) {
    (void) size;
    JanetPeg *peg = (JanetPeg *)p;
    if (NULL != peg->constants)
        for (uint32_t i = 0; i < peg->num_constants; i++)
            janet_mark(peg->constants[i]);
    return 0;
}

static void peg_marshal(void *p, JanetMarshalContext *ctx) {
    JanetPeg *peg = (JanetPeg *)p;
    janet_marshal_size(ctx, peg->bytecode_len);
    janet_marshal_int(ctx, (int32_t)peg->num_constants);
    janet_marshal_abstract(ctx, p);
    for (size_t i = 0; i < peg->bytecode_len; i++)
        janet_marshal_int(ctx, (int32_t) peg->bytecode[i]);
    for (uint32_t j = 0; j < peg->num_constants; j++)
        janet_marshal_janet(ctx, peg->constants[j]);
}

/* Used to ensure that if we place several arrays in one memory chunk, each
 * array will be correctly aligned */
static size_t size_padded(size_t offset, size_t size) {
    size_t x = size + offset - 1;
    return x - (x % size);
}

static void *peg_unmarshal(JanetMarshalContext *ctx) {
    size_t bytecode_len = janet_unmarshal_size(ctx);
    uint32_t num_constants = (uint32_t) janet_unmarshal_int(ctx);

    /* Calculate offsets. Should match those in make_peg */
    size_t bytecode_start = size_padded(sizeof(JanetPeg), sizeof(uint32_t));
    size_t bytecode_size = bytecode_len * sizeof(uint32_t);
    size_t constants_start = size_padded(bytecode_start + bytecode_size, sizeof(Janet));
    size_t total_size = constants_start + sizeof(Janet) * (size_t) num_constants;

    /* DOS prevention? I.E. we could read bytecode and constants before
     * hand so we don't allocated a ton of memory on bad, short input */

    /* Allocate PEG */
    char *mem = janet_unmarshal_abstract(ctx, total_size);
    JanetPeg *peg = (JanetPeg *)mem;
    uint32_t *bytecode = (uint32_t *)(mem + bytecode_start);
    Janet *constants = (Janet *)(mem + constants_start);
    peg->bytecode = NULL;
    peg->constants = NULL;
    peg->bytecode_len = bytecode_len;
    peg->num_constants = num_constants;

    for (size_t i = 0; i < peg->bytecode_len; i++)
        bytecode[i] = (uint32_t) janet_unmarshal_int(ctx);
    for (uint32_t j = 0; j < peg->num_constants; j++)
        constants[j] = janet_unmarshal_janet(ctx);

    /* After here, no panics except for the bad: label. */

    /* Keep track at each index if an instruction was
     * reference (0x01) or is in a main bytecode position
     * (0x02). This lets us do a linear scan and not
     * need to a depth first traversal. It is stricter
     * than a dfs by not allowing certain kinds of unused
     * bytecode. */
    uint32_t blen = (int32_t) peg->bytecode_len;
    uint32_t clen = peg->num_constants;
    uint8_t *op_flags = janet_calloc(1, blen);
    if (NULL == op_flags) {
        JANET_OUT_OF_MEMORY;
    }

    /* verify peg bytecode */
    int32_t has_backref = 0;
    uint32_t i = 0;
    while (i < blen) {
        uint32_t instr = bytecode[i];
        uint32_t *rule = bytecode + i;
        op_flags[i] |= 0x02;
        switch (instr) {
            case RULE_LITERAL:
                i += 2 + ((rule[1] + 3) >> 2);
                break;
            case RULE_NCHAR:
            case RULE_NOTNCHAR:
            case RULE_RANGE:
            case RULE_POSITION:
            case RULE_LINE:
            case RULE_COLUMN:
                /* [1 word] */
                i += 2;
                break;
            case RULE_BACKMATCH:
                /* [1 word] */
                i += 2;
                has_backref = 1;
                break;
            case RULE_SET:
                /* [8 words] */
                i += 9;
                break;
            case RULE_LOOK:
                /* [offset, rule] */
                if (rule[2] >= blen) goto bad;
                op_flags[rule[2]] |= 0x1;
                i += 3;
                break;
            case RULE_CHOICE:
            case RULE_SEQUENCE:
                /* [len, rules...] */
            {
                uint32_t len = rule[1];
                for (uint32_t j = 0; j < len; j++) {
                    if (rule[2 + j] >= blen) goto bad;
                    op_flags[rule[2 + j]] |= 0x1;
                }
                i += 2 + len;
            }
            break;
            case RULE_IF:
            case RULE_IFNOT:
            case RULE_LENPREFIX:
                /* [rule_a, rule_b (b if not a)] */
                if (rule[1] >= blen) goto bad;
                if (rule[2] >= blen) goto bad;
                op_flags[rule[1]] |= 0x01;
                op_flags[rule[2]] |= 0x01;
                i += 3;
                break;
            case RULE_BETWEEN:
                /* [lo, hi, rule] */
                if (rule[3] >= blen) goto bad;
                op_flags[rule[3]] |= 0x01;
                i += 4;
                break;
            case RULE_ARGUMENT:
                /* [searchtag, tag] */
                i += 3;
                break;
            case RULE_GETTAG:
                /* [searchtag, tag] */
                i += 3;
                has_backref = 1;
                break;
            case RULE_CONSTANT:
                /* [constant, tag] */
                if (rule[1] >= clen) goto bad;
                i += 3;
                break;
            case RULE_CAPTURE_NUM:
                /* [rule, base, tag] */
                if (rule[1] >= blen) goto bad;
                op_flags[rule[1]] |= 0x01;
                i += 4;
                break;
            case RULE_ACCUMULATE:
            case RULE_GROUP:
            case RULE_CAPTURE:
            case RULE_UNREF:
                /* [rule, tag] */
                if (rule[1] >= blen) goto bad;
                op_flags[rule[1]] |= 0x01;
                i += 3;
                break;
            case RULE_REPLACE:
            case RULE_MATCHTIME:
                /* [rule, constant, tag] */
                if (rule[1] >= blen) goto bad;
                if (rule[2] >= clen) goto bad;
                op_flags[rule[1]] |= 0x01;
                i += 4;
                break;
            case RULE_SUB:
            case RULE_SPLIT:
                /* [rule, rule] */
                if (rule[1] >= blen) goto bad;
                if (rule[2] >= blen) goto bad;
                op_flags[rule[1]] |= 0x01;
                op_flags[rule[2]] |= 0x01;
                i += 3;
                break;
            case RULE_ERROR:
            case RULE_DROP:
            case RULE_ONLY_TAGS:
            case RULE_NOT:
            case RULE_TO:
            case RULE_THRU:
                /* [rule] */
                if (rule[1] >= blen) goto bad;
                op_flags[rule[1]] |= 0x01;
                i += 2;
                break;
            case RULE_READINT:
                /* [ width | (endianness << 5) | (signedness << 6), tag ] */
                if (rule[1] > JANET_MAX_READINT_WIDTH) goto bad;
                i += 3;
                break;
            case RULE_NTH:
                /* [nth, rule, tag] */
                if (rule[2] >= blen) goto bad;
                op_flags[rule[2]] |= 0x01;
                i += 4;
                break;
            default:
                goto bad;
        }
    }

    /* last instruction cannot overflow */
    if (i != blen) goto bad;

    /* Make sure all referenced instructions are actually
     * in instruction positions. */
    for (i = 0; i < blen; i++)
        if (op_flags[i] == 0x01) goto bad;

    /* Good return */
    peg->bytecode = bytecode;
    peg->constants = constants;
    peg->has_backref = has_backref;
    janet_free(op_flags);
    return peg;

bad:
    janet_free(op_flags);
    janet_panic("invalid peg bytecode");
}

static int cfun_peg_getter(JanetAbstract a, Janet key, Janet *out);
static Janet peg_next(void *p, Janet key);

const JanetAbstractType janet_peg_type = {
    "core/peg",
    NULL,
    peg_mark,
    cfun_peg_getter,
    NULL, /* put */
    peg_marshal,
    peg_unmarshal,
    NULL, /* tostring */
    NULL, /* compare */
    NULL, /* hash */
    peg_next,
    JANET_ATEND_NEXT
};

/* Convert Builder to JanetPeg (Janet Abstract Value) */
static JanetPeg *make_peg(Builder *b) {
    size_t bytecode_start = size_padded(sizeof(JanetPeg), sizeof(uint32_t));
    size_t bytecode_size = janet_v_count(b->bytecode) * sizeof(uint32_t);
    size_t constants_start = size_padded(bytecode_start + bytecode_size, sizeof(Janet));
    size_t constants_size = janet_v_count(b->constants) * sizeof(Janet);
    size_t total_size = constants_start + constants_size;
    char *mem = janet_abstract(&janet_peg_type, total_size);
    JanetPeg *peg = (JanetPeg *)mem;
    peg->bytecode = (uint32_t *)(mem + bytecode_start);
    peg->constants = (Janet *)(mem + constants_start);
    peg->num_constants = janet_v_count(b->constants);
    safe_memcpy(peg->bytecode, b->bytecode, bytecode_size);
    safe_memcpy(peg->constants, b->constants, constants_size);
    peg->bytecode_len = janet_v_count(b->bytecode);
    peg->has_backref = b->has_backref;
    return peg;
}

/* Compiler entry point */
static JanetPeg *compile_peg(Janet x) {
    Builder builder;
    builder.grammar = janet_table(0);
    builder.default_grammar = NULL;
    {
        Janet default_grammarv = janet_dyn("peg-grammar");
        if (janet_checktype(default_grammarv, JANET_TABLE)) {
            builder.default_grammar = janet_unwrap_table(default_grammarv);
        }
    }
    builder.tags = janet_table(0);
    builder.constants = NULL;
    builder.bytecode = NULL;
    builder.nexttag = 1;
    builder.form = x;
    builder.depth = JANET_RECURSION_GUARD;
    builder.has_backref = 0;
    peg_compile1(&builder, x);
    JanetPeg *peg = make_peg(&builder);
    builder_cleanup(&builder);
    return peg;
}

/*
 * C Functions
 */

JANET_CORE_FN(cfun_peg_compile,
              "(peg/compile peg)",
              "Compiles a peg source data structure into a <core/peg>. This will speed up matching "
              "if the same peg will be used multiple times. Will also use `(dyn :peg-grammar)` to supplement "
              "the grammar of the peg for otherwise undefined peg keywords.") {
    janet_fixarity(argc, 1);
    JanetPeg *peg = compile_peg(argv[0]);
    return janet_wrap_abstract(peg);
}

/* Common data for peg cfunctions */
typedef struct {
    JanetPeg *peg;
    PegState s;
    JanetByteView bytes;
    Janet subst;
    int32_t start;
} PegCall;

/* Initialize state for peg cfunctions */
static PegCall peg_cfun_init(int32_t argc, Janet *argv, int get_replace) {
    PegCall ret;
    int32_t min = get_replace ? 3 : 2;
    janet_arity(argc, min, -1);
    if (janet_checktype(argv[0], JANET_ABSTRACT) &&
            janet_abstract_type(janet_unwrap_abstract(argv[0])) == &janet_peg_type) {
        ret.peg = janet_unwrap_abstract(argv[0]);
    } else {
        ret.peg = compile_peg(argv[0]);
    }
    if (get_replace) {
        ret.subst = argv[1];
        ret.bytes = janet_getbytes(argv, 2);
    } else {
        ret.bytes = janet_getbytes(argv, 1);
    }
    if (argc > min) {
        ret.start = janet_gethalfrange(argv, min, ret.bytes.len, "offset");
        ret.s.extrac = argc - min - 1;
        ret.s.extrav = janet_tuple_n(argv + min + 1, argc - min - 1);
    } else {
        ret.start = 0;
        ret.s.extrac = 0;
        ret.s.extrav = NULL;
    }
    ret.s.mode = PEG_MODE_NORMAL;
    ret.s.text_start = ret.bytes.bytes;
    ret.s.text_end = ret.bytes.bytes + ret.bytes.len;
    ret.s.outer_text_end = ret.s.text_end;
    ret.s.depth = JANET_RECURSION_GUARD;
    ret.s.captures = janet_array(0);
    ret.s.tagged_captures = janet_array(0);
    ret.s.scratch = janet_buffer(10);
    ret.s.tags = janet_buffer(10);
    ret.s.constants = ret.peg->constants;
    ret.s.bytecode = ret.peg->bytecode;
    ret.s.linemap = NULL;
    ret.s.linemaplen = -1;
    ret.s.has_backref = ret.peg->has_backref;
    return ret;
}

static void peg_call_reset(PegCall *c) {
    c->s.depth = JANET_RECURSION_GUARD;
    c->s.captures->count = 0;
    c->s.tagged_captures->count = 0;
    c->s.scratch->count = 0;
    c->s.tags->count = 0;
}

JANET_CORE_FN(cfun_peg_match,
              "(peg/match peg text &opt start & args)",
              "Match a Parsing Expression Grammar to a byte string and return an array of captured values. "
              "Returns nil if text does not match the language defined by peg. The syntax of PEGs is documented on the Janet website.") {
    PegCall c = peg_cfun_init(argc, argv, 0);
    const uint8_t *result = peg_rule(&c.s, c.s.bytecode, c.bytes.bytes + c.start);
    return result ? janet_wrap_array(c.s.captures) : janet_wrap_nil();
}

JANET_CORE_FN(cfun_peg_find,
              "(peg/find peg text &opt start & args)",
              "Find first index where the peg matches in text. Returns an integer, or nil if not found.") {
    PegCall c = peg_cfun_init(argc, argv, 0);
    for (int32_t i = c.start; i < c.bytes.len; i++) {
        peg_call_reset(&c);
        if (peg_rule(&c.s, c.s.bytecode, c.bytes.bytes + i))
            return janet_wrap_integer(i);
    }
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_peg_find_all,
              "(peg/find-all peg text &opt start & args)",
              "Find all indexes where the peg matches in text. Returns an array of integers.") {
    PegCall c = peg_cfun_init(argc, argv, 0);
    JanetArray *ret = janet_array(0);
    for (int32_t i = c.start; i < c.bytes.len; i++) {
        peg_call_reset(&c);
        if (peg_rule(&c.s, c.s.bytecode, c.bytes.bytes + i))
            janet_array_push(ret, janet_wrap_integer(i));
    }
    return janet_wrap_array(ret);
}

static Janet cfun_peg_replace_generic(int32_t argc, Janet *argv, int only_one) {
    PegCall c = peg_cfun_init(argc, argv, 1);
    JanetBuffer *ret = janet_buffer(0);
    int32_t trail = 0;
    for (int32_t i = c.start; i < c.bytes.len;) {
        peg_call_reset(&c);
        const uint8_t *result = peg_rule(&c.s, c.s.bytecode, c.bytes.bytes + i);
        if (NULL != result) {
            if (trail < i) {
                janet_buffer_push_bytes(ret, c.bytes.bytes + trail, (i - trail));
                trail = i;
            }
            int32_t nexti = (int32_t)(result - c.bytes.bytes);
            JanetByteView subst = janet_text_substitution(&c.subst, c.bytes.bytes + i, nexti - i, c.s.captures);
            janet_buffer_push_bytes(ret, subst.bytes, subst.len);
            trail = nexti;
            if (nexti == i) nexti++;
            i = nexti;
            if (only_one) break;
        } else {
            i++;
        }
    }
    if (trail < c.bytes.len) {
        janet_buffer_push_bytes(ret, c.bytes.bytes + trail, (c.bytes.len - trail));
    }
    return janet_wrap_buffer(ret);
}

JANET_CORE_FN(cfun_peg_replace_all,
              "(peg/replace-all peg subst text &opt start & args)",
              "Replace all matches of `peg` in `text` with `subst`, returning a new buffer. "
              "The peg does not need to make captures to do replacement. "
              "If `subst` is a function, it will be called with the "
              "matching text followed by any captures.") {
    return cfun_peg_replace_generic(argc, argv, 0);
}

JANET_CORE_FN(cfun_peg_replace,
              "(peg/replace peg subst text &opt start & args)",
              "Replace first match of `peg` in `text` with `subst`, returning a new buffer. "
              "The peg does not need to make captures to do replacement. "
              "If `subst` is a function, it will be called with the "
              "matching text followed by any captures. "
              "If no matches are found, returns the input string in a new buffer.") {
    return cfun_peg_replace_generic(argc, argv, 1);
}

static JanetMethod peg_methods[] = {
    {"match", cfun_peg_match},
    {"find", cfun_peg_find},
    {"find-all", cfun_peg_find_all},
    {"replace", cfun_peg_replace},
    {"replace-all", cfun_peg_replace_all},
    {NULL, NULL}
};

static int cfun_peg_getter(JanetAbstract a, Janet key, Janet *out) {
    (void) a;
    if (!janet_checktype(key, JANET_KEYWORD))
        return 0;
    return janet_getmethod(janet_unwrap_keyword(key), peg_methods, out);
}

static Janet peg_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(peg_methods, key);
}

/* Load the peg module */
void janet_lib_peg(JanetTable *env) {
    JanetRegExt cfuns[] = {
        JANET_CORE_REG("peg/compile", cfun_peg_compile),
        JANET_CORE_REG("peg/match", cfun_peg_match),
        JANET_CORE_REG("peg/find", cfun_peg_find),
        JANET_CORE_REG("peg/find-all", cfun_peg_find_all),
        JANET_CORE_REG("peg/replace", cfun_peg_replace),
        JANET_CORE_REG("peg/replace-all", cfun_peg_replace_all),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, cfuns);
    janet_register_abstract_type(&janet_peg_type);
}

#endif /* ifdef JANET_PEG */

/*
* Copyright (c) 2024 Calvin Rose
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
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

#include <string.h>

/* Begin building a string */
uint8_t *janet_string_begin(int32_t length) {
    JanetStringHead *head = janet_gcalloc(JANET_MEMORY_STRING, sizeof(JanetStringHead) + (size_t) length + 1);
    head->length = length;
    uint8_t *data = (uint8_t *)head->data;
    data[length] = 0;
    return data;
}

/* Finish building a string */
const uint8_t *janet_string_end(uint8_t *str) {
    janet_string_hash(str) = janet_string_calchash(str, janet_string_length(str));
    return str;
}

/* Load a buffer as a string */
const uint8_t *janet_string(const uint8_t *buf, int32_t len) {
    JanetStringHead *head = janet_gcalloc(JANET_MEMORY_STRING, sizeof(JanetStringHead) + (size_t) len + 1);
    head->length = len;
    head->hash = janet_string_calchash(buf, len);
    uint8_t *data = (uint8_t *)head->data;
    safe_memcpy(data, buf, len);
    data[len] = 0;
    return data;
}

/* Compare two strings */
int janet_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    int32_t xlen = janet_string_length(lhs);
    int32_t ylen = janet_string_length(rhs);
    int32_t len = xlen > ylen ? ylen : xlen;
    int res = memcmp(lhs, rhs, len);
    if (res) return res > 0 ? 1 : -1;
    if (xlen == ylen) return 0;
    return xlen < ylen ? -1 : 1;
}

/* Compare a janet string with a piece of memory */
int janet_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash) {
    int32_t lhash = janet_string_hash(lhs);
    int32_t llen = janet_string_length(lhs);
    if (lhash != rhash || llen != rlen)
        return 0;
    if (lhs == rhs)
        return 1;
    return !memcmp(lhs, rhs, rlen);
}

/* Check if two strings are equal */
int janet_string_equal(const uint8_t *lhs, const uint8_t *rhs) {
    return janet_string_equalconst(lhs, rhs,
                                   janet_string_length(rhs), janet_string_hash(rhs));
}

/* Load a c string */
const uint8_t *janet_cstring(const char *str) {
    return janet_string((const uint8_t *)str, (int32_t)strlen(str));
}

/* Knuth Morris Pratt Algorithm */

struct kmp_state {
    int32_t i;
    int32_t j;
    int32_t textlen;
    int32_t patlen;
    int32_t *lookup;
    const uint8_t *text;
    const uint8_t *pat;
};

static void kmp_init(
    struct kmp_state *s,
    const uint8_t *text, int32_t textlen,
    const uint8_t *pat, int32_t patlen) {
    if (patlen == 0) {
        janet_panic("expected non-empty pattern");
    }
    int32_t *lookup = janet_calloc(patlen, sizeof(int32_t));
    if (!lookup) {
        JANET_OUT_OF_MEMORY;
    }
    s->lookup = lookup;
    s->i = 0;
    s->j = 0;
    s->text = text;
    s->pat = pat;
    s->textlen = textlen;
    s->patlen = patlen;
    /* Init state machine */
    {
        int32_t i, j;
        for (i = 1, j = 0; i < patlen; i++) {
            while (j && pat[j] != pat[i]) j = lookup[j - 1];
            if (pat[j] == pat[i]) j++;
            lookup[i] = j;
        }
    }
}

static void kmp_deinit(struct kmp_state *state) {
    janet_free(state->lookup);
}

static void kmp_seti(struct kmp_state *state, int32_t i) {
    state->i = i;
    state->j = 0;
}

static int32_t kmp_next(struct kmp_state *state) {
    int32_t i = state->i;
    int32_t j = state->j;
    int32_t textlen = state->textlen;
    int32_t patlen = state->patlen;
    const uint8_t *text = state->text;
    const uint8_t *pat = state->pat;
    int32_t *lookup = state->lookup;
    while (i < textlen) {
        if (text[i] == pat[j]) {
            if (j == patlen - 1) {
                state->i = i + 1;
                state->j = lookup[j];
                return i - j;
            } else {
                i++;
                j++;
            }
        } else {
            if (j > 0) {
                j = lookup[j - 1];
            } else {
                i++;
            }
        }
    }
    return -1;
}

/* CFuns */

JANET_CORE_FN(cfun_string_slice,
              "(string/slice bytes &opt start end)",
              "Returns a substring from a byte sequence. The substring is from "
              "index `start` inclusive to index `end`, exclusive. All indexing "
              "is from 0. `start` and `end` can also be negative to indicate indexing "
              "from the end of the string. Note that if `start` is negative it is "
              "exclusive, and if `end` is negative it is inclusive, to allow a full "
              "negative slice range.") {
    JanetByteView view = janet_getbytes(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    return janet_stringv(view.bytes + range.start, range.end - range.start);
}

JANET_CORE_FN(cfun_symbol_slice,
              "(symbol/slice bytes &opt start end)",
              "Same as string/slice, but returns a symbol.") {
    JanetByteView view = janet_getbytes(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    return janet_symbolv(view.bytes + range.start, range.end - range.start);
}

JANET_CORE_FN(cfun_keyword_slice,
              "(keyword/slice bytes &opt start end)",
              "Same as string/slice, but returns a keyword.") {
    JanetByteView view = janet_getbytes(argv, 0);
    JanetRange range = janet_getslice(argc, argv);
    return janet_keywordv(view.bytes + range.start, range.end - range.start);
}

JANET_CORE_FN(cfun_string_repeat,
              "(string/repeat bytes n)",
              "Returns a string that is `n` copies of `bytes` concatenated.") {
    janet_fixarity(argc, 2);
    JanetByteView view = janet_getbytes(argv, 0);
    int32_t rep = janet_getinteger(argv, 1);
    if (rep < 0) janet_panic("expected non-negative number of repetitions");
    if (rep == 0) return janet_cstringv("");
    int64_t mulres = (int64_t) rep * view.len;
    if (mulres > INT32_MAX) janet_panic("result string is too long");
    uint8_t *newbuf = janet_string_begin((int32_t) mulres);
    uint8_t *end = newbuf + mulres;
    for (uint8_t *p = newbuf; p < end; p += view.len) {
        safe_memcpy(p, view.bytes, view.len);
    }
    return janet_wrap_string(janet_string_end(newbuf));
}

JANET_CORE_FN(cfun_string_bytes,
              "(string/bytes str)",
              "Returns a tuple of integers that are the byte values of the string.") {
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    Janet *tup = janet_tuple_begin(view.len);
    int32_t i;
    for (i = 0; i < view.len; i++) {
        tup[i] = janet_wrap_integer((int32_t) view.bytes[i]);
    }
    return janet_wrap_tuple(janet_tuple_end(tup));
}

JANET_CORE_FN(cfun_string_frombytes,
              "(string/from-bytes & byte-vals)",
              "Creates a string from integer parameters with byte values. All integers "
              "will be coerced to the range of 1 byte 0-255.") {
    int32_t i;
    uint8_t *buf = janet_string_begin(argc);
    for (i = 0; i < argc; i++) {
        int32_t c = janet_getinteger(argv, i);
        buf[i] = c & 0xFF;
    }
    return janet_wrap_string(janet_string_end(buf));
}

JANET_CORE_FN(cfun_string_asciilower,
              "(string/ascii-lower str)",
              "Returns a new string where all bytes are replaced with the "
              "lowercase version of themselves in ASCII. Does only a very simple "
              "case check, meaning no unicode support.") {
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    uint8_t *buf = janet_string_begin(view.len);
    for (int32_t i = 0; i < view.len; i++) {
        uint8_t c = view.bytes[i];
        if (c >= 65 && c <= 90) {
            buf[i] = c + 32;
        } else {
            buf[i] = c;
        }
    }
    return janet_wrap_string(janet_string_end(buf));
}

JANET_CORE_FN(cfun_string_asciiupper,
              "(string/ascii-upper str)",
              "Returns a new string where all bytes are replaced with the "
              "uppercase version of themselves in ASCII. Does only a very simple "
              "case check, meaning no unicode support.") {
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    uint8_t *buf = janet_string_begin(view.len);
    for (int32_t i = 0; i < view.len; i++) {
        uint8_t c = view.bytes[i];
        if (c >= 97 && c <= 122) {
            buf[i] = c - 32;
        } else {
            buf[i] = c;
        }
    }
    return janet_wrap_string(janet_string_end(buf));
}

JANET_CORE_FN(cfun_string_reverse,
              "(string/reverse str)",
              "Returns a string that is the reversed version of `str`.") {
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    uint8_t *buf = janet_string_begin(view.len);
    int32_t i, j;
    for (i = 0, j = view.len - 1; i < view.len; i++, j--) {
        buf[i] = view.bytes[j];
    }
    return janet_wrap_string(janet_string_end(buf));
}

static void findsetup(int32_t argc, Janet *argv, struct kmp_state *s, int32_t extra) {
    janet_arity(argc, 2, 3 + extra);
    JanetByteView pat = janet_getbytes(argv, 0);
    JanetByteView text = janet_getbytes(argv, 1);
    int32_t start = 0;
    if (argc >= 3) {
        start = janet_getinteger(argv, 2);
        if (start < 0) janet_panic("expected non-negative start index");
    }
    kmp_init(s, text.bytes, text.len, pat.bytes, pat.len);
    s->i = start;
}

JANET_CORE_FN(cfun_string_find,
              "(string/find patt str &opt start-index)",
              "Searches for the first instance of pattern `patt` in string "
              "`str`. Returns the index of the first character in `patt` if found, "
              "otherwise returns nil.") {
    int32_t result;
    struct kmp_state state;
    findsetup(argc, argv, &state, 0);
    result = kmp_next(&state);
    kmp_deinit(&state);
    return result < 0
           ? janet_wrap_nil()
           : janet_wrap_integer(result);
}

JANET_CORE_FN(cfun_string_hasprefix,
              "(string/has-prefix? pfx str)",
              "Tests whether `str` starts with `pfx`.") {
    janet_fixarity(argc, 2);
    JanetByteView prefix = janet_getbytes(argv, 0);
    JanetByteView str = janet_getbytes(argv, 1);
    return str.len < prefix.len
           ? janet_wrap_false()
           : janet_wrap_boolean(memcmp(prefix.bytes, str.bytes, prefix.len) == 0);
}

JANET_CORE_FN(cfun_string_hassuffix,
              "(string/has-suffix? sfx str)",
              "Tests whether `str` ends with `sfx`.") {
    janet_fixarity(argc, 2);
    JanetByteView suffix = janet_getbytes(argv, 0);
    JanetByteView str = janet_getbytes(argv, 1);
    return str.len < suffix.len
           ? janet_wrap_false()
           : janet_wrap_boolean(memcmp(suffix.bytes,
                                       str.bytes + str.len - suffix.len,
                                       suffix.len) == 0);
}

JANET_CORE_FN(cfun_string_findall,
              "(string/find-all patt str &opt start-index)",
              "Searches for all instances of pattern `patt` in string "
              "`str`. Returns an array of all indices of found patterns. Overlapping "
              "instances of the pattern are counted individually, meaning a byte in `str` "
              "may contribute to multiple found patterns.") {
    int32_t result;
    struct kmp_state state;
    findsetup(argc, argv, &state, 0);
    JanetArray *array = janet_array(0);
    while ((result = kmp_next(&state)) >= 0) {
        janet_array_push(array, janet_wrap_integer(result));
    }
    kmp_deinit(&state);
    return janet_wrap_array(array);
}

struct replace_state {
    struct kmp_state kmp;
    Janet subst;
};

static void replacesetup(int32_t argc, Janet *argv, struct replace_state *s) {
    janet_arity(argc, 3, 4);
    JanetByteView pat = janet_getbytes(argv, 0);
    Janet subst = argv[1];
    JanetByteView text = janet_getbytes(argv, 2);
    int32_t start = 0;
    if (argc == 4) {
        start = janet_getinteger(argv, 3);
        if (start < 0) janet_panic("expected non-negative start index");
    }
    kmp_init(&s->kmp, text.bytes, text.len, pat.bytes, pat.len);
    s->kmp.i = start;
    s->subst = subst;
}

JANET_CORE_FN(cfun_string_replace,
              "(string/replace patt subst str)",
              "Replace the first occurrence of `patt` with `subst` in the string `str`. "
              "If `subst` is a function, it will be called with `patt` only if a match is found, "
              "and should return the actual replacement text to use. "
              "Will return the new string if `patt` is found, otherwise returns `str`.") {
    int32_t result;
    struct replace_state s;
    uint8_t *buf;
    replacesetup(argc, argv, &s);
    result = kmp_next(&s.kmp);
    if (result < 0) {
        kmp_deinit(&s.kmp);
        return janet_stringv(s.kmp.text, s.kmp.textlen);
    }
    JanetByteView subst = janet_text_substitution(&s.subst, s.kmp.text + result, s.kmp.patlen, NULL);
    buf = janet_string_begin(s.kmp.textlen - s.kmp.patlen + subst.len);
    safe_memcpy(buf, s.kmp.text, result);
    safe_memcpy(buf + result, subst.bytes, subst.len);
    safe_memcpy(buf + result + subst.len,
                s.kmp.text + result + s.kmp.patlen,
                s.kmp.textlen - result - s.kmp.patlen);
    kmp_deinit(&s.kmp);
    return janet_wrap_string(janet_string_end(buf));
}

JANET_CORE_FN(cfun_string_replaceall,
              "(string/replace-all patt subst str)",
              "Replace all instances of `patt` with `subst` in the string `str`. Overlapping "
              "matches will not be counted, only the first match in such a span will be replaced. "
              "If `subst` is a function, it will be called with `patt` once for each match, "
              "and should return the actual replacement text to use. "
              "Will return the new string if `patt` is found, otherwise returns `str`.") {
    int32_t result;
    struct replace_state s;
    JanetBuffer b;
    int32_t lastindex = 0;
    replacesetup(argc, argv, &s);
    janet_buffer_init(&b, s.kmp.textlen);
    while ((result = kmp_next(&s.kmp)) >= 0) {
        JanetByteView subst = janet_text_substitution(&s.subst, s.kmp.text + result, s.kmp.patlen, NULL);
        janet_buffer_push_bytes(&b, s.kmp.text + lastindex, result - lastindex);
        janet_buffer_push_bytes(&b, subst.bytes, subst.len);
        lastindex = result + s.kmp.patlen;
        kmp_seti(&s.kmp, lastindex);
    }
    janet_buffer_push_bytes(&b, s.kmp.text + lastindex, s.kmp.textlen - lastindex);
    const uint8_t *ret = janet_string(b.data, b.count);
    janet_buffer_deinit(&b);
    kmp_deinit(&s.kmp);
    return janet_wrap_string(ret);
}

JANET_CORE_FN(cfun_string_split,
              "(string/split delim str &opt start limit)",
              "Splits a string `str` with delimiter `delim` and returns an array of "
              "substrings. The substrings will not contain the delimiter `delim`. If `delim` "
              "is not found, the returned array will have one element. Will start searching "
              "for `delim` at the index `start` (if provided), and return up to a maximum "
              "of `limit` results (if provided).") {
    int32_t result;
    JanetArray *array;
    struct kmp_state state;
    int32_t limit = -1, lastindex = 0;
    if (argc == 4) {
        limit = janet_getinteger(argv, 3);
    }
    findsetup(argc, argv, &state, 1);
    array = janet_array(0);
    while ((result = kmp_next(&state)) >= 0 && --limit) {
        const uint8_t *slice = janet_string(state.text + lastindex, result - lastindex);
        janet_array_push(array, janet_wrap_string(slice));
        lastindex = result + state.patlen;
        kmp_seti(&state, lastindex);
    }
    const uint8_t *slice = janet_string(state.text + lastindex, state.textlen - lastindex);
    janet_array_push(array, janet_wrap_string(slice));
    kmp_deinit(&state);
    return janet_wrap_array(array);
}

JANET_CORE_FN(cfun_string_checkset,
              "(string/check-set set str)",
              "Checks that the string `str` only contains bytes that appear in the string `set`. "
              "Returns true if all bytes in `str` appear in `set`, false if some bytes in `str` do "
              "not appear in `set`.") {
    uint32_t bitset[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    janet_fixarity(argc, 2);
    JanetByteView set = janet_getbytes(argv, 0);
    JanetByteView str = janet_getbytes(argv, 1);
    /* Populate set */
    for (int32_t i = 0; i < set.len; i++) {
        int index = set.bytes[i] >> 5;
        uint32_t mask = 1 << (set.bytes[i] & 0x1F);
        bitset[index] |= mask;
    }
    /* Check set */
    for (int32_t i = 0; i < str.len; i++) {
        int index = str.bytes[i] >> 5;
        uint32_t mask = 1 << (str.bytes[i] & 0x1F);
        if (!(bitset[index] & mask)) {
            return janet_wrap_false();
        }
    }
    return janet_wrap_true();
}

JANET_CORE_FN(cfun_string_join,
              "(string/join parts &opt sep)",
              "Joins an array of strings into one string, optionally separated by "
              "a separator string `sep`.") {
    janet_arity(argc, 1, 2);
    JanetView parts = janet_getindexed(argv, 0);
    JanetByteView joiner;
    if (argc == 2) {
        joiner = janet_getbytes(argv, 1);
    } else {
        joiner.bytes = NULL;
        joiner.len = 0;
    }
    /* Check args */
    int32_t i;
    int64_t finallen = 0;
    for (i = 0; i < parts.len; i++) {
        const uint8_t *chunk;
        int32_t chunklen = 0;
        if (!janet_bytes_view(parts.items[i], &chunk, &chunklen)) {
            janet_panicf("item %d of parts is not a byte sequence, got %v", i, parts.items[i]);
        }
        if (i) finallen += joiner.len;
        finallen += chunklen;
        if (finallen > INT32_MAX)
            janet_panic("result string too long");
    }
    uint8_t *buf, *out;
    out = buf = janet_string_begin((int32_t) finallen);
    for (i = 0; i < parts.len; i++) {
        const uint8_t *chunk = NULL;
        int32_t chunklen = 0;
        if (i) {
            safe_memcpy(out, joiner.bytes, joiner.len);
            out += joiner.len;
        }
        janet_bytes_view(parts.items[i], &chunk, &chunklen);
        safe_memcpy(out, chunk, chunklen);
        out += chunklen;
    }
    return janet_wrap_string(janet_string_end(buf));
}

JANET_CORE_FN(cfun_string_format,
              "(string/format format & values)",
              "Similar to C's `snprintf`, but specialized for operating with Janet values. Returns "
              "a new string.\n\n"
              "The following conversion specifiers are supported, where the upper case specifiers generate "
              "upper case output:\n"
              "- `c`: ASCII character.\n"
              "- `d`, `i`: integer, formatted as a decimal number.\n"
              "- `x`, `X`: integer, formatted as a hexadecimal number.\n"
              "- `o`: integer, formatted as an octal number.\n"
              "- `f`, `F`: floating point number, formatted as a decimal number.\n"
              "- `e`, `E`: floating point number, formatted in scientific notation.\n"
              "- `g`, `G`: floating point number, formatted in its shortest form.\n"
              "- `a`, `A`: floating point number, formatted as a hexadecimal number.\n"
              "- `s`: formatted as a string, precision indicates padding and maximum length.\n"
              "- `t`: emit the type of the given value.\n"
              "- `v`: format with (describe x)\n"
              "- `V`: format with (string x)\n"
              "- `j`: format to jdn (Janet data notation).\n"
              "\n"
              "The following conversion specifiers are used for \"pretty-printing\", where the upper-case "
              "variants generate colored output. These specifiers can take a precision "
              "argument to specify the maximum nesting depth to print.\n"
              "- `p`, `P`: pretty format, truncating if necessary\n"
              "- `m`, `M`: pretty format without truncating.\n"
              "- `q`, `Q`: pretty format on one line, truncating if necessary.\n"
              "- `n`, `N`: pretty format on one line without truncation.\n") {
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_buffer(0);
    const char *strfrmt = (const char *) janet_getstring(argv, 0);
    janet_buffer_format(buffer, strfrmt, 0, argc, argv);
    return janet_stringv(buffer->data, buffer->count);
}

static int trim_help_checkset(JanetByteView set, uint8_t x) {
    for (int32_t j = 0; j < set.len; j++)
        if (set.bytes[j] == x)
            return 1;
    return 0;
}

static int32_t trim_help_leftedge(JanetByteView str, JanetByteView set) {
    for (int32_t i = 0; i < str.len; i++)
        if (!trim_help_checkset(set, str.bytes[i]))
            return i;
    return str.len;
}

static int32_t trim_help_rightedge(JanetByteView str, JanetByteView set) {
    for (int32_t i = str.len - 1; i >= 0; i--)
        if (!trim_help_checkset(set, str.bytes[i]))
            return i + 1;
    return 0;
}

static void trim_help_args(int32_t argc, Janet *argv, JanetByteView *str, JanetByteView *set) {
    janet_arity(argc, 1, 2);
    *str = janet_getbytes(argv, 0);
    if (argc >= 2) {
        *set = janet_getbytes(argv, 1);
    } else {
        set->bytes = (const uint8_t *)(" \t\r\n\v\f");
        set->len = 6;
    }
}

JANET_CORE_FN(cfun_string_trim,
              "(string/trim str &opt set)",
              "Trim leading and trailing whitespace from a byte sequence. If the argument "
              "`set` is provided, consider only characters in `set` to be whitespace.") {
    JanetByteView str, set;
    trim_help_args(argc, argv, &str, &set);
    int32_t left_edge = trim_help_leftedge(str, set);
    int32_t right_edge = trim_help_rightedge(str, set);
    if (right_edge < left_edge)
        return janet_stringv(NULL, 0);
    return janet_stringv(str.bytes + left_edge, right_edge - left_edge);
}

JANET_CORE_FN(cfun_string_triml,
              "(string/triml str &opt set)",
              "Trim leading whitespace from a byte sequence. If the argument "
              "`set` is provided, consider only characters in `set` to be whitespace.") {
    JanetByteView str, set;
    trim_help_args(argc, argv, &str, &set);
    int32_t left_edge = trim_help_leftedge(str, set);
    return janet_stringv(str.bytes + left_edge, str.len - left_edge);
}

JANET_CORE_FN(cfun_string_trimr,
              "(string/trimr str &opt set)",
              "Trim trailing whitespace from a byte sequence. If the argument "
              "`set` is provided, consider only characters in `set` to be whitespace.") {
    JanetByteView str, set;
    trim_help_args(argc, argv, &str, &set);
    int32_t right_edge = trim_help_rightedge(str, set);
    return janet_stringv(str.bytes, right_edge);
}

/* Module entry point */
void janet_lib_string(JanetTable *env) {
    JanetRegExt string_cfuns[] = {
        JANET_CORE_REG("string/slice", cfun_string_slice),
        JANET_CORE_REG("keyword/slice", cfun_keyword_slice),
        JANET_CORE_REG("symbol/slice", cfun_symbol_slice),
        JANET_CORE_REG("string/repeat", cfun_string_repeat),
        JANET_CORE_REG("string/bytes", cfun_string_bytes),
        JANET_CORE_REG("string/from-bytes", cfun_string_frombytes),
        JANET_CORE_REG("string/ascii-lower", cfun_string_asciilower),
        JANET_CORE_REG("string/ascii-upper", cfun_string_asciiupper),
        JANET_CORE_REG("string/reverse", cfun_string_reverse),
        JANET_CORE_REG("string/find", cfun_string_find),
        JANET_CORE_REG("string/find-all", cfun_string_findall),
        JANET_CORE_REG("string/has-prefix?", cfun_string_hasprefix),
        JANET_CORE_REG("string/has-suffix?", cfun_string_hassuffix),
        JANET_CORE_REG("string/replace", cfun_string_replace),
        JANET_CORE_REG("string/replace-all", cfun_string_replaceall),
        JANET_CORE_REG("string/split", cfun_string_split),
        JANET_CORE_REG("string/check-set", cfun_string_checkset),
        JANET_CORE_REG("string/join", cfun_string_join),
        JANET_CORE_REG("string/format", cfun_string_format),
        JANET_CORE_REG("string/trim", cfun_string_trim),
        JANET_CORE_REG("string/triml", cfun_string_triml),
        JANET_CORE_REG("string/trimr", cfun_string_trimr),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, string_cfuns);
}

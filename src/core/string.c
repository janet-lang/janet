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
#include "gc.h"
#include "util.h"
#include "state.h"

/* Begin building a string */
uint8_t *janet_string_begin(int32_t length) {
    char *data = janet_gcalloc(JANET_MEMORY_STRING, 2 * sizeof(int32_t) + length + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    janet_string_length(str) = length;
    str[length] = 0;
    return str;
}

/* Finish building a string */
const uint8_t *janet_string_end(uint8_t *str) {
    janet_string_hash(str) = janet_string_calchash(str, janet_string_length(str));
    return str;
}

/* Load a buffer as a string */
const uint8_t *janet_string(const uint8_t *buf, int32_t len) {
    int32_t hash = janet_string_calchash(buf, len);
    char *data = janet_gcalloc(JANET_MEMORY_STRING, 2 * sizeof(int32_t) + len + 1);
    uint8_t *str = (uint8_t *) (data + 2 * sizeof(int32_t));
    memcpy(str, buf, len);
    str[len] = 0;
    janet_string_length(str) = len;
    janet_string_hash(str) = hash;
    return str;
}

/* Compare two strings */
int janet_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    int32_t xlen = janet_string_length(lhs);
    int32_t ylen = janet_string_length(rhs);
    int32_t len = xlen > ylen ? ylen : xlen;
    int res = memcmp(lhs, rhs, len);
    if (res) return res;
    if (xlen == ylen) return 0;
    return xlen < ylen ? -1 : 1;
}

/* Compare a janet string with a piece of memory */
int janet_string_equalconst(const uint8_t *lhs, const uint8_t *rhs, int32_t rlen, int32_t rhash) {
    int32_t lhash = janet_string_hash(lhs);
    int32_t llen = janet_string_length(lhs);
    if (lhs == rhs)
        return 1;
    if (lhash != rhash || llen != rlen)
        return 0;
    return !memcmp(lhs, rhs, rlen);
}

/* Check if two strings are equal */
int janet_string_equal(const uint8_t *lhs, const uint8_t *rhs) {
    return janet_string_equalconst(lhs, rhs,
            janet_string_length(rhs), janet_string_hash(rhs));
}

/* Load a c string */
const uint8_t *janet_cstring(const char *str) {
    return janet_string((const uint8_t *)str, strlen(str));
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
    int32_t *lookup = calloc(patlen, sizeof(int32_t));
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
    free(state->lookup);
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

static Janet cfun_slice(int32_t argc, Janet *argv) {
    JanetRange range = janet_getslice(argc, argv);
    JanetByteView view = janet_getbytes(argv, 0);
    return janet_stringv(view.bytes + range.start, range.end - range.start);
}

static Janet cfun_repeat(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetByteView view = janet_getbytes(argv, 0);
    int32_t rep = janet_getinteger(argv, 1);
    if (rep < 0) janet_panic("expected non-negative number of repetitions");
    if (rep == 0) return janet_cstringv("");
    int64_t mulres = (int64_t) rep * view.len;
    if (mulres > INT32_MAX) janet_panic("result string is too long");
    uint8_t *newbuf = janet_string_begin((int32_t) mulres);
    uint8_t *end = newbuf + mulres;
    uint8_t *p = newbuf;
    for (p = newbuf; p < end; p += view.len) {
        memcpy(p, view.bytes, view.len);
    }
    return janet_wrap_string(janet_string_end(newbuf));
}

static Janet cfun_bytes(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetByteView view = janet_getbytes(argv, 0);
    Janet *tup = janet_tuple_begin(view.len);
    int32_t i;
    for (i = 0; i < view.len; i++) {
        tup[i] = janet_wrap_integer((int32_t) view.bytes[i]);
    }
    return janet_wrap_tuple(janet_tuple_end(tup));
}

static Janet cfun_frombytes(int32_t argc, Janet *argv) {
    int32_t i;
    uint8_t *buf = janet_string_begin(argc);
    for (i = 0; i < argc; i++) {
        int32_t c = janet_getinteger(argv, i);
        buf[i] = c & 0xFF;
    }
    return janet_wrap_string(janet_string_end(buf));
}

static Janet cfun_asciilower(int32_t argc, Janet *argv) {
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

static Janet cfun_asciiupper(int32_t argc, Janet *argv) {
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

static Janet cfun_reverse(int32_t argc, Janet *argv) {
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

static Janet cfun_find(int32_t argc, Janet *argv) {
    int32_t result;
    struct kmp_state state;
    findsetup(argc, argv, &state, 0);
    result = kmp_next(&state);
    kmp_deinit(&state);
    return result < 0
        ? janet_wrap_nil()
        : janet_wrap_integer(result);
}

static Janet cfun_findall(int32_t argc, Janet *argv) {
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
    const uint8_t *subst;
    int32_t substlen;
};

static void replacesetup(int32_t argc, Janet *argv, struct replace_state *s) {
    janet_arity(argc, 3, 4);
    JanetByteView pat = janet_getbytes(argv, 0);
    JanetByteView subst = janet_getbytes(argv, 1);
    JanetByteView text = janet_getbytes(argv, 2);
    int32_t start = 0;
    if (argc == 4) {
        start = janet_getinteger(argv, 3);
        if (start < 0) janet_panic("expected non-negative start index");
    }
    kmp_init(&s->kmp, text.bytes, text.len, pat.bytes, pat.len);
    s->kmp.i = start;
    s->subst = subst.bytes;
    s->substlen = subst.len;
}

static Janet cfun_replace(int32_t argc, Janet *argv) {
    int32_t result;
    struct replace_state s;
    uint8_t *buf;
    replacesetup(argc, argv, &s);
    result = kmp_next(&s.kmp);
    if (result < 0) {
        kmp_deinit(&s.kmp);
        return janet_stringv(s.kmp.text, s.kmp.textlen);
    }
    buf = janet_string_begin(s.kmp.textlen - s.kmp.patlen + s.substlen);
    memcpy(buf, s.kmp.text, result);
    memcpy(buf + result, s.subst, s.substlen);
    memcpy(buf + result + s.substlen,
            s.kmp.text + result + s.kmp.patlen,
            s.kmp.textlen - result - s.kmp.patlen);
    kmp_deinit(&s.kmp);
    return janet_wrap_string(janet_string_end(buf));
}

static Janet cfun_replaceall(int32_t argc, Janet *argv) {
    int32_t result;
    struct replace_state s;
    JanetBuffer b;
    int32_t lastindex = 0;
    replacesetup(argc, argv, &s);
    janet_buffer_init(&b, s.kmp.textlen);
    while ((result = kmp_next(&s.kmp)) >= 0) {
        janet_buffer_push_bytes(&b, s.kmp.text + lastindex, result - lastindex);
        janet_buffer_push_bytes(&b, s.subst, s.substlen);
        lastindex = result + s.kmp.patlen;
        kmp_seti(&s.kmp, lastindex);
    }
    janet_buffer_push_bytes(&b, s.kmp.text + lastindex, s.kmp.textlen - lastindex);
    const uint8_t *ret = janet_string(b.data, b.count);
    janet_buffer_deinit(&b);
    kmp_deinit(&s.kmp);
    return janet_wrap_string(ret);
}

static Janet cfun_split(int32_t argc, Janet *argv) {
    int32_t result;
    JanetArray *array;
    struct kmp_state state;
    int32_t limit = -1, lastindex = 0;
    if (argc == 4) {
        limit = janet_getinteger(argv, 3);
    }
    findsetup(argc, argv, &state, 1);
    array = janet_array(0);
    while ((result = kmp_next(&state)) >= 0 && limit--) {
        const uint8_t *slice = janet_string(state.text + lastindex, result - lastindex);
        janet_array_push(array, janet_wrap_string(slice));
        lastindex = result + state.patlen;
    }
    {
        const uint8_t *slice = janet_string(state.text + lastindex, state.textlen - lastindex);
        janet_array_push(array, janet_wrap_string(slice));
    }
    kmp_deinit(&state);
    return janet_wrap_array(array);
}

static Janet cfun_checkset(int32_t argc, Janet *argv) {
    uint32_t bitset[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    janet_arity(argc, 2, 3);
    JanetByteView set = janet_getbytes(argv, 0);
    JanetByteView str = janet_getbytes(argv, 1);
    /* Populate set */
    for (int32_t i = 0; i < set.len; i++) {
        int index = set.bytes[i] >> 5;
        uint32_t mask = 1 << (set.bytes[i] & 7);
        bitset[index] |= mask;
    }
    if (argc == 3) {
        if (janet_getboolean(argv, 2)) {
            for (int i = 0; i < 8; i++)
                bitset[i] = ~bitset[i];
        }
    }
    /* Check set */
    for (int32_t i = 0; i < str.len; i++) {
        int index = str.bytes[i] >> 5;
        uint32_t mask = 1 << (str.bytes[i] & 7);
        if (!(bitset[index] & mask)) {
            return janet_wrap_false();
        }
    }
    return janet_wrap_true();
}

static Janet cfun_join(int32_t argc, Janet *argv) {
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
            memcpy(out, joiner.bytes, joiner.len);
            out += joiner.len;
        }
        janet_bytes_view(parts.items[i], &chunk, &chunklen);
        memcpy(out, chunk, chunklen);
        out += chunklen;
    }
    return janet_wrap_string(janet_string_end(buf));
}

static struct formatter {
    const char *lead;
    const char *f1;
    const char *f2;
} formatters[] = {
    {"g", "%g", "%.*g"},
    {"G", "%G", "%.*G"},
    {"e", "%e", "%.*e"},
    {"E", "%E", "%.*E"},
    {"f", "%f", "%.*f"},
    {"F", "%F", "%.*F"}
};

static Janet cfun_number(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 4);
    double x = janet_getnumber(argv, 0);
    struct formatter fmter = formatters[0];
    char buf[100];
    int formatNargs = 1;
    int32_t precision = 0;
    if (argc >= 2) {
        const uint8_t *flag = janet_getkeyword(argv, 1);
        int i;
        for (i = 0; i < 6; i++) {
            struct formatter fmttest = formatters[i];
            if (!janet_cstrcmp(flag, fmttest.lead)) {
                fmter = fmttest;
                break;
            }
        }
        if (i == 6)
            janet_panicf("unsupported formatter %v", argv[1]);
    }

    if (argc >= 3) {
        precision = janet_getinteger(argv, 2);
        formatNargs++;
    }

    if (formatNargs == 1) {
        snprintf(buf, sizeof(buf), fmter.f1, x);
    } else if (formatNargs == 2) {
        snprintf(buf, sizeof(buf), fmter.f2, precision, x);
    }

    return janet_cstringv(buf);
}

static Janet cfun_pretty(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetBuffer *buffer = NULL;
    int32_t depth = 4;
    if (argc > 1)
        depth = janet_getinteger(argv, 1);
    if (argc > 2)
        buffer = janet_getbuffer(argv, 2);
    buffer = janet_pretty(buffer, depth, argv[0]);
    return janet_wrap_buffer(buffer);
}

static const JanetReg cfuns[] = {
    {
        "string/slice", cfun_slice,
        JDOC("(string/slice bytes [,start=0 [,end=(length str)]])\n\n"
                "Returns a substring from a byte sequence. The substring is from "
                "index start inclusive to index end exclusive. All indexing "
                "is from 0. 'start' and 'end' can also be negative to indicate indexing "
                "from the end of the string.")
    },
    {
        "string/repeat", cfun_repeat,
        JDOC("(string/repeat bytes n)\n\n"
                "Returns a string that is n copies of bytes concatenated.")
    },
    {
        "string/bytes", cfun_bytes,
        JDOC("(string/bytes str)\n\n"
                "Returns an array of integers that are the byte values of the string.")
    },
    {
        "string/from-bytes", cfun_frombytes,
        JDOC("(string/from-bytes byte-array)\n\n"
                "Creates a string from an array of integers with byte values. All integers "
                "will be coerced to the range of 1 byte 0-255.")
    },
    {
        "string/ascii-lower", cfun_asciilower,
        JDOC("(string/ascii-lower str)\n\n"
                "Returns a new string where all bytes are replaced with the "
                "lowercase version of themselves in ASCII. Does only a very simple "
                "case check, meaning no unicode support.")
    },
    {
        "string/ascii-upper", cfun_asciiupper,
        JDOC("(string/ascii-upper str)\n\n"
                "Returns a new string where all bytes are replaced with the "
                "uppercase version of themselves in ASCII. Does only a very simple "
                "case check, meaning no unicode support.")
    },
    {
        "string/reverse", cfun_reverse,
        JDOC("(string/reverse str)\n\n"
                "Returns a string that is the reversed version of str.")
    },
    {
        "string/find", cfun_find,
        JDOC("(string/find patt str)\n\n"
                "Searches for the first instance of pattern patt in string "
                "str. Returns the index of the first character in patt if found, "
                "otherwise returns nil.")
    },
    {
        "string/find-all", cfun_findall,
        JDOC("(string/find patt str)\n\n"
                "Searches for all instances of pattern patt in string "
                "str. Returns an array of all indices of found patterns. Overlapping "
                "instances of the pattern are not counted, meaning a byte in string "
                "will only contribute to finding at most on occurrence of pattern. If no "
                "occurrences are found, will return an empty array.")
    },
    {
        "string/replace", cfun_replace,
        JDOC("(string/replace patt subst str)\n\n"
                "Replace the first occurrence of patt with subst in the string str. "
                "Will return the new string if patt is found, otherwise returns str.")
    },
    {
        "string/replace-all", cfun_replaceall,
        JDOC("(string/replace-all patt subst str)\n\n"
                "Replace all instances of patt with subst in the string str. "
                "Will return the new string if patt is found, otherwise returns str.")
    },
    {
        "string/split", cfun_split,
        JDOC("(string/split delim str)\n\n"
                "Splits a string str with delimiter delim and returns an array of "
                "substrings. The substrings will not contain the delimiter delim. If delim "
                "is not found, the returned array will have one element.")
    },
    {
        "string/check-set", cfun_checkset,
        JDOC("(string/check-set set str)\n\n"
                "Checks if any of the bytes in the string set appear in the string str. "
                "Returns true if some bytes in set do appear in str, false if no bytes do.")
    },
    {
        "string/join", cfun_join,
        JDOC("(string/join parts [,sep])\n\n"
                "Joins an array of strings into one string, optionally separated by "
                "a separator string sep.")
    },
    {
        "string/number", cfun_number,
        JDOC("(string/number x [,format [,maxlen [,precision]]])\n\n"
                "Formats a number as string. The format parameter indicates how "
                "to display the number, either as floating point, scientific, or "
                "whichever representation is shorter. format can be:\n\n"
                "\t:g - (default) shortest representation with lowercase e.\n"
                "\t:G - shortest representation with uppercase E.\n"
                "\t:e - scientific with lowercase e.\n"
                "\t:E - scientific with uppercase E.\n"
                "\t:f - floating point representation.\n"
                "\t:F - same as :f\n\n"
                "The programmer can also specify the max length of the output string "
                "and the precision (number of places after decimal) in the output number. "
                "Returns a string representation of x.")
    },
    {
        "string/pretty", cfun_pretty,
        JDOC("(string/pretty x [,depth=4 [,buffer=@\"\"]])\n\n"
                "Pretty prints a value to a buffer. Optionally allows setting max "
                "recursion depth, as well as writing to a buffer. Returns the buffer.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_string(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
}

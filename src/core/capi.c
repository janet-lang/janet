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
#include "state.h"
#include "fiber.h"
#include "util.h"
#endif

#ifndef JANET_SINGLE_THREADED
#ifndef JANET_WINDOWS
#include <pthread.h>
#endif
#endif

#ifdef JANET_WINDOWS
#include <windows.h>
#endif

#ifdef JANET_USE_STDATOMIC
#include <stdatomic.h>
/* We don't need stdatomic on most compilers since we use compiler builtins for atomic operations.
 * Some (TCC), explicitly require using stdatomic.h and don't have any exposed builtins (that I know of).
 * For TCC and similar compilers, one would need -std=c11 or similar then to get access. */
#endif

JANET_NO_RETURN static void janet_top_level_signal(const char *msg) {
#ifdef JANET_TOP_LEVEL_SIGNAL
    JANET_TOP_LEVEL_SIGNAL(msg);
#else
    fputs(msg, stdout);
# ifdef JANET_SINGLE_THREADED
    exit(-1);
# elif defined(JANET_WINDOWS)
    ExitThread(-1);
# else
    pthread_exit(NULL);
# endif
#endif
}

void janet_signalv(JanetSignal sig, Janet message) {
    if (janet_vm.return_reg != NULL) {
        /* Should match logic in janet_call for coercing everything not ok to an error (no awaits, yields, etc.) */
        if (janet_vm.coerce_error && sig != JANET_SIGNAL_OK) {
            if (sig != JANET_SIGNAL_ERROR) {
                message = janet_wrap_string(janet_formatc("%v coerced from %s to error", message, janet_signal_names[sig]));
            }
            sig = JANET_SIGNAL_ERROR;
        }
        *janet_vm.return_reg = message;
        if (NULL != janet_vm.fiber) {
            janet_vm.fiber->flags |= JANET_FIBER_DID_LONGJUMP;
        }
#if defined(JANET_BSD) || defined(JANET_APPLE)
        _longjmp(*janet_vm.signal_buf, sig);
#else
        longjmp(*janet_vm.signal_buf, sig);
#endif
    } else {
        const char *str = (const char *)janet_formatc("janet top level signal - %v\n", message);
        janet_top_level_signal(str);
    }
}

void janet_panicv(Janet message) {
    janet_signalv(JANET_SIGNAL_ERROR, message);
}

void janet_panicf(const char *format, ...) {
    va_list args;
    const uint8_t *ret;
    JanetBuffer buffer;
    int32_t len = 0;
    while (format[len]) len++;
    janet_buffer_init(&buffer, len);
    va_start(args, format);
    janet_formatbv(&buffer, format, args);
    va_end(args);
    ret = janet_string(buffer.data, buffer.count);
    janet_buffer_deinit(&buffer);
    janet_panics(ret);
}

void janet_panic(const char *message) {
    janet_panicv(janet_cstringv(message));
}

void janet_panics(const uint8_t *message) {
    janet_panicv(janet_wrap_string(message));
}

void janet_panic_type(Janet x, int32_t n, int expected) {
    janet_panicf("bad slot #%d, expected %T, got %v", n, expected, x);
}

void janet_panic_abstract(Janet x, int32_t n, const JanetAbstractType *at) {
    janet_panicf("bad slot #%d, expected %s, got %v", n, at->name, x);
}

void janet_fixarity(int32_t arity, int32_t fix) {
    if (arity != fix)
        janet_panicf("arity mismatch, expected %d, got %d", fix, arity);
}

void janet_arity(int32_t arity, int32_t min, int32_t max) {
    if (min >= 0 && arity < min)
        janet_panicf("arity mismatch, expected at least %d, got %d", min, arity);
    if (max >= 0 && arity > max)
        janet_panicf("arity mismatch, expected at most %d, got %d", max, arity);
}

#define DEFINE_GETTER(name, NAME, type) \
type janet_get##name(const Janet *argv, int32_t n) { \
    Janet x = argv[n]; \
    if (!janet_checktype(x, JANET_##NAME)) { \
        janet_panic_type(x, n, JANET_TFLAG_##NAME); \
    } \
    return janet_unwrap_##name(x); \
}

#define DEFINE_OPT(name, NAME, type) \
type janet_opt##name(const Janet *argv, int32_t argc, int32_t n, type dflt) { \
    if (n >= argc) return dflt; \
    if (janet_checktype(argv[n], JANET_NIL)) return dflt; \
    return janet_get##name(argv, n); \
}

#define DEFINE_OPTLEN(name, NAME, type) \
type janet_opt##name(const Janet *argv, int32_t argc, int32_t n, int32_t dflt_len) { \
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {\
        return janet_##name(dflt_len); \
    }\
    return janet_get##name(argv, n); \
}

int janet_getmethod(const uint8_t *method, const JanetMethod *methods, Janet *out) {
    while (methods->name) {
        if (!janet_cstrcmp(method, methods->name)) {
            *out = janet_wrap_cfunction(methods->cfun);
            return 1;
        }
        methods++;
    }
    return 0;
}

Janet janet_nextmethod(const JanetMethod *methods, Janet key) {
    if (!janet_checktype(key, JANET_NIL)) {
        while (methods->name) {
            if (janet_keyeq(key, methods->name)) {
                methods++;
                break;
            }
            methods++;
        }
    }
    if (methods->name) {
        return janet_ckeywordv(methods->name);
    } else {
        return janet_wrap_nil();
    }
}

DEFINE_GETTER(number, NUMBER, double)
DEFINE_GETTER(array, ARRAY, JanetArray *)
DEFINE_GETTER(tuple, TUPLE, const Janet *)
DEFINE_GETTER(table, TABLE, JanetTable *)
DEFINE_GETTER(struct, STRUCT, const JanetKV *)
DEFINE_GETTER(string, STRING, const uint8_t *)
DEFINE_GETTER(keyword, KEYWORD, const uint8_t *)
DEFINE_GETTER(symbol, SYMBOL, const uint8_t *)
DEFINE_GETTER(buffer, BUFFER, JanetBuffer *)
DEFINE_GETTER(fiber, FIBER, JanetFiber *)
DEFINE_GETTER(function, FUNCTION, JanetFunction *)
DEFINE_GETTER(cfunction, CFUNCTION, JanetCFunction)
DEFINE_GETTER(boolean, BOOLEAN, int)
DEFINE_GETTER(pointer, POINTER, void *)

DEFINE_OPT(number, NUMBER, double)
DEFINE_OPT(tuple, TUPLE, const Janet *)
DEFINE_OPT(struct, STRUCT, const JanetKV *)
DEFINE_OPT(string, STRING, const uint8_t *)
DEFINE_OPT(keyword, KEYWORD, const uint8_t *)
DEFINE_OPT(symbol, SYMBOL, const uint8_t *)
DEFINE_OPT(fiber, FIBER, JanetFiber *)
DEFINE_OPT(function, FUNCTION, JanetFunction *)
DEFINE_OPT(cfunction, CFUNCTION, JanetCFunction)
DEFINE_OPT(boolean, BOOLEAN, int)
DEFINE_OPT(pointer, POINTER, void *)

DEFINE_OPTLEN(buffer, BUFFER, JanetBuffer *)
DEFINE_OPTLEN(table, TABLE, JanetTable *)
DEFINE_OPTLEN(array, ARRAY, JanetArray *)

const char *janet_optcstring(const Janet *argv, int32_t argc, int32_t n, const char *dflt) {
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {
        return dflt;
    }
    return janet_getcstring(argv, n);
}

#undef DEFINE_GETTER
#undef DEFINE_OPT
#undef DEFINE_OPTLEN

const char *janet_getcstring(const Janet *argv, int32_t n) {
    if (!janet_checktype(argv[n], JANET_STRING)) {
        janet_panic_type(argv[n], n, JANET_TFLAG_STRING);
    }
    return janet_getcbytes(argv, n);
}

const char *janet_getcbytes(const Janet *argv, int32_t n) {
    /* Ensure buffer 0-padded */
    if (janet_checktype(argv[n], JANET_BUFFER)) {
        JanetBuffer *b = janet_unwrap_buffer(argv[n]);
        if ((b->gc.flags & JANET_BUFFER_FLAG_NO_REALLOC) && b->count == b->capacity) {
            /* Make a copy with janet_smalloc in the rare case we have a buffer that
             * cannot be realloced and pushing a 0 byte would panic. */
            char *new_string = janet_smalloc(b->count + 1);
            memcpy(new_string, b->data, b->count);
            new_string[b->count] = 0;
            if (strlen(new_string) != (size_t) b->count) goto badzeros;
            return new_string;
        } else {
            /* Ensure trailing 0 */
            janet_buffer_push_u8(b, 0);
            b->count--;
            if (strlen((char *)b->data) != (size_t) b->count) goto badzeros;
            return (const char *) b->data;
        }
    }
    JanetByteView view = janet_getbytes(argv, n);
    const char *cstr = (const char *)view.bytes;
    if (strlen(cstr) != (size_t) view.len) goto badzeros;
    return cstr;

badzeros:
    janet_panic("bytes contain embedded 0s");
}

const char *janet_optcbytes(const Janet *argv, int32_t argc, int32_t n, const char *dflt) {
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {
        return dflt;
    }
    return janet_getcbytes(argv, n);
}

int32_t janet_getnat(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkint(x)) goto bad;
    int32_t ret = janet_unwrap_integer(x);
    if (ret < 0) goto bad;
    return ret;
bad:
    janet_panicf("bad slot #%d, expected non-negative 32 bit signed integer, got %v", n, x);
}

JanetAbstract janet_checkabstract(Janet x, const JanetAbstractType *at) {
    if (!janet_checktype(x, JANET_ABSTRACT)) return NULL;
    JanetAbstract a = janet_unwrap_abstract(x);
    if (janet_abstract_type(a) != at) return NULL;
    return a;
}

static int janet_strlike_cmp(JanetType type, Janet x, const char *cstring) {
    if (janet_type(x) != type) return 0;
    return !janet_cstrcmp(janet_unwrap_string(x), cstring);
}

int janet_keyeq(Janet x, const char *cstring) {
    return janet_strlike_cmp(JANET_KEYWORD, x, cstring);
}

int janet_streq(Janet x, const char *cstring) {
    return janet_strlike_cmp(JANET_STRING, x, cstring);
}

int janet_symeq(Janet x, const char *cstring) {
    return janet_strlike_cmp(JANET_SYMBOL, x, cstring);
}

int32_t janet_getinteger(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkint(x)) {
        janet_panicf("bad slot #%d, expected 32 bit signed integer, got %v", n, x);
    }
    return janet_unwrap_integer(x);
}

uint32_t janet_getuinteger(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkuint(x)) {
        janet_panicf("bad slot #%d, expected 32 bit unsigned integer, got %v", n, x);
    }
    return (uint32_t) janet_unwrap_number(x);
}

int16_t janet_getinteger16(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkint16(x)) {
        janet_panicf("bad slot #%d, expected 16 bit signed integer, got %v", n, x);
    }
    return (int16_t) janet_unwrap_number(x);
}

uint16_t janet_getuinteger16(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checkuint16(x)) {
        janet_panicf("bad slot #%d, expected 16 bit unsigned integer, got %v", n, x);
    }
    return (uint16_t) janet_unwrap_number(x);
}


int64_t janet_getinteger64(const Janet *argv, int32_t n) {
#ifdef JANET_INT_TYPES
    return janet_unwrap_s64(argv[n]);
#else
    Janet x = argv[n];
    if (!janet_checkint64(x)) {
        janet_panicf("bad slot #%d, expected 64 bit signed integer, got %v", n, x);
    }
    return (int64_t) janet_unwrap_number(x);
#endif
}

uint64_t janet_getuinteger64(const Janet *argv, int32_t n) {
#ifdef JANET_INT_TYPES
    return janet_unwrap_u64(argv[n]);
#else
    Janet x = argv[n];
    if (!janet_checkuint64(x)) {
        janet_panicf("bad slot #%d, expected 64 bit unsigned integer, got %v", n, x);
    }
    return (uint64_t) janet_unwrap_number(x);
#endif
}

size_t janet_getsize(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    if (!janet_checksize(x)) {
        janet_panicf("bad slot #%d, expected size, got %v", n, x);
    }
    return (size_t) janet_unwrap_number(x);
}

int32_t janet_gethalfrange(const Janet *argv, int32_t n, int32_t length, const char *which) {
    int32_t raw = janet_getinteger(argv, n);
    int32_t not_raw = raw;
    if (not_raw < 0) not_raw += length + 1;
    if (not_raw < 0 || not_raw > length)
        janet_panicf("%s index %d out of range [%d,%d]", which, (int64_t) raw, -(int64_t)length - 1, (int64_t) length);
    return not_raw;
}

int32_t janet_getstartrange(const Janet *argv, int32_t argc, int32_t n, int32_t length) {
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {
        return 0;
    }
    return janet_gethalfrange(argv, n, length, "start");
}

int32_t janet_getendrange(const Janet *argv, int32_t argc, int32_t n, int32_t length) {
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {
        return length;
    }
    return janet_gethalfrange(argv, n, length, "end");
}

int32_t janet_getargindex(const Janet *argv, int32_t n, int32_t length, const char *which) {
    int32_t raw = janet_getinteger(argv, n);
    int32_t not_raw = raw;
    if (not_raw < 0) not_raw += length;
    if (not_raw < 0 || not_raw > length)
        janet_panicf("%s index %d out of range [%d,%d)", which, (int64_t)raw, -(int64_t)length, (int64_t)length);
    return not_raw;
}

JanetView janet_getindexed(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetView view;
    if (!janet_indexed_view(x, &view.items, &view.len)) {
        janet_panic_type(x, n, JANET_TFLAG_INDEXED);
    }
    return view;
}

JanetByteView janet_getbytes(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetByteView view;
    if (!janet_bytes_view(x, &view.bytes, &view.len)) {
        janet_panic_type(x, n, JANET_TFLAG_BYTES);
    }
    return view;
}

JanetDictView janet_getdictionary(const Janet *argv, int32_t n) {
    Janet x = argv[n];
    JanetDictView view;
    if (!janet_dictionary_view(x, &view.kvs, &view.len, &view.cap)) {
        janet_panic_type(x, n, JANET_TFLAG_DICTIONARY);
    }
    return view;
}

void *janet_getabstract(const Janet *argv, int32_t n, const JanetAbstractType *at) {
    Janet x = argv[n];
    if (!janet_checktype(x, JANET_ABSTRACT)) {
        janet_panic_abstract(x, n, at);
    }
    void *abstractx = janet_unwrap_abstract(x);
    if (janet_abstract_type(abstractx) != at) {
        janet_panic_abstract(x, n, at);
    }
    return abstractx;
}

JanetRange janet_getslice(int32_t argc, const Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetRange range;
    int32_t length = janet_length(argv[0]);
    range.start = janet_getstartrange(argv, argc, 1, length);
    range.end = janet_getendrange(argv, argc, 2, length);
    if (range.end < range.start)
        range.end = range.start;
    return range;
}

Janet janet_dyn(const char *name) {
    if (!janet_vm.fiber) {
        if (!janet_vm.top_dyns) return janet_wrap_nil();
        return janet_table_get(janet_vm.top_dyns, janet_ckeywordv(name));
    }
    if (janet_vm.fiber->env) {
        return janet_table_get(janet_vm.fiber->env, janet_ckeywordv(name));
    } else {
        return janet_wrap_nil();
    }
}

void janet_setdyn(const char *name, Janet value) {
    if (!janet_vm.fiber) {
        if (!janet_vm.top_dyns) janet_vm.top_dyns = janet_table(10);
        janet_table_put(janet_vm.top_dyns, janet_ckeywordv(name), value);
    } else {
        if (!janet_vm.fiber->env) {
            janet_vm.fiber->env = janet_table(1);
        }
        janet_table_put(janet_vm.fiber->env, janet_ckeywordv(name), value);
    }
}

/* Create a function that when called, returns X. Trivial in Janet, a pain in C. */
JanetFunction *janet_thunk_delay(Janet x) {
    static const uint32_t bytecode[] = {
        JOP_LOAD_CONSTANT,
        JOP_RETURN
    };
    JanetFuncDef *def = janet_funcdef_alloc();
    def->arity = 0;
    def->min_arity = 0;
    def->max_arity = INT32_MAX;
    def->flags = JANET_FUNCDEF_FLAG_VARARG;
    def->slotcount = 1;
    def->bytecode = janet_malloc(sizeof(bytecode));
    def->bytecode_length = (int32_t)(sizeof(bytecode) / sizeof(uint32_t));
    def->constants = janet_malloc(sizeof(Janet));
    def->constants_length = 1;
    def->name = NULL;
    if (!def->bytecode || !def->constants) {
        JANET_OUT_OF_MEMORY;
    }
    def->constants[0] = x;
    memcpy(def->bytecode, bytecode, sizeof(bytecode));
    janet_def_addflags(def);
    /* janet_verify(def); */
    return janet_thunk(def);
}

uint64_t janet_getflags(const Janet *argv, int32_t n, const char *flags) {
    uint64_t ret = 0;
    const uint8_t *keyw = janet_getkeyword(argv, n);
    int32_t klen = janet_string_length(keyw);
    int32_t flen = (int32_t) strlen(flags);
    if (flen > 64) {
        flen = 64;
    }
    for (int32_t j = 0; j < klen; j++) {
        for (int32_t i = 0; i < flen; i++) {
            if (((uint8_t) flags[i]) == keyw[j]) {
                ret |= 1ULL << i;
                goto found;
            }
        }
        janet_panicf("unexpected flag %c, expected one of \"%s\"", (char) keyw[j], flags);
    found:
        ;
    }
    return ret;
}

int32_t janet_optnat(const Janet *argv, int32_t argc, int32_t n, int32_t dflt) {
    if (argc <= n) return dflt;
    if (janet_checktype(argv[n], JANET_NIL)) return dflt;
    return janet_getnat(argv, n);
}

int32_t janet_optinteger(const Janet *argv, int32_t argc, int32_t n, int32_t dflt) {
    if (argc <= n) return dflt;
    if (janet_checktype(argv[n], JANET_NIL)) return dflt;
    return janet_getinteger(argv, n);
}

int64_t janet_optinteger64(const Janet *argv, int32_t argc, int32_t n, int64_t dflt) {
    if (argc <= n) return dflt;
    if (janet_checktype(argv[n], JANET_NIL)) return dflt;
    return janet_getinteger64(argv, n);
}

size_t janet_optsize(const Janet *argv, int32_t argc, int32_t n, size_t dflt) {
    if (argc <= n) return dflt;
    if (janet_checktype(argv[n], JANET_NIL)) return dflt;
    return janet_getsize(argv, n);
}

void *janet_optabstract(const Janet *argv, int32_t argc, int32_t n, const JanetAbstractType *at, void *dflt) {
    if (argc <= n) return dflt;
    if (janet_checktype(argv[n], JANET_NIL)) return dflt;
    return janet_getabstract(argv, n, at);
}

/* Atomic refcounts */

JanetAtomicInt janet_atomic_inc(JanetAtomicInt volatile *x) {
#ifdef _MSC_VER
    return _InterlockedIncrement(x);
#elif defined(JANET_USE_STDATOMIC)
    return atomic_fetch_add_explicit(x, 1, memory_order_relaxed) + 1;
#else
    return __atomic_add_fetch(x, 1, __ATOMIC_RELAXED);
#endif
}

JanetAtomicInt janet_atomic_dec(JanetAtomicInt volatile *x) {
#ifdef _MSC_VER
    return _InterlockedDecrement(x);
#elif defined(JANET_USE_STDATOMIC)
    return atomic_fetch_add_explicit(x, -1, memory_order_acq_rel) - 1;
#else
    return __atomic_add_fetch(x, -1, __ATOMIC_ACQ_REL);
#endif
}

JanetAtomicInt janet_atomic_load(JanetAtomicInt volatile *x) {
#ifdef _MSC_VER
    return _InterlockedOr(x, 0);
#elif defined(JANET_USE_STDATOMIC)
    return atomic_load_explicit(x, memory_order_acquire);
#else
    return __atomic_load_n(x, __ATOMIC_ACQUIRE);
#endif
}

/* Some definitions for function-like macros */

JANET_API JanetStructHead *(janet_struct_head)(JanetStruct st) {
    return janet_struct_head(st);
}

JANET_API JanetAbstractHead *(janet_abstract_head)(const void *abstract) {
    return janet_abstract_head(abstract);
}

JANET_API JanetStringHead *(janet_string_head)(JanetString s) {
    return janet_string_head(s);
}

JANET_API JanetTupleHead *(janet_tuple_head)(JanetTuple tuple) {
    return janet_tuple_head(tuple);
}

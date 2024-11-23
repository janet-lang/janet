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
#include "util.h"
#include "state.h"
#include "gc.h"
#ifdef JANET_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif
#endif

#ifdef JANET_WINDOWS
#ifdef JANET_DYNAMIC_MODULES
#include <psapi.h>
#ifdef JANET_MSVC
#pragma comment (lib, "Psapi.lib")
#endif
#endif
#endif

#ifdef JANET_APPLE
#include <AvailabilityMacros.h>
#endif

#include <inttypes.h>

/* Base 64 lookup table for digits */
const char janet_base64[65] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "_=";

/* The JANET value types in order. These types can be used as
 * mnemonics instead of a bit pattern for type checking */
const char *const janet_type_names[16] = {
    "number",
    "nil",
    "boolean",
    "fiber",
    "string",
    "symbol",
    "keyword",
    "array",
    "tuple",
    "table",
    "struct",
    "buffer",
    "function",
    "cfunction",
    "abstract",
    "pointer"
};

/* Docstring for signal lists these */
const char *const janet_signal_names[14] = {
    "ok",
    "error",
    "debug",
    "yield",
    "user0",
    "user1",
    "user2",
    "user3",
    "user4",
    "user5",
    "user6",
    "user7",
    "interrupt",
    "await"
};

/* Docstring for fiber/status lists these */
const char *const janet_status_names[16] = {
    "dead",
    "error",
    "debug",
    "pending",
    "user0",
    "user1",
    "user2",
    "user3",
    "user4",
    "user5",
    "user6",
    "user7",
    "interrupted",
    "suspended",
    "new",
    "alive"
};

uint32_t janet_hash_mix(uint32_t input, uint32_t more) {
    uint32_t mix1 = (more + 0x9e3779b9 + (input << 6) + (input >> 2));
    return input ^ (0x9e3779b9 + (mix1 << 6) + (mix1 >> 2));
}

#ifndef JANET_PRF

int32_t janet_string_calchash(const uint8_t *str, int32_t len) {
    if (NULL == str || len == 0) return 5381;
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    hash = janet_hash_mix(hash, (uint32_t) len);
    return (int32_t) hash;
}

#else

/*
  Public domain siphash implementation sourced from:

  https://raw.githubusercontent.com/veorq/SipHash/master/halfsiphash.c

  We have made a few alterations, such as hardcoding the output size
  and then removing dead code.
*/
#define cROUNDS 2
#define dROUNDS 4

#define ROTL(x, b) (uint32_t)(((x) << (b)) | ((x) >> (32 - (b))))

#define U8TO32_LE(p)                                                           \
    (((uint32_t)((p)[0])) | ((uint32_t)((p)[1]) << 8) |                        \
     ((uint32_t)((p)[2]) << 16) | ((uint32_t)((p)[3]) << 24))

#define SIPROUND                                                               \
    do {                                                                       \
        v0 += v1;                                                              \
        v1 = ROTL(v1, 5);                                                      \
        v1 ^= v0;                                                              \
        v0 = ROTL(v0, 16);                                                     \
        v2 += v3;                                                              \
        v3 = ROTL(v3, 8);                                                      \
        v3 ^= v2;                                                              \
        v0 += v3;                                                              \
        v3 = ROTL(v3, 7);                                                      \
        v3 ^= v0;                                                              \
        v2 += v1;                                                              \
        v1 = ROTL(v1, 13);                                                     \
        v1 ^= v2;                                                              \
        v2 = ROTL(v2, 16);                                                     \
    } while (0)

static uint32_t halfsiphash(const uint8_t *in, const size_t inlen, const uint8_t *k) {

    uint32_t v0 = 0;
    uint32_t v1 = 0;
    uint32_t v2 = UINT32_C(0x6c796765);
    uint32_t v3 = UINT32_C(0x74656462);
    uint32_t k0 = U8TO32_LE(k);
    uint32_t k1 = U8TO32_LE(k + 4);
    uint32_t m;
    int i;
    const uint8_t *end = in + inlen - (inlen % sizeof(uint32_t));
    const int left = inlen & 3;
    uint32_t b = ((uint32_t)inlen) << 24;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    for (; in != end; in += 4) {
        m = U8TO32_LE(in);
        v3 ^= m;

        for (i = 0; i < cROUNDS; ++i)
            SIPROUND;

        v0 ^= m;
    }

    switch (left) {
        case 3:
            b |= ((uint32_t)in[2]) << 16;
        /* fallthrough */
        case 2:
            b |= ((uint32_t)in[1]) << 8;
        /* fallthrough */
        case 1:
            b |= ((uint32_t)in[0]);
            break;
        case 0:
            break;
    }

    v3 ^= b;

    for (i = 0; i < cROUNDS; ++i)
        SIPROUND;

    v0 ^= b;

    v2 ^= 0xff;

    for (i = 0; i < dROUNDS; ++i)
        SIPROUND;

    b = v1 ^ v3;
    return b;
}
/* end of siphash */

static uint8_t hash_key[JANET_HASH_KEY_SIZE] = {0};

void janet_init_hash_key(uint8_t new_key[JANET_HASH_KEY_SIZE]) {
    memcpy(hash_key, new_key, sizeof(hash_key));
}

/* Calculate hash for string */

int32_t janet_string_calchash(const uint8_t *str, int32_t len) {
    uint32_t hash;
    hash = halfsiphash(str, len, hash_key);
    return (int32_t)hash;
}

#endif

/* Computes hash of an array of values */
int32_t janet_array_calchash(const Janet *array, int32_t len) {
    const Janet *end = array + len;
    uint32_t hash = 33;
    while (array < end) {
        hash = janet_hash_mix(hash, janet_hash(*array++));
    }
    return (int32_t) hash;
}

/* Computes hash of an array of values */
int32_t janet_kv_calchash(const JanetKV *kvs, int32_t len) {
    const JanetKV *end = kvs + len;
    uint32_t hash = 33;
    while (kvs < end) {
        hash = janet_hash_mix(hash, janet_hash(kvs->key));
        hash = janet_hash_mix(hash, janet_hash(kvs->value));
        kvs++;
    }
    return (int32_t) hash;
}

/* Calculate next power of 2. May overflow. If n is 0,
 * will return 0. */
int32_t janet_tablen(int32_t n) {
    if (n < 0) return 0;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* Avoid some undefined behavior that was common in the code base. */
void safe_memcpy(void *dest, const void *src, size_t len) {
    if (!len) return;
    memcpy(dest, src, len);
}

/* Helper to find a value in a Janet struct or table. Returns the bucket
 * containing the key, or the first empty bucket if there is no such key. */
const JanetKV *janet_dict_find(const JanetKV *buckets, int32_t cap, Janet key) {
    int32_t index = janet_maphash(cap, janet_hash(key));
    int32_t i;
    const JanetKV *first_bucket = NULL;
    /* Higher half */
    for (i = index; i < cap; i++) {
        const JanetKV *kv = buckets + i;
        if (janet_checktype(kv->key, JANET_NIL)) {
            if (janet_checktype(kv->value, JANET_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (janet_equals(kv->key, key)) {
            return buckets + i;
        }
    }
    /* Lower half */
    for (i = 0; i < index; i++) {
        const JanetKV *kv = buckets + i;
        if (janet_checktype(kv->key, JANET_NIL)) {
            if (janet_checktype(kv->value, JANET_NIL)) {
                return kv;
            } else if (NULL == first_bucket) {
                first_bucket = kv;
            }
        } else if (janet_equals(kv->key, key)) {
            return buckets + i;
        }
    }
    return first_bucket;
}

/* Get a value from a janet struct or table. */
Janet janet_dictionary_get(const JanetKV *data, int32_t cap, Janet key) {
    const JanetKV *kv = janet_dict_find(data, cap, key);
    if (kv && !janet_checktype(kv->key, JANET_NIL)) {
        return kv->value;
    }
    return janet_wrap_nil();
}

/* Iterate through a struct or dictionary generically */
const JanetKV *janet_dictionary_next(const JanetKV *kvs, int32_t cap, const JanetKV *kv) {
    const JanetKV *end = kvs + cap;
    kv = (kv == NULL) ? kvs : kv + 1;
    while (kv < end) {
        if (!janet_checktype(kv->key, JANET_NIL))
            return kv;
        kv++;
    }
    return NULL;
}

/* Compare a janet string with a cstring. More efficient than loading
 * c string as a janet string. */
int janet_cstrcmp(const uint8_t *str, const char *other) {
    int32_t len = janet_string_length(str);
    int32_t index;
    for (index = 0; index < len; index++) {
        uint8_t c = str[index];
        uint8_t k = ((const uint8_t *)other)[index];
        if (c < k) return -1;
        if (c > k) return 1;
        if (k == '\0') break;
    }
    return (other[index] == '\0') ? 0 : -1;
}

/* Do a binary search on a static array of structs. Each struct must
 * have a string as its first element, and the struct must be sorted
 * lexicographically by that element. */
const void *janet_strbinsearch(
    const void *tab,
    size_t tabcount,
    size_t itemsize,
    const uint8_t *key) {
    size_t low = 0;
    size_t hi = tabcount;
    const char *t = (const char *)tab;
    while (low < hi) {
        size_t mid = low + ((hi - low) / 2);
        const char **item = (const char **)(t + mid * itemsize);
        const char *name = *item;
        int comp = janet_cstrcmp(key, name);
        if (comp < 0) {
            hi = mid;
        } else if (comp > 0) {
            low = mid + 1;
        } else {
            return (const void *)item;
        }
    }
    return NULL;
}

/* Add sourcemapping and documentation to a binding table */
static void janet_add_meta(JanetTable *table, const char *doc, const char *source_file, int32_t source_line) {
    if (doc) {
        janet_table_put(table, janet_ckeywordv("doc"), janet_cstringv(doc));
    }
    if (source_file && source_line) {
        Janet triple[3];
        triple[0] = janet_cstringv(source_file);
        triple[1] = janet_wrap_integer(source_line);
        triple[2] = janet_wrap_integer(1);
        Janet value = janet_wrap_tuple(janet_tuple_n(triple, 3));
        janet_table_put(table, janet_ckeywordv("source-map"), value);
    }
}

/* Add a def to an environment */
void janet_def_sm(JanetTable *env, const char *name, Janet val, const char *doc, const char *source_file, int32_t source_line) {
    JanetTable *subt = janet_table(2);
    janet_table_put(subt, janet_ckeywordv("value"), val);
    janet_add_meta(subt, doc, source_file, source_line);
    janet_table_put(env, janet_csymbolv(name), janet_wrap_table(subt));
}
void janet_def(JanetTable *env, const char *name, Janet value, const char *doc) {
    janet_def_sm(env, name, value, doc, NULL, 0);
}

/* Add a var to the environment */
void janet_var_sm(JanetTable *env, const char *name, Janet val, const char *doc, const char *source_file, int32_t source_line) {
    JanetArray *array = janet_array(1);
    JanetTable *subt = janet_table(2);
    janet_array_push(array, val);
    janet_table_put(subt, janet_ckeywordv("ref"), janet_wrap_array(array));
    janet_add_meta(subt, doc, source_file, source_line);
    janet_table_put(env, janet_csymbolv(name), janet_wrap_table(subt));
}
void janet_var(JanetTable *env, const char *name, Janet val, const char *doc) {
    janet_var_sm(env, name, val, doc, NULL, 0);
}

/* Registry functions */

/* Put the registry in sorted order. */
static void janet_registry_sort(void) {
    for (size_t i = 1; i < janet_vm.registry_count; i++) {
        JanetCFunRegistry reg = janet_vm.registry[i];
        size_t j;
        for (j = i; j > 0; j--) {
            if ((void *)(janet_vm.registry[j - 1].cfun) < (void *)(reg.cfun)) break;
            janet_vm.registry[j] = janet_vm.registry[j - 1];
        }
        janet_vm.registry[j] = reg;
    }
    janet_vm.registry_dirty = 0;
}

void janet_registry_put(
    JanetCFunction key,
    const char *name,
    const char *name_prefix,
    const char *source_file,
    int32_t source_line) {
    if (janet_vm.registry_count == janet_vm.registry_cap) {
        size_t newcap = (janet_vm.registry_count + 1) * 2;
        /* Size it nicely with core by default */
        if (newcap < 512) {
            newcap = 512;
        }
        void *newmem = janet_realloc(janet_vm.registry, newcap * sizeof(JanetCFunRegistry));
        if (NULL == newmem) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.registry = newmem;
        janet_vm.registry_cap = newcap;
    }
    JanetCFunRegistry value = {
        key,
        name,
        name_prefix,
        source_file,
        source_line
    };
    janet_vm.registry[janet_vm.registry_count++] = value;
    janet_vm.registry_dirty = 1;
}

JanetCFunRegistry *janet_registry_get(JanetCFunction key) {
    if (janet_vm.registry_dirty) {
        janet_registry_sort();
    }
    for (size_t i = 0; i < janet_vm.registry_count; i++) {
        if (janet_vm.registry[i].cfun == key) {
            return janet_vm.registry + i;
        }
    }
    JanetCFunRegistry *lo = janet_vm.registry;
    JanetCFunRegistry *hi = lo + janet_vm.registry_count;
    while (lo < hi) {
        JanetCFunRegistry *mid = lo + (hi - lo) / 2;
        if (mid->cfun == key) {
            return mid;
        }
        if ((void *)(mid->cfun) > (void *)(key)) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

typedef struct {
    char *buf;
    size_t plen;
} NameBuf;

static void namebuf_init(NameBuf *namebuf, const char *prefix) {
    size_t plen = strlen(prefix);
    namebuf->plen = plen;
    namebuf->buf = janet_smalloc(namebuf->plen + 256);
    if (NULL == namebuf->buf) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(namebuf->buf, prefix, plen);
    namebuf->buf[plen] = '/';
}

static void namebuf_deinit(NameBuf *namebuf) {
    janet_sfree(namebuf->buf);
}

static char *namebuf_name(NameBuf *namebuf, const char *suffix) {
    size_t slen = strlen(suffix);
    namebuf->buf = janet_srealloc(namebuf->buf, namebuf->plen + 2 + slen);
    if (NULL == namebuf->buf) {
        JANET_OUT_OF_MEMORY;
    }
    memcpy(namebuf->buf + namebuf->plen + 1, suffix, slen);
    namebuf->buf[namebuf->plen + 1 + slen] = '\0';
    return (char *)(namebuf->buf);
}

void janet_cfuns(JanetTable *env, const char *regprefix, const JanetReg *cfuns) {
    while (cfuns->name) {
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        if (env) janet_def(env, cfuns->name, fun, cfuns->documentation);
        janet_registry_put(cfuns->cfun, cfuns->name, regprefix, NULL, 0);
        cfuns++;
    }
}

void janet_cfuns_ext(JanetTable *env, const char *regprefix, const JanetRegExt *cfuns) {
    while (cfuns->name) {
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        if (env) janet_def_sm(env, cfuns->name, fun, cfuns->documentation, cfuns->source_file, cfuns->source_line);
        janet_registry_put(cfuns->cfun, cfuns->name, regprefix, cfuns->source_file, cfuns->source_line);
        cfuns++;
    }
}

void janet_cfuns_prefix(JanetTable *env, const char *regprefix, const JanetReg *cfuns) {
    NameBuf nb;
    if (env) namebuf_init(&nb, regprefix);
    while (cfuns->name) {
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        if (env) janet_def(env, namebuf_name(&nb, cfuns->name), fun, cfuns->documentation);
        janet_registry_put(cfuns->cfun, cfuns->name, regprefix, NULL, 0);
        cfuns++;
    }
    if (env) namebuf_deinit(&nb);
}

void janet_cfuns_ext_prefix(JanetTable *env, const char *regprefix, const JanetRegExt *cfuns) {
    NameBuf nb;
    if (env) namebuf_init(&nb, regprefix);
    while (cfuns->name) {
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        if (env) janet_def_sm(env, namebuf_name(&nb, cfuns->name), fun, cfuns->documentation, cfuns->source_file, cfuns->source_line);
        janet_registry_put(cfuns->cfun, cfuns->name, regprefix, cfuns->source_file, cfuns->source_line);
        cfuns++;
    }
    if (env) namebuf_deinit(&nb);
}

/* Register a value in the global registry */
void janet_register(const char *name, JanetCFunction cfun) {
    janet_registry_put(cfun, name, NULL, NULL, 0);
}

/* Abstract type introspection */

void janet_register_abstract_type(const JanetAbstractType *at) {
    Janet sym = janet_csymbolv(at->name);
    Janet check = janet_table_get(janet_vm.abstract_registry, sym);
    if (!janet_checktype(check, JANET_NIL) && at != janet_unwrap_pointer(check)) {
        janet_panicf("cannot register abstract type %s, "
                     "a type with the same name exists", at->name);
    }
    janet_table_put(janet_vm.abstract_registry, sym, janet_wrap_pointer((void *) at));
}

const JanetAbstractType *janet_get_abstract_type(Janet key) {
    Janet wrapped = janet_table_get(janet_vm.abstract_registry, key);
    if (janet_checktype(wrapped, JANET_NIL)) {
        return NULL;
    }
    return (JanetAbstractType *)(janet_unwrap_pointer(wrapped));
}

#ifndef JANET_BOOTSTRAP
void janet_core_def_sm(JanetTable *env, const char *name, Janet x, const void *p, const void *sf, int32_t sl) {
    (void) sf;
    (void) sl;
    (void) p;
    Janet key = janet_csymbolv(name);
    janet_table_put(env, key, x);
    if (janet_checktype(x, JANET_CFUNCTION)) {
        janet_registry_put(janet_unwrap_cfunction(x), name, NULL, NULL, 0);
    }
}

void janet_core_cfuns_ext(JanetTable *env, const char *regprefix, const JanetRegExt *cfuns) {
    (void) regprefix;
    while (cfuns->name) {
        Janet fun = janet_wrap_cfunction(cfuns->cfun);
        janet_table_put(env, janet_csymbolv(cfuns->name), fun);
        janet_registry_put(cfuns->cfun, cfuns->name, regprefix, cfuns->source_file, cfuns->source_line);
        cfuns++;
    }
}
#endif

JanetBinding janet_binding_from_entry(Janet entry) {
    JanetTable *entry_table;
    JanetBinding binding = {
        JANET_BINDING_NONE,
        janet_wrap_nil(),
        JANET_BINDING_DEP_NONE
    };

    /* Check environment for entry */
    if (!janet_checktype(entry, JANET_TABLE))
        return binding;
    entry_table = janet_unwrap_table(entry);

    /* deprecation check */
    Janet deprecate = janet_table_get(entry_table, janet_ckeywordv("deprecated"));
    if (janet_checktype(deprecate, JANET_KEYWORD)) {
        JanetKeyword depkw = janet_unwrap_keyword(deprecate);
        if (!janet_cstrcmp(depkw, "relaxed")) {
            binding.deprecation = JANET_BINDING_DEP_RELAXED;
        } else if (!janet_cstrcmp(depkw, "normal")) {
            binding.deprecation = JANET_BINDING_DEP_NORMAL;
        } else if (!janet_cstrcmp(depkw, "strict")) {
            binding.deprecation = JANET_BINDING_DEP_STRICT;
        }
    } else if (!janet_checktype(deprecate, JANET_NIL)) {
        binding.deprecation = JANET_BINDING_DEP_NORMAL;
    }

    int macro = janet_truthy(janet_table_get(entry_table, janet_ckeywordv("macro")));
    Janet value = janet_table_get(entry_table, janet_ckeywordv("value"));
    Janet ref = janet_table_get(entry_table, janet_ckeywordv("ref"));
    int ref_is_valid = janet_checktype(ref, JANET_ARRAY);
    int redef = ref_is_valid && janet_truthy(janet_table_get(entry_table, janet_ckeywordv("redef")));

    if (macro) {
        binding.value = redef ? ref : value;
        binding.type = redef ? JANET_BINDING_DYNAMIC_MACRO : JANET_BINDING_MACRO;
        return binding;
    }

    if (ref_is_valid) {
        binding.value = ref;
        binding.type = redef ? JANET_BINDING_DYNAMIC_DEF : JANET_BINDING_VAR;
    } else {
        binding.value = value;
        binding.type = JANET_BINDING_DEF;
    }

    return binding;
}

/* If the value at the given address can be coerced to a byte view,
   return that byte view. If it can't, replace the value at the address
   with the result of janet_to_string, and return a byte view over that
   string. */
static JanetByteView memoize_byte_view(Janet *value) {
    JanetByteView result;
    if (!janet_bytes_view(*value, &result.bytes, &result.len)) {
        JanetString str = janet_to_string(*value);
        *value = janet_wrap_string(str);
        result.bytes = str;
        result.len = janet_string_length(str);
    }
    return result;
}

static JanetByteView to_byte_view(Janet value) {
    JanetByteView result;
    if (!janet_bytes_view(value, &result.bytes, &result.len)) {
        JanetString str = janet_to_string(value);
        result.bytes = str;
        result.len = janet_string_length(str);
    }
    return result;
}

JanetByteView janet_text_substitution(
    Janet *subst,
    const uint8_t *bytes,
    uint32_t len,
    JanetArray *extra_argv) {
    int32_t extra_argc = extra_argv == NULL ? 0 : extra_argv->count;
    JanetType type = janet_type(*subst);
    switch (type) {
        case JANET_FUNCTION:
        case JANET_CFUNCTION: {
            int32_t argc = 1 + extra_argc;
            Janet *argv = janet_tuple_begin(argc);
            argv[0] = janet_stringv(bytes, len);
            for (int32_t i = 0; i < extra_argc; i++) {
                argv[i + 1] = extra_argv->data[i];
            }
            janet_tuple_end(argv);
            if (type == JANET_FUNCTION) {
                return to_byte_view(janet_call(janet_unwrap_function(*subst), argc, argv));
            } else {
                return to_byte_view(janet_unwrap_cfunction(*subst)(argc, argv));
            }
        }
        default:
            return memoize_byte_view(subst);
    }
}

JanetBinding janet_resolve_ext(JanetTable *env, const uint8_t *sym) {
    Janet entry = janet_table_get(env, janet_wrap_symbol(sym));
    return janet_binding_from_entry(entry);
}

JanetBindingType janet_resolve(JanetTable *env, const uint8_t *sym, Janet *out) {
    JanetBinding binding = janet_resolve_ext(env, sym);
    if (binding.type == JANET_BINDING_DYNAMIC_DEF || binding.type == JANET_BINDING_DYNAMIC_MACRO) {
        *out = janet_array_peek(janet_unwrap_array(binding.value));
    } else {
        *out = binding.value;
    }
    return binding.type;
}

/* Resolve a symbol in the core environment. */
Janet janet_resolve_core(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    janet_resolve(env, janet_csymbol(name), &out);
    return out;
}

/* Read both tuples and arrays as c pointers + int32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int janet_indexed_view(Janet seq, const Janet **data, int32_t *len) {
    if (janet_checktype(seq, JANET_ARRAY)) {
        *data = janet_unwrap_array(seq)->data;
        *len = janet_unwrap_array(seq)->count;
        return 1;
    } else if (janet_checktype(seq, JANET_TUPLE)) {
        *data = janet_unwrap_tuple(seq);
        *len = janet_tuple_length(janet_unwrap_tuple(seq));
        return 1;
    }
    return 0;
}

/* Read both strings and buffer as unsigned character array + int32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int janet_bytes_view(Janet str, const uint8_t **data, int32_t *len) {
    JanetType t = janet_type(str);
    if (t == JANET_STRING || t == JANET_SYMBOL || t == JANET_KEYWORD) {
        *data = janet_unwrap_string(str);
        *len = janet_string_length(janet_unwrap_string(str));
        return 1;
    } else if (t == JANET_BUFFER) {
        *data = janet_unwrap_buffer(str)->data;
        *len = janet_unwrap_buffer(str)->count;
        return 1;
    } else if (t == JANET_ABSTRACT) {
        void *abst = janet_unwrap_abstract(str);
        const JanetAbstractType *atype = janet_abstract_type(abst);
        if (NULL == atype->bytes) {
            return 0;
        }
        JanetByteView view = atype->bytes(abst, janet_abstract_size(abst));
        *data = view.bytes;
        *len = view.len;
        return 1;
    }
    return 0;
}

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int janet_dictionary_view(Janet tab, const JanetKV **data, int32_t *len, int32_t *cap) {
    if (janet_checktype(tab, JANET_TABLE)) {
        *data = janet_unwrap_table(tab)->data;
        *cap = janet_unwrap_table(tab)->capacity;
        *len = janet_unwrap_table(tab)->count;
        return 1;
    } else if (janet_checktype(tab, JANET_STRUCT)) {
        *data = janet_unwrap_struct(tab);
        *cap = janet_struct_capacity(janet_unwrap_struct(tab));
        *len = janet_struct_length(janet_unwrap_struct(tab));
        return 1;
    }
    return 0;
}

int janet_checkint(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkintrange(dval);
}

int janet_checkuint(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkuintrange(dval);
}

int janet_checkint64(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkint64range(dval);
}

int janet_checkuint64(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkuint64range(dval);
}

int janet_checkint16(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkint16range(dval);
}

int janet_checkuint16(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    return janet_checkuint16range(dval);
}

int janet_checksize(Janet x) {
    if (!janet_checktype(x, JANET_NUMBER))
        return 0;
    double dval = janet_unwrap_number(x);
    if (dval != (double)((size_t) dval)) return 0;
    if (SIZE_MAX > JANET_INTMAX_INT64) {
        return dval <= JANET_INTMAX_INT64;
    } else {
        return dval <= SIZE_MAX;
    }
}

JanetTable *janet_get_core_table(const char *name) {
    JanetTable *env = janet_core_env(NULL);
    Janet out = janet_wrap_nil();
    JanetBindingType bt = janet_resolve(env, janet_csymbol(name), &out);
    if (bt == JANET_BINDING_NONE) return NULL;
    if (!janet_checktype(out, JANET_TABLE)) return NULL;
    return janet_unwrap_table(out);
}

/* Sort keys of a dictionary type */
int32_t janet_sorted_keys(const JanetKV *dict, int32_t cap, int32_t *index_buffer) {

    /* First, put populated indices into index_buffer */
    int32_t next_index = 0;
    for (int32_t i = 0; i < cap; i++) {
        if (!janet_checktype(dict[i].key, JANET_NIL)) {
            index_buffer[next_index++] = i;
        }
    }

    /* Next, sort those (simple insertion sort here for now) */
    for (int32_t i = 1; i < next_index; i++) {
        int32_t index_to_insert = index_buffer[i];
        Janet lhs = dict[index_to_insert].key;
        for (int32_t j = i - 1; j >= 0; j--) {
            index_buffer[j + 1] = index_buffer[j];
            Janet rhs = dict[index_buffer[j]].key;
            if (janet_compare(lhs, rhs) >= 0) {
                index_buffer[j + 1] = index_to_insert;
                break;
            } else if (j == 0) {
                index_buffer[0] = index_to_insert;
            }
        }
    }

    /* Return number of indices found */
    return next_index;

}

/* Clock shims for various platforms */
#ifdef JANET_GETTIME
#ifdef JANET_WINDOWS
#include <profileapi.h>
int janet_gettime(struct timespec *spec, enum JanetTimeSource source) {
    if (source == JANET_TIME_REALTIME) {
        FILETIME ftime;
        GetSystemTimeAsFileTime(&ftime);
        int64_t wintime = (int64_t)(ftime.dwLowDateTime) | ((int64_t)(ftime.dwHighDateTime) << 32);
        /* Windows epoch is January 1, 1601 apparently */
        wintime -= 116444736000000000LL;
        spec->tv_sec  = wintime / 10000000LL;
        /* Resolution is 100 nanoseconds. */
        spec->tv_nsec = wintime % 10000000LL * 100;
    } else if (source == JANET_TIME_MONOTONIC) {
        LARGE_INTEGER count;
        LARGE_INTEGER perf_freq;
        QueryPerformanceCounter(&count);
        QueryPerformanceFrequency(&perf_freq);
        spec->tv_sec = count.QuadPart / perf_freq.QuadPart;
        spec->tv_nsec = (long)((count.QuadPart % perf_freq.QuadPart) * 1000000000 / perf_freq.QuadPart);
    } else if (source == JANET_TIME_CPUTIME) {
        FILETIME creationTime, exitTime, kernelTime, userTime;
        GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime);
        int64_t tmp = ((int64_t)userTime.dwHighDateTime << 32) + userTime.dwLowDateTime;
        spec->tv_sec = tmp / 10000000LL;
        spec->tv_nsec = tmp % 10000000LL * 100;
    }
    return 0;
}
/* clock_gettime() wasn't available on Mac until 10.12. */
#elif defined(JANET_APPLE) && !defined(MAC_OS_X_VERSION_10_12)
#include <mach/clock.h>
#include <mach/mach.h>
int janet_gettime(struct timespec *spec, enum JanetTimeSource source) {
    if (source == JANET_TIME_REALTIME) {
        clock_serv_t cclock;
        mach_timespec_t mts;
        host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
        clock_get_time(cclock, &mts);
        mach_port_deallocate(mach_task_self(), cclock);
        spec->tv_sec = mts.tv_sec;
        spec->tv_nsec = mts.tv_nsec;
    } else if (source == JANET_TIME_MONOTONIC) {
        clock_serv_t cclock;
        int nsecs;
        mach_msg_type_number_t count;
        host_get_clock_service(mach_host_self(), clock, &cclock);
        clock_get_attributes(cclock, CLOCK_GET_TIME_RES, (clock_attr_t)&nsecs, &count);
        mach_port_deallocate(mach_task_self(), cclock);
        clock_getres(CLOCK_MONOTONIC, spec);
    }
    if (source == JANET_TIME_CPUTIME) {
        clock_t tmp = clock();
        spec->tv_sec = tmp;
        spec->tv_nsec = (tmp - spec->tv_sec) * 1.0e9;
    }
    return 0;
}
#else
int janet_gettime(struct timespec *spec, enum JanetTimeSource source) {
    clockid_t cid = CLOCK_REALTIME;
    if (source == JANET_TIME_REALTIME) {
        cid = CLOCK_REALTIME;
    } else if (source == JANET_TIME_MONOTONIC) {
        cid = CLOCK_MONOTONIC;
    } else if (source == JANET_TIME_CPUTIME) {
        cid = CLOCK_PROCESS_CPUTIME_ID;
    }
    return clock_gettime(cid, spec);
}
#endif
#endif

/* Better strerror (thread-safe if available) */
const char *janet_strerror(int e) {
#ifdef JANET_WINDOWS
    /* Microsoft strerror seems sane here and is thread safe by default */
    return strerror(e);
#elif defined(__GLIBC__)
    /* See https://linux.die.net/man/3/strerror_r */
    return strerror_r(e, janet_vm.strerror_buf, sizeof(janet_vm.strerror_buf));
#else
    strerror_r(e, janet_vm.strerror_buf, sizeof(janet_vm.strerror_buf));
    return janet_vm.strerror_buf;
#endif
}

/* Setting C99 standard makes this not available, but it should
 * work/link properly if we detect a BSD */
#if defined(JANET_BSD) || defined(MAC_OS_X_VERSION_10_7)
void arc4random_buf(void *buf, size_t nbytes);
#endif

int janet_cryptorand(uint8_t *out, size_t n) {
#ifndef JANET_NO_CRYPTORAND
#ifdef JANET_WINDOWS
    for (size_t i = 0; i < n; i += sizeof(unsigned int)) {
        unsigned int v;
        if (rand_s(&v))
            return -1;
        for (int32_t j = 0; (j < (int32_t) sizeof(unsigned int)) && (i + j < n); j++) {
            out[i + j] = v & 0xff;
            v = v >> 8;
        }
    }
    return 0;
#elif defined(JANET_BSD) || defined(MAC_OS_X_VERSION_10_7)
    arc4random_buf(out, n);
    return 0;
#else
    /* We should be able to call getrandom on linux, but it doesn't seem
       to be uniformly supported on linux distros.
       On Mac, arc4random_buf wasn't available on until 10.7.
       In these cases, use this fallback path for now... */
    int rc;
    int randfd;
    RETRY_EINTR(randfd, open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (randfd < 0)
        return -1;
    while (n > 0) {
        ssize_t nread;
        RETRY_EINTR(nread, read(randfd, out, n));
        if (nread <= 0) {
            RETRY_EINTR(rc, close(randfd));
            return -1;
        }
        out += nread;
        n -= nread;
    }
    RETRY_EINTR(rc, close(randfd));
    return 0;
#endif
#else
    (void) out;
    (void) n;
    return -1;
#endif
}

/* Dynamic library loading */

char *get_processed_name(const char *name) {
    if (name[0] == '.') return (char *) name;
    const char *c;
    for (c = name; *c; c++) {
        if (*c == '/') return (char *) name;
    }
    size_t l = (size_t)(c - name);
    char *ret = janet_malloc(l + 3);
    if (NULL == ret) {
        JANET_OUT_OF_MEMORY;
    }
    ret[0] = '.';
    ret[1] = '/';
    memcpy(ret + 2, name, l + 1);
    return ret;
}

#if defined(JANET_NO_DYNAMIC_MODULES)

const char *error_clib(void) {
    return "dynamic modules not supported";
}

#else
#if defined(JANET_WINDOWS)

static char error_clib_buf[256];
char *error_clib(void) {
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   error_clib_buf, sizeof(error_clib_buf), NULL);
    error_clib_buf[strlen(error_clib_buf) - 1] = '\0';
    return error_clib_buf;
}

Clib load_clib(const char *name) {
    if (name == NULL) {
        return GetModuleHandle(NULL);
    } else {
        return LoadLibrary(name);
    }
}

void free_clib(HINSTANCE clib) {
    if (clib != GetModuleHandle(NULL)) {
        FreeLibrary(clib);
    }
}

void *symbol_clib(HINSTANCE clib, const char *sym) {
    if (clib != GetModuleHandle(NULL)) {
        return GetProcAddress(clib, sym);
    } else {
        /* Look up symbols from all loaded modules */
        HMODULE hMods[1024];
        DWORD needed = 0;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &needed)) {
            needed /= sizeof(HMODULE);
            for (DWORD i = 0; i < needed; i++) {
                void *address = GetProcAddress(hMods[i], sym);
                if (NULL != address) {
                    return address;
                }
            }
        } else {
            janet_panicf("ffi: %s", error_clib());
        }
        return NULL;
    }
}

#endif
#endif

/* Alloc function macro fills */
void *(janet_malloc)(size_t size) {
    return janet_malloc(size);
}

void (janet_free)(void *ptr) {
    janet_free(ptr);
}

void *(janet_calloc)(size_t nmemb, size_t size) {
    return janet_calloc(nmemb, size);
}

void *(janet_realloc)(void *ptr, size_t size) {
    return janet_realloc(ptr, size);
}

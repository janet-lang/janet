/*
* Copyright (c) 2020 Calvin Rose
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

#ifndef JANET_UTIL_H_defined
#define JANET_UTIL_H_defined

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#endif

#include <stdio.h>
#include <errno.h>

#if !defined(JANET_REDUCED_OS) || !defined(JANET_SINGLE_THREADED)
#include <time.h>
#define JANET_GETTIME
#endif

/* Handle runtime errors */
#ifndef JANET_EXIT
#include <stdio.h>
#define JANET_EXIT(m) do { \
    fprintf(stderr, "C runtime error at line %d in file %s: %s\n",\
        __LINE__,\
        __FILE__,\
        (m));\
    exit(1);\
} while (0)
#endif

#define janet_assert(c, m) do { \
    if (!(c)) JANET_EXIT((m)); \
} while (0)

/* What to do when out of memory */
#ifndef JANET_OUT_OF_MEMORY
#include <stdio.h>
#define JANET_OUT_OF_MEMORY do { fprintf(stderr, "janet out of memory\n"); exit(1); } while (0)
#endif

/* Omit docstrings in some builds */
#ifndef JANET_BOOTSTRAP
#define JDOC(x) NULL
#define JANET_NO_BOOTSTRAP
#else
#define JDOC(x) x
#endif

/* Utils */
#define janet_maphash(cap, hash) ((uint32_t)(hash) & (cap - 1))
extern const char janet_base64[65];
int32_t janet_array_calchash(const Janet *array, int32_t len);
int32_t janet_kv_calchash(const JanetKV *kvs, int32_t len);
int32_t janet_string_calchash(const uint8_t *str, int32_t len);
int32_t janet_tablen(int32_t n);
void safe_memcpy(void *dest, const void *src, size_t len);
void janet_buffer_push_types(JanetBuffer *buffer, int types);
const JanetKV *janet_dict_find(const JanetKV *buckets, int32_t cap, Janet key);
void janet_memempty(JanetKV *mem, int32_t count);
void *janet_memalloc_empty(int32_t count);
JanetTable *janet_get_core_table(const char *name);
void janet_def_addflags(JanetFuncDef *def);
const void *janet_strbinsearch(
    const void *tab,
    size_t tabcount,
    size_t itemsize,
    const uint8_t *key);
void janet_buffer_format(
    JanetBuffer *b,
    const char *strfrmt,
    int32_t argstart,
    int32_t argc,
    Janet *argv);

/* Inside the janet core, defining globals is different
 * at bootstrap time and normal runtime */
#ifdef JANET_BOOTSTRAP
#define janet_core_def janet_def
#define janet_core_cfuns janet_cfuns
#else
void janet_core_def(JanetTable *env, const char *name, Janet x, const void *p);
void janet_core_cfuns(JanetTable *env, const char *regprefix, const JanetReg *cfuns);
#endif

/* Clock gettime */
#ifdef JANET_GETTIME
int janet_gettime(struct timespec *spec);
#endif

#define RETRY_EINTR(RC, CALL) do { (RC) = CALL; } while((RC) < 0 && errno == EINTR)

/* Initialize builtin libraries */
void janet_lib_io(JanetTable *env);
void janet_lib_math(JanetTable *env);
void janet_lib_array(JanetTable *env);
void janet_lib_tuple(JanetTable *env);
void janet_lib_buffer(JanetTable *env);
void janet_lib_table(JanetTable *env);
void janet_lib_fiber(JanetTable *env);
void janet_lib_os(JanetTable *env);
void janet_lib_string(JanetTable *env);
void janet_lib_marsh(JanetTable *env);
void janet_lib_parse(JanetTable *env);
#ifdef JANET_ASSEMBLER
void janet_lib_asm(JanetTable *env);
#endif
void janet_lib_compile(JanetTable *env);
void janet_lib_debug(JanetTable *env);
#ifdef JANET_PEG
void janet_lib_peg(JanetTable *env);
#endif
#ifdef JANET_TYPED_ARRAY
void janet_lib_typed_array(JanetTable *env);
#endif
#ifdef JANET_INT_TYPES
void janet_lib_inttypes(JanetTable *env);
#endif
#ifdef JANET_THREADS
void janet_lib_thread(JanetTable *env);
#endif
#ifdef JANET_NET
void janet_lib_net(JanetTable *env);
void janet_net_deinit(void);
void janet_net_markloop(void);
#endif

#endif

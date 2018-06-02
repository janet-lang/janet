/*
* Copyright (c) 2018 Calvin Rose
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

#ifndef DST_CONFIG_H_defined
#define DST_CONFIG_H_defined

#ifdef __cplusplus
extern "C" {
#endif

#define DST_VERSION "0.0.0 alpha"

/*
 * Detect OS and endianess.
 * From webkit source. There is likely some extreneous
 * detection for unsupported platforms
 */

/* Check Unix */
#if defined(_AIX) \
    || defined(__APPLE__) /* Darwin */ \
    || defined(__FreeBSD__) || defined(__DragonFly__) \
    || defined(__FreeBSD_kernel__) \
    || defined(__GNU__) /* GNU/Hurd */ \
    || defined(__linux__) \
    || defined(__NetBSD__) \
    || defined(__OpenBSD__) \
    || defined(__QNXNTO__) \
    || defined(sun) || defined(__sun) /* Solaris */ \
    || defined(unix) || defined(__unix) || defined(__unix__)
#define DST_UNIX 1
/* Enable certain posix features */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#elif defined(__EMSCRIPTEN__)
#define DST_WEB 1
#elif defined(WIN32) || defined(_WIN32)
#define DST_WINDOWS 1
#endif

/* Check 64-bit vs 32-bit */
#if ((defined(__x86_64__) || defined(_M_X64)) \
     && (defined(DST_UNIX) || defined(DST_WINDOWS))) \
	|| (defined(_WIN64)) /* Windows 64 bit */ \
    || (defined(__ia64__) && defined(__LP64__)) /* Itanium in LP64 mode */ \
    || defined(__alpha__) /* DEC Alpha */ \
    || (defined(__sparc__) && defined(__arch64__) || defined (__sparcv9)) /* BE */ \
    || defined(__s390x__) /* S390 64-bit (BE) */ \
    || (defined(__ppc64__) || defined(__PPC64__)) \
    || defined(__aarch64__) /* ARM 64-bit */
#define DST_64 1
#else
#define DST_32 1
#endif

/* Check big endian */
#if defined(__MIPSEB__) /* MIPS 32-bit */ \
    || defined(__ppc__) || defined(__PPC__) /* CPU(PPC) - PowerPC 32-bit */ \
    || defined(__powerpc__) || defined(__powerpc) || defined(__POWERPC__) \
    || defined(_M_PPC) || defined(__PPC) \
    || defined(__ppc64__) || defined(__PPC64__) /* PowerPC 64-bit */ \
    || defined(__sparc)   /* Sparc 32bit */  \
    || defined(__sparc__) /* Sparc 64-bit */ \
    || defined(__s390x__) /* S390 64-bit */ \
    || defined(__s390__)  /* S390 32-bit */ \
    || defined(__ARMEB__) /* ARM big endian */ \
    || ((defined(__CC_ARM) || defined(__ARMCC__)) /* ARM RealView compiler */ \
        && defined(__BIG_ENDIAN))
#define DST_BIG_ENDIAN 1
#else
#define DST_LITTLE_ENDIAN 1
#endif

/* Define how global dst state is declared */
#ifdef DST_SINGLE_THREADED
#define DST_THREAD_LOCAL
#else
#if defined(__GNUC__)
#define DST_THREAD_LOCAL __thread
#elif defined(_MSC_BUILD)
#define DST_THREAD_LOCAL __declspec(thread)
#else
#define DST_THREAD_LOCAL
#endif
#endif

/* Include default headers */
#include <stdint.h>

/* Handle runtime errors */
#ifndef dst_exit
#include <stdlib.h>
#include <stdio.h>
#define dst_exit(m) do { \
    printf("runtime error at line %d in file %s: %s\n",\
        __LINE__,\
        __FILE__,\
        (m));\
    exit(1);\
} while (0)
#endif

#define dst_assert(c, m) do { \
    if (!(c)) dst_exit((m)); \
} while (0)

/* What to do when out of memory */
#ifndef DST_OUT_OF_MEMORY
#include <stdio.h>
#define DST_OUT_OF_MEMORY do { printf("out of memory\n"); exit(1); } while (0)
#endif

/* Helper for debugging */
#define dst_trace(x) dst_puts(dst_formatc("DST TRACE %s, %d: %v\n", __FILE__, __LINE__, x))

/* Prevent some recursive functions from recursing too deeply
 * ands crashing (the parser). Instead, error out. */
#define DST_RECURSION_GUARD 1024

/* Maximum depth to follow table prototypes before giving up and returning nil. */
#define DST_MAX_PROTO_DEPTH 200

/* Define max stack size for stacks before raising a stack overflow error.
 * If this is not defined, fiber stacks can grow without limit (until memory
 * runs out) */
#define DST_STACK_MAX 8192

/* Use nanboxed values - uses 8 bytes per value instead of 12 or 16. */
#define DST_NANBOX
#define DST_NANBOX_47

/* Alignment for pointers */
#ifdef DST_32
#define DST_WALIGN 4
#else
#define DST_WALIGN 8
#endif

#ifdef __cplusplus
}
#endif

#endif /* DST_CONFIG_H_defined */

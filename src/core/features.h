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

/* Feature test macros */

#ifndef JANET_FEATURES_H_defined
#define JANET_FEATURES_H_defined

#if defined(__NetBSD__) || defined(__APPLE__) || defined(__OpenBSD__) \
    || defined(__bsdi__) || defined(__DragonFly__) || defined(__FreeBSD__)
/* Use BSD source on any BSD systems, include OSX */
# define _BSD_SOURCE
# define _POSIX_C_SOURCE 200809L
#else
/* Use POSIX feature flags */
# ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200809L
# endif
#endif

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

/* Needed for sched.h for cpu count */
#ifdef __linux__
#define _GNU_SOURCE
#endif

#if defined(WIN32) || defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#endif

/* needed for inet_pton and InitializeSRWLock */
#ifdef __MINGW32__
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif

/* Needed for realpath on linux, as well as pthread rwlocks. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#if _XOPEN_SOURCE < 600
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

/* Needed for timegm and other extensions when building with -std=c99.
 * It also defines realpath, etc, which would normally require
 * _XOPEN_SOURCE >= 500. */
#if !defined(_NETBSD_SOURCE) && defined(__NetBSD__)
#define _NETBSD_SOURCE
#endif

/* Needed for several things when building with -std=c99. */
#if !__BSD_VISIBLE && (defined(__DragonFly__) || defined(__FreeBSD__))
#define __BSD_VISIBLE 1
#endif

#define _FILE_OFFSET_BITS 64

#endif

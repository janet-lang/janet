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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#endif

#ifndef JANET_REDUCED_OS

#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define RETRY_EINTR(RC, CALL) do { (RC) = CALL; } while((RC) < 0 && errno == EINTR)

#ifdef JANET_APPLE
#include <AvailabilityMacros.h>
#endif

#ifdef JANET_WINDOWS
#include <windows.h>
#include <direct.h>
#include <sys/utime.h>
#include <io.h>
#include <process.h>
#else
#include <spawn.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
extern char **environ;
#ifdef JANET_THREADS
#include <pthread.h>
#endif
#endif

/* For macos */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

/* Setting C99 standard makes this not available, but it should
 * work/link properly if we detect a BSD */
#if defined(JANET_BSD) || defined(MAC_OS_X_VERSION_10_7)
void arc4random_buf(void *buf, size_t nbytes);
#endif

/* Not POSIX, but all Unixes but Solaris have this function. */
#if defined(JANET_POSIX) && !defined(__sun)
time_t timegm(struct tm *tm);
#elif defined(JANET_WINDOWS)
#define timegm _mkgmtime
#endif

/* Access to some global variables should be synchronized if not in single threaded mode, as
 * setenv/getenv are not thread safe. */
#ifdef JANET_THREADS
# ifdef JANET_WINDOWS
static int env_lock_initialized = 0;
static CRITICAL_SECTION env_lock;
static void janet_lock_environ(void) {
    EnterCriticalSection(&env_lock);
}
static void janet_unlock_environ(void) {
    LeaveCriticalSection(&env_lock);
}
# else
static pthread_mutex_t env_lock = PTHREAD_MUTEX_INITIALIZER;
static void janet_lock_environ(void) {
    pthread_mutex_lock(&env_lock);
}
static void janet_unlock_environ(void) {
    pthread_mutex_unlock(&env_lock);
}
# endif
#else
static void janet_lock_environ(void) {
}
static void janet_unlock_environ(void) {
}
#endif

#endif /* JANET_REDCUED_OS */

/* Core OS functions */

/* Full OS functions */

#define janet_stringify1(x) #x
#define janet_stringify(x) janet_stringify1(x)

static Janet os_which(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
#if defined(JANET_OS_NAME)
    return janet_ckeywordv(janet_stringify(JANET_OS_NAME));
#elif defined(JANET_WINDOWS)
    return janet_ckeywordv("windows");
#elif defined(JANET_APPLE)
    return janet_ckeywordv("macos");
#elif defined(__EMSCRIPTEN__)
    return janet_ckeywordv("web");
#elif defined(JANET_LINUX)
    return janet_ckeywordv("linux");
#elif defined(__FreeBSD__)
    return janet_ckeywordv("freebsd");
#elif defined(__NetBSD__)
    return janet_ckeywordv("netbsd");
#elif defined(__OpenBSD__)
    return janet_ckeywordv("openbsd");
#elif defined(JANET_BSD)
    return janet_ckeywordv("bsd");
#else
    return janet_ckeywordv("posix");
#endif
}

/* Detect the ISA we are compiled for */
static Janet os_arch(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    /* Check 64-bit vs 32-bit */
#if defined(JANET_ARCH_NAME)
    return janet_ckeywordv(janet_stringify(JANET_ARCH_NAME));
#elif defined(__EMSCRIPTEN__)
    return janet_ckeywordv("wasm");
#elif (defined(__x86_64__) || defined(_M_X64))
    return janet_ckeywordv("x64");
#elif defined(__i386) || defined(_M_IX86)
    return janet_ckeywordv("x86");
#elif defined(_M_ARM64) || defined(__aarch64__)
    return janet_ckeywordv("aarch64");
#elif defined(_M_ARM) || defined(__arm__)
    return janet_ckeywordv("arm");
#elif (defined(__sparc__))
    return janet_ckeywordv("sparc");
#elif (defined(__ppc__))
    return janet_ckeywordv("ppc");
#else
    return janet_ckeywordv("unknown");
#endif
}

#undef janet_stringify1
#undef janet_stringify

static Janet os_exit(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    int status;
    if (argc == 0) {
        status = EXIT_SUCCESS;
    } else if (janet_checkint(argv[0])) {
        status = janet_unwrap_integer(argv[0]);
    } else {
        status = EXIT_FAILURE;
    }
    janet_deinit();
    exit(status);
    return janet_wrap_nil();
}

#ifndef JANET_REDUCED_OS

#ifndef JANET_NO_PROCESSES
/* Get env for os_execute */
static char **os_execute_env(int32_t argc, const Janet *argv) {
    char **envp = NULL;
    if (argc > 2) {
        JanetDictView dict = janet_getdictionary(argv, 2);
        envp = janet_smalloc(sizeof(char *) * ((size_t)dict.len + 1));
        int32_t j = 0;
        for (int32_t i = 0; i < dict.cap; i++) {
            const JanetKV *kv = dict.kvs + i;
            if (!janet_checktype(kv->key, JANET_STRING)) continue;
            if (!janet_checktype(kv->value, JANET_STRING)) continue;
            const uint8_t *keys = janet_unwrap_string(kv->key);
            const uint8_t *vals = janet_unwrap_string(kv->value);
            int32_t klen = janet_string_length(keys);
            int32_t vlen = janet_string_length(vals);
            /* Check keys has no embedded 0s or =s. */
            int skip = 0;
            for (int32_t k = 0; k < klen; k++) {
                if (keys[k] == '\0' || keys[k] == '=') {
                    skip = 1;
                    break;
                }
            }
            if (skip) continue;
            char *envitem = janet_smalloc((size_t) klen + (size_t) vlen + 2);
            memcpy(envitem, keys, klen);
            envitem[klen] = '=';
            memcpy(envitem + klen + 1, vals, vlen);
            envitem[klen + vlen + 1] = 0;
            envp[j++] = envitem;
        }
        envp[j] = NULL;
    }
    return envp;
}

/* Free memory from os_execute */
static void os_execute_cleanup(char **envp, const char **child_argv) {
#ifdef JANET_WINDOWS
    (void) child_argv;
#else
    janet_sfree((void *)child_argv);
#endif
    if (NULL != envp) {
        char **envitem = envp;
        while (*envitem != NULL) {
            janet_sfree(*envitem);
            envitem++;
        }
    }
    janet_sfree(envp);
}

#ifdef JANET_WINDOWS
/* Windows processes created via CreateProcess get only one command line argument string, and
 * must parse this themselves. Each processes is free to do this however they like, but the
 * standard parsing method is CommandLineToArgvW. We need to properly escape arguments into
 * a single string of this format. Returns a buffer that can be cast into a c string. */
static JanetBuffer *os_exec_escape(JanetView args) {
    JanetBuffer *b = janet_buffer(0);
    for (int32_t i = 0; i < args.len; i++) {
        const char *arg = janet_getcstring(args.items, i);

        /* Push leading space if not first */
        if (i) janet_buffer_push_u8(b, ' ');

        /* Find first special character */
        const char *first_spec = arg;
        while (*first_spec) {
            switch (*first_spec) {
                case ' ':
                case '\t':
                case '\v':
                case '\n':
                case '"':
                    goto found;
                case '\0':
                    janet_panic("embedded 0 not allowed in command line string");
                default:
                    first_spec++;
                    break;
            }
        }
    found:

        /* Check if needs escape */
        if (*first_spec == '\0') {
            /* No escape needed */
            janet_buffer_push_cstring(b, arg);
        } else {
            /* Escape */
            janet_buffer_push_u8(b, '"');
            for (const char *c = arg; ; c++) {
                unsigned numBackSlashes = 0;
                while (*c == '\\') {
                    c++;
                    numBackSlashes++;
                }
                if (*c == '"') {
                    /* Escape all backslashes and double quote mark */
                    int32_t n = 2 * numBackSlashes + 1;
                    janet_buffer_extra(b, n + 1);
                    memset(b->data + b->count, '\\', n);
                    b->count += n;
                    janet_buffer_push_u8(b, '"');
                } else if (*c) {
                    /* Don't escape backslashes. */
                    int32_t n = numBackSlashes;
                    janet_buffer_extra(b, n + 1);
                    memset(b->data + b->count, '\\', n);
                    b->count += n;
                    janet_buffer_push_u8(b, *c);
                } else {
                    /* we finished Escape all backslashes */
                    int32_t n = 2 * numBackSlashes;
                    janet_buffer_extra(b, n + 1);
                    memset(b->data + b->count, '\\', n);
                    b->count += n;
                    break;
                }
            }
            janet_buffer_push_u8(b, '"');
        }
    }
    janet_buffer_push_u8(b, 0);
    return b;
}
#endif

static Janet os_execute(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);

    /* Get flags */
    uint64_t flags = 0;
    if (argc > 1) {
        flags = janet_getflags(argv, 1, "ep");
    }

    /* Get environment */
    char **envp = os_execute_env(argc, argv);

    /* Get arguments */
    JanetView exargs = janet_getindexed(argv, 0);
    if (exargs.len < 1) {
        janet_panic("expected at least 1 command line argument");
    }

    /* Result */
    int status = 0;

#ifdef JANET_WINDOWS

    JanetBuffer *buf = os_exec_escape(exargs);
    if (buf->count > 8191) {
        janet_panic("command line string too long (max 8191 characters)");
    }
    const char *path = (const char *) janet_unwrap_string(exargs.items[0]);
    char *cargv[2] = {(char *) buf->data, NULL};

    /* Use _spawn family of functions. */
    /* Windows docs say do this before any spawns. */
    _flushall();

    /* Use an empty env instead when envp is NULL to be consistent with other implementation. */
    char *empty_env[1] = {NULL};
    char **envp1 = (NULL == envp) ? empty_env : envp;

    if (janet_flag_at(flags, 1) && janet_flag_at(flags, 0)) {
        status = (int) _spawnvpe(_P_WAIT, path, cargv, envp1);
    } else if (janet_flag_at(flags, 1)) {
        status = (int) _spawnvp(_P_WAIT, path, cargv);
    } else if (janet_flag_at(flags, 0)) {
        status = (int) _spawnve(_P_WAIT, path, cargv, envp1);
    } else {
        status = (int) _spawnv(_P_WAIT, path, cargv);
    }
    os_execute_cleanup(envp, NULL);

    /* Check error */
    if (-1 == status) {
        janet_panicf("%p: %s", argv[0], strerror(errno));
    }

    return janet_wrap_integer(status);
#else

    const char **child_argv = janet_smalloc(sizeof(char *) * ((size_t) exargs.len + 1));
    for (int32_t i = 0; i < exargs.len; i++)
        child_argv[i] = janet_getcstring(exargs.items, i);
    child_argv[exargs.len] = NULL;
    /* Coerce to form that works for spawn. I'm fairly confident no implementation
     * of posix_spawn would modify the argv array passed in. */
    char *const *cargv = (char *const *)child_argv;

    /* Use posix_spawn to spawn new process */

    int use_environ = !janet_flag_at(flags, 0);

    if (use_environ) {
        janet_lock_environ();
    }

    pid_t pid;
    if (janet_flag_at(flags, 1)) {
        status = posix_spawnp(&pid,
                              child_argv[0], NULL, NULL, cargv,
                              use_environ ? environ : envp);
    } else {
        status = posix_spawn(&pid,
                             child_argv[0], NULL, NULL, cargv,
                             use_environ ? environ : envp);
    }

    if (use_environ) {
        janet_unlock_environ();
    }

    /* Wait for child */
    if (status) {
        os_execute_cleanup(envp, child_argv);
        janet_panicf("%p: %s", argv[0], strerror(errno));
    } else {
        waitpid(pid, &status, 0);
    }

    os_execute_cleanup(envp, child_argv);
    /* Use POSIX shell semantics for interpreting signals */
    int ret;
    if (WIFEXITED(status)) {
        ret = WEXITSTATUS(status);
    } else if (WIFSTOPPED(status)) {
        ret = WSTOPSIG(status) + 128;
    } else {
        ret = WTERMSIG(status) + 128;
    }
    return janet_wrap_integer(ret);
#endif
}

static Janet os_shell(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    const char *cmd = argc
                      ? janet_getcstring(argv, 0)
                      : NULL;
    int stat = system(cmd);
    return argc
           ? janet_wrap_integer(stat)
           : janet_wrap_boolean(stat);
}

#endif /* JANET_NO_PROCESSES */

static Janet os_environ(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    int32_t nenv = 0;
    janet_lock_environ();
    char **env = environ;
    while (*env++)
        nenv += 1;
    JanetTable *t = janet_table(nenv);
    for (int32_t i = 0; i < nenv; i++) {
        char *e = environ[i];
        char *eq = strchr(e, '=');
        if (!eq) {
            janet_unlock_environ();
            janet_panic("no '=' in environ");
        }
        char *v = eq + 1;
        int32_t full_len = (int32_t) strlen(e);
        int32_t val_len = (int32_t) strlen(v);
        janet_table_put(
            t,
            janet_stringv((const uint8_t *)e, full_len - val_len - 1),
            janet_stringv((const uint8_t *)v, val_len)
        );
    }
    janet_unlock_environ();
    return janet_wrap_table(t);
}

static Janet os_getenv(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const char *cstr = janet_getcstring(argv, 0);
    const char *res = getenv(cstr);
    janet_lock_environ();
    Janet ret = res
                ? janet_cstringv(res)
                : argc == 2
                ? argv[1]
                : janet_wrap_nil();
    janet_unlock_environ();
    return ret;
}

static Janet os_setenv(int32_t argc, Janet *argv) {
#ifdef JANET_WINDOWS
#define SETENV(K,V) _putenv_s(K, V)
#define UNSETENV(K) _putenv_s(K, "")
#else
#define SETENV(K,V) setenv(K, V, 1)
#define UNSETENV(K) unsetenv(K)
#endif
    janet_arity(argc, 1, 2);
    const char *ks = janet_getcstring(argv, 0);
    const char *vs = janet_optcstring(argv, argc, 1, NULL);
    janet_lock_environ();
    if (NULL == vs) {
        UNSETENV(ks);
    } else {
        SETENV(ks, vs);
    }
    janet_unlock_environ();
    return janet_wrap_nil();
}

static Janet os_time(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    double dtime = (double)(time(NULL));
    return janet_wrap_number(dtime);
}

static Janet os_clock(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    struct timespec tv;
    if (janet_gettime(&tv)) janet_panic("could not get time");
    double dtime = tv.tv_sec + (tv.tv_nsec / 1E9);
    return janet_wrap_number(dtime);
}

static Janet os_sleep(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    double delay = janet_getnumber(argv, 0);
    if (delay < 0) janet_panic("invalid argument to sleep");
#ifdef JANET_WINDOWS
    Sleep((DWORD)(delay * 1000));
#else
    int rc;
    struct timespec ts;
    ts.tv_sec = (time_t) delay;
    ts.tv_nsec = (delay <= UINT32_MAX)
                 ? (long)((delay - ((uint32_t)delay)) * 1000000000)
                 : 0;
    RETRY_EINTR(rc, nanosleep(&ts, &ts));
#endif
    return janet_wrap_nil();
}

static Janet os_cwd(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    char buf[FILENAME_MAX];
    char *ptr;
#ifdef JANET_WINDOWS
    ptr = _getcwd(buf, FILENAME_MAX);
#else
    ptr = getcwd(buf, FILENAME_MAX);
#endif
    if (NULL == ptr) janet_panic("could not get current directory");
    return janet_cstringv(ptr);
}

static Janet os_cryptorand(int32_t argc, Janet *argv) {
    JanetBuffer *buffer;
    const char *genericerr = "unable to get sufficient random data";
    janet_arity(argc, 1, 2);
    int32_t offset;
    int32_t n = janet_getinteger(argv, 0);
    if (n < 0) janet_panic("expected positive integer");
    if (argc == 2) {
        buffer = janet_getbuffer(argv, 1);
        offset = buffer->count;
    } else {
        offset = 0;
        buffer = janet_buffer(n);
    }
    /* We could optimize here by adding setcount_uninit */
    janet_buffer_setcount(buffer, offset + n);

#ifdef JANET_WINDOWS
    for (int32_t i = offset; i < buffer->count; i += sizeof(unsigned int)) {
        unsigned int v;
        if (rand_s(&v))
            janet_panic(genericerr);
        for (int32_t j = 0; (j < sizeof(unsigned int)) && (i + j < buffer->count); j++) {
            buffer->data[i + j] = v & 0xff;
            v = v >> 8;
        }
    }
#elif defined(JANET_LINUX) || ( defined(JANET_APPLE) && !defined(MAC_OS_X_VERSION_10_7) )
    /* We should be able to call getrandom on linux, but it doesn't seem
       to be uniformly supported on linux distros.
       On Mac, arc4random_buf wasn't available on until 10.7.
       In these cases, use this fallback path for now... */
    int rc;
    int randfd;
    RETRY_EINTR(randfd, open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (randfd < 0)
        janet_panic(genericerr);
    while (n > 0) {
        ssize_t nread;
        RETRY_EINTR(nread, read(randfd, buffer->data + offset, n));
        if (nread <= 0) {
            RETRY_EINTR(rc, close(randfd));
            janet_panic(genericerr);
        }
        offset += nread;
        n -= nread;
    }
    RETRY_EINTR(rc, close(randfd));
#elif defined(JANET_BSD) || defined(MAC_OS_X_VERSION_10_7)
    (void) genericerr;
    arc4random_buf(buffer->data + offset, n);
#else
    (void) genericerr;
    janet_panic("cryptorand currently unsupported on this platform");
#endif
    return janet_wrap_buffer(buffer);
}

static Janet os_date(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 2);
    (void) argv;
    time_t t;
    struct tm t_infos;
    struct tm *t_info = NULL;
    if (argc) {
        int64_t integer = janet_getinteger64(argv, 0);
        t = (time_t) integer;
    } else {
        time(&t);
    }
    if (argc >= 2 && janet_truthy(argv[1])) {
        /* local time */
#ifdef JANET_WINDOWS
        localtime_s(&t_infos, &t);
        t_info = &t_infos;
#else
        tzset();
        t_info = localtime_r(&t, &t_infos);
#endif
    } else {
        /* utc time */
#ifdef JANET_WINDOWS
        gmtime_s(&t_infos, &t);
        t_info = &t_infos;
#else
        t_info = gmtime_r(&t, &t_infos);
#endif
    }
    JanetKV *st = janet_struct_begin(9);
    janet_struct_put(st, janet_ckeywordv("seconds"), janet_wrap_number(t_info->tm_sec));
    janet_struct_put(st, janet_ckeywordv("minutes"), janet_wrap_number(t_info->tm_min));
    janet_struct_put(st, janet_ckeywordv("hours"), janet_wrap_number(t_info->tm_hour));
    janet_struct_put(st, janet_ckeywordv("month-day"), janet_wrap_number(t_info->tm_mday - 1));
    janet_struct_put(st, janet_ckeywordv("month"), janet_wrap_number(t_info->tm_mon));
    janet_struct_put(st, janet_ckeywordv("year"), janet_wrap_number(t_info->tm_year + 1900));
    janet_struct_put(st, janet_ckeywordv("week-day"), janet_wrap_number(t_info->tm_wday));
    janet_struct_put(st, janet_ckeywordv("year-day"), janet_wrap_number(t_info->tm_yday));
    janet_struct_put(st, janet_ckeywordv("dst"), janet_wrap_boolean(t_info->tm_isdst));
    return janet_wrap_struct(janet_struct_end(st));
}

static int entry_getdst(Janet env_entry) {
    Janet v;
    if (janet_checktype(env_entry, JANET_TABLE)) {
        JanetTable *entry = janet_unwrap_table(env_entry);
        v = janet_table_get(entry, janet_ckeywordv("dst"));
    } else if (janet_checktype(env_entry, JANET_STRUCT)) {
        const JanetKV *entry = janet_unwrap_struct(env_entry);
        v = janet_struct_get(entry, janet_ckeywordv("dst"));
    } else {
        v = janet_wrap_nil();
    }
    if (janet_checktype(v, JANET_NIL)) {
        return -1;
    } else {
        return janet_truthy(v);
    }
}

#ifdef JANET_WINDOWS
typedef int32_t timeint_t;
#else
typedef int64_t timeint_t;
#endif

static timeint_t entry_getint(Janet env_entry, char *field) {
    Janet i;
    if (janet_checktype(env_entry, JANET_TABLE)) {
        JanetTable *entry = janet_unwrap_table(env_entry);
        i = janet_table_get(entry, janet_ckeywordv(field));
    } else if (janet_checktype(env_entry, JANET_STRUCT)) {
        const JanetKV *entry = janet_unwrap_struct(env_entry);
        i = janet_struct_get(entry, janet_ckeywordv(field));
    } else {
        return 0;
    }

    if (janet_checktype(i, JANET_NIL)) {
        return 0;
    }

#ifdef JANET_WINDOWS
    if (!janet_checkint(i)) {
        janet_panicf("bad slot #%s, expected 32 bit signed integer, got %v",
                     field, i);
    }
#else
    if (!janet_checkint64(i)) {
        janet_panicf("bad slot #%s, expected 64 bit signed integer, got %v",
                     field, i);
    }
#endif

    return (timeint_t)janet_unwrap_number(i);
}

static Janet os_mktime(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    time_t t;
    struct tm t_info;

    /* Use memset instead of = {0} to silence paranoid warning in macos */
    memset(&t_info, 0, sizeof(t_info));

    if (!janet_checktype(argv[0], JANET_TABLE) &&
            !janet_checktype(argv[0], JANET_STRUCT))
        janet_panic_type(argv[0], 0, JANET_TFLAG_DICTIONARY);

    t_info.tm_sec = entry_getint(argv[0], "seconds");
    t_info.tm_min = entry_getint(argv[0], "minutes");
    t_info.tm_hour = entry_getint(argv[0], "hours");
    t_info.tm_mday = entry_getint(argv[0], "month-day") + 1;
    t_info.tm_mon = entry_getint(argv[0], "month");
    t_info.tm_year = entry_getint(argv[0], "year") - 1900;
    t_info.tm_isdst = entry_getdst(argv[0]);

    if (argc >= 2 && janet_truthy(argv[1])) {
        /* local time */
        t = mktime(&t_info);
    } else {
        /* utc time */
#ifdef JANET_NO_UTC_MKTIME
        janet_panic("os/mktime UTC not supported on this platform");
        return janet_wrap_nil();
#else
        t = timegm(&t_info);
#endif
    }

    if (t == (time_t) -1) {
        janet_panicf("%s", strerror(errno));
    }

    return janet_wrap_number((double)t);
}

#ifdef JANET_NO_SYMLINKS
#define j_symlink link
#else
#define j_symlink symlink
#endif

static Janet os_link(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
#ifdef JANET_WINDOWS
    (void) argc;
    (void) argv;
    janet_panic("os/link not supported on Windows");
    return janet_wrap_nil();
#else
    const char *oldpath = janet_getcstring(argv, 0);
    const char *newpath = janet_getcstring(argv, 1);
    int res = ((argc == 3 && janet_truthy(argv[2])) ? j_symlink : link)(oldpath, newpath);
    if (-1 == res) janet_panicf("%s: %s -> %s", strerror(errno), oldpath, newpath);
    return janet_wrap_nil();
#endif
}

static Janet os_symlink(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
#ifdef JANET_WINDOWS
    (void) argc;
    (void) argv;
    janet_panic("os/symlink not supported on Windows");
    return janet_wrap_nil();
#else
    const char *oldpath = janet_getcstring(argv, 0);
    const char *newpath = janet_getcstring(argv, 1);
    int res = j_symlink(oldpath, newpath);
    if (-1 == res) janet_panicf("%s: %s -> %s", strerror(errno), oldpath, newpath);
    return janet_wrap_nil();
#endif
}

#undef j_symlink

static Janet os_mkdir(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _mkdir(path);
#else
    int res = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
    if (res == 0) return janet_wrap_true();
    if (errno == EEXIST) return janet_wrap_false();
    janet_panicf("%s: %s", strerror(errno), path);
}

static Janet os_rmdir(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _rmdir(path);
#else
    int res = rmdir(path);
#endif
    if (-1 == res) janet_panicf("%s: %s", strerror(errno), path);
    return janet_wrap_nil();
}

static Janet os_cd(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _chdir(path);
#else
    int res = chdir(path);
#endif
    if (-1 == res) janet_panicf("%s: %s", strerror(errno), path);
    return janet_wrap_nil();
}

static Janet os_touch(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    const char *path = janet_getcstring(argv, 0);
    struct utimbuf timebuf, *bufp;
    if (argc >= 2) {
        bufp = &timebuf;
        timebuf.actime = (time_t) janet_getnumber(argv, 1);
        if (argc >= 3) {
            timebuf.modtime = (time_t) janet_getnumber(argv, 2);
        } else {
            timebuf.modtime = timebuf.actime;
        }
    } else {
        bufp = NULL;
    }
    int res = utime(path, bufp);
    if (-1 == res) janet_panic(strerror(errno));
    return janet_wrap_nil();
}

static Janet os_remove(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
    int status = remove(path);
    if (-1 == status) janet_panicf("%s: %s", strerror(errno), path);
    return janet_wrap_nil();
}

#ifndef JANET_NO_SYMLINKS
static Janet os_readlink(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
#ifdef JANET_WINDOWS
    (void) argc;
    (void) argv;
    janet_panic("os/readlink not supported on Windows");
    return janet_wrap_nil();
#else
    static char buffer[PATH_MAX];
    const char *path = janet_getcstring(argv, 0);
    ssize_t len = readlink(path, buffer, sizeof buffer);
    if (len < 0 || (size_t)len >= sizeof buffer)
        janet_panicf("%s: %s", strerror(errno), path);
    return janet_stringv((const uint8_t *)buffer, len);
#endif
}
#endif

#ifdef JANET_WINDOWS

typedef struct _stat jstat_t;
typedef unsigned short jmode_t;

static int32_t janet_perm_to_unix(unsigned short m) {
    int32_t ret = 0;
    if (m & S_IEXEC) ret |= 0111;
    if (m & S_IWRITE) ret |= 0222;
    if (m & S_IREAD) ret |= 0444;
    return ret;
}

static unsigned short janet_perm_from_unix(int32_t x) {
    unsigned short m = 0;
    if (x & 111) m |= S_IEXEC;
    if (x & 222) m |= S_IWRITE;
    if (x & 444) m |= S_IREAD;
    return m;
}

static const uint8_t *janet_decode_mode(unsigned short m) {
    const char *str = "other";
    if (m & _S_IFREG) str = "file";
    else if (m & _S_IFDIR) str = "directory";
    else if (m & _S_IFCHR) str = "character";
    return janet_ckeyword(str);
}

static int32_t janet_decode_permissions(jmode_t mode) {
    return (int32_t)(mode & (S_IEXEC | S_IWRITE | S_IREAD));
}

#else

typedef struct stat jstat_t;
typedef mode_t jmode_t;

static int32_t janet_perm_to_unix(mode_t m) {
    return (int32_t) m;
}

static mode_t janet_perm_from_unix(int32_t x) {
    return (mode_t) x;
}

static const uint8_t *janet_decode_mode(mode_t m) {
    const char *str = "other";
    if (S_ISREG(m)) str = "file";
    else if (S_ISDIR(m)) str = "directory";
    else if (S_ISFIFO(m)) str = "fifo";
    else if (S_ISBLK(m)) str = "block";
    else if (S_ISSOCK(m)) str = "socket";
    else if (S_ISLNK(m)) str = "link";
    else if (S_ISCHR(m)) str = "character";
    return janet_ckeyword(str);
}

static int32_t janet_decode_permissions(jmode_t mode) {
    return (int32_t)(mode & 0777);
}

#endif

static int32_t os_parse_permstring(const uint8_t *perm) {
    int32_t m = 0;
    if (perm[0] == 'r') m |= 0400;
    if (perm[1] == 'w') m |= 0200;
    if (perm[2] == 'x') m |= 0100;
    if (perm[3] == 'r') m |= 0040;
    if (perm[4] == 'w') m |= 0020;
    if (perm[5] == 'x') m |= 0010;
    if (perm[6] == 'r') m |= 0004;
    if (perm[7] == 'w') m |= 0002;
    if (perm[8] == 'x') m |= 0001;
    return m;
}

static Janet os_make_permstring(int32_t permissions) {
    uint8_t bytes[9] = {0};
    bytes[0] = (permissions & 0400) ? 'r' : '-';
    bytes[1] = (permissions & 0200) ? 'w' : '-';
    bytes[2] = (permissions & 0100) ? 'x' : '-';
    bytes[3] = (permissions & 0040) ? 'r' : '-';
    bytes[4] = (permissions & 0020) ? 'w' : '-';
    bytes[5] = (permissions & 0010) ? 'x' : '-';
    bytes[6] = (permissions & 0004) ? 'r' : '-';
    bytes[7] = (permissions & 0002) ? 'w' : '-';
    bytes[8] = (permissions & 0001) ? 'x' : '-';
    return janet_stringv(bytes, sizeof(bytes));
}

static int32_t os_get_unix_mode(const Janet *argv, int32_t n) {
    int32_t unix_mode;
    if (janet_checkint(argv[n])) {
        /* Integer mode */
        int32_t x = janet_unwrap_integer(argv[n]);
        if (x < 0 || x > 0777) {
            janet_panicf("bad slot #%d, expected integer in range [0, 8r777], got %v", n, argv[n]);
        }
        unix_mode = x;
    } else {
        /* Bytes mode */
        JanetByteView bytes = janet_getbytes(argv, n);
        if (bytes.len != 9) {
            janet_panicf("bad slot #%d: expected byte sequence of length 9, got %v", n, argv[n]);
        }
        unix_mode = os_parse_permstring(bytes.bytes);
    }
    return unix_mode;
}

static jmode_t os_getmode(const Janet *argv, int32_t n) {
    return janet_perm_from_unix(os_get_unix_mode(argv, n));
}

/* Getters */
static Janet os_stat_dev(jstat_t *st) {
    return janet_wrap_number(st->st_dev);
}
static Janet os_stat_inode(jstat_t *st) {
    return janet_wrap_number(st->st_ino);
}
static Janet os_stat_mode(jstat_t *st) {
    return janet_wrap_keyword(janet_decode_mode(st->st_mode));
}
static Janet os_stat_int_permissions(jstat_t *st) {
    return janet_wrap_integer(janet_perm_to_unix(janet_decode_permissions(st->st_mode)));
}
static Janet os_stat_permissions(jstat_t *st) {
    return os_make_permstring(janet_perm_to_unix(janet_decode_permissions(st->st_mode)));
}
static Janet os_stat_uid(jstat_t *st) {
    return janet_wrap_number(st->st_uid);
}
static Janet os_stat_gid(jstat_t *st) {
    return janet_wrap_number(st->st_gid);
}
static Janet os_stat_nlink(jstat_t *st) {
    return janet_wrap_number(st->st_nlink);
}
static Janet os_stat_rdev(jstat_t *st) {
    return janet_wrap_number(st->st_rdev);
}
static Janet os_stat_size(jstat_t *st) {
    return janet_wrap_number(st->st_size);
}
static Janet os_stat_accessed(jstat_t *st) {
    return janet_wrap_number((double) st->st_atime);
}
static Janet os_stat_modified(jstat_t *st) {
    return janet_wrap_number((double) st->st_mtime);
}
static Janet os_stat_changed(jstat_t *st) {
    return janet_wrap_number((double) st->st_ctime);
}
#ifdef JANET_WINDOWS
static Janet os_stat_blocks(jstat_t *st) {
    return janet_wrap_number(0);
}
static Janet os_stat_blocksize(jstat_t *st) {
    return janet_wrap_number(0);
}
#else
static Janet os_stat_blocks(jstat_t *st) {
    return janet_wrap_number(st->st_blocks);
}
static Janet os_stat_blocksize(jstat_t *st) {
    return janet_wrap_number(st->st_blksize);
}
#endif

struct OsStatGetter {
    const char *name;
    Janet(*fn)(jstat_t *st);
};

static const struct OsStatGetter os_stat_getters[] = {
    {"dev", os_stat_dev},
    {"inode", os_stat_inode},
    {"mode", os_stat_mode},
    {"int-permissions", os_stat_int_permissions},
    {"permissions", os_stat_permissions},
    {"uid", os_stat_uid},
    {"gid", os_stat_gid},
    {"nlink", os_stat_nlink},
    {"rdev", os_stat_rdev},
    {"size", os_stat_size},
    {"blocks", os_stat_blocks},
    {"blocksize", os_stat_blocksize},
    {"accessed", os_stat_accessed},
    {"modified", os_stat_modified},
    {"changed", os_stat_changed},
    {NULL, NULL}
};

static Janet os_stat_or_lstat(int do_lstat, int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const char *path = janet_getcstring(argv, 0);
    JanetTable *tab = NULL;
    int getall = 1;
    const uint8_t *key;
    if (argc == 2) {
        if (janet_checktype(argv[1], JANET_KEYWORD)) {
            getall = 0;
            key = janet_getkeyword(argv, 1);
        } else {
            tab = janet_gettable(argv, 1);
        }
    } else {
        tab = janet_table(0);
    }

    /* Build result */
    jstat_t st;
#ifdef JANET_WINDOWS
    (void) do_lstat;
    int res = _stat(path, &st);
#else
    int res;
    if (do_lstat) {
        res = lstat(path, &st);
    } else {
        res = stat(path, &st);
    }
#endif
    if (-1 == res) {
        return janet_wrap_nil();
    }

    if (getall) {
        /* Put results in table */
        for (const struct OsStatGetter *sg = os_stat_getters; sg->name != NULL; sg++) {
            janet_table_put(tab, janet_ckeywordv(sg->name), sg->fn(&st));
        }
        return janet_wrap_table(tab);
    } else {
        /* Get one result */
        for (const struct OsStatGetter *sg = os_stat_getters; sg->name != NULL; sg++) {
            if (janet_cstrcmp(key, sg->name)) continue;
            return sg->fn(&st);
        }
        janet_panicf("unexpected keyword %v", janet_wrap_keyword(key));
        return janet_wrap_nil();
    }
}

static Janet os_stat(int32_t argc, Janet *argv) {
    return os_stat_or_lstat(0, argc, argv);
}

static Janet os_lstat(int32_t argc, Janet *argv) {
    return os_stat_or_lstat(1, argc, argv);
}

static Janet os_chmod(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _chmod(path, os_getmode(argv, 1));
#else
    int res = chmod(path, os_getmode(argv, 1));
#endif
    if (-1 == res) janet_panicf("%s: %s", strerror(errno), path);
    return janet_wrap_nil();
}

#ifndef JANET_NO_UMASK
static Janet os_umask(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    int mask = (int) os_getmode(argv, 0);
#ifdef JANET_WINDOWS
    int res = _umask(mask);
#else
    int res = umask(mask);
#endif
    return janet_wrap_integer(janet_perm_to_unix(res));
}
#endif

static Janet os_dir(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const char *dir = janet_getcstring(argv, 0);
    JanetArray *paths = (argc == 2) ? janet_getarray(argv, 1) : janet_array(0);
#ifdef JANET_WINDOWS
    /* Read directory items with FindFirstFile / FindNextFile / FindClose */
    struct _finddata_t afile;
    char pattern[MAX_PATH + 1];
    if (strlen(dir) > (sizeof(pattern) - 3))
        janet_panicf("path too long: %s", dir);
    sprintf(pattern, "%s/*", dir);
    intptr_t res = _findfirst(pattern, &afile);
    if (-1 == res) janet_panicv(janet_cstringv(strerror(errno)));
    do {
        if (strcmp(".", afile.name) && strcmp("..", afile.name)) {
            janet_array_push(paths, janet_cstringv(afile.name));
        }
    } while (_findnext(res, &afile) != -1);
    _findclose(res);
#else
    /* Read directory items with opendir / readdir / closedir */
    struct dirent *dp;
    DIR *dfd = opendir(dir);
    if (dfd == NULL) janet_panicf("cannot open directory %s", dir);
    while ((dp = readdir(dfd)) != NULL) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
            continue;
        }
        janet_array_push(paths, janet_cstringv(dp->d_name));
    }
    closedir(dfd);
#endif
    return janet_wrap_array(paths);
}

static Janet os_rename(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    const char *src = janet_getcstring(argv, 0);
    const char *dest = janet_getcstring(argv, 1);
    int status = rename(src, dest);
    if (status) {
        janet_panic(strerror(errno));
    }
    return janet_wrap_nil();
}

static Janet os_realpath(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *src = janet_getcstring(argv, 0);
#ifdef JANET_NO_REALPATH
    janet_panic("os/realpath not enabled for this platform");
#else
#ifdef JANET_WINDOWS
    char *dest = _fullpath(NULL, src, _MAX_PATH);
#else
    char *dest = realpath(src, NULL);
#endif
    if (NULL == dest) janet_panicf("%s: %s", strerror(errno), src);
    Janet ret = janet_cstringv(dest);
    free(dest);
    return ret;
#endif
}

static Janet os_permission_string(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return os_make_permstring(os_get_unix_mode(argv, 0));
}

static Janet os_permission_int(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    return janet_wrap_integer(os_get_unix_mode(argv, 0));
}

#endif /* JANET_REDUCED_OS */

static const JanetReg os_cfuns[] = {
    {
        "os/exit", os_exit,
        JDOC("(os/exit &opt x)\n\n"
             "Exit from janet with an exit code equal to x. If x is not an integer, "
             "the exit with status equal the hash of x.")
    },
    {
        "os/which", os_which,
        JDOC("(os/which)\n\n"
             "Check the current operating system. Returns one of:\n\n"
             "\t:windows\n"
             "\t:macos\n"
             "\t:web - Web assembly (emscripten)\n"
             "\t:linux\n"
             "\t:freebsd\n"
             "\t:openbsd\n"
             "\t:netbsd\n"
             "\t:posix - A POSIX compatible system (default)\n\n"
             "May also return a custom keyword specified at build time.")
    },
    {
        "os/arch", os_arch,
        JDOC("(os/arch)\n\n"
             "Check the ISA that janet was compiled for. Returns one of:\n\n"
             "\t:x86\n"
             "\t:x86-64\n"
             "\t:arm\n"
             "\t:aarch64\n"
             "\t:sparc\n"
             "\t:wasm\n"
             "\t:unknown\n")
    },
#ifndef JANET_REDUCED_OS
    {
        "os/environ", os_environ,
        JDOC("(os/environ)\n\n"
             "Get a copy of the os environment table.")
    },
    {
        "os/getenv", os_getenv,
        JDOC("(os/getenv variable &opt dflt)\n\n"
             "Get the string value of an environment variable.")
    },
    {
        "os/dir", os_dir,
        JDOC("(os/dir dir &opt array)\n\n"
             "Iterate over files and subdirectories in a directory. Returns an array of paths parts, "
             "with only the file name or directory name and no prefix.")
    },
    {
        "os/stat", os_stat,
        JDOC("(os/stat path &opt tab|key)\n\n"
             "Gets information about a file or directory. Returns a table If the third argument is a keyword, returns "
             " only that information from stat. If the file or directory does not exist, returns nil. The keys are\n\n"
             "\t:dev - the device that the file is on\n"
             "\t:mode - the type of file, one of :file, :directory, :block, :character, :fifo, :socket, :link, or :other\n"
             "\t:int-permissions - A Unix permission integer like 8r744\n"
             "\t:permissions - A Unix permission string like \"rwxr--r--\"\n"
             "\t:uid - File uid\n"
             "\t:gid - File gid\n"
             "\t:nlink - number of links to file\n"
             "\t:rdev - Real device of file. 0 on windows.\n"
             "\t:size - size of file in bytes\n"
             "\t:blocks - number of blocks in file. 0 on windows\n"
             "\t:blocksize - size of blocks in file. 0 on windows\n"
             "\t:accessed - timestamp when file last accessed\n"
             "\t:changed - timestamp when file last changed (permissions changed)\n"
             "\t:modified - timestamp when file last modified (content changed)\n")
    },
    {
        "os/lstat", os_lstat,
        JDOC("(os/lstat path &opt tab|key)\n\n"
             "Like os/stat, but don't follow symlinks.\n")
    },
    {
        "os/chmod", os_chmod,
        JDOC("(os/chmod path mode)\n\n"
             "Change file permissions, where mode is a permission string as returned by "
             "os/perm-string, or an integer as returned by os/perm-int. "
             "When mode is an integer, it is interpreted as a Unix permission value, best specified in octal, like "
             "8r666 or 8r400. Windows will not differentiate between user, group, and other permissions, and thus will combine all of these permissions. Returns nil.")
    },
    {
        "os/touch", os_touch,
        JDOC("(os/touch path &opt actime modtime)\n\n"
             "Update the access time and modification times for a file. By default, sets "
             "times to the current time.")
    },
    {
        "os/cd", os_cd,
        JDOC("(os/cd path)\n\n"
             "Change current directory to path. Returns nil on success, errors on failure.")
    },
#ifndef JANET_NO_UMASK
    {
        "os/umask", os_umask,
        JDOC("(os/umask mask)\n\n"
             "Set a new umask, returns the old umask.")
    },
#endif
    {
        "os/mkdir", os_mkdir,
        JDOC("(os/mkdir path)\n\n"
             "Create a new directory. The path will be relative to the current directory if relative, otherwise "
             "it will be an absolute path. Returns true if the directory was created, false if the directory already exists, and "
             "errors otherwise.")
    },
    {
        "os/rmdir", os_rmdir,
        JDOC("(os/rmdir path)\n\n"
             "Delete a directory. The directory must be empty to succeed.")
    },
    {
        "os/rm", os_remove,
        JDOC("(os/rm path)\n\n"
             "Delete a file. Returns nil.")
    },
    {
        "os/link", os_link,
        JDOC("(os/link oldpath newpath &opt symlink)\n\n"
             "Create a link at newpath that points to oldpath and returns nil. "
             "Iff symlink is truthy, creates a symlink. "
             "Iff symlink is falsey or not provided, "
             "creates a hard link. Does not work on Windows.")
    },
#ifndef JANET_NO_SYMLINKS
    {
        "os/symlink", os_symlink,
        JDOC("(os/symlink oldpath newpath)\n\n"
             "Create a symlink from oldpath to newpath, returning nil. Same as (os/link oldpath newpath true).")
    },
    {
        "os/readlink", os_readlink,
        JDOC("(os/readlink path)\n\n"
             "Read the contents of a symbolic link. Does not work on Windows.\n")
    },
#endif
#ifndef JANET_NO_PROCESSES
    {
        "os/execute", os_execute,
        JDOC("(os/execute args &opts flags env)\n\n"
             "Execute a program on the system and pass it string arguments. Flags "
             "is a keyword that modifies how the program will execute.\n\n"
             "\t:e - enables passing an environment to the program. Without :e, the "
             "current environment is inherited.\n"
             "\t:p - allows searching the current PATH for the binary to execute. "
             "Without this flag, binaries must use absolute paths.\n\n"
             "env is a table or struct mapping environment variables to values. "
             "Returns the exit status of the program.")
    },
    {
        "os/shell", os_shell,
        JDOC("(os/shell str)\n\n"
             "Pass a command string str directly to the system shell.")
    },
#endif
    {
        "os/setenv", os_setenv,
        JDOC("(os/setenv variable value)\n\n"
             "Set an environment variable.")
    },
    {
        "os/time", os_time,
        JDOC("(os/time)\n\n"
             "Get the current time expressed as the number of seconds since "
             "January 1, 1970, the Unix epoch. Returns a real number.")
    },
    {
        "os/mktime", os_mktime,
        JDOC("(os/mktime date-struct &opt local)\n\n"
             "Get the broken down date-struct time expressed as the number "
             " of seconds since January 1, 1970, the Unix epoch. "
             "Returns a real number. "
             "Date is given in UTC unless local is truthy, in which case the "
             "date is computed for the local timezone.\n\n"
             "Inverse function to os/date.")
    },
    {
        "os/clock", os_clock,
        JDOC("(os/clock)\n\n"
             "Return the number of seconds since some fixed point in time. The clock "
             "is guaranteed to be non decreasing in real time.")
    },
    {
        "os/sleep", os_sleep,
        JDOC("(os/sleep nsec)\n\n"
             "Suspend the program for nsec seconds. 'nsec' can be a real number. Returns "
             "nil.")
    },
    {
        "os/cwd", os_cwd,
        JDOC("(os/cwd)\n\n"
             "Returns the current working directory.")
    },
    {
        "os/cryptorand", os_cryptorand,
        JDOC("(os/cryptorand n &opt buf)\n\n"
             "Get or append n bytes of good quality random data provided by the OS. Returns a new buffer or buf.")
    },
    {
        "os/date", os_date,
        JDOC("(os/date &opt time local)\n\n"
             "Returns the given time as a date struct, or the current time if no time is given. "
             "Returns a struct with following key values. Note that all numbers are 0-indexed. "
             "Date is given in UTC unless local is truthy, in which case the date is formatted for "
             "the local timezone.\n\n"
             "\t:seconds - number of seconds [0-61]\n"
             "\t:minutes - number of minutes [0-59]\n"
             "\t:hours - number of hours [0-23]\n"
             "\t:month-day - day of month [0-30]\n"
             "\t:month - month of year [0, 11]\n"
             "\t:year - years since year 0 (e.g. 2019)\n"
             "\t:week-day - day of the week [0-6]\n"
             "\t:year-day - day of the year [0-365]\n"
             "\t:dst - If Day Light Savings is in effect")
    },
    {
        "os/rename", os_rename,
        JDOC("(os/rename oldname newname)\n\n"
             "Rename a file on disk to a new path. Returns nil.")
    },
    {
        "os/realpath", os_realpath,
        JDOC("(os/realpath path)\n\n"
             "Get the absolute path for a given path, following ../, ./, and symlinks. "
             "Returns an absolute path as a string. Will raise an error on Windows.")
    },
    {
        "os/perm-string", os_permission_string,
        JDOC("(os/perm-string int)\n\n"
             "Convert a Unix octal permission value from a permission integer as returned by os/stat "
             "to a human readable string, that follows the formatting "
             "of unix tools like ls. Returns the string as a 9 character string of r, w, x and - characters. Does not "
             "include the file/directory/symlink character as rendered by `ls`.")
    },
    {
        "os/perm-int", os_permission_int,
        JDOC("(os/perm-int bytes)\n\n"
             "Parse a 9 character permission string and return an integer that can be used by chmod.")
    },
#endif
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_os(JanetTable *env) {
#if !defined(JANET_REDUCED_OS) && defined(JANET_WINDOWS) && defined(JANET_THREADS)
    /* During start up, the top-most abstract machine (thread)
     * in the thread tree sets up the critical section. */
    if (!env_lock_initialized) {
        InitializeCriticalSection(&env_lock);
        env_lock_initialized = 1;
    }
#endif
    janet_core_cfuns(env, NULL, os_cfuns);
}

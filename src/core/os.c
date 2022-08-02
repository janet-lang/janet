/*
* Copyright (c) 2022 Calvin Rose and contributors.
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
#include "gc.h"
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
#include <signal.h>

#ifdef JANET_BSD
#include <sys/sysctl.h>
#endif

#ifdef JANET_LINUX
#include <sched.h>
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
#ifdef JANET_APPLE
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif
#ifdef JANET_THREADS
#include <pthread.h>
#endif
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

JANET_CORE_FN(os_which,
              "(os/which)",
              "Check the current operating system. Returns one of:\n\n"
              "* :windows\n\n"
              "* :macos\n\n"
              "* :web - Web assembly (emscripten)\n\n"
              "* :linux\n\n"
              "* :freebsd\n\n"
              "* :openbsd\n\n"
              "* :netbsd\n\n"
              "* :posix - A POSIX compatible system (default)\n\n"
              "May also return a custom keyword specified at build time.") {
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
JANET_CORE_FN(os_arch,
              "(os/arch)",
              "Check the ISA that janet was compiled for. Returns one of:\n\n"
              "* :x86\n\n"
              "* :x64\n\n"
              "* :arm\n\n"
              "* :aarch64\n\n"
              "* :sparc\n\n"
              "* :wasm\n\n"
              "* :unknown\n") {
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
#elif (defined(__ppc64__) || defined(_ARCH_PPC64) || defined(_M_PPC))
    return janet_ckeywordv("ppc64");
#else
    return janet_ckeywordv("unknown");
#endif
}

#undef janet_stringify1
#undef janet_stringify

JANET_CORE_FN(os_exit,
              "(os/exit &opt x)",
              "Exit from janet with an exit code equal to x. If x is not an integer, "
              "the exit with status equal the hash of x.") {
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

JANET_CORE_FN(os_cpu_count,
              "(os/cpu-count &opt dflt)",
              "Get an approximate number of CPUs available on for this process to use. If "
              "unable to get an approximation, will return a default value dflt.") {
    janet_arity(argc, 0, 1);
    Janet dflt = argc > 0 ? argv[0] : janet_wrap_nil();
#ifdef JANET_WINDOWS
    (void) dflt;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return janet_wrap_integer(info.dwNumberOfProcessors);
#elif defined(JANET_LINUX)
    (void) dflt;
    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);
    int count = CPU_COUNT(&cs);
    return janet_wrap_integer(count);
#elif defined(JANET_BSD) && defined(HW_NCPUONLINE)
    (void) dflt;
    const int name[2] = {CTL_HW, HW_NCPUONLINE};
    int result = 0;
    size_t len = sizeof(int);
    if (-1 == sysctl(name, 2, &result, &len, NULL, 0)) {
        return dflt;
    }
    return janet_wrap_integer(result);
#elif defined(JANET_BSD) && defined(HW_NCPU)
    (void) dflt;
    const int name[2] = {CTL_HW, HW_NCPU};
    int result = 0;
    size_t len = sizeof(int);
    if (-1 == sysctl(name, 2, &result, &len, NULL, 0)) {
        return dflt;
    }
    return janet_wrap_integer(result);
#else
    return dflt;
#endif
}

#ifndef JANET_REDUCED_OS

#ifndef JANET_NO_PROCESSES

/* Get env for os_execute */
#ifdef JANET_WINDOWS
typedef char *EnvBlock;
#else
typedef char **EnvBlock;
#endif

/* Get env for os_execute */
static EnvBlock os_execute_env(int32_t argc, const Janet *argv) {
    if (argc <= 2) return NULL;
    JanetDictView dict = janet_getdictionary(argv, 2);
#ifdef JANET_WINDOWS
    JanetBuffer *temp = janet_buffer(10);
    for (int32_t i = 0; i < dict.cap; i++) {
        const JanetKV *kv = dict.kvs + i;
        if (!janet_checktype(kv->key, JANET_STRING)) continue;
        if (!janet_checktype(kv->value, JANET_STRING)) continue;
        const uint8_t *keys = janet_unwrap_string(kv->key);
        const uint8_t *vals = janet_unwrap_string(kv->value);
        janet_buffer_push_bytes(temp, keys, janet_string_length(keys));
        janet_buffer_push_u8(temp, '=');
        janet_buffer_push_bytes(temp, vals, janet_string_length(vals));
        janet_buffer_push_u8(temp, '\0');
    }
    janet_buffer_push_u8(temp, '\0');
    char *ret = janet_smalloc(temp->count);
    memcpy(ret, temp->data, temp->count);
    return ret;
#else
    char **envp = janet_smalloc(sizeof(char *) * ((size_t)dict.len + 1));
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
    return envp;
#endif
}

static void os_execute_cleanup(EnvBlock envp, const char **child_argv) {
#ifdef JANET_WINDOWS
    (void) child_argv;
    if (NULL != envp) janet_sfree(envp);
#else
    janet_sfree((void *)child_argv);
    if (NULL != envp) {
        char **envitem = envp;
        while (*envitem != NULL) {
            janet_sfree(*envitem);
            envitem++;
        }
    }
    janet_sfree(envp);
#endif
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

/* Process type for when running a subprocess and not immediately waiting */
static const JanetAbstractType ProcAT;
#define JANET_PROC_CLOSED 1
#define JANET_PROC_WAITED 2
#define JANET_PROC_WAITING 4
#define JANET_PROC_ERROR_NONZERO 8
#define JANET_PROC_OWNS_STDIN 16
#define JANET_PROC_OWNS_STDOUT 32
#define JANET_PROC_OWNS_STDERR 64
#define JANET_PROC_ALLOW_ZOMBIE 128
typedef struct {
    int flags;
#ifdef JANET_WINDOWS
    HANDLE pHandle;
    HANDLE tHandle;
#else
    pid_t pid;
#endif
    int return_code;
#ifdef JANET_EV
    JanetStream *in;
    JanetStream *out;
    JanetStream *err;
#else
    JanetFile *in;
    JanetFile *out;
    JanetFile *err;
#endif
} JanetProc;

#ifdef JANET_EV

#ifdef JANET_WINDOWS

static JanetEVGenericMessage janet_proc_wait_subr(JanetEVGenericMessage args) {
    JanetProc *proc = (JanetProc *) args.argp;
    WaitForSingleObject(proc->pHandle, INFINITE);
    GetExitCodeProcess(proc->pHandle, &args.tag);
    return args;
}

#else /* windows check */

static int proc_get_status(JanetProc *proc) {
    /* Use POSIX shell semantics for interpreting signals */
    int status = 0;
    pid_t result;
    do {
        result = waitpid(proc->pid, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    } else if (WIFSTOPPED(status)) {
        status = WSTOPSIG(status) + 128;
    } else {
        status = WTERMSIG(status) + 128;
    }
    return status;
}

/* Function that is called in separate thread to wait on a pid */
static JanetEVGenericMessage janet_proc_wait_subr(JanetEVGenericMessage args) {
    JanetProc *proc = (JanetProc *) args.argp;
#ifdef WNOWAIT
    pid_t result;
    int status = 0;
    do {
        result = waitpid(proc->pid, &status, WNOWAIT);
    } while (result == -1 && errno == EINTR);
#else
    args.tag = proc_get_status(proc);
#endif
    return args;
}

#endif /* End windows check */

/* Callback that is called in main thread when subroutine completes. */
static void janet_proc_wait_cb(JanetEVGenericMessage args) {
    janet_ev_dec_refcount();
    JanetProc *proc = (JanetProc *) args.argp;
    if (NULL != proc) {
#ifdef WNOWAIT
        int status = proc_get_status(proc);
#else
        int status = args.tag;
#endif
        proc->return_code = (int32_t) status;
        proc->flags |= JANET_PROC_WAITED;
        proc->flags &= ~JANET_PROC_WAITING;
        janet_gcunroot(janet_wrap_abstract(proc));
        janet_gcunroot(janet_wrap_fiber(args.fiber));
        if ((status != 0) && (proc->flags & JANET_PROC_ERROR_NONZERO)) {
            JanetString s = janet_formatc("command failed with non-zero exit code %d", status);
            janet_cancel(args.fiber, janet_wrap_string(s));
        } else {
            janet_schedule(args.fiber, janet_wrap_integer(status));
        }
    }
}

#endif /* End ev check */

static int janet_proc_gc(void *p, size_t s) {
    (void) s;
    JanetProc *proc = (JanetProc *) p;
#ifdef JANET_WINDOWS
    if (!(proc->flags & JANET_PROC_CLOSED)) {
        if (!(proc->flags & JANET_PROC_ALLOW_ZOMBIE)) {
            TerminateProcess(proc->pHandle, 1);
        }
        CloseHandle(proc->pHandle);
        CloseHandle(proc->tHandle);
    }
#else
    if (!(proc->flags & (JANET_PROC_WAITED | JANET_PROC_ALLOW_ZOMBIE))) {
        /* Kill and wait to prevent zombies */
        kill(proc->pid, SIGKILL);
        int status;
        if (!(proc->flags & JANET_PROC_WAITING)) {
            waitpid(proc->pid, &status, 0);
        }
    }
#endif
    return 0;
}

static int janet_proc_mark(void *p, size_t s) {
    (void) s;
    JanetProc *proc = (JanetProc *)p;
    if (NULL != proc->in) janet_mark(janet_wrap_abstract(proc->in));
    if (NULL != proc->out) janet_mark(janet_wrap_abstract(proc->out));
    if (NULL != proc->err) janet_mark(janet_wrap_abstract(proc->err));
    return 0;
}

#ifdef JANET_EV
static JANET_NO_RETURN void
#else
static Janet
#endif
os_proc_wait_impl(JanetProc *proc) {
    if (proc->flags & (JANET_PROC_WAITED | JANET_PROC_WAITING)) {
        janet_panicf("cannot wait twice on a process");
    }
#ifdef JANET_EV
    /* Event loop implementation - threaded call */
    proc->flags |= JANET_PROC_WAITING;
    JanetEVGenericMessage targs;
    memset(&targs, 0, sizeof(targs));
    targs.argp = proc;
    targs.fiber = janet_root_fiber();
    janet_gcroot(janet_wrap_abstract(proc));
    janet_gcroot(janet_wrap_fiber(targs.fiber));
    janet_ev_threaded_call(janet_proc_wait_subr, targs, janet_proc_wait_cb);
    janet_await();
#else
    /* Non evented implementation */
    proc->flags |= JANET_PROC_WAITED;
    int status = 0;
#ifdef JANET_WINDOWS
    WaitForSingleObject(proc->pHandle, INFINITE);
    GetExitCodeProcess(proc->pHandle, &status);
    if (!(proc->flags & JANET_PROC_CLOSED)) {
        proc->flags |= JANET_PROC_CLOSED;
        CloseHandle(proc->pHandle);
        CloseHandle(proc->tHandle);
    }
#else
    waitpid(proc->pid, &status, 0);
#endif
    proc->return_code = (int32_t) status;
    return janet_wrap_integer(proc->return_code);
#endif
}

JANET_CORE_FN(os_proc_wait,
              "(os/proc-wait proc)",
              "Block until the subprocess completes. Returns the subprocess return code.") {
    janet_fixarity(argc, 1);
    JanetProc *proc = janet_getabstract(argv, 0, &ProcAT);
#ifdef JANET_EV
    os_proc_wait_impl(proc);
    return janet_wrap_nil();
#else
    return os_proc_wait_impl(proc);
#endif
}

JANET_CORE_FN(os_proc_kill,
              "(os/proc-kill proc &opt wait)",
              "Kill a subprocess by sending SIGKILL to it on posix systems, or by closing the process "
              "handle on windows. If `wait` is truthy, will wait for the process to finish and "
              "returns the exit code. Otherwise, returns `proc`.") {
    janet_arity(argc, 1, 2);
    JanetProc *proc = janet_getabstract(argv, 0, &ProcAT);
    if (proc->flags & JANET_PROC_WAITED) {
        janet_panicf("cannot kill process that has already finished");
    }
#ifdef JANET_WINDOWS
    if (proc->flags & JANET_PROC_CLOSED) {
        janet_panicf("cannot close process handle that is already closed");
    }
    proc->flags |= JANET_PROC_CLOSED;
    TerminateProcess(proc->pHandle, 1);
    CloseHandle(proc->pHandle);
    CloseHandle(proc->tHandle);
#else
    int status = kill(proc->pid, SIGKILL);
    if (status) {
        janet_panic(strerror(errno));
    }
#endif
    /* After killing process we wait on it. */
    if (argc > 1 && janet_truthy(argv[1])) {
#ifdef JANET_EV
        os_proc_wait_impl(proc);
        return janet_wrap_nil();
#else
        return os_proc_wait_impl(proc);
#endif
    } else {
        return argv[0];
    }
}

JANET_CORE_FN(os_proc_close,
              "(os/proc-close proc)",
              "Wait on a process if it has not been waited on, and close pipes created by `os/spawn` "
              "if they have not been closed. Returns nil.") {
    janet_fixarity(argc, 1);
    JanetProc *proc = janet_getabstract(argv, 0, &ProcAT);
#ifdef JANET_EV
    if (proc->flags & JANET_PROC_OWNS_STDIN) janet_stream_close(proc->in);
    if (proc->flags & JANET_PROC_OWNS_STDOUT) janet_stream_close(proc->out);
    if (proc->flags & JANET_PROC_OWNS_STDERR) janet_stream_close(proc->err);
#else
    if (proc->flags & JANET_PROC_OWNS_STDIN) janet_file_close(proc->in);
    if (proc->flags & JANET_PROC_OWNS_STDOUT) janet_file_close(proc->out);
    if (proc->flags & JANET_PROC_OWNS_STDERR) janet_file_close(proc->err);
#endif
    proc->flags &= ~(JANET_PROC_OWNS_STDIN | JANET_PROC_OWNS_STDOUT | JANET_PROC_OWNS_STDERR);
    if (proc->flags & (JANET_PROC_WAITED | JANET_PROC_WAITING)) {
        return janet_wrap_nil();
    }
#ifdef JANET_EV
    os_proc_wait_impl(proc);
    return janet_wrap_nil();
#else
    return os_proc_wait_impl(proc);
#endif
}

static void swap_handles(JanetHandle *handles) {
    JanetHandle temp = handles[0];
    handles[0] = handles[1];
    handles[1] = temp;
}

static void close_handle(JanetHandle handle) {
#ifdef JANET_WINDOWS
    CloseHandle(handle);
#else
    close(handle);
#endif
}

/* Create piped file for os/execute and os/spawn. Need to be careful that we mark
   the error flag if we can't create pipe and don't leak handles. *handle will be cleaned
   up by the calling function. If everything goes well, *handle is owned by the calling function,
   (if it is set) and the returned handle owns the other end of the pipe, which will be closed
   on GC or fclose. */
static JanetHandle make_pipes(JanetHandle *handle, int reverse, int *errflag) {
    JanetHandle handles[2];
#ifdef JANET_EV

    /* non-blocking pipes */
    if (janet_make_pipe(handles, reverse ? 2 : 1)) goto error;
    if (reverse) swap_handles(handles);
#ifdef JANET_WINDOWS
    if (!SetHandleInformation(handles[0], HANDLE_FLAG_INHERIT, 0)) goto error;
#endif
    *handle = handles[1];
    return handles[0];

#else

    /* Normal blocking pipes */
#ifdef JANET_WINDOWS
    SECURITY_ATTRIBUTES saAttr;
    memset(&saAttr, 0, sizeof(saAttr));
    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    if (!CreatePipe(handles, handles + 1, &saAttr, 0)) goto error;
    if (reverse) swap_handles(handles);
    /* Don't inherit the side of the pipe owned by this process */
    if (!SetHandleInformation(handles[0], HANDLE_FLAG_INHERIT, 0)) goto error;
    *handle = handles[1];
    return handles[0];
#else
    if (pipe(handles)) goto error;
    if (reverse) swap_handles(handles);
    *handle = handles[1];
    return handles[0];
#endif

#endif
error:
    *errflag = 1;
    return JANET_HANDLE_NONE;
}

static const JanetMethod proc_methods[] = {
    {"wait", os_proc_wait},
    {"kill", os_proc_kill},
    {"close", os_proc_close},
    /* dud methods for janet_proc_next */
    {"in", NULL},
    {"out", NULL},
    {"err", NULL},
    {NULL, NULL}
};

static int janet_proc_get(void *p, Janet key, Janet *out) {
    JanetProc *proc = (JanetProc *)p;
    if (janet_keyeq(key, "in")) {
        *out = (NULL == proc->in) ? janet_wrap_nil() : janet_wrap_abstract(proc->in);
        return 1;
    }
    if (janet_keyeq(key, "out")) {
        *out = (NULL == proc->out) ? janet_wrap_nil() : janet_wrap_abstract(proc->out);
        return 1;
    }
    if (janet_keyeq(key, "err")) {
        *out = (NULL == proc->err) ? janet_wrap_nil() : janet_wrap_abstract(proc->err);
        return 1;
    }
#ifndef JANET_WINDOWS
    if (janet_keyeq(key, "pid")) {
        *out = janet_wrap_number(proc->pid);
        return 1;
    }
#endif
    if ((-1 != proc->return_code) && janet_keyeq(key, "return-code")) {
        *out = janet_wrap_integer(proc->return_code);
        return 1;
    }
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), proc_methods, out);
}

static Janet janet_proc_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(proc_methods, key);
}

static const JanetAbstractType ProcAT = {
    "core/process",
    janet_proc_gc,
    janet_proc_mark,
    janet_proc_get,
    NULL, /* put */
    NULL, /* marshal */
    NULL, /* unmarshal */
    NULL, /* tostring */
    NULL, /* compare */
    NULL, /* hash */
    janet_proc_next,
    JANET_ATEND_NEXT
};

static JanetHandle janet_getjstream(Janet *argv, int32_t n, void **orig) {
#ifdef JANET_EV
    JanetStream *stream = janet_checkabstract(argv[n], &janet_stream_type);
    if (stream != NULL) {
        if (stream->flags & JANET_STREAM_CLOSED)
            janet_panic("stream is closed");
        *orig = stream;
        return stream->handle;
    }
#endif
    JanetFile *f = janet_checkabstract(argv[n], &janet_file_type);
    if (f != NULL) {
        if (f->flags & JANET_FILE_CLOSED) {
            janet_panic("file is closed");
        }
        *orig = f;
#ifdef JANET_WINDOWS
        return (HANDLE) _get_osfhandle(_fileno(f->file));
#else
        return fileno(f->file);
#endif
    }
    janet_panicf("expected file|stream, got %v", argv[n]);
}

#ifdef JANET_EV
static JanetStream *get_stdio_for_handle(JanetHandle handle, void *orig, int iswrite) {
    if (orig == NULL) {
        return janet_stream(handle, iswrite ? JANET_STREAM_WRITABLE : JANET_STREAM_READABLE, NULL);
    } else if (janet_abstract_type(orig) == &janet_file_type) {
        JanetFile *jf = (JanetFile *)orig;
        uint32_t flags = 0;
        if (jf->flags & JANET_FILE_WRITE) {
            flags |= JANET_STREAM_WRITABLE;
        }
        if (jf->flags & JANET_FILE_READ) {
            flags |= JANET_STREAM_READABLE;
        }
        /* duplicate handle when converting file to stream */
#ifdef JANET_WINDOWS
        HANDLE prochandle = GetCurrentProcess();
        HANDLE newHandle = INVALID_HANDLE_VALUE;
        if (!DuplicateHandle(prochandle, handle, prochandle, &newHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            return NULL;
        }
#else
        int newHandle = dup(handle);
        if (newHandle < 0) {
            return NULL;
        }
#endif
        return janet_stream(newHandle, flags, NULL);
    } else {
        return orig;
    }
}
#else
static JanetFile *get_stdio_for_handle(JanetHandle handle, void *orig, int iswrite) {
    if (NULL != orig) return (JanetFile *) orig;
#ifdef JANET_WINDOWS
    int fd = _open_osfhandle((intptr_t) handle, iswrite ? _O_WRONLY : _O_RDONLY);
    if (-1 == fd) return NULL;
    FILE *f = _fdopen(fd, iswrite ? "w" : "r");
    if (NULL == f) {
        _close(fd);
        return NULL;
    }
#else
    FILE *f = fdopen(handle, iswrite ? "w" : "r");
    if (NULL == f) return NULL;
#endif
    return janet_makejfile(f, iswrite ? JANET_FILE_WRITE : JANET_FILE_READ);
}
#endif

static Janet os_execute_impl(int32_t argc, Janet *argv, int is_spawn) {
    janet_arity(argc, 1, 3);

    /* Get flags */
    uint64_t flags = 0;
    if (argc > 1) {
        flags = janet_getflags(argv, 1, "epxd");
    }

    /* Get environment */
    int use_environ = !janet_flag_at(flags, 0);
    EnvBlock envp = os_execute_env(argc, argv);

    /* Get arguments */
    JanetView exargs = janet_getindexed(argv, 0);
    if (exargs.len < 1) {
        janet_panic("expected at least 1 command line argument");
    }

    /* Optional stdio redirections */
    JanetAbstract orig_in = NULL, orig_out = NULL, orig_err = NULL;
    JanetHandle new_in = JANET_HANDLE_NONE, new_out = JANET_HANDLE_NONE, new_err = JANET_HANDLE_NONE;
    JanetHandle pipe_in = JANET_HANDLE_NONE, pipe_out = JANET_HANDLE_NONE, pipe_err = JANET_HANDLE_NONE;
    int pipe_errflag = 0; /* Track errors setting up pipes */
    int pipe_owner_flags = (is_spawn && (flags & 0x8)) ? JANET_PROC_ALLOW_ZOMBIE : 0;

    /* Get optional redirections */
    if (argc > 2) {
        JanetDictView tab = janet_getdictionary(argv, 2);
        Janet maybe_stdin = janet_dictionary_get(tab.kvs, tab.cap, janet_ckeywordv("in"));
        Janet maybe_stdout = janet_dictionary_get(tab.kvs, tab.cap, janet_ckeywordv("out"));
        Janet maybe_stderr = janet_dictionary_get(tab.kvs, tab.cap, janet_ckeywordv("err"));
        if (is_spawn && janet_keyeq(maybe_stdin, "pipe")) {
            new_in = make_pipes(&pipe_in, 1, &pipe_errflag);
            pipe_owner_flags |= JANET_PROC_OWNS_STDIN;
        } else if (!janet_checktype(maybe_stdin, JANET_NIL)) {
            new_in = janet_getjstream(&maybe_stdin, 0, &orig_in);
        }
        if (is_spawn && janet_keyeq(maybe_stdout, "pipe")) {
            new_out = make_pipes(&pipe_out, 0, &pipe_errflag);
            pipe_owner_flags |= JANET_PROC_OWNS_STDOUT;
        } else if (!janet_checktype(maybe_stdout, JANET_NIL)) {
            new_out = janet_getjstream(&maybe_stdout, 0, &orig_out);
        }
        if (is_spawn && janet_keyeq(maybe_stderr, "pipe")) {
            new_err = make_pipes(&pipe_err, 0, &pipe_errflag);
            pipe_owner_flags |= JANET_PROC_OWNS_STDERR;
        } else if (!janet_checktype(maybe_stderr, JANET_NIL)) {
            new_err = janet_getjstream(&maybe_stderr, 0, &orig_err);
        }
    }

    /* Clean up if any of the pipes have any issues */
    if (pipe_errflag) {
        if (pipe_in != JANET_HANDLE_NONE) close_handle(pipe_in);
        if (pipe_out != JANET_HANDLE_NONE) close_handle(pipe_out);
        if (pipe_err != JANET_HANDLE_NONE) close_handle(pipe_err);
        janet_panic("failed to create pipes");
    }

    /* Result */
    int status = 0;

#ifdef JANET_WINDOWS

    HANDLE pHandle, tHandle;
    SECURITY_ATTRIBUTES saAttr;
    PROCESS_INFORMATION processInfo;
    STARTUPINFO startupInfo;
    memset(&saAttr, 0, sizeof(saAttr));
    memset(&processInfo, 0, sizeof(processInfo));
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    saAttr.nLength = sizeof(saAttr);

    JanetBuffer *buf = os_exec_escape(exargs);
    if (buf->count > 8191) {
        if (pipe_in != JANET_HANDLE_NONE) CloseHandle(pipe_in);
        if (pipe_out != JANET_HANDLE_NONE) CloseHandle(pipe_out);
        if (pipe_err != JANET_HANDLE_NONE) CloseHandle(pipe_err);
        janet_panic("command line string too long (max 8191 characters)");
    }
    const char *path = (const char *) janet_unwrap_string(exargs.items[0]);

    /* Do IO redirection */

    if (pipe_in != JANET_HANDLE_NONE) {
        startupInfo.hStdInput = pipe_in;
    } else if (new_in != JANET_HANDLE_NONE) {
        startupInfo.hStdInput = new_in;
    } else {
        startupInfo.hStdInput = (HANDLE) _get_osfhandle(0);
    }


    if (pipe_out != JANET_HANDLE_NONE) {
        startupInfo.hStdOutput = pipe_out;
    } else if (new_out != JANET_HANDLE_NONE) {
        startupInfo.hStdOutput = new_out;
    } else {
        startupInfo.hStdOutput = (HANDLE) _get_osfhandle(1);
    }

    if (pipe_err != JANET_HANDLE_NONE) {
        startupInfo.hStdError = pipe_err;
    } else if (new_err != NULL) {
        startupInfo.hStdError = new_err;
    } else {
        startupInfo.hStdError = (HANDLE) _get_osfhandle(2);
    }

    int cp_failed = 0;
    if (!CreateProcess(janet_flag_at(flags, 1) ? NULL : path,
                       (char *) buf->data, /* Single CLI argument */
                       &saAttr, /* no proc inheritance */
                       &saAttr, /* no thread inheritance */
                       TRUE, /* handle inheritance */
                       0, /* flags */
                       use_environ ? NULL : envp, /* pass in environment */
                       NULL, /* use parents starting directory */
                       &startupInfo,
                       &processInfo)) {
        cp_failed = 1;
    }

    if (pipe_in != JANET_HANDLE_NONE) CloseHandle(pipe_in);
    if (pipe_out != JANET_HANDLE_NONE) CloseHandle(pipe_out);
    if (pipe_err != JANET_HANDLE_NONE) CloseHandle(pipe_err);

    os_execute_cleanup(envp, NULL);

    if (cp_failed)  {
        janet_panic("failed to create process");
    }

    pHandle = processInfo.hProcess;
    tHandle = processInfo.hThread;

#else

    const char **child_argv = janet_smalloc(sizeof(char *) * ((size_t) exargs.len + 1));
    for (int32_t i = 0; i < exargs.len; i++)
        child_argv[i] = janet_getcstring(exargs.items, i);
    child_argv[exargs.len] = NULL;
    /* Coerce to form that works for spawn. I'm fairly confident no implementation
     * of posix_spawn would modify the argv array passed in. */
    char *const *cargv = (char *const *)child_argv;

    /* Use posix_spawn to spawn new process */

    if (use_environ) {
        janet_lock_environ();
    }

    /* Posix spawn setup */
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (pipe_in != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, pipe_in, 0);
        posix_spawn_file_actions_addclose(&actions, pipe_in);
    } else if (new_in != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, new_in, 0);
        posix_spawn_file_actions_addclose(&actions, new_in);
    }
    if (pipe_out != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, pipe_out, 1);
        posix_spawn_file_actions_addclose(&actions, pipe_out);
    } else if (new_out != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, new_out, 1);
        posix_spawn_file_actions_addclose(&actions, new_out);
    }
    if (pipe_err != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, pipe_err, 2);
        posix_spawn_file_actions_addclose(&actions, pipe_err);
    } else if (new_err != JANET_HANDLE_NONE) {
        posix_spawn_file_actions_adddup2(&actions, new_err, 2);
        posix_spawn_file_actions_addclose(&actions, new_err);
    }

    pid_t pid;
    if (janet_flag_at(flags, 1)) {
        status = posix_spawnp(&pid,
                              child_argv[0], &actions, NULL, cargv,
                              use_environ ? environ : envp);
    } else {
        status = posix_spawn(&pid,
                             child_argv[0], &actions, NULL, cargv,
                             use_environ ? environ : envp);
    }

    posix_spawn_file_actions_destroy(&actions);

    if (pipe_in != JANET_HANDLE_NONE) close(pipe_in);
    if (pipe_out != JANET_HANDLE_NONE) close(pipe_out);
    if (pipe_err != JANET_HANDLE_NONE) close(pipe_err);

    if (use_environ) {
        janet_unlock_environ();
    }

    os_execute_cleanup(envp, child_argv);
    if (status) {
        janet_panicf("%p: %s", argv[0], strerror(errno));
    }

#endif
    JanetProc *proc = janet_abstract(&ProcAT, sizeof(JanetProc));
    proc->return_code = -1;
#ifdef JANET_WINDOWS
    proc->pHandle = pHandle;
    proc->tHandle = tHandle;
#else
    proc->pid = pid;
#endif
    proc->in = NULL;
    proc->out = NULL;
    proc->err = NULL;
    proc->flags = pipe_owner_flags;
    if (janet_flag_at(flags, 2)) {
        proc->flags |= JANET_PROC_ERROR_NONZERO;
    }
    if (is_spawn) {
        /* Only set up pointers to stdin, stdout, and stderr if os/spawn. */
        if (new_in != JANET_HANDLE_NONE) {
            proc->in = get_stdio_for_handle(new_in, orig_in, 1);
            if (NULL == proc->in) janet_panic("failed to construct proc");
        }
        if (new_out != JANET_HANDLE_NONE) {
            proc->out = get_stdio_for_handle(new_out, orig_out, 0);
            if (NULL == proc->out) janet_panic("failed to construct proc");
        }
        if (new_err != JANET_HANDLE_NONE) {
            proc->err = get_stdio_for_handle(new_err, orig_err, 0);
            if (NULL == proc->err) janet_panic("failed to construct proc");
        }
        return janet_wrap_abstract(proc);
    } else {
#ifdef JANET_EV
        os_proc_wait_impl(proc);
#else
        return os_proc_wait_impl(proc);
#endif
    }
}

JANET_CORE_FN(os_execute,
              "(os/execute args &opt flags env)",
              "Execute a program on the system and pass it string arguments. `flags` "
              "is a keyword that modifies how the program will execute.\n"
              "* :e - enables passing an environment to the program. Without :e, the "
              "current environment is inherited.\n"
              "* :p - allows searching the current PATH for the binary to execute. "
              "Without this flag, binaries must use absolute paths.\n"
              "* :x - raise error if exit code is non-zero.\n"
              "* :d - Don't try and terminate the process on garbage collection (allow spawning zombies).\n"
              "`env` is a table or struct mapping environment variables to values. It can also "
              "contain the keys :in, :out, and :err, which allow redirecting stdio in the subprocess. "
              "These arguments should be core/file values. "
              "Returns the exit status of the program.") {
    return os_execute_impl(argc, argv, 0);
}

JANET_CORE_FN(os_spawn,
              "(os/spawn args &opt flags env)",
              "Execute a program on the system and return a handle to the process. Otherwise, takes the "
              "same arguments as `os/execute`. Does not wait for the process. "
              "For each of the :in, :out, and :err keys to the `env` argument, one "
              "can also pass in the keyword `:pipe` "
              "to get streams for standard IO of the subprocess that can be read from and written to. "
              "The returned value `proc` has the fields :in, :out, :err, :return-code, and "
              "the additional field :pid on unix-like platforms. Use `(os/proc-wait proc)` to rejoin the "
              "subprocess or `(os/proc-kill proc)`.") {
    return os_execute_impl(argc, argv, 1);
}

#ifdef JANET_EV
/* Runs in a separate thread */
static JanetEVGenericMessage os_shell_subr(JanetEVGenericMessage args) {
    int stat = system((const char *) args.argp);
    janet_free(args.argp);
    if (args.argi) {
        args.tag = JANET_EV_TCTAG_INTEGER;
    } else {
        args.tag = JANET_EV_TCTAG_BOOLEAN;
    }
    args.argi = stat;
    return args;
}
#endif

JANET_CORE_FN(os_shell,
              "(os/shell str)",
              "Pass a command string str directly to the system shell.") {
    janet_arity(argc, 0, 1);
    const char *cmd = argc
                      ? janet_getcstring(argv, 0)
                      : NULL;
#ifdef JANET_EV
    janet_ev_threaded_await(os_shell_subr, 0, argc, cmd ? strdup(cmd) : NULL);
#else
    int stat = system(cmd);
    return argc
           ? janet_wrap_integer(stat)
           : janet_wrap_boolean(stat);
#endif
}

#endif /* JANET_NO_PROCESSES */

JANET_CORE_FN(os_environ,
              "(os/environ)",
              "Get a copy of the OS environment table.") {
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

JANET_CORE_FN(os_getenv,
              "(os/getenv variable &opt dflt)",
              "Get the string value of an environment variable.") {
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

JANET_CORE_FN(os_setenv,
              "(os/setenv variable value)",
              "Set an environment variable.") {
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

JANET_CORE_FN(os_time,
              "(os/time)",
              "Get the current time expressed as the number of whole seconds since "
              "January 1, 1970, the Unix epoch. Returns a real number.") {
    janet_fixarity(argc, 0);
    (void) argv;
    double dtime = (double)(time(NULL));
    return janet_wrap_number(dtime);
}

JANET_CORE_FN(os_clock,
              "(os/clock)",
              "Return the number of whole + fractional seconds since some fixed point in time. The clock "
              "is guaranteed to be non-decreasing in real time.") {
    janet_fixarity(argc, 0);
    (void) argv;
    struct timespec tv;
    if (janet_gettime(&tv)) janet_panic("could not get time");
    double dtime = tv.tv_sec + (tv.tv_nsec / 1E9);
    return janet_wrap_number(dtime);
}

JANET_CORE_FN(os_sleep,
              "(os/sleep n)",
              "Suspend the program for `n` seconds. `n` can be a real number. Returns "
              "nil.") {
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

JANET_CORE_FN(os_cwd,
              "(os/cwd)",
              "Returns the current working directory.") {
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

JANET_CORE_FN(os_cryptorand,
              "(os/cryptorand n &opt buf)",
              "Get or append `n` bytes of good quality random data provided by the OS. Returns a new buffer or `buf`.") {
    JanetBuffer *buffer;
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

    if (janet_cryptorand(buffer->data + offset, n) != 0)
        janet_panic("unable to get sufficient random data");

    return janet_wrap_buffer(buffer);
}

JANET_CORE_FN(os_date,
              "(os/date &opt time local)",
              "Returns the given time as a date struct, or the current time if `time` is not given. "
              "Returns a struct with following key values. Note that all numbers are 0-indexed. "
              "Date is given in UTC unless `local` is truthy, in which case the date is formatted for "
              "the local timezone.\n\n"
              "* :seconds - number of seconds [0-61]\n\n"
              "* :minutes - number of minutes [0-59]\n\n"
              "* :hours - number of hours [0-23]\n\n"
              "* :month-day - day of month [0-30]\n\n"
              "* :month - month of year [0, 11]\n\n"
              "* :year - years since year 0 (e.g. 2019)\n\n"
              "* :week-day - day of the week [0-6]\n\n"
              "* :year-day - day of the year [0-365]\n\n"
              "* :dst - if Day Light Savings is in effect") {
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
        _tzset();
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

JANET_CORE_FN(os_mktime,
              "(os/mktime date-struct &opt local)",
              "Get the broken down date-struct time expressed as the number "
              "of seconds since January 1, 1970, the Unix epoch. "
              "Returns a real number. "
              "Date is given in UTC unless `local` is truthy, in which case the "
              "date is computed for the local timezone.\n\n"
              "Inverse function to os/date.") {
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

JANET_CORE_FN(os_link,
              "(os/link oldpath newpath &opt symlink)",
              "Create a link at newpath that points to oldpath and returns nil. "
              "Iff symlink is truthy, creates a symlink. "
              "Iff symlink is falsey or not provided, "
              "creates a hard link. Does not work on Windows.") {
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

JANET_CORE_FN(os_symlink,
              "(os/symlink oldpath newpath)",
              "Create a symlink from oldpath to newpath, returning nil. Same as `(os/link oldpath newpath true)`.") {
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

JANET_CORE_FN(os_mkdir,
              "(os/mkdir path)",
              "Create a new directory. The path will be relative to the current directory if relative, otherwise "
              "it will be an absolute path. Returns true if the directory was created, false if the directory already exists, and "
              "errors otherwise.") {
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

JANET_CORE_FN(os_rmdir,
              "(os/rmdir path)",
              "Delete a directory. The directory must be empty to succeed.") {
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

JANET_CORE_FN(os_cd,
              "(os/cd path)",
              "Change current directory to path. Returns nil on success, errors on failure.") {
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

JANET_CORE_FN(os_touch,
              "(os/touch path &opt actime modtime)",
              "Update the access time and modification times for a file. By default, sets "
              "times to the current time.") {
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

JANET_CORE_FN(os_remove,
              "(os/rm path)",
              "Delete a file. Returns nil.") {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
    int status = remove(path);
    if (-1 == status) janet_panicf("%s: %s", strerror(errno), path);
    return janet_wrap_nil();
}

#ifndef JANET_NO_SYMLINKS
JANET_CORE_FN(os_readlink,
              "(os/readlink path)",
              "Read the contents of a symbolic link. Does not work on Windows.\n") {
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

JANET_CORE_FN(os_stat,
              "(os/stat path &opt tab|key)",
              "Gets information about a file or directory. Returns a table if the second argument is a keyword, returns "
              " only that information from stat. If the file or directory does not exist, returns nil. The keys are:\n\n"
              "* :dev - the device that the file is on\n\n"
              "* :mode - the type of file, one of :file, :directory, :block, :character, :fifo, :socket, :link, or :other\n\n"
              "* :int-permissions - A Unix permission integer like 8r744\n\n"
              "* :permissions - A Unix permission string like \"rwxr--r--\"\n\n"
              "* :uid - File uid\n\n"
              "* :gid - File gid\n\n"
              "* :nlink - number of links to file\n\n"
              "* :rdev - Real device of file. 0 on Windows\n\n"
              "* :size - size of file in bytes\n\n"
              "* :blocks - number of blocks in file. 0 on Windows\n\n"
              "* :blocksize - size of blocks in file. 0 on Windows\n\n"
              "* :accessed - timestamp when file last accessed\n\n"
              "* :changed - timestamp when file last changed (permissions changed)\n\n"
              "* :modified - timestamp when file last modified (content changed)\n") {
    return os_stat_or_lstat(0, argc, argv);
}

JANET_CORE_FN(os_lstat,
              "(os/lstat path &opt tab|key)",
              "Like os/stat, but don't follow symlinks.\n") {
    return os_stat_or_lstat(1, argc, argv);
}

JANET_CORE_FN(os_chmod,
              "(os/chmod path mode)",
              "Change file permissions, where `mode` is a permission string as returned by "
              "`os/perm-string`, or an integer as returned by `os/perm-int`. "
              "When `mode` is an integer, it is interpreted as a Unix permission value, best specified in octal, like "
              "8r666 or 8r400. Windows will not differentiate between user, group, and other permissions, and thus will combine all of these permissions. Returns nil.") {
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
JANET_CORE_FN(os_umask,
              "(os/umask mask)",
              "Set a new umask, returns the old umask.") {
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

JANET_CORE_FN(os_dir,
              "(os/dir dir &opt array)",
              "Iterate over files and subdirectories in a directory. Returns an array of paths parts, "
              "with only the file name or directory name and no prefix.") {
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

JANET_CORE_FN(os_rename,
              "(os/rename oldname newname)",
              "Rename a file on disk to a new path. Returns nil.") {
    janet_fixarity(argc, 2);
    const char *src = janet_getcstring(argv, 0);
    const char *dest = janet_getcstring(argv, 1);
    int status = rename(src, dest);
    if (status) {
        janet_panic(strerror(errno));
    }
    return janet_wrap_nil();
}

JANET_CORE_FN(os_realpath,
              "(os/realpath path)",
              "Get the absolute path for a given path, following ../, ./, and symlinks. "
              "Returns an absolute path as a string. Will raise an error on Windows.") {
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
    janet_free(dest);
    return ret;
#endif
}

JANET_CORE_FN(os_permission_string,
              "(os/perm-string int)",
              "Convert a Unix octal permission value from a permission integer as returned by `os/stat` "
              "to a human readable string, that follows the formatting "
              "of Unix tools like `ls`. Returns the string as a 9-character string of r, w, x and - characters. Does not "
              "include the file/directory/symlink character as rendered by `ls`.") {
    janet_fixarity(argc, 1);
    return os_make_permstring(os_get_unix_mode(argv, 0));
}

JANET_CORE_FN(os_permission_int,
              "(os/perm-int bytes)",
              "Parse a 9-character permission string and return an integer that can be used by chmod.") {
    janet_fixarity(argc, 1);
    return janet_wrap_integer(os_get_unix_mode(argv, 0));
}

#ifdef JANET_EV

/*
 * Define a few functions on streams the require JANET_EV to be defined.
 */

static jmode_t os_optmode(int32_t argc, const Janet *argv, int32_t n, int32_t dflt) {
    if (argc > n) return os_getmode(argv, n);
    return janet_perm_from_unix(dflt);
}

JANET_CORE_FN(os_open,
              "(os/open path &opt flags mode)",
              "Create a stream from a file, like the POSIX open system call. Returns a new stream. "
              "`mode` should be a file mode as passed to `os/chmod`, but only if the create flag is given. "
              "The default mode is 8r666. "
              "Allowed flags are as follows:\n\n"
              "  * :r - open this file for reading\n"
              "  * :w - open this file for writing\n"
              "  * :c - create a new file (O\\_CREATE)\n"
              "  * :e - fail if the file exists (O\\_EXCL)\n"
              "  * :t - shorten an existing file to length 0 (O\\_TRUNC)\n\n"
              "Posix-only flags:\n\n"
              "  * :a - append to a file (O\\_APPEND)\n"
              "  * :x - O\\_SYNC\n"
              "  * :C - O\\_NOCTTY\n\n"
              "Windows-only flags:\n\n"
              "  * :R - share reads (FILE\\_SHARE\\_READ)\n"
              "  * :W - share writes (FILE\\_SHARE\\_WRITE)\n"
              "  * :D - share deletes (FILE\\_SHARE\\_DELETE)\n"
              "  * :H - FILE\\_ATTRIBUTE\\_HIDDEN\n"
              "  * :O - FILE\\_ATTRIBUTE\\_READONLY\n"
              "  * :F - FILE\\_ATTRIBUTE\\_OFFLINE\n"
              "  * :T - FILE\\_ATTRIBUTE\\_TEMPORARY\n"
              "  * :d - FILE\\_FLAG\\_DELETE\\_ON\\_CLOSE\n"
              "  * :b - FILE\\_FLAG\\_NO\\_BUFFERING\n") {
    janet_arity(argc, 1, 3);
    const char *path = janet_getcstring(argv, 0);
    const uint8_t *opt_flags = janet_optkeyword(argv, argc, 1, (const uint8_t *) "r");
    jmode_t mode = os_optmode(argc, argv, 2, 0666);
    uint32_t stream_flags = 0;
    JanetHandle fd;
#ifdef JANET_WINDOWS
    DWORD desiredAccess = 0;
    DWORD shareMode = 0;
    DWORD creationDisp = 0;
    DWORD flagsAndAttributes = FILE_FLAG_OVERLAPPED;
    /* We map unix-like open flags to the creationDisp parameter */
    int creatUnix = 0;
#define OCREAT 1
#define OEXCL 2
#define OTRUNC 4
    for (const uint8_t *c = opt_flags; *c; c++) {
        switch (*c) {
            default:
                break;
            case 'r':
                desiredAccess |= GENERIC_READ;
                stream_flags |= JANET_STREAM_READABLE;
                break;
            case 'w':
                desiredAccess |= GENERIC_WRITE;
                stream_flags |= JANET_STREAM_WRITABLE;
                break;
            case 'c':
                creatUnix |= OCREAT;
                break;
            case 'e':
                creatUnix |= OEXCL;
                break;
            case 't':
                creatUnix |= OTRUNC;
                break;
            /* Windows only flags */
            case 'D':
                shareMode |= FILE_SHARE_DELETE;
                break;
            case 'R':
                shareMode |= FILE_SHARE_READ;
                break;
            case 'W':
                shareMode |= FILE_SHARE_WRITE;
                break;
            case 'H':
                flagsAndAttributes |= FILE_ATTRIBUTE_HIDDEN;
                break;
            case 'O':
                flagsAndAttributes |= FILE_ATTRIBUTE_READONLY;
                break;
            case 'F':
                flagsAndAttributes |= FILE_ATTRIBUTE_OFFLINE;
                break;
            case 'T':
                flagsAndAttributes |= FILE_ATTRIBUTE_TEMPORARY;
                break;
            case 'd':
                flagsAndAttributes |= FILE_FLAG_DELETE_ON_CLOSE;
                break;
            case 'b':
                flagsAndAttributes |= FILE_FLAG_NO_BUFFERING;
                break;
                /* we could potentially add more here -
                 * https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea
                 */
        }
    }
    switch (creatUnix) {
        default:
            janet_panic("invalid creation flags");
        case 0:
            creationDisp = OPEN_EXISTING;
            break;
        case OCREAT:
            creationDisp = OPEN_ALWAYS;
            break;
        case OCREAT + OEXCL:
            creationDisp = CREATE_NEW;
            break;
        case OCREAT + OTRUNC:
            creationDisp = CREATE_ALWAYS;
            break;
        case OTRUNC:
            creationDisp = TRUNCATE_EXISTING;
            break;
    }
    fd = CreateFileA(path, desiredAccess, shareMode, NULL, creationDisp, flagsAndAttributes, NULL);
    if (fd == INVALID_HANDLE_VALUE) janet_panicv(janet_ev_lasterr());
#else
    int open_flags = O_NONBLOCK;
#ifdef JANET_LINUX
    open_flags |= O_CLOEXEC;
#endif
    for (const uint8_t *c = opt_flags; *c; c++) {
        switch (*c) {
            default:
                break;
            case 'r':
                open_flags = (open_flags & O_WRONLY)
                             ? ((open_flags & ~O_WRONLY) | O_RDWR)
                             : (open_flags | O_RDONLY);
                stream_flags |= JANET_STREAM_READABLE;
                break;
            case 'w':
                open_flags = (open_flags & O_RDONLY)
                             ? ((open_flags & ~O_RDONLY) | O_RDWR)
                             : (open_flags | O_WRONLY);
                stream_flags |= JANET_STREAM_WRITABLE;
                break;
            case 'c':
                open_flags |= O_CREAT;
                break;
            case 'e':
                open_flags |= O_EXCL;
                break;
            case 't':
                open_flags |= O_TRUNC;
                break;
            /* posix only */
            case 'x':
                open_flags |= O_SYNC;
                break;
            case 'C':
                open_flags |= O_NOCTTY;
                break;
            case 'a':
                open_flags |= O_APPEND;
                break;
        }
    }
    do {
        fd = open(path, open_flags, mode);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) janet_panicv(janet_ev_lasterr());
#endif
    return janet_wrap_abstract(janet_stream(fd, stream_flags, NULL));
}

JANET_CORE_FN(os_pipe,
              "(os/pipe)",
              "Create a readable stream and a writable stream that are connected. Returns a two-element "
              "tuple where the first element is a readable stream and the second element is the writable "
              "stream.") {
    (void) argv;
    janet_fixarity(argc, 0);
    JanetHandle fds[2];
    if (janet_make_pipe(fds, 0)) janet_panicv(janet_ev_lasterr());
    JanetStream *reader = janet_stream(fds[0], JANET_STREAM_READABLE, NULL);
    JanetStream *writer = janet_stream(fds[1], JANET_STREAM_WRITABLE, NULL);
    Janet tup[2] = {janet_wrap_abstract(reader), janet_wrap_abstract(writer)};
    return janet_wrap_tuple(janet_tuple_n(tup, 2));
}

#endif

#endif /* JANET_REDUCED_OS */

/* Module entry point */
void janet_lib_os(JanetTable *env) {
#if !defined(JANET_REDUCED_OS) && defined(JANET_WINDOWS) && defined(JANET_THREADS)
    /* During start up, the top-most abstract machine (thread)
     * in the thread tree sets up the critical section. */
    static volatile long env_lock_initializing = 0;
    static volatile long env_lock_initialized = 0;
    if (!InterlockedExchange(&env_lock_initializing, 1)) {
        InitializeCriticalSection(&env_lock);
        InterlockedOr(&env_lock_initialized, 1);
    } else {
        while (!InterlockedOr(&env_lock_initialized, 0)) {
            Sleep(0);
        }
    }

#endif
#ifndef JANET_NO_PROCESSES
#endif
    JanetRegExt os_cfuns[] = {
        JANET_CORE_REG("os/exit", os_exit),
        JANET_CORE_REG("os/which", os_which),
        JANET_CORE_REG("os/arch", os_arch),
#ifndef JANET_REDUCED_OS
        JANET_CORE_REG("os/environ", os_environ),
        JANET_CORE_REG("os/getenv", os_getenv),
        JANET_CORE_REG("os/dir", os_dir),
        JANET_CORE_REG("os/stat", os_stat),
        JANET_CORE_REG("os/lstat", os_lstat),
        JANET_CORE_REG("os/chmod", os_chmod),
        JANET_CORE_REG("os/touch", os_touch),
        JANET_CORE_REG("os/cd", os_cd),
        JANET_CORE_REG("os/cpu-count", os_cpu_count),
#ifndef JANET_NO_UMASK
        JANET_CORE_REG("os/umask", os_umask),
#endif
        JANET_CORE_REG("os/mkdir", os_mkdir),
        JANET_CORE_REG("os/rmdir", os_rmdir),
        JANET_CORE_REG("os/rm", os_remove),
        JANET_CORE_REG("os/link", os_link),
#ifndef JANET_NO_SYMLINKS
        JANET_CORE_REG("os/symlink", os_symlink),
        JANET_CORE_REG("os/readlink", os_readlink),
#endif
#ifndef JANET_NO_PROCESSES
        JANET_CORE_REG("os/execute", os_execute),
        JANET_CORE_REG("os/spawn", os_spawn),
        JANET_CORE_REG("os/shell", os_shell),
        JANET_CORE_REG("os/proc-wait", os_proc_wait),
        JANET_CORE_REG("os/proc-kill", os_proc_kill),
        JANET_CORE_REG("os/proc-close", os_proc_close),
#endif
        JANET_CORE_REG("os/setenv", os_setenv),
        JANET_CORE_REG("os/time", os_time),
        JANET_CORE_REG("os/mktime", os_mktime),
        JANET_CORE_REG("os/clock", os_clock),
        JANET_CORE_REG("os/sleep", os_sleep),
        JANET_CORE_REG("os/cwd", os_cwd),
        JANET_CORE_REG("os/cryptorand", os_cryptorand),
        JANET_CORE_REG("os/date", os_date),
        JANET_CORE_REG("os/rename", os_rename),
        JANET_CORE_REG("os/realpath", os_realpath),
        JANET_CORE_REG("os/perm-string", os_permission_string),
        JANET_CORE_REG("os/perm-int", os_permission_int),
#ifdef JANET_EV
        JANET_CORE_REG("os/open", os_open),
        JANET_CORE_REG("os/pipe", os_pipe),
#endif
#endif
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, os_cfuns);
}

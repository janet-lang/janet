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

#include <janet/janet.h>
#include <stdlib.h>
#include <time.h>

#ifdef JANET_WINDOWS
#include <Windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#endif

/* For macos */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

static int os_which(JanetArgs args) {
    #ifdef JANET_WINDOWS
        JANET_RETURN_CSYMBOL(args, ":windows");
    #elif __APPLE__
        JANET_RETURN_CSYMBOL(args, ":macos");
    #else
        JANET_RETURN_CSYMBOL(args, ":posix");
    #endif
}

#ifdef JANET_WINDOWS
static int os_execute(JanetArgs args) {
    JANET_MINARITY(args, 1);
    JanetBuffer *buffer = janet_buffer(10);
    for (int32_t i = 0; i < args.n; i++) {
        const uint8_t *argstring;
        JANET_ARG_STRING(argstring, args, i);
        janet_buffer_push_bytes(buffer, argstring, janet_string_length(argstring));
        if (i != args.n - 1) {
            janet_buffer_push_u8(buffer, ' ');
        }
    }
    janet_buffer_push_u8(buffer, 0);

    /* Convert to wide chars */
    wchar_t *sys_str = malloc(buffer->count * sizeof(wchar_t));
    if (NULL == sys_str) {
        JANET_OUT_OF_MEMORY;
    }
    int nwritten = MultiByteToWideChar(
        CP_UTF8,
        MB_PRECOMPOSED,
        buffer->data,
        buffer->count,
        sys_str,
        buffer->count);
    if (nwritten == 0) {
        free(sys_str);
        JANET_THROW(args, "could not create process");
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process. 
    if(!CreateProcess(NULL,
                (LPSTR) sys_str,
                NULL,
                NULL,
                FALSE,
                0,
                NULL,
                NULL,
                &si,
                &pi)) {
        free(sys_str);
        JANET_THROW(args, "could not create process");
    }
    free(sys_str);

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close process and thread handles.
    WORD status;
    GetExitCodeProcess(pi.hProcess, (LPDWORD)&status);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    JANET_RETURN_INTEGER(args, (int32_t)status);
}
#else
static int os_execute(JanetArgs args) {
    JANET_MINARITY(args, 1);
    const uint8_t **argv = malloc(sizeof(uint8_t *) * (args.n + 1));
    if (NULL == argv) {
        JANET_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < args.n; i++) {
        JANET_ARG_STRING(argv[i], args, i);
    }
    argv[args.n] = NULL;

    /* Fork child process */
    pid_t pid;
    if (0 == (pid = fork())) {
        if (-1 == execve((const char *)argv[0], (char **)argv, NULL)) {
            exit(1);
        }
    }

    /* Wait for child process */
    int status;
    struct timespec waiter;
    waiter.tv_sec = 0;
    waiter.tv_nsec = 200;
    while (0 == waitpid(pid, &status, WNOHANG)) {
        waiter.tv_nsec = (waiter.tv_nsec * 3) / 2;
        /* Keep increasing sleep time by a factor of 3/2
         * until a maximum */
        if (waiter.tv_nsec > 4999999)
            waiter.tv_nsec = 5000000;
        nanosleep(&waiter, NULL);
    }

    JANET_RETURN_INTEGER(args, status);
}
#endif

static int os_shell(JanetArgs args) {
    int nofirstarg = (args.n < 1 || !janet_checktype(args.v[0], JANET_STRING));
    const char *cmd = nofirstarg
        ? NULL
        : (const char *) janet_unwrap_string(args.v[0]);
    int stat = system(cmd);
    JANET_RETURN(args, cmd
            ? janet_wrap_integer(stat)
            : janet_wrap_boolean(stat));
}

static int os_getenv(JanetArgs args) {
    const uint8_t *k;
    JANET_FIXARITY(args, 1);
    JANET_ARG_STRING(k, args, 0);
    const char *cstr = (const char *) k;
    const char *res = getenv(cstr);
    if (!res) {
        JANET_RETURN_NIL(args);
    }
    JANET_RETURN(args, cstr
            ? janet_cstringv(res)
            : janet_wrap_nil());
}

static int os_setenv(JanetArgs args) {
#ifdef JANET_WINDOWS
#define SETENV(K,V) _putenv_s(K, V)
#define UNSETENV(K) _putenv_s(K, "")
#else
#define SETENV(K,V) setenv(K, V, 1)
#define UNSETENV(K) unsetenv(K)
#endif
    const uint8_t *k;
    const char *ks;
    JANET_MAXARITY(args, 2);
    JANET_MINARITY(args, 1);
    JANET_ARG_STRING(k, args, 0);
    ks = (const char *) k;
    if (args.n == 1 || janet_checktype(args.v[1], JANET_NIL)) {
        UNSETENV(ks);
    } else {
        const uint8_t *v;
        JANET_ARG_STRING(v, args, 1);
        const char *vc = (const char *) v;
        SETENV(ks, vc);
    }
    return 0;
}

static int os_exit(JanetArgs args) {
    JANET_MAXARITY(args, 1);
    if (args.n == 0) {
        exit(EXIT_SUCCESS);
    } else if (janet_checktype(args.v[0], JANET_INTEGER)) {
        exit(janet_unwrap_integer(args.v[0]));
    } else {
        exit(EXIT_FAILURE);
    }
    return 0;
}

static int os_time(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    double dtime = (double)(time(NULL));
    JANET_RETURN_REAL(args, dtime);
}

/* Clock shims */
#ifdef JANET_WINDOWS
static int gettime(struct timespec *spec) {
    int64_t wintime = 0LL;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    /* Windows epoch is January 1, 1601 apparently*/
    wintime -= 116444736000000000LL;
    spec->tv_sec  = wintime / 10000000LL;
    /* Resolution is 100 nanoseconds. */
    spec->tv_nsec = wintime % 10000000LL * 100;
    return 0;
}
#elif defined(__MACH__)
static int gettime(struct timespec *spec) {
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    spec->tv_sec = mts.tv_sec;
    spec->tv_nsec = mts.tv_nsec;
    return 0;
}
#else 
#define gettime(TV) clock_gettime(CLOCK_MONOTONIC, (TV))
#endif

static int os_clock(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    struct timespec tv;
    if (clock_gettime(CLOCK_MONOTONIC, &tv))
        JANET_THROW(args, "could not get time");
    double dtime = tv.tv_sec + (tv.tv_nsec / 1E9);
    JANET_RETURN_REAL(args, dtime);
}

static int os_sleep(JanetArgs args) {
    double delay;
    JANET_FIXARITY(args, 1);
    JANET_ARG_NUMBER(delay, args, 0);
    if (delay < 0) {
        JANET_THROW(args, "invalid argument to sleep");
    }
#ifdef JANET_WINDOWS
    Sleep((DWORD) (delay * 1000));
#else
    struct timespec ts;
    ts.tv_sec = (time_t) delay;
    ts.tv_nsec = (delay <= UINT32_MAX)
        ? (long)((delay - ((uint32_t)delay)) * 1000000000)
        : 0;
    nanosleep(&ts, NULL);
#endif
    return 0;
}

static int os_cwd(JanetArgs args) {
    JANET_FIXARITY(args, 0);
    char buf[FILENAME_MAX];
    char *ptr;
#ifdef JANET_WINDOWS
    ptr = _getcwd(buf, FILENAME_MAX);
#else
    ptr = getcwd(buf, FILENAME_MAX);
#endif
    if (NULL == ptr) {
        JANET_THROW(args, "could not get current directory");
    }
    JANET_RETURN_CSTRING(args, ptr);
}

static const JanetReg cfuns[] = {
    {"os.which", os_which},
    {"os.execute", os_execute},
    {"os.shell", os_shell},
    {"os.exit", os_exit},
    {"os.getenv", os_getenv},
    {"os.setenv", os_setenv},
    {"os.time", os_time},
    {"os.clock", os_clock},
    {"os.sleep", os_sleep},
    {"os.cwd", os_cwd},
    {NULL, NULL}
};

/* Module entry point */
int janet_lib_os(JanetArgs args) {
    JanetTable *env = janet_env(args);
    janet_cfuns(env, NULL, cfuns);
    return 0;
}

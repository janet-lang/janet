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

#include <dst/dst.h>
#include <stdlib.h>
#include <time.h>

#ifdef DST_WINDOWS
#include <Windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#endif

#ifdef DST_WINDOWS
static int os_execute(DstArgs args) {
    DST_MINARITY(args, 1);
    DstBuffer *buffer = dst_buffer(10);
    for (int32_t i = 0; i < args.n; i++) {
        const uint8_t *argstring;
        DST_ARG_STRING(argstring, args, i);
        dst_buffer_push_bytes(buffer, argstring, dst_string_length(argstring));
        if (i != args.n - 1) {
            dst_buffer_push_u8(buffer, ' ');
        }
    }
    dst_buffer_push_u8(buffer, 0);

    /* Convert to wide chars */
    wchar_t *sys_str = malloc(buffer->count * sizeof(wchar_t));
    if (NULL == sys_str) {
        DST_OUT_OF_MEMORY;
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
        DST_THROW(args, "could not create process");
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process. 
    if(!CreateProcess(NULL,
                sys_str,
                NULL,
                NULL,
                FALSE,
                0,
                NULL,
                NULL,
                &si,
                &pi)) {
        free(sys_str);
        DST_THROW(args, "could not create process");
    }
    free(sys_str);

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close process and thread handles. 
    WORD status = 0;
    GetExitCodeProcess(pi.hProcess, &status);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DST_RETURN_INTEGER(args, (int32_t)status);
}
#else
static int os_execute(DstArgs args) {
    DST_MINARITY(args, 1);
    const uint8_t **argv = malloc(sizeof(uint8_t *) * (args.n + 1));
    if (NULL == argv) {
        DST_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < args.n; i++) {
        DST_ARG_STRING(argv[i], args, i);
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

    DST_RETURN_INTEGER(args, status);
}
#endif

static int os_shell(DstArgs args) {
    int nofirstarg = (args.n < 1 || !dst_checktype(args.v[0], DST_STRING));
    const char *cmd = nofirstarg
        ? NULL
        : (const char *) dst_unwrap_string(args.v[0]);
    int stat = system(cmd);
    DST_RETURN(args, cmd
            ? dst_wrap_integer(stat)
            : dst_wrap_boolean(stat));
}

static int os_getenv(DstArgs args) {
    const uint8_t *k;
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(k, args, 0);
    const char *cstr = (const char *) k;
    const char *res = getenv(cstr);
    DST_RETURN(args, cstr
            ? dst_cstringv(res)
            : dst_wrap_nil());
}

static int os_setenv(DstArgs args) {
#ifdef DST_WINDOWS
#define SETENV(K,V) _putenv_s(K, V)
#define UNSETENV(K) _putenv_s(K, "")
#else
#define SETENV(K,V) setenv(K, V, 1)
#define UNSETENV(K) unsetenv(K)
#endif
    const uint8_t *k;
    const char *ks;
    DST_MAXARITY(args, 2);
    DST_MINARITY(args, 1);
    DST_ARG_STRING(k, args, 0);
    ks = (const char *) k;
    if (args.n == 1 || dst_checktype(args.v[1], DST_NIL)) {
        UNSETENV(ks);
    } else {
        const uint8_t *v;
        DST_ARG_STRING(v, args, 1);
        const char *vc = (const char *) v;
        SETENV(ks, vc);
    }
    return 0;
}

static int os_exit(DstArgs args) {
    DST_MAXARITY(args, 1);
    if (args.n == 0) {
        exit(EXIT_SUCCESS);
    } else if (dst_checktype(args.v[0], DST_INTEGER)) {
        exit(dst_unwrap_integer(args.v[0]));
    } else {
        exit(EXIT_FAILURE);
    }
    return 0;
}

/* Shim for windows */
#ifdef DST_WINDOWS
struct timespec {
    long tv_sec;
    long tv_nsec;
};
static int clock_gettime(int, struct timespec *spec) {
    int64 wintime;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    wintime -= 116444736000000000LL;  /* Windows epic is 1601, jan 1 */
    spec->tv_sec  = wintime / 10000000LL;
    /* Resolution is 100 nanoseconds. */
    spec->tv_nsec = wintime % 10000000LL * 100;
    return 0;
}
#endif

static int os_clock(DstArgs args) {
    DST_FIXARITY(args, 0);
    struct timespec tv;
    if (clock_gettime(CLOCK_REALTIME, &tv))
        DST_THROW(args, "could not get time");
    double dtime = tv.tv_sec + (tv.tv_nsec / 1E9);
    DST_RETURN_REAL(args, dtime);
}

static int os_sleep(DstArgs args) {
    double delay;
    DST_FIXARITY(args, 1);
    DST_ARG_NUMBER(delay, args, 0);
    if (delay < 0) {
        DST_THROW(args, "invalid argument to sleep");
    }
#ifdef DST_WINDOWS
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

static int os_cwd(DstArgs args) {
    DST_FIXARITY(args, 0);
    char buf[FILENAME_MAX];
    char *ptr;
#ifdef DST_WINDOWS
    ptr = _getcwd(buf, FILENAME_MAX);
#else
    ptr = getcwd(buf, FILENAME_MAX);
#endif
    if (NULL == ptr) {
        DST_THROW(args, "could not get current directory");
    }
    DST_RETURN_CSTRING(args, ptr);
}

static const DstReg cfuns[] = {
    {"os.execute", os_execute},
    {"os.shell", os_shell},
    {"os.exit", os_exit},
    {"os.getenv", os_getenv},
    {"os.setenv", os_setenv},
    {"os.clock", os_clock},
    {"os.sleep", os_sleep},
    {"os.cwd", os_cwd},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_os(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

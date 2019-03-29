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

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

#include <stdlib.h>

#ifndef JANET_REDUCED_OS

#include <time.h>

#ifdef JANET_WINDOWS
#include <Windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#endif

/* For macos */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#endif

/* Core OS functions */

/* Full OS functions */

static Janet os_which(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
#ifdef JANET_WINDOWS
    return janet_ckeywordv("windows");
#elif __APPLE__
    return janet_ckeywordv("macos");
#elif defined(__EMSCRIPTEN__)
    return janet_ckeywordv("web");
#else
    return janet_ckeywordv("posix");
#endif
}

static Janet os_exit(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    if (argc == 0) {
        exit(EXIT_SUCCESS);
    } else if (janet_checkint(argv[0])) {
        exit(janet_unwrap_integer(argv[0]));
    } else {
        exit(EXIT_FAILURE);
    }
    return janet_wrap_nil();
}

#ifdef JANET_REDUCED_OS
/* Provide a dud os/getenv so init.janet works, but nothing else */

static Janet os_getenv(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 1);
    return janet_wrap_nil();
}

#else
/* Provide full os functionality */

#ifdef JANET_WINDOWS
static Janet os_execute(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    JanetBuffer *buffer = janet_buffer(10);
    for (int32_t i = 0; i < argc; i++) {
        const uint8_t *argstring = janet_getstring(argv, i);
        janet_buffer_push_bytes(buffer, argstring, janet_string_length(argstring));
        if (i != argc - 1) {
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
        janet_panic("could not create process");
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process.
    if (!CreateProcess(NULL,
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
        janet_panic("could not create process");
    }
    free(sys_str);

    // Wait until child process exits.
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close process and thread handles.
    WORD status;
    GetExitCodeProcess(pi.hProcess, (LPDWORD)&status);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return janet_wrap_integer(status);
}
#else
static Janet os_execute(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    const char **child_argv = malloc(sizeof(char *) * (argc + 1));
    int status = 0;
    if (NULL == child_argv) {
        JANET_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < argc; i++) {
        child_argv[i] = janet_getcstring(argv, i);
    }
    child_argv[argc] = NULL;

    /* Fork child process */
    pid_t pid = fork();
    if (pid < 0) {
        janet_panic("failed to execute");
    } else if (pid == 0) {
        if (-1 == execve(child_argv[0], (char **)child_argv, NULL)) {
            exit(1);
        }
    } else {
        waitpid(pid, &status, 0);
    }
    free(child_argv);
    return janet_wrap_integer(status);
}
#endif

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

static Janet os_getenv(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *cstr = janet_getcstring(argv, 0);
    const char *res = getenv(cstr);
    return res
           ? janet_cstringv(res)
           : janet_wrap_nil();
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
    if (argc == 1 || janet_checktype(argv[1], JANET_NIL)) {
        UNSETENV(ks);
    } else {
        SETENV(ks, janet_getcstring(argv, 1));
    }
    return janet_wrap_nil();
}

static Janet os_time(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    double dtime = (double)(time(NULL));
    return janet_wrap_number(dtime);
}

/* Clock shims */
#ifdef JANET_WINDOWS
static int gettime(struct timespec *spec) {
    int64_t wintime = 0LL;
    GetSystemTimeAsFileTime((FILETIME *)&wintime);
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

static Janet os_clock(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 0);
    (void) argv;
    struct timespec tv;
    if (gettime(&tv)) janet_panic("could not get time");
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
    struct timespec ts;
    ts.tv_sec = (time_t) delay;
    ts.tv_nsec = (delay <= UINT32_MAX)
                 ? (long)((delay - ((uint32_t)delay)) * 1000000000)
                 : 0;
    nanosleep(&ts, NULL);
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

static Janet os_date(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    (void) argv;
    time_t t;
    struct tm *t_info;
    if (argc) {
        t = (time_t) janet_getinteger64(argv, 0);
    } else {
        time(&t);
    }
    t_info = localtime(&t);
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
    int res = ((argc == 3 && janet_getboolean(argv, 2)) ? symlink : link)(oldpath, newpath);
    if (res == -1) janet_panicv(janet_cstringv(strerror(errno)));
    return janet_wrap_integer(res);
#endif
}

static Janet os_mkdir(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
#ifdef JANET_WINDOWS
    int res = _mkdir(path);
#else
    int res = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH);
#endif
    return janet_wrap_boolean(res != -1);
}

static Janet os_cd(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const char *path = janet_getcstring(argv, 0);
    int res = chdir(path);
    return janet_wrap_boolean(res != -1);
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
    return janet_wrap_boolean(res != -1);
}

#endif /* JANET_REDUCED_OS */

static const JanetReg os_cfuns[] = {
    {
        "os/exit", os_exit,
        JDOC("(os/exit x)\n\n"
             "Exit from janet with an exit code equal to x. If x is not an integer, "
             "the exit with status equal the hash of x.")
    },
    {
        "os/which", os_which,
        JDOC("(os/which)\n\n"
             "Check the current operating system. Returns one of:\n\n"
             "\t:windows - Microsoft Windows\n"
             "\t:macos - Apple macos\n"
             "\t:posix - A POSIX compatible system (default)")
    },
    {
        "os/getenv", os_getenv,
        JDOC("(os/getenv variable)\n\n"
             "Get the string value of an environment variable.")
    },
#ifndef JANET_REDUCED_OS
    {
        "os/touch", os_touch,
        JDOC("(os/touch path [, actime [, modtime]])\n\n"
             "Update the access time and modification times for a file. By default, sets "
             "times to the current time.")
    },
    {
        "os/cd", os_cd,
        JDOC("(os/cd path)\n\n"
             "Change current directory to path. Returns true on success, false on failure.")
    },
    {
        "os/mkdir", os_mkdir,
        JDOC("(os/mkdir path)\n\n"
             "Create a new directory. The path will be relative to the current directory if relative, otherwise "
             "it will be an absolute path.")
    },
    {
        "os/link", os_link,
        JDOC("(os/link oldpath newpath [, symlink])\n\n"
             "Create a symlink from oldpath to newpath. The 3 optional paramater "
             "enables a hard link over a soft link. Does not work on Windows.")
    },
    {
        "os/execute", os_execute,
        JDOC("(os/execute program & args)\n\n"
             "Execute a program on the system and pass it string arguments. Returns "
             "the exit status of the program.")
    },
    {
        "os/shell", os_shell,
        JDOC("(os/shell str)\n\n"
             "Pass a command string str directly to the system shell.")
    },
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
        "os/date", os_date,
        JDOC("(os/date [,time])\n\n"
             "Returns the given time as a date struct, or the current time if no time is given. "
             "Returns a struct with following key values. Note that all numbers are 0-indexed.\n\n"
             "\t:seconds - number of seconds [0-61]\n"
             "\t:minutes - number of minutes [0-59]\n"
             "\t:seconds - number of hours [0-23]\n"
             "\t:month-day - day of month [0-30]\n"
             "\t:month - month of year [0, 11]\n"
             "\t:year - years since year 0 (e.g. 2019)\n"
             "\t:week-day - day of the week [0-6]\n"
             "\t:year-day - day of the year [0-365]\n"
             "\t:dst - If Day Light Savings is in effect")
    },
#endif
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_os(JanetTable *env) {
    janet_core_cfuns(env, NULL, os_cfuns);
}

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
#endif

static int os_execute(DstArgs args) {
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

static int os_clock(DstArgs args) {
    DST_FIXARITY(args, 0);
    clock_t time = clock();
    double dtime = time / (double) (CLOCKS_PER_SEC);
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
    usleep((useconds_t)(delay * 1000000));
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

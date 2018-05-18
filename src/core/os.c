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
#include <dst/dstcorelib.h>

#include <stdlib.h>
#include <time.h>

#ifdef DST_WINDOWS
#include <Windows.h>
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
    if (args.n != 1 || !dst_checktype(args.v[0], DST_STRING))
        DST_THROW(args, "expected string");
    const char *cstr = (const char *) dst_unwrap_string(args.v[0]);
    const char *res = getenv(cstr);
    DST_RETURN(args, cstr
            ? dst_cstringv(res)
            : dst_wrap_nil());
}

static int os_setenv(DstArgs args) {
    int t2;
    if (args.n < 2) DST_THROW(args, "expected 2 arguments");
    t2 = dst_type(args.v[1]);
    if (!dst_checktype(args.v[0], DST_STRING)
        || (t2 != DST_STRING && t2 != DST_NIL))
        DST_THROW(args, "expected string");
    const char *k = (const char *) dst_unwrap_string(args.v[0]);
#ifdef DST_WINDOWS
    if (t2 == DST_NIL) {
        // Investigate best way to delete env vars on windows. Use winapi?
        _putenv_s(k, "");
    } else {
        const char *v = (const char *) dst_unwrap_string(args.v[1]);
        _putenv_s(k, v);
    }
#else
    if (t2 == DST_NIL) {
        unsetenv(k);
    } else {
        const char *v = (const char *) dst_unwrap_string(args.v[1]);
        setenv(k, v, 1);
    }
#endif
    return 0;
}

static int os_exit(DstArgs args) {
    DST_MAXARITY(args, 1);
    if (args.n == 0) {
        exit(EXIT_SUCCESS);
    } else if (dst_checktype(args.v[0], DST_TRUE)
            || dst_checktype(args.v[0], DST_FALSE)) {
        exit(dst_unwrap_boolean(args.v[0]) ? EXIT_SUCCESS : EXIT_FAILURE);
        return 0;
    } else {
        exit(dst_hash(args.v[0]));
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
    int32_t delay;
    DST_FIXARITY(args, 1);
    DST_ARG_INTEGER(delay, args, 0);
    if (delay < 0) {
        DST_THROW(args, "invalid argument to sleep");
    }
#ifdef DST_WINDOWS
    Sleep(delay);
#else
    sleep((unsigned int) delay);
#endif
    return 0;
}

static const DstReg cfuns[] = {
    {"os.execute", os_execute},
    {"os.exit", os_exit},
    {"os.getenv", os_getenv},
    {"os.setenv", os_setenv},
    {"os.clock", os_clock},
    {"os.sleep", os_sleep},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_os(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

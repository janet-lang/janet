/*
* Copyright (c) 2017 Calvin Rose
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

static int os_execute(DstArgs args) {
    int nofirstarg = (args.n < 1 || !dst_checktype(args.v[0], DST_STRING));
    const char *cmd = nofirstarg 
        ? NULL
        : (const char *) dst_unwrap_string(args.v[0]);
    int stat = system(cmd);
    return dst_return(args, cmd
            ? dst_wrap_integer(stat)
            : dst_wrap_boolean(stat));
}

static int os_getenv(DstArgs args) {
    if (args.n != 1 || !dst_checktype(args.v[0], DST_STRING))
        return dst_throw(args, "expected string");
    const char *cstr = (const char *) dst_unwrap_string(args.v[0]);
    const char *res = getenv(cstr);
    return dst_return(args, cstr
            ? dst_cstringv(res)
            : dst_wrap_nil());
}

static int os_setenv(DstArgs args) {
    int t2;
    if (args.n < 2) return dst_throw(args, "expected 2 arguments");
    t2 = dst_type(args.v[1]);
    if (!dst_checktype(args.v[0], DST_STRING)
        || (t2 != DST_STRING && t2 != DST_NIL))
        return dst_throw(args, "expected string");
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

static const DstReg cfuns[] = {
    {"os-execute", os_execute},
    {"os-exit", os_exit},
    {"os-getenv", os_getenv},
    {"os-setenv", os_setenv},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_os(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

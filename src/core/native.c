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
#include <dst/dstcorelib.h>

/* Use LoadLibrary on windows or dlopen on posix to load dynamic libaries
 * with native code. */
#ifdef DST_WINDOWS
#include <windows.h>
typedef HINSTANCE Clib;
#define load_clib(name) LoadLibrary((name))
#define symbol_clib(lib, sym) GetProcAddress((lib), (sym))
#define error_clib() "could not load dynamic library"
#elif defined(DST_WEB)
#include <emscripten.h>
/* TODO - figure out how loading modules will work in JS */
typedef int Clib;
#define load_clib(name) 0
#define symbol_clib(lib, sym) 0
#define error_clib() "dynamic libraries not supported"
#else
#include <dlfcn.h>
typedef void *Clib;
#define load_clib(name) dlopen((name), RTLD_NOW)
#define symbol_clib(lib, sym) dlsym((lib), (sym))
#define error_clib() dlerror()
#endif

DstCFunction dst_native(const char *name, const uint8_t **error) {
    Clib lib = load_clib(name);
    DstCFunction init;
    if (!lib) {
        *error = dst_cstring(error_clib());
        return NULL;
    }
    init = (DstCFunction) symbol_clib(lib, "_dst_init");
    if (!init) {
        *error = dst_cstring("could not find _dst_init symbol");
        return NULL;
    }
    return init;
}

int dst_core_native(DstArgs args) {
    DstCFunction init;
    const uint8_t *error = NULL;
    const uint8_t *path = NULL;
    DST_FIXARITY(args, 1);
    DST_ARG_STRING(path, args, 0);
    init = dst_native((const char *)path, &error);
    if (!init) {
        DST_THROWV(args, dst_wrap_string(error));
    }
    DST_RETURN_CFUNCTION(args, init);
}

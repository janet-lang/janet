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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#endif

#include <stdio.h>
#include <errno.h>

#ifndef JANET_WINDOWS
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int cfun_io_gc(void *p, size_t len);
static int io_file_get(void *p, Janet key, Janet *out);
static void io_file_marshal(void *p, JanetMarshalContext *ctx);
static void *io_file_unmarshal(JanetMarshalContext *ctx);
static Janet io_file_next(void *p, Janet key);

#ifdef JANET_WINDOWS
#define ftell _ftelli64
#define fseek _fseeki64
#endif

const JanetAbstractType janet_file_type = {
    "core/file",
    cfun_io_gc,
    NULL,
    io_file_get,
    NULL,
    io_file_marshal,
    io_file_unmarshal,
    NULL, /* tostring */
    NULL, /* compare */
    NULL, /* hash */
    io_file_next,
    JANET_ATEND_NEXT
};

/* Check arguments to fopen */
static int32_t checkflags(const uint8_t *str) {
    int32_t flags = 0;
    int32_t i;
    int32_t len = janet_string_length(str);
    if (!len || len > 10)
        janet_panic("file mode must have a length between 1 and 10");
    switch (*str) {
        default:
            janet_panicf("invalid flag %c, expected w, a, or r", *str);
            break;
        case 'w':
            flags |= JANET_FILE_WRITE;
            janet_sandbox_assert(JANET_SANDBOX_FS_WRITE);
            break;
        case 'a':
            flags |= JANET_FILE_APPEND;
            janet_sandbox_assert(JANET_SANDBOX_FS);
            break;
        case 'r':
            flags |= JANET_FILE_READ;
            janet_sandbox_assert(JANET_SANDBOX_FS_READ);
            break;
    }
    for (i = 1; i < len; i++) {
        switch (str[i]) {
            default:
                janet_panicf("invalid flag %c, expected +, b, or n", str[i]);
                break;
            case '+':
                if (flags & JANET_FILE_UPDATE) return -1;
                janet_sandbox_assert(JANET_SANDBOX_FS_WRITE);
                flags |= JANET_FILE_UPDATE;
                break;
            case 'b':
                if (flags & JANET_FILE_BINARY) return -1;
                flags |= JANET_FILE_BINARY;
                break;
            case 'n':
                if (flags & JANET_FILE_NONIL) return -1;
                flags |= JANET_FILE_NONIL;
                break;
        }
    }
    return flags;
}

static void *makef(FILE *f, int32_t flags) {
    JanetFile *iof = (JanetFile *) janet_abstract(&janet_file_type, sizeof(JanetFile));
    iof->file = f;
    iof->flags = flags;
#ifndef JANET_WINDOWS
    /* While we would like fopen to set cloexec by default (like O_CLOEXEC) with the e flag, that is
     * not standard. */
    if (!(flags & JANET_FILE_NOT_CLOSEABLE))
        fcntl(fileno(f), F_SETFD, FD_CLOEXEC);
#endif
    return iof;
}

JANET_CORE_FN(cfun_io_temp,
              "(file/temp)",
              "Open an anonymous temporary file that is removed on close. "
              "Raises an error on failure.") {
    janet_sandbox_assert(JANET_SANDBOX_FS_TEMP);
    (void)argv;
    janet_fixarity(argc, 0);
    // XXX use mkostemp when we can to avoid CLOEXEC race.
    FILE *tmp = tmpfile();
    if (!tmp)
        janet_panicf("unable to create temporary file - %s", janet_strerror(errno));
    return janet_makefile(tmp, JANET_FILE_WRITE | JANET_FILE_READ | JANET_FILE_BINARY);
}

JANET_CORE_FN(cfun_io_fopen,
              "(file/open path &opt mode buffer-size)",
              "Open a file. `path` is an absolute or relative path, and "
              "`mode` is a set of flags indicating the mode to open the file in. "
              "`mode` is a keyword where each character represents a flag. If the file "
              "cannot be opened, returns nil, otherwise returns the new file handle. "
              "Mode flags:\n\n"
              "* r - allow reading from the file\n\n"
              "* w - allow writing to the file\n\n"
              "* a - append to the file\n\n"
              "Following one of the initial flags, 0 or more of the following flags can be appended:\n\n"
              "* b - open the file in binary mode (rather than text mode)\n\n"
              "* + - append to the file instead of overwriting it\n\n"
              "* n - error if the file cannot be opened instead of returning nil\n\n"
              "See fopen (<stdio.h>, C99) for further details.") {
    janet_arity(argc, 1, 3);
    const uint8_t *fname = janet_getstring(argv, 0);
    const uint8_t *fmode;
    int32_t flags;
    if (argc == 2) {
        fmode = janet_getkeyword(argv, 1);
        flags = checkflags(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        janet_sandbox_assert(JANET_SANDBOX_FS_READ);
        flags = JANET_FILE_READ;
    }
    FILE *f = fopen((const char *)fname, (const char *)fmode);
    if (f != NULL) {
#ifndef JANET_WINDOWS
        struct stat st;
        fstat(fileno(f), &st);
        if (S_ISDIR(st.st_mode)) {
            fclose(f);
            janet_panicf("cannot open directory: %s", fname);
        }
#endif
        size_t bufsize = janet_optsize(argv, argc, 2, BUFSIZ);
        if (bufsize != BUFSIZ) {
            int result = setvbuf(f, NULL, bufsize ? _IOFBF : _IONBF, bufsize);
            if (result) {
                janet_panic("failed to set buffer size for file");
            }
        }
    }
    return f ? janet_makefile(f, flags)
           : (flags & JANET_FILE_NONIL) ? (janet_panicf("failed to open file %s: %s", fname, janet_strerror(errno)), janet_wrap_nil())
           : janet_wrap_nil();
}

/* Read up to n bytes into buffer. */
static void read_chunk(JanetFile *iof, JanetBuffer *buffer, int32_t nBytesMax) {
    if (!(iof->flags & (JANET_FILE_READ | JANET_FILE_UPDATE)))
        janet_panic("file is not readable");
    janet_buffer_extra(buffer, nBytesMax);
    size_t ntoread = nBytesMax;
    size_t nread = fread((char *)(buffer->data + buffer->count), 1, ntoread, iof->file);
    if (nread != ntoread && ferror(iof->file))
        janet_panic("could not read file");
    buffer->count += (int32_t) nread;
}

/* Read a certain number of bytes into memory */
JANET_CORE_FN(cfun_io_fread,
              "(file/read f what &opt buf)",
              "Read a number of bytes from a file `f` into a buffer. A buffer `buf` can "
              "be provided as an optional third argument, otherwise a new buffer "
              "is created. `what` can either be an integer or a keyword. Returns the "
              "buffer with file contents. "
              "Values for `what`:\n\n"
              "* :all - read the whole file\n\n"
              "* :line - read up to and including the next newline character\n\n"
              "* n (integer) - read up to n bytes from the file") {
    janet_arity(argc, 2, 3);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    if (iof->flags & JANET_FILE_CLOSED) janet_panic("file is closed");
    JanetBuffer *buffer;
    if (argc == 2) {
        buffer = janet_buffer(0);
    } else {
        buffer = janet_getbuffer(argv, 2);
    }
    int32_t bufstart = buffer->count;
    if (janet_checktype(argv[1], JANET_KEYWORD)) {
        const uint8_t *sym = janet_unwrap_keyword(argv[1]);
        if (!janet_cstrcmp(sym, "all")) {
            int32_t sizeBefore;
            do {
                sizeBefore = buffer->count;
                read_chunk(iof, buffer, 4096);
            } while (sizeBefore < buffer->count);
            /* Never return nil for :all */
            return janet_wrap_buffer(buffer);
        } else if (!janet_cstrcmp(sym, "line")) {
            for (;;) {
                int x = fgetc(iof->file);
                if (x != EOF) janet_buffer_push_u8(buffer, (uint8_t)x);
                if (x == EOF || x == '\n') break;
            }
        } else {
            janet_panicf("expected one of :all, :line, got %v", argv[1]);
        }
    } else {
        int32_t len = janet_getinteger(argv, 1);
        if (len < 0) janet_panic("expected positive integer");
        read_chunk(iof, buffer, len);
    }
    if (bufstart == buffer->count) return janet_wrap_nil();
    return janet_wrap_buffer(buffer);
}

/* Write bytes to a file */
JANET_CORE_FN(cfun_io_fwrite,
              "(file/write f bytes)",
              "Writes to a file. 'bytes' must be string, buffer, or symbol. Returns the "
              "file.") {
    janet_arity(argc, 1, -1);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    if (iof->flags & JANET_FILE_CLOSED)
        janet_panic("file is closed");
    if (!(iof->flags & (JANET_FILE_WRITE | JANET_FILE_APPEND | JANET_FILE_UPDATE)))
        janet_panic("file is not writeable");
    int32_t i;
    /* Verify all arguments before writing to file */
    for (i = 1; i < argc; i++)
        janet_getbytes(argv, i);
    for (i = 1; i < argc; i++) {
        JanetByteView view = janet_getbytes(argv, i);
        if (view.len) {
            if (!fwrite(view.bytes, view.len, 1, iof->file)) {
                janet_panic("error writing to file");
            }
        }
    }
    return argv[0];
}

static void io_assert_writeable(JanetFile *iof) {
    if (iof->flags & JANET_FILE_CLOSED)
        janet_panic("file is closed");
    if (!(iof->flags & (JANET_FILE_WRITE | JANET_FILE_APPEND | JANET_FILE_UPDATE)))
        janet_panic("file is not writeable");
}

/* Flush the bytes in the file */
JANET_CORE_FN(cfun_io_fflush,
              "(file/flush f)",
              "Flush any buffered bytes to the file system. In most files, writes are "
              "buffered for efficiency reasons. Returns the file handle.") {
    janet_fixarity(argc, 1);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    io_assert_writeable(iof);
    if (fflush(iof->file))
        janet_panic("could not flush file");
    return argv[0];
}

#ifdef JANET_WINDOWS
#define WEXITSTATUS(x) x
#endif

/* For closing files from C API */
int janet_file_close(JanetFile *file) {
    int ret = 0;
    if (!(file->flags & (JANET_FILE_NOT_CLOSEABLE | JANET_FILE_CLOSED))) {
        ret = fclose(file->file);
        file->flags |= JANET_FILE_CLOSED;
        file->file = NULL; /* NULL dereference is easier to debug then other problems */
        return ret;
    }
    return 0;
}

/* Cleanup a file */
static int cfun_io_gc(void *p, size_t len) {
    (void) len;
    JanetFile *iof = (JanetFile *)p;
    janet_file_close(iof);
    return 0;
}

/* Close a file */
JANET_CORE_FN(cfun_io_fclose,
              "(file/close f)",
              "Close a file and release all related resources. When you are "
              "done reading a file, close it to prevent a resource leak and let "
              "other processes read the file.") {
    janet_fixarity(argc, 1);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    if (iof->flags & JANET_FILE_CLOSED)
        return janet_wrap_nil();
    if (iof->flags & (JANET_FILE_NOT_CLOSEABLE))
        janet_panic("file not closable");
    if (fclose(iof->file)) {
        iof->flags |= JANET_FILE_NOT_CLOSEABLE;
        janet_panic("could not close file");
    }
    iof->flags |= JANET_FILE_CLOSED;
    return janet_wrap_nil();
}

/* Seek a file */
JANET_CORE_FN(cfun_io_fseek,
              "(file/seek f &opt whence n)",
              "Jump to a relative location in the file `f`. `whence` must be one of:\n\n"
              "* :cur - jump relative to the current file location\n\n"
              "* :set - jump relative to the beginning of the file\n\n"
              "* :end - jump relative to the end of the file\n\n"
              "By default, `whence` is :cur. Optionally a value `n` may be passed "
              "for the relative number of bytes to seek in the file. `n` may be a real "
              "number to handle large files of more than 4GB. Returns the file handle.") {
    janet_arity(argc, 2, 3);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    if (iof->flags & JANET_FILE_CLOSED)
        janet_panic("file is closed");
    int64_t offset = 0;
    int whence = SEEK_CUR;
    if (argc >= 2) {
        const uint8_t *whence_sym = janet_getkeyword(argv, 1);
        if (!janet_cstrcmp(whence_sym, "cur")) {
            whence = SEEK_CUR;
        } else if (!janet_cstrcmp(whence_sym, "set")) {
            whence = SEEK_SET;
        } else if (!janet_cstrcmp(whence_sym, "end")) {
            whence = SEEK_END;
        } else {
            janet_panicf("expected one of :cur, :set, :end, got %v", argv[1]);
        }
        if (argc == 3) {
            offset = (int64_t) janet_getinteger64(argv, 2);
        }
    }
    if (fseek(iof->file, offset, whence)) janet_panic("error seeking file");
    return argv[0];
}

JANET_CORE_FN(cfun_io_ftell,
              "(file/tell f)",
              "Get the current value of the file position for file `f`.") {
    janet_fixarity(argc, 1);
    JanetFile *iof = janet_getabstract(argv, 0, &janet_file_type);
    if (iof->flags & JANET_FILE_CLOSED)
        janet_panic("file is closed");
    int64_t pos = ftell(iof->file);
    if (pos == -1) janet_panic("error getting position in file");
    return janet_wrap_number((double)pos);
}

static JanetMethod io_file_methods[] = {
    {"close", cfun_io_fclose},
    {"flush", cfun_io_fflush},
    {"read", cfun_io_fread},
    {"seek", cfun_io_fseek},
    {"tell", cfun_io_ftell},
    {"write", cfun_io_fwrite},
    {NULL, NULL}
};

static int io_file_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        return 0;
    return janet_getmethod(janet_unwrap_keyword(key), io_file_methods, out);
}

static Janet io_file_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(io_file_methods, key);
}

static void io_file_marshal(void *p, JanetMarshalContext *ctx) {
    JanetFile *iof = (JanetFile *)p;
    if (ctx->flags & JANET_MARSHAL_UNSAFE) {
        janet_marshal_abstract(ctx, p);
#ifdef JANET_WINDOWS
        janet_marshal_int(ctx, _fileno(iof->file));
#else
        janet_marshal_int(ctx, fileno(iof->file));
#endif
        janet_marshal_int(ctx, iof->flags);
    } else {
        janet_panic("cannot marshal file in safe mode");
    }
}

static void *io_file_unmarshal(JanetMarshalContext *ctx) {
    if (ctx->flags & JANET_MARSHAL_UNSAFE) {
        JanetFile *iof = janet_unmarshal_abstract(ctx, sizeof(JanetFile));
        int32_t fd = janet_unmarshal_int(ctx);
        int32_t flags = janet_unmarshal_int(ctx);
        char fmt[4] = {0};
        int index = 0;
        if (flags & JANET_FILE_READ) fmt[index++] = 'r';
        if (flags & JANET_FILE_APPEND) {
            fmt[index++] = 'a';
        } else if (flags & JANET_FILE_WRITE) {
            fmt[index++] = 'w';
        }
#ifdef JANET_WINDOWS
        iof->file = _fdopen(fd, fmt);
#else
        iof->file = fdopen(fd, fmt);
#endif
        if (iof->file == NULL) {
            iof->flags = JANET_FILE_CLOSED;
        } else {
            iof->flags = flags;
        }
        return iof;
    } else {
        janet_panic("cannot unmarshal file in safe mode");
    }
}

FILE *janet_dynfile(const char *name, FILE *def) {
    Janet x = janet_dyn(name);
    if (!janet_checktype(x, JANET_ABSTRACT)) return def;
    void *abstract = janet_unwrap_abstract(x);
    if (janet_abstract_type(abstract) != &janet_file_type) return def;
    JanetFile *iofile = abstract;
    return iofile->file;
}

static Janet cfun_io_print_impl_x(int32_t argc, Janet *argv, int newline,
                                  FILE *dflt_file, int32_t offset, Janet x) {
    FILE *f;
    switch (janet_type(x)) {
        default:
            janet_panicf("cannot print to %v", x);
        case JANET_BUFFER: {
            /* Special case buffer */
            JanetBuffer *buf = janet_unwrap_buffer(x);
            for (int32_t i = offset; i < argc; ++i) {
                janet_to_string_b(buf, argv[i]);
            }
            if (newline)
                janet_buffer_push_u8(buf, '\n');
            return janet_wrap_nil();
        }
        case JANET_FUNCTION: {
            /* Special case function */
            JanetFunction *fun = janet_unwrap_function(x);
            JanetBuffer *buf = janet_buffer(0);
            for (int32_t i = offset; i < argc; ++i) {
                janet_to_string_b(buf, argv[i]);
            }
            if (newline)
                janet_buffer_push_u8(buf, '\n');
            Janet args[1] = { janet_wrap_buffer(buf) };
            janet_call(fun, 1, args);
            return janet_wrap_nil();
        }
        case JANET_NIL:
            f = dflt_file;
            if (f == NULL) janet_panic("cannot print to nil");
            break;
        case JANET_ABSTRACT: {
            void *abstract = janet_unwrap_abstract(x);
            if (janet_abstract_type(abstract) != &janet_file_type)
                return janet_wrap_nil();
            JanetFile *iofile = abstract;
            io_assert_writeable(iofile);
            f = iofile->file;
            break;
        }
    }
    for (int32_t i = offset; i < argc; ++i) {
        int32_t len;
        const uint8_t *vstr;
        if (janet_checktype(argv[i], JANET_BUFFER)) {
            JanetBuffer *b = janet_unwrap_buffer(argv[i]);
            vstr = b->data;
            len = b->count;
        } else {
            vstr = janet_to_string(argv[i]);
            len = janet_string_length(vstr);
        }
        if (len) {
            if (1 != fwrite(vstr, len, 1, f)) {
                if (f == dflt_file) {
                    janet_panicf("cannot print %d bytes", len);
                } else {
                    janet_panicf("cannot print %d bytes to %v", len, x);
                }
            }
        }
    }
    if (newline)
        putc('\n', f);
    return janet_wrap_nil();
}

static Janet cfun_io_print_impl(int32_t argc, Janet *argv,
                                int newline, const char *name, FILE *dflt_file) {
    Janet x = janet_dyn(name);
    return cfun_io_print_impl_x(argc, argv, newline, dflt_file, 0, x);
}

JANET_CORE_FN(cfun_io_print,
              "(print & xs)",
              "Print values to the console (standard out). Value are converted "
              "to strings if they are not already. After printing all values, a "
              "newline character is printed. Use the value of `(dyn :out stdout)` to determine "
              "what to push characters to. Expects `(dyn :out stdout)` to be either a core/file or "
              "a buffer. Returns nil.") {
    return cfun_io_print_impl(argc, argv, 1, "out", stdout);
}

JANET_CORE_FN(cfun_io_prin,
              "(prin & xs)",
              "Same as `print`, but does not add trailing newline.") {
    return cfun_io_print_impl(argc, argv, 0, "out", stdout);
}

JANET_CORE_FN(cfun_io_eprint,
              "(eprint & xs)",
              "Same as `print`, but uses `(dyn :err stderr)` instead of `(dyn :out stdout)`.") {
    return cfun_io_print_impl(argc, argv, 1, "err", stderr);
}

JANET_CORE_FN(cfun_io_eprin,
              "(eprin & xs)",
              "Same as `prin`, but uses `(dyn :err stderr)` instead of `(dyn :out stdout)`.") {
    return cfun_io_print_impl(argc, argv, 0, "err", stderr);
}

JANET_CORE_FN(cfun_io_xprint,
              "(xprint to & xs)",
              "Print to a file or other value explicitly (no dynamic bindings) with a trailing "
              "newline character. The value to print "
              "to is the first argument, and is otherwise the same as `print`. Returns nil.") {
    janet_arity(argc, 1, -1);
    return cfun_io_print_impl_x(argc, argv, 1, NULL, 1, argv[0]);
}

JANET_CORE_FN(cfun_io_xprin,
              "(xprin to & xs)",
              "Print to a file or other value explicitly (no dynamic bindings). The value to print "
              "to is the first argument, and is otherwise the same as `prin`. Returns nil.") {
    janet_arity(argc, 1, -1);
    return cfun_io_print_impl_x(argc, argv, 0, NULL, 1, argv[0]);
}

static Janet cfun_io_printf_impl_x(int32_t argc, Janet *argv, int newline,
                                   FILE *dflt_file, int32_t offset, Janet x) {
    FILE *f;
    const char *fmt = janet_getcstring(argv, offset);
    switch (janet_type(x)) {
        default:
            janet_panicf("cannot print to %v", x);
        case JANET_BUFFER: {
            /* Special case buffer */
            JanetBuffer *buf = janet_unwrap_buffer(x);
            janet_buffer_format(buf, fmt, offset, argc, argv);
            if (newline) janet_buffer_push_u8(buf, '\n');
            return janet_wrap_nil();
        }
        case JANET_FUNCTION: {
            /* Special case function */
            JanetFunction *fun = janet_unwrap_function(x);
            JanetBuffer *buf = janet_buffer(0);
            janet_buffer_format(buf, fmt, offset, argc, argv);
            if (newline) janet_buffer_push_u8(buf, '\n');
            Janet args[1] = { janet_wrap_buffer(buf) };
            janet_call(fun, 1, args);
            return janet_wrap_nil();
        }
        case JANET_NIL:
            f = dflt_file;
            if (f == NULL) janet_panic("cannot print to nil");
            break;
        case JANET_ABSTRACT: {
            void *abstract = janet_unwrap_abstract(x);
            if (janet_abstract_type(abstract) != &janet_file_type)
                return janet_wrap_nil();
            JanetFile *iofile = abstract;
            if (iofile->flags & JANET_FILE_CLOSED) {
                janet_panic("cannot print to closed file");
            }
            io_assert_writeable(iofile);
            f = iofile->file;
            break;
        }
    }
    JanetBuffer *buf = janet_buffer(10);
    janet_buffer_format(buf, fmt, offset, argc, argv);
    if (newline) janet_buffer_push_u8(buf, '\n');
    if (buf->count) {
        if (1 != fwrite(buf->data, buf->count, 1, f)) {
            janet_panicf("could not print %d bytes to file", buf->count);
        }
    }
    /* Clear buffer to make things easier for GC */
    buf->count = 0;
    buf->capacity = 0;
    janet_free(buf->data);
    buf->data = NULL;
    return janet_wrap_nil();
}

static Janet cfun_io_printf_impl(int32_t argc, Janet *argv, int newline,
                                 const char *name, FILE *dflt_file) {
    janet_arity(argc, 1, -1);
    Janet x = janet_dyn(name);
    return cfun_io_printf_impl_x(argc, argv, newline, dflt_file, 0, x);

}

JANET_CORE_FN(cfun_io_printf,
              "(printf fmt & xs)",
              "Prints output formatted as if with `(string/format fmt ;xs)` to `(dyn :out stdout)` with a trailing newline.") {
    return cfun_io_printf_impl(argc, argv, 1, "out", stdout);
}

JANET_CORE_FN(cfun_io_prinf,
              "(prinf fmt & xs)",
              "Like `printf` but with no trailing newline.") {
    return cfun_io_printf_impl(argc, argv, 0, "out", stdout);
}

JANET_CORE_FN(cfun_io_eprintf,
              "(eprintf fmt & xs)",
              "Prints output formatted as if with `(string/format fmt ;xs)` to `(dyn :err stderr)` with a trailing newline.") {
    return cfun_io_printf_impl(argc, argv, 1, "err", stderr);
}

JANET_CORE_FN(cfun_io_eprinf,
              "(eprinf fmt & xs)",
              "Like `eprintf` but with no trailing newline.") {
    return cfun_io_printf_impl(argc, argv, 0, "err", stderr);
}

JANET_CORE_FN(cfun_io_xprintf,
              "(xprintf to fmt & xs)",
              "Like `printf` but prints to an explicit file or value `to`. Returns nil.") {
    janet_arity(argc, 2, -1);
    return cfun_io_printf_impl_x(argc, argv, 1, NULL, 1, argv[0]);
}

JANET_CORE_FN(cfun_io_xprinf,
              "(xprinf to fmt & xs)",
              "Like `prinf` but prints to an explicit file or value `to`. Returns nil.") {
    janet_arity(argc, 2, -1);
    return cfun_io_printf_impl_x(argc, argv, 0, NULL, 1, argv[0]);
}

static void janet_flusher(const char *name, FILE *dflt_file) {
    Janet x = janet_dyn(name);
    switch (janet_type(x)) {
        default:
            break;
        case JANET_NIL:
            fflush(dflt_file);
            break;
        case JANET_ABSTRACT: {
            void *abstract = janet_unwrap_abstract(x);
            if (janet_abstract_type(abstract) != &janet_file_type) break;
            JanetFile *iofile = abstract;
            fflush(iofile->file);
            break;
        }
    }
}

JANET_CORE_FN(cfun_io_flush,
              "(flush)",
              "Flush `(dyn :out stdout)` if it is a file, otherwise do nothing.") {
    janet_fixarity(argc, 0);
    (void) argv;
    janet_flusher("out", stdout);
    return janet_wrap_nil();
}

JANET_CORE_FN(cfun_io_eflush,
              "(eflush)",
              "Flush `(dyn :err stderr)` if it is a file, otherwise do nothing.") {
    janet_fixarity(argc, 0);
    (void) argv;
    janet_flusher("err", stderr);
    return janet_wrap_nil();
}

void janet_dynprintf(const char *name, FILE *dflt_file, const char *format, ...) {
    va_list args;
    va_start(args, format);
    Janet x = janet_dyn(name);
    JanetType xtype = janet_type(x);
    switch (xtype) {
        default:
            /* Other values simply do nothing */
            break;
        case JANET_NIL:
        case JANET_ABSTRACT: {
            FILE *f = dflt_file;
            JanetBuffer buffer;
            int32_t len = 0;
            while (format[len]) len++;
            janet_buffer_init(&buffer, len);
            janet_formatbv(&buffer, format, args);
            if (xtype == JANET_ABSTRACT) {
                void *abstract = janet_unwrap_abstract(x);
                if (janet_abstract_type(abstract) != &janet_file_type)
                    break;
                JanetFile *iofile = abstract;
                io_assert_writeable(iofile);
                f = iofile->file;
            }
            fwrite(buffer.data, buffer.count, 1, f);
            janet_buffer_deinit(&buffer);
            break;
        }
        case JANET_FUNCTION: {
            JanetFunction *fun = janet_unwrap_function(x);
            int32_t len = 0;
            while (format[len]) len++;
            JanetBuffer *buf = janet_buffer(len);
            janet_formatbv(buf, format, args);
            Janet args[1] = { janet_wrap_buffer(buf) };
            janet_call(fun, 1, args);
            break;
        }
        case JANET_BUFFER:
            janet_formatbv(janet_unwrap_buffer(x), format, args);
            break;
    }
    va_end(args);
    return;
}

/* C API */

JanetFile *janet_getjfile(const Janet *argv, int32_t n) {
    return janet_getabstract(argv, n, &janet_file_type);
}

FILE *janet_getfile(const Janet *argv, int32_t n, int32_t *flags) {
    JanetFile *iof = janet_getabstract(argv, n, &janet_file_type);
    if (NULL != flags) *flags = iof->flags;
    return iof->file;
}

JanetFile *janet_makejfile(FILE *f, int32_t flags) {
    return makef(f, flags);
}

Janet janet_makefile(FILE *f, int32_t flags) {
    return janet_wrap_abstract(makef(f, flags));
}

JanetAbstract janet_checkfile(Janet j) {
    return janet_checkabstract(j, &janet_file_type);
}

FILE *janet_unwrapfile(Janet j, int32_t *flags) {
    JanetFile *iof = janet_unwrap_abstract(j);
    if (NULL != flags) *flags = iof->flags;
    return iof->file;
}

/* Module entry point */
void janet_lib_io(JanetTable *env) {
    JanetRegExt io_cfuns[] = {
        JANET_CORE_REG("print", cfun_io_print),
        JANET_CORE_REG("prin", cfun_io_prin),
        JANET_CORE_REG("printf", cfun_io_printf),
        JANET_CORE_REG("prinf", cfun_io_prinf),
        JANET_CORE_REG("eprin", cfun_io_eprin),
        JANET_CORE_REG("eprint", cfun_io_eprint),
        JANET_CORE_REG("eprintf", cfun_io_eprintf),
        JANET_CORE_REG("eprinf", cfun_io_eprinf),
        JANET_CORE_REG("xprint", cfun_io_xprint),
        JANET_CORE_REG("xprin", cfun_io_xprin),
        JANET_CORE_REG("xprintf", cfun_io_xprintf),
        JANET_CORE_REG("xprinf", cfun_io_xprinf),
        JANET_CORE_REG("flush", cfun_io_flush),
        JANET_CORE_REG("eflush", cfun_io_eflush),
        JANET_CORE_REG("file/temp", cfun_io_temp),
        JANET_CORE_REG("file/open", cfun_io_fopen),
        JANET_CORE_REG("file/close", cfun_io_fclose),
        JANET_CORE_REG("file/read", cfun_io_fread),
        JANET_CORE_REG("file/write", cfun_io_fwrite),
        JANET_CORE_REG("file/flush", cfun_io_fflush),
        JANET_CORE_REG("file/seek", cfun_io_fseek),
        JANET_CORE_REG("file/tell", cfun_io_ftell),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, io_cfuns);
    janet_register_abstract_type(&janet_file_type);
    int default_flags = JANET_FILE_NOT_CLOSEABLE | JANET_FILE_SERIALIZABLE;
    /* stdout */
    JANET_CORE_DEF(env, "stdout",
                   janet_makefile(stdout, JANET_FILE_APPEND | default_flags),
                   "The standard output file.");
    /* stderr */
    JANET_CORE_DEF(env, "stderr",
                   janet_makefile(stderr, JANET_FILE_APPEND | default_flags),
                   "The standard error file.");
    /* stdin */
    JANET_CORE_DEF(env, "stdin",
                   janet_makefile(stdin, JANET_FILE_READ | default_flags),
                   "The standard input file.");

}

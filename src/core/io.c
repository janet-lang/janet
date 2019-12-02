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

/* Compiler feature test macros for things */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <errno.h>

#ifndef JANET_AMALG
#include <janet.h>
#include "util.h"
#endif

#ifndef JANET_WINDOWS
#include <sys/wait.h>
#endif

#define IO_WRITE 1
#define IO_READ 2
#define IO_APPEND 4
#define IO_UPDATE 8
#define IO_NOT_CLOSEABLE 16
#define IO_CLOSED 32
#define IO_BINARY 64
#define IO_SERIALIZABLE 128
#define IO_PIPED 256

typedef struct IOFile IOFile;
struct IOFile {
    FILE *file;
    int flags;
};

static int cfun_io_gc(void *p, size_t len);
static Janet io_file_get(void *p, Janet);

JanetAbstractType cfun_io_filetype = {
    "core/file",
    cfun_io_gc,
    NULL,
    io_file_get,
    NULL,
    NULL,
    NULL,
    NULL
};

/* Check arguments to fopen */
static int checkflags(const uint8_t *str) {
    int flags = 0;
    int32_t i;
    int32_t len = janet_string_length(str);
    if (!len || len > 3)
        janet_panic("file mode must have a length between 1 and 3");
    switch (*str) {
        default:
            janet_panicf("invalid flag %c, expected w, a, or r", *str);
            break;
        case 'w':
            flags |= IO_WRITE;
            break;
        case 'a':
            flags |= IO_APPEND;
            break;
        case 'r':
            flags |= IO_READ;
            break;
    }
    for (i = 1; i < len; i++) {
        switch (str[i]) {
            default:
                janet_panicf("invalid flag %c, expected + or b", str[i]);
                break;
            case '+':
                if (flags & IO_UPDATE) return -1;
                flags |= IO_UPDATE;
                break;
            case 'b':
                if (flags & IO_BINARY) return -1;
                flags |= IO_BINARY;
                break;
        }
    }
    return flags;
}

static Janet makef(FILE *f, int flags) {
    IOFile *iof = (IOFile *) janet_abstract(&cfun_io_filetype, sizeof(IOFile));
    iof->file = f;
    iof->flags = flags;
    return janet_wrap_abstract(iof);
}

/* Open a process */
#ifdef __EMSCRIPTEN__
static Janet cfun_io_popen(int32_t argc, Janet *argv) {
    (void) argc;
    (void) argv;
    janet_panic("not implemented on this platform");
    return janet_wrap_nil();
}
#else
static Janet cfun_io_popen(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const uint8_t *fname = janet_getstring(argv, 0);
    const uint8_t *fmode = NULL;
    int flags;
    if (argc == 2) {
        fmode = janet_getkeyword(argv, 1);
        if (janet_string_length(fmode) != 1 ||
                !(fmode[0] == 'r' || fmode[0] == 'w')) {
            janet_panicf("invalid file mode :%S, expected :r or :w", fmode);
        }
        flags = IO_PIPED | (fmode[0] == 'r' ? IO_READ : IO_WRITE);
    } else {
        fmode = (const uint8_t *)"r";
        flags = IO_PIPED | IO_READ;
    }
#ifdef JANET_WINDOWS
#define popen _popen
#endif
    FILE *f = popen((const char *)fname, (const char *)fmode);
    if (!f) {
        return janet_wrap_nil();
    }
    return makef(f, flags);
}
#endif

static Janet cfun_io_fopen(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const uint8_t *fname = janet_getstring(argv, 0);
    const uint8_t *fmode;
    int flags;
    if (argc == 2) {
        fmode = janet_getkeyword(argv, 1);
        flags = checkflags(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        flags = IO_READ;
    }
    FILE *f = fopen((const char *)fname, (const char *)fmode);
    return f ? makef(f, flags) : janet_wrap_nil();
}

static Janet cfun_io_fdopen(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const int fd = janet_getinteger(argv, 0);
    const uint8_t *fmode;
    int flags;
    if (argc == 2) {
        fmode = janet_getkeyword(argv, 1);
        flags = checkflags(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        flags = IO_READ;
    }
#ifdef JANET_WINDOWS
#define fdopen _fdopen
#endif
    FILE *f = fdopen(fd, (const char *)fmode);
    return f ? makef(f, flags) : janet_wrap_nil();
}

static Janet cfun_io_fileno(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
#ifdef JANET_WINDOWS
#define fileno _fileno
#endif
    return janet_wrap_integer(fileno(iof->file));
}

/* Read up to n bytes into buffer. */
static void read_chunk(IOFile *iof, JanetBuffer *buffer, int32_t nBytesMax) {
    if (!(iof->flags & (IO_READ | IO_UPDATE)))
        janet_panic("file is not readable");
    janet_buffer_extra(buffer, nBytesMax);
    size_t ntoread = nBytesMax;
    size_t nread = fread((char *)(buffer->data + buffer->count), 1, ntoread, iof->file);
    if (nread != ntoread && ferror(iof->file))
        janet_panic("could not read file");
    buffer->count += (int32_t) nread;
}

/* Read a certain number of bytes into memory */
static Janet cfun_io_fread(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED) janet_panic("file is closed");
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
static Janet cfun_io_fwrite(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
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

/* Flush the bytes in the file */
static Janet cfun_io_fflush(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        janet_panic("file is not writeable");
    if (fflush(iof->file))
        janet_panic("could not flush file");
    return argv[0];
}

/* Cleanup a file */
static int cfun_io_gc(void *p, size_t len) {
    (void) len;
    IOFile *iof = (IOFile *)p;
    if (!(iof->flags & (IO_NOT_CLOSEABLE | IO_CLOSED))) {
        return fclose(iof->file);
    }
    return 0;
}

/* Close a file */
static Janet cfun_io_fclose(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    if (iof->flags & (IO_NOT_CLOSEABLE))
        janet_panic("file not closable");
    if (iof->flags & IO_PIPED) {
#ifdef JANET_WINDOWS
#define pclose _pclose
#define WEXITSTATUS(x) x
#endif
        int status = pclose(iof->file);
        iof->flags |= IO_CLOSED;
        if (status == -1) janet_panic("could not close file");
        return janet_wrap_integer(WEXITSTATUS(status));
    } else {
        if (fclose(iof->file)) janet_panic("could not close file");
        iof->flags |= IO_CLOSED;
        return janet_wrap_nil();
    }
}

/* Seek a file */
static Janet cfun_io_fseek(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    IOFile *iof = janet_getabstract(argv, 0, &cfun_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    long int offset = 0;
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
            offset = (long) janet_getinteger64(argv, 2);
        }
    }
    if (fseek(iof->file, offset, whence)) janet_panic("error seeking file");
    return argv[0];
}

static JanetMethod io_file_methods[] = {
    {"close", cfun_io_fclose},
    {"fileno", cfun_io_fileno},
    {"flush", cfun_io_fflush},
    {"read", cfun_io_fread},
    {"seek", cfun_io_fseek},
    {"write", cfun_io_fwrite},
    {NULL, NULL}
};

static Janet io_file_get(void *p, Janet key) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD))
        janet_panicf("expected keyword, got %v", key);
    return janet_getmethod(janet_unwrap_keyword(key), io_file_methods);
}

FILE *janet_dynfile(const char *name, FILE *def) {
    Janet x = janet_dyn(name);
    if (!janet_checktype(x, JANET_ABSTRACT)) return def;
    void *abstract = janet_unwrap_abstract(x);
    if (janet_abstract_type(abstract) != &cfun_io_filetype) return def;
    IOFile *iofile = abstract;
    return iofile->file;
}

static Janet cfun_io_print_impl(int32_t argc, Janet *argv,
                                int newline, const char *name, FILE *dflt_file) {
    FILE *f;
    Janet x = janet_dyn(name);
    switch (janet_type(x)) {
        default:
            /* Other values simply do nothing */
            return janet_wrap_nil();
        case JANET_BUFFER: {
            /* Special case buffer */
            JanetBuffer *buf = janet_unwrap_buffer(x);
            for (int32_t i = 0; i < argc; ++i) {
                janet_to_string_b(buf, argv[i]);
            }
            if (newline)
                janet_buffer_push_u8(buf, '\n');
            return janet_wrap_nil();
        }
        case JANET_NIL:
            f = dflt_file;
            break;
        case JANET_ABSTRACT: {
            void *abstract = janet_unwrap_abstract(x);
            if (janet_abstract_type(abstract) != &cfun_io_filetype)
                return janet_wrap_nil();
            IOFile *iofile = abstract;
            f = iofile->file;
            break;
        }
    }
    for (int32_t i = 0; i < argc; ++i) {
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
                janet_panicf("could not print %d bytes to (dyn :%s)", len, name);
            }
        }
    }
    if (newline)
        putc('\n', f);
    return janet_wrap_nil();
}

static Janet cfun_io_print(int32_t argc, Janet *argv) {
    return cfun_io_print_impl(argc, argv, 1, "out", stdout);
}

static Janet cfun_io_prin(int32_t argc, Janet *argv) {
    return cfun_io_print_impl(argc, argv, 0, "out", stdout);
}

static Janet cfun_io_eprint(int32_t argc, Janet *argv) {
    return cfun_io_print_impl(argc, argv, 1, "err", stderr);
}

static Janet cfun_io_eprin(int32_t argc, Janet *argv) {
    return cfun_io_print_impl(argc, argv, 0, "err", stderr);
}

static Janet cfun_io_printf_impl(int32_t argc, Janet *argv, int newline,
                                 const char *name, FILE *dflt_file) {
    FILE *f;
    janet_arity(argc, 1, -1);
    const char *fmt = janet_getcstring(argv, 0);
    Janet x = janet_dyn(name);
    switch (janet_type(x)) {
        default:
            /* Other values simply do nothing */
            return janet_wrap_nil();
        case JANET_BUFFER: {
            /* Special case buffer */
            JanetBuffer *buf = janet_unwrap_buffer(x);
            janet_buffer_format(buf, fmt, 0, argc, argv);
            if (newline) janet_buffer_push_u8(buf, '\n');
            return janet_wrap_nil();
        }
        case JANET_NIL:
            f = dflt_file;
            break;
        case JANET_ABSTRACT: {
            void *abstract = janet_unwrap_abstract(x);
            if (janet_abstract_type(abstract) != &cfun_io_filetype)
                return janet_wrap_nil();
            IOFile *iofile = abstract;
            f = iofile->file;
            break;
        }
    }
    JanetBuffer *buf = janet_buffer(10);
    janet_buffer_format(buf, fmt, 0, argc, argv);
    if (newline) janet_buffer_push_u8(buf, '\n');
    if (buf->count) {
        if (1 != fwrite(buf->data, buf->count, 1, f)) {
            janet_panicf("could not print %d bytes to file", buf->count, name);
        }
    }
    /* Clear buffer to make things easier for GC */
    buf->count = 0;
    buf->capacity = 0;
    free(buf->data);
    buf->data = NULL;
    return janet_wrap_nil();
}

static Janet cfun_io_printf(int32_t argc, Janet *argv) {
    return cfun_io_printf_impl(argc, argv, 1, "out", stdout);
}

static Janet cfun_io_prinf(int32_t argc, Janet *argv) {
    return cfun_io_printf_impl(argc, argv, 0, "out", stdout);
}

static Janet cfun_io_eprintf(int32_t argc, Janet *argv) {
    return cfun_io_printf_impl(argc, argv, 1, "err", stderr);
}

static Janet cfun_io_eprinf(int32_t argc, Janet *argv) {
    return cfun_io_printf_impl(argc, argv, 0, "err", stderr);
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
            janet_formatb(&buffer, format, args);
            if (xtype == JANET_ABSTRACT) {
                void *abstract = janet_unwrap_abstract(x);
                if (janet_abstract_type(abstract) != &cfun_io_filetype)
                    break;
                IOFile *iofile = abstract;
                f = iofile->file;
            }
            fwrite(buffer.data, buffer.count, 1, f);
            janet_buffer_deinit(&buffer);
            break;
        }
        case JANET_BUFFER:
            janet_formatb(janet_unwrap_buffer(x), format, args);
            break;
    }
    va_end(args);
    return;
}

static const JanetReg io_cfuns[] = {
    {
        "print", cfun_io_print,
        JDOC("(print & xs)\n\n"
             "Print values to the console (standard out). Value are converted "
             "to strings if they are not already. After printing all values, a "
             "newline character is printed. Use the value of (dyn :out stdout) to determine "
             "what to push characters to. Expects (dyn :out stdout) to be either a core/file or "
             "a buffer. Returns nil.")
    },
    {
        "prin", cfun_io_prin,
        JDOC("(prin & xs)\n\n"
             "Same as print, but does not add trailing newline.")
    },
    {
        "printf", cfun_io_printf,
        JDOC("(printf fmt & xs)\n\n"
             "Prints output formatted as if with (string/format fmt ;xs) to (dyn :out stdout) with a trailing newline.")
    },
    {
        "prinf", cfun_io_prinf,
        JDOC("(prinf fmt & xs)\n\n"
             "Like printf but with no trailing newline.")
    },
    {
        "eprin", cfun_io_eprin,
        JDOC("(eprin & xs)\n\n"
             "Same as prin, but uses (dyn :err stderr) instead of (dyn :out stdout).")
    },
    {
        "eprint", cfun_io_eprint,
        JDOC("(eprint & xs)\n\n"
             "Same as print, but uses (dyn :err stderr) instead of (dyn :out stdout).")
    },
    {
        "eprintf", cfun_io_eprintf,
        JDOC("(eprintf fmt & xs)\n\n"
             "Prints output formatted as if with (string/format fmt ;xs) to (dyn :err stderr) with a trailing newline.")
    },
    {
        "eprinf", cfun_io_eprinf,
        JDOC("(eprinf fmt & xs)\n\n"
             "Like eprintf but with no trailing newline.")
    },
    {
        "file/open", cfun_io_fopen,
        JDOC("(file/open path &opt mode)\n\n"
             "Open a file. path is an absolute or relative path, and "
             "mode is a set of flags indicating the mode to open the file in. "
             "mode is a keyword where each character represents a flag. If the file "
             "cannot be opened, returns nil, otherwise returns the new file handle. "
             "Mode flags:\n\n"
             "\tr - allow reading from the file\n"
             "\tw - allow writing to the file\n"
             "\ta - append to the file\n"
             "\tb - open the file in binary mode (rather than text mode)\n"
             "\t+ - append to the file instead of overwriting it")
    },
    {
        "file/fdopen", cfun_io_fdopen,
        JDOC("(file/fdopen fd &opt mode)\n\n"
             "Create a file from an fd. fd is a platform specific file descriptor, and "
             "mode is a set of flags indicating the mode to open the file in. "
             "mode is a keyword where each character represents a flag. If the file "
             "cannot be opened, returns nil, otherwise returns the new file handle. "
             "Mode flags:\n\n"
             "\tr - allow reading from the file\n"
             "\tw - allow writing to the file\n"
             "\ta - append to the file\n"
             "\tb - open the file in binary mode (rather than text mode)\n"
             "\t+ - append to the file instead of overwriting it")
    },
    {
        "file/fileno", cfun_io_fileno,
        JDOC("(file/fileno f)\n\n"
             "Return the underlying file descriptor for the file as a number."
             "The meaning of this number is platform specific.")
    },
    {
        "file/close", cfun_io_fclose,
        JDOC("(file/close f)\n\n"
             "Close a file and release all related resources. When you are "
             "done reading a file, close it to prevent a resource leak and let "
             "other processes read the file.")
    },
    {
        "file/read", cfun_io_fread,
        JDOC("(file/read f what &opt buf)\n\n"
             "Read a number of bytes from a file into a buffer. A buffer can "
             "be provided as an optional fourth argument, otherwise a new buffer "
             "is created. 'what' can either be an integer or a keyword. Returns the "
             "buffer with file contents. "
             "Values for 'what':\n\n"
             "\t:all - read the whole file\n"
             "\t:line - read up to and including the next newline character\n"
             "\tn (integer) - read up to n bytes from the file")
    },
    {
        "file/write", cfun_io_fwrite,
        JDOC("(file/write f bytes)\n\n"
             "Writes to a file. 'bytes' must be string, buffer, or symbol. Returns the "
             "file.")
    },
    {
        "file/flush", cfun_io_fflush,
        JDOC("(file/flush f)\n\n"
             "Flush any buffered bytes to the file system. In most files, writes are "
             "buffered for efficiency reasons. Returns the file handle.")
    },
    {
        "file/seek", cfun_io_fseek,
        JDOC("(file/seek f &opt whence n)\n\n"
             "Jump to a relative location in the file. 'whence' must be one of\n\n"
             "\t:cur - jump relative to the current file location\n"
             "\t:set - jump relative to the beginning of the file\n"
             "\t:end - jump relative to the end of the file\n\n"
             "By default, 'whence' is :cur. Optionally a value n may be passed "
             "for the relative number of bytes to seek in the file. n may be a real "
             "number to handle large files of more the 4GB. Returns the file handle.")
    },
    {
        "file/popen", cfun_io_popen,
        JDOC("(file/popen path &opt mode)\n\n"
             "Open a file that is backed by a process. The file must be opened in either "
             "the :r (read) or the :w (write) mode. In :r mode, the stdout of the "
             "process can be read from the file. In :w mode, the stdin of the process "
             "can be written to. Returns the new file.")
    },
    {NULL, NULL, NULL}
};

/* C API */

FILE *janet_getfile(const Janet *argv, int32_t n, int *flags) {
    IOFile *iof = janet_getabstract(argv, n, &cfun_io_filetype);
    if (NULL != flags) *flags = iof->flags;
    return iof->file;
}

/* Module entry point */
void janet_lib_io(JanetTable *env) {
    janet_core_cfuns(env, NULL, io_cfuns);

    /* stdout */
    janet_core_def(env, "stdout",
                   makef(stdout, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
                   JDOC("The standard output file."));
    /* stderr */
    janet_core_def(env, "stderr",
                   makef(stderr, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
                   JDOC("The standard error file."));
    /* stdin */
    janet_core_def(env, "stdin",
                   makef(stdin, IO_READ | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
                   JDOC("The standard input file."));

}

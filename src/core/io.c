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

/* Compiler feature test macros for things */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <janet/janet.h>
#include <errno.h>

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

static int janet_io_gc(void *p, size_t len);

JanetAbstractType janet_io_filetype = {
    "core/file",
    janet_io_gc,
    NULL
};

/* Check argupments to fopen */
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
    IOFile *iof = (IOFile *) janet_abstract(&janet_io_filetype, sizeof(IOFile));
    iof->file = f;
    iof->flags = flags;
    return janet_wrap_abstract(iof);
}

/* Open a process */
#ifdef __EMSCRIPTEN__
static Janet janet_io_popen(int32_t argc, Janet *argv) {
    (void) argc;
    (void) argv;
    janet_panic("not implemented on this platform");
    return janet_wrap_nil();
}
#else
static Janet janet_io_popen(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    const uint8_t *fname = janet_getstring(argv, 0);
    const uint8_t *fmode = NULL;
    if (argc == 2) {
        fmode = janet_getkeyword(argv, 1);
        if (janet_string_length(fmode) != 1 ||
            !(fmode[0] == 'r' || fmode[0] == 'w')) {
            janet_panicf("invalid file mode :%S, expected :r or :w", fmode);
        }
    }
    int flags = (fmode && fmode[0] == '2')
        ? IO_PIPED | IO_WRITE
        : IO_PIPED | IO_READ;
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

/* Open a a file and return a userdata wrapper around the C file API. */
static Janet janet_io_fopen(int32_t argc, Janet *argv) {
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

/* Read up to n bytes into buffer. Return error string if error. */
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
static Janet janet_io_fread(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    IOFile *iof = janet_getabstract(argv, 0, &janet_io_filetype);
    if (iof->flags & IO_CLOSED) janet_panic("file is closed");
    JanetBuffer *buffer;
    if (argc == 2) {
        buffer = janet_buffer(0);
    } else {
        buffer = janet_getbuffer(argv, 2);
    }
    if (janet_checktype(argv[1], JANET_KEYWORD)) {
        const uint8_t *sym = janet_unwrap_keyword(argv[1]);
        if (!janet_cstrcmp(sym, "all")) {
            /* Read whole file */
            int status = fseek(iof->file, 0, SEEK_SET);
            if (status) {
                /* backwards fseek did not work (stream like popen) */
                int32_t sizeBefore;
                do {
                    sizeBefore = buffer->count;
                    read_chunk(iof, buffer, 1024);
                } while (sizeBefore < buffer->count);
            } else {
                fseek(iof->file, 0, SEEK_END);
                long fsize = ftell(iof->file);
                fseek(iof->file, 0, SEEK_SET);
                read_chunk(iof, buffer, (int32_t) fsize);
            }
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
    return janet_wrap_buffer(buffer);
}

/* Write bytes to a file */
static Janet janet_io_fwrite(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    IOFile *iof = janet_getabstract(argv, 0, &janet_io_filetype);
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
static Janet janet_io_fflush(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    IOFile *iof = janet_getabstract(argv, 0, &janet_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        janet_panic("file is not writeable");
    if (fflush(iof->file))
        janet_panic("could not flush file");
    return argv[0];
}

/* Cleanup a file */
static int janet_io_gc(void *p, size_t len) {
    (void) len;
    IOFile *iof = (IOFile *)p;
    if (!(iof->flags & (IO_NOT_CLOSEABLE | IO_CLOSED))) {
        return fclose(iof->file);
    }
    return 0;
}

/* Close a file */
static Janet janet_io_fclose(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    IOFile *iof = janet_getabstract(argv, 0, &janet_io_filetype);
    if (iof->flags & IO_CLOSED)
        janet_panic("file is closed");
    if (iof->flags & (IO_NOT_CLOSEABLE))
        janet_panic("file not closable");
    if (iof->flags & IO_PIPED) {
#ifdef JANET_WINDOWS
#define pclose _pclose
#endif
        if (pclose(iof->file)) janet_panic("could not close file");
    } else {
        if (fclose(iof->file)) janet_panic("could not close file");
    }
    iof->flags |= IO_CLOSED;
    return argv[0];
}

/* Seek a file */
static Janet janet_io_fseek(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    IOFile *iof = janet_getabstract(argv, 0, &janet_io_filetype);
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

static const JanetReg cfuns[] = {
    {"file/open", janet_io_fopen,
        "(file/open path [,mode])\n\n"
        "Open a file. path is files absolute or relative path, and "
        "mode is a set of flags indicating the mode to open the file in. "
        "mode is a keyword where each character represents a flag. If the file "
        "cannot be opened, returns nil, otherwise returns the new file handle. "
        "Mode flags:\n\n"
        "\tr - allow reading from the file\n"
        "\tw - allow witing to the file\n"
        "\ta - append to the file\n"
        "\tb - open the file in binary mode (rather than text mode)\n"
        "\t+ - append to the file instead of overwriting it"
    },
    {"file/close", janet_io_fclose,
        "(file/close f)\n\n"
        "Close a file and release all related resources. When you are "
        "done reading a file, close it to prevent a resource leak and let "
        "other processes read the file."
    },
    {"file/read", janet_io_fread,
        "(file/read f what [,buf])\n\n"
        "Read a number of bytes from a file into a buffer. A buffer can "
        "be provided as an optional fourth argument. otherwise a new buffer "
        "is created. 'what' can either be an integer or a keyword. Returns the "
        "buffer with file contents. "
        "Values for 'what':\n\n"
        "\t:all - read the whole file\n"
        "\t:line - read up to and including the next newline character\n"
        "\tn (integer) - read up to n bytes from the file"
    },
    {"file/write", janet_io_fwrite,
        "(file/write f bytes)\n\n"
        "Writes to a file. 'bytes' must be string, buffer, or symbol. Returns the "
        "file"
    },
    {"file/flush", janet_io_fflush,
        "(file/flush f)\n\n"
        "Flush any buffered bytes to the filesystem. In most files, writes are "
        "buffered for efficiency reasons. Returns the file handle."
    },
    {"file/seek", janet_io_fseek,
        "(file/seek f [,whence [,n]])\n\n"
        "Jump to a relative location in the file. 'whence' must be one of\n\n"
        "\t:cur - jump relative to the current file location\n"
        "\t:set - jump relative to the beginning of the file\n"
        "\t:end - jump relative to the end of the file\n\n"
        "By default, 'whence' is :cur. Optionally a value n may be passed "
        "for the relative number of bytes to seek in the file. n may be a real "
        "number to handle large files of more the 4GB. Returns the file handle."
    },
    {"file/popen", janet_io_popen,
        "(file/popen path [,mode])\n\n"
        "Open a file that is backed by a process. The file must be opened in either "
        "the :r (read) or the :w (write) mode. In :r mode, the stdout of the "
        "process can be read from the file. In :w mode, the stdin of the process "
        "can be written to. Returns the new file."
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_io(JanetTable *env) {
    janet_cfuns(env, NULL, cfuns);
    /* stdout */
    janet_def(env, "stdout",
            makef(stdout, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
            "The standard output file.");
    /* stderr */
    janet_def(env, "stderr",
            makef(stderr, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
            "The standard error file.");
    /* stdin */
    janet_def(env, "stdin",
            makef(stdin, IO_READ | IO_NOT_CLOSEABLE | IO_SERIALIZABLE),
            "The standard input file.");
}

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
    ":core/file",
    janet_io_gc,
    NULL
};

/* Check argupments to fopen */
static int checkflags(const uint8_t *str, int32_t len) {
    int flags = 0;
    int32_t i;
    if (!len || len > 3) return -1;
    switch (*str) {
        default:
            return -1;
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
                return -1;
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

/* Check file argument */
static IOFile *checkfile(JanetArgs args, int32_t n) {
    IOFile *iof;
    if (n >= args.n) {
        *args.ret = janet_cstringv("expected core.file");
        return NULL;
    }
    if (!janet_checktype(args.v[n], JANET_ABSTRACT)) {
        *args.ret = janet_cstringv("expected core.file");
        return NULL;
    }
    iof = (IOFile *) janet_unwrap_abstract(args.v[n]);
    if (janet_abstract_type(iof) != &janet_io_filetype) {
        *args.ret = janet_cstringv("expected core.file");
        return NULL;
    }
    return iof;
}

/* Check buffer argument */
static JanetBuffer *checkbuffer(JanetArgs args, int32_t n, int optional) {
    if (optional && n == args.n) {
        return janet_buffer(0);
    }
    if (n >= args.n) {
        *args.ret = janet_cstringv("expected buffer");
        return NULL;
    }
    if (!janet_checktype(args.v[n], JANET_BUFFER)) {
        *args.ret = janet_cstringv("expected buffer");
        return NULL;
    }
    return janet_unwrap_abstract(args.v[n]);
}

static Janet makef(FILE *f, int flags) {
    IOFile *iof = (IOFile *) janet_abstract(&janet_io_filetype, sizeof(IOFile));
    iof->file = f;
    iof->flags = flags;
    return janet_wrap_abstract(iof);
}

/* Open a process */
static int janet_io_popen(JanetArgs args) {
    const uint8_t *fname, *fmode;
    int32_t modelen;
    FILE *f;
    int flags;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_STRING(fname, args, 0);
    if (args.n == 2) {
        if (!janet_checktype(args.v[1], JANET_STRING) &&
            !janet_checktype(args.v[1], JANET_SYMBOL))
            JANET_THROW(args, "expected string mode");
        fmode = janet_unwrap_string(args.v[1]);
        modelen = janet_string_length(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        modelen = 1;
    }
    if (fmode[0] == ':') {
        fmode++;
        modelen--;
    }
    if (modelen != 1 || !(fmode[0] == 'r' || fmode[0] == 'w')) {
        JANET_THROW(args, "invalid file mode");
    }
    flags = (fmode[0] == 'r') ? IO_PIPED | IO_READ : IO_PIPED | IO_WRITE;
#ifdef JANET_WINDOWS
#define popen _popen
#endif
#ifdef __EMSCRIPTEN__
#define popen(A, B) (errno = 0, NULL)
#endif
    f = popen((const char *)fname, (const char *)fmode);
    if (!f) {
        if (errno == EMFILE) {
            JANET_THROW(args, "too many streams are open");
        }
        JANET_THROW(args, "could not open file");
    }
    JANET_RETURN(args, makef(f, flags));
}

/* Open a a file and return a userdata wrapper around the C file API. */
static int janet_io_fopen(JanetArgs args) {
    const uint8_t *fname, *fmode;
    int32_t modelen;
    FILE *f;
    int flags;
    JANET_MINARITY(args, 1);
    JANET_MAXARITY(args, 2);
    JANET_ARG_STRING(fname, args, 0);
    if (args.n == 2) {
        if (!janet_checktype(args.v[1], JANET_STRING) &&
            !janet_checktype(args.v[1], JANET_SYMBOL))
            JANET_THROW(args, "expected string mode");
        fmode = janet_unwrap_string(args.v[1]);
        modelen = janet_string_length(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        modelen = 1;
    }
    if (fmode[0] == ':') {
        fmode++;
        modelen--;
    }
    if ((flags = checkflags(fmode, modelen)) < 0) {
        JANET_THROW(args, "invalid file mode");
    }
    f = fopen((const char *)fname, (const char *)fmode);
    JANET_RETURN(args, f ? makef(f, flags) : janet_wrap_nil());
}

/* Read up to n bytes into buffer. Return error string if error. */
static const char *read_chunk(IOFile *iof, JanetBuffer *buffer, int32_t nBytesMax) {
    if (!(iof->flags & (IO_READ | IO_UPDATE)))
        return "file is not readable";
    /* Ensure buffer size */
    if (janet_buffer_extra(buffer, nBytesMax))
        return "buffer overflow";
    size_t ntoread = nBytesMax;
    size_t nread = fread((char *)(buffer->data + buffer->count), 1, ntoread, iof->file);
    if (nread != ntoread && ferror(iof->file))
        return "could not read file";
    buffer->count += (int32_t) nread;
    return NULL;
}

/* Read a certain number of bytes into memory */
static int janet_io_fread(JanetArgs args) {
    JanetBuffer *b;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        JANET_THROW(args, "file is closed");
    b = checkbuffer(args, 2, 1);
    if (!b) return 1;
    if (janet_checktype(args.v[1], JANET_SYMBOL)) {
        const uint8_t *sym = janet_unwrap_symbol(args.v[1]);
        if (!janet_cstrcmp(sym, ":all")) {
            /* Read whole file */
            int status = fseek(iof->file, 0, SEEK_SET);
            if (status) {
                /* backwards fseek did not work (stream like popen) */
                int32_t sizeBefore;
                do {
                    sizeBefore = b->count;
                    const char *maybeErr = read_chunk(iof, b, 1024);
                    if (maybeErr) JANET_THROW(args, maybeErr);
                } while (sizeBefore < b->count);
            } else {
                fseek(iof->file, 0, SEEK_END);
                long fsize = ftell(iof->file);
                fseek(iof->file, 0, SEEK_SET);
                if (fsize > INT32_MAX) JANET_THROW(args, "buffer overflow");
                const char *maybeErr = read_chunk(iof, b, (int32_t) fsize);;
                if (maybeErr) JANET_THROW(args, maybeErr);
            }
        } else if (!janet_cstrcmp(sym, ":line")) {
            for (;;) {
                int x = fgetc(iof->file);
                if (x != EOF && janet_buffer_push_u8(b, (uint8_t)x))
                    JANET_THROW(args, "buffer overflow");
                if (x == EOF || x == '\n') break;
            }
        } else {
            JANET_THROW(args, "expected one of :all, :line");
        }
    } else if (!janet_checktype(args.v[1], JANET_INTEGER)) {
        JANET_THROW(args, "expected positive integer");
    } else {
        int32_t len = janet_unwrap_integer(args.v[1]);
        if (len < 0) JANET_THROW(args, "expected positive integer");
        const char *maybeErr = read_chunk(iof, b, len);
        if (maybeErr) JANET_THROW(args, maybeErr);
    }
    JANET_RETURN(args, janet_wrap_buffer(b));
}

/* Write bytes to a file */
static int janet_io_fwrite(JanetArgs args) {
    int32_t len, i;
    const uint8_t *str;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        JANET_THROW(args, "file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        JANET_THROW(args, "file is not writeable");
    for (i = 1; i < args.n; i++) {
        JANET_CHECKMANY(args, i, JANET_TFLAG_BYTES);
    }
    for (i = 1; i < args.n; i++) {
        JANET_ARG_BYTES(str, len, args, i);
        if (len) {
            if (!fwrite(str, len, 1, iof->file)) JANET_THROW(args, "error writing to file");
        }
    }
    JANET_RETURN(args, janet_wrap_abstract(iof));
}

/* Flush the bytes in the file */
static int janet_io_fflush(JanetArgs args) {
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        JANET_THROW(args, "file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        JANET_THROW(args, "file is not flushable");
    if (fflush(iof->file)) JANET_THROW(args, "could not flush file");
    JANET_RETURN(args, janet_wrap_abstract(iof));
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
static int janet_io_fclose(JanetArgs args) {
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & (IO_CLOSED))
        JANET_THROW(args, "file already closed");
    if (iof->flags & (IO_NOT_CLOSEABLE))
        JANET_THROW(args, "file not closable");
    if (iof->flags & IO_PIPED) {
#ifdef JANET_WINDOWS
#define pclose _pclose
#endif
        if (pclose(iof->file)) JANET_THROW(args, "could not close file");
    } else {
        if (fclose(iof->file)) JANET_THROW(args, "could not close file");
    }
    iof->flags |= IO_CLOSED;
    JANET_RETURN(args, janet_wrap_abstract(iof));
}

/* Seek a file */
static int janet_io_fseek(JanetArgs args) {
    long int offset = 0;
    int whence = SEEK_CUR;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        JANET_THROW(args, "file is closed");
    if (args.n >= 2) {
        const uint8_t *whence_sym;
        if (!janet_checktype(args.v[1], JANET_SYMBOL))
            JANET_THROW(args, "expected symbol");
        whence_sym = janet_unwrap_symbol(args.v[1]);
        if (!janet_cstrcmp(whence_sym, ":cur")) {
            whence = SEEK_CUR;
        } else if (!janet_cstrcmp(whence_sym, ":set")) {
            whence = SEEK_SET;
        } else if (!janet_cstrcmp(whence_sym, ":end")) {
            whence = SEEK_END;
        } else {
            JANET_THROW(args, "expected one of :cur, :set, :end");
        }
        if (args.n >= 3) {
            double doffset;
            JANET_ARG_NUMBER(doffset, args, 2);
            offset = (long int)doffset;
        }
    }
    if (fseek(iof->file, offset, whence))
        JANET_THROW(args, "error seeking file");
    JANET_RETURN(args, args.v[0]);
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
int janet_lib_io(JanetArgs args) {
    JanetTable *env = janet_env(args);
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

    return 0;
}

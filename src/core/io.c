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

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <dst/dst.h>
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

static int dst_io_gc(void *p, size_t len);

DstAbstractType dst_io_filetype = {
    ":core.file",
    dst_io_gc,
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
static IOFile *checkfile(DstArgs args, int32_t n) {
    IOFile *iof;
    if (n >= args.n) {
        *args.ret = dst_cstringv("expected core.file");
        return NULL;
    }
    if (!dst_checktype(args.v[n], DST_ABSTRACT)) {
        *args.ret = dst_cstringv("expected core.file");
        return NULL;
    }
    iof = (IOFile *) dst_unwrap_abstract(args.v[n]);
    if (dst_abstract_type(iof) != &dst_io_filetype) {
        *args.ret = dst_cstringv("expected core.file");
        return NULL;
    }
    return iof;
}

/* Check buffer argument */
static DstBuffer *checkbuffer(DstArgs args, int32_t n, int optional) {
    if (optional && n == args.n) {
        return dst_buffer(0);
    }
    if (n >= args.n) {
        *args.ret = dst_cstringv("expected buffer");
        return NULL;
    }
    if (!dst_checktype(args.v[n], DST_BUFFER)) {
        *args.ret = dst_cstringv("expected buffer");
        return NULL;
    }
    return dst_unwrap_abstract(args.v[n]);
}

static Dst makef(FILE *f, int flags) {
    IOFile *iof = (IOFile *) dst_abstract(&dst_io_filetype, sizeof(IOFile));
    iof->file = f;
    iof->flags = flags;
    return dst_wrap_abstract(iof);
}

/* Open a process */
static int dst_io_popen(DstArgs args) {
    const uint8_t *fname, *fmode;
    int32_t modelen;
    FILE *f;
    int flags;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_STRING(fname, args, 0);
    if (args.n == 2) {
        if (!dst_checktype(args.v[1], DST_STRING) &&
            !dst_checktype(args.v[1], DST_SYMBOL))
            DST_THROW(args, "expected string mode");
        fmode = dst_unwrap_string(args.v[1]);
        modelen = dst_string_length(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        modelen = 1;
    }
    if (fmode[0] == ':') {
        fmode++;
        modelen--;
    }
    if (modelen != 1 || !(fmode[0] == 'r' || fmode[0] == 'w')) {
        DST_THROW(args, "invalid file mode");
    }
    flags = (fmode[0] == 'r') ? IO_PIPED | IO_READ : IO_PIPED | IO_WRITE;
#ifdef DST_WINDOWS
#define popen _popen
#endif
    f = popen((const char *)fname, (const char *)fmode);
    if (!f) {
        if (errno == EMFILE) {
            DST_THROW(args, "too many streams are open");
        }
        DST_THROW(args, "could not open file");
    }
    DST_RETURN(args, makef(f, flags));
}

/* Open a a file and return a userdata wrapper around the C file API. */
static int dst_io_fopen(DstArgs args) {
    const uint8_t *fname, *fmode;
    int32_t modelen;
    FILE *f;
    int flags;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    DST_ARG_STRING(fname, args, 0);
    if (args.n == 2) {
        if (!dst_checktype(args.v[1], DST_STRING) &&
            !dst_checktype(args.v[1], DST_SYMBOL))
            DST_THROW(args, "expected string mode");
        fmode = dst_unwrap_string(args.v[1]);
        modelen = dst_string_length(fmode);
    } else {
        fmode = (const uint8_t *)"r";
        modelen = 1;
    }
    if (fmode[0] == ':') {
        fmode++;
        modelen--;
    }
    if ((flags = checkflags(fmode, modelen)) < 0) {
        DST_THROW(args, "invalid file mode");
    }
    f = fopen((const char *)fname, (const char *)fmode);
    DST_RETURN(args, f ? makef(f, flags) : dst_wrap_nil());
}

/* Read up to n bytes into buffer. Return error string if error. */
static const char *read_chunk(IOFile *iof, DstBuffer *buffer, int32_t nBytesMax) {
    if (!(iof->flags & (IO_READ | IO_UPDATE)))
        return "file is not readable";
    /* Ensure buffer size */
    if (dst_buffer_extra(buffer, nBytesMax)) 
        return "buffer overflow";
    size_t ntoread = nBytesMax;
    size_t nread = fread((char *)(buffer->data + buffer->count), 1, ntoread, iof->file);
    if (nread != ntoread && ferror(iof->file)) 
        return "could not read file";
    buffer->count += (int32_t) nread;
    return NULL;
}

/* Read a certain number of bytes into memory */
static int dst_io_fread(DstArgs args) {
    DstBuffer *b;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        DST_THROW(args, "file is closed");
    b = checkbuffer(args, 2, 1);
    if (!b) return 1;
    if (dst_checktype(args.v[1], DST_SYMBOL)) {
        const uint8_t *sym = dst_unwrap_symbol(args.v[1]);
        if (!dst_cstrcmp(sym, ":all")) {
            /* Read whole file */
            int status = fseek(iof->file, 0, SEEK_SET);
            if (status) {
                /* backwards fseek did not work (stream like popen) */
                int32_t sizeBefore;
                do {
                    sizeBefore = b->count;
                    const char *maybeErr = read_chunk(iof, b, 1024);
                    if (maybeErr) DST_THROW(args, maybeErr);
                } while (sizeBefore < b->count);
            } else {
                fseek(iof->file, 0, SEEK_END);
                long fsize = ftell(iof->file);
                fseek(iof->file, 0, SEEK_SET);
                if (fsize > INT32_MAX) DST_THROW(args, "buffer overflow");
                const char *maybeErr = read_chunk(iof, b, (int32_t) fsize);;
                if (maybeErr) DST_THROW(args, maybeErr);
            }
        } else if (!dst_cstrcmp(sym, ":line")) {
            for (;;) {
                int x = fgetc(iof->file);
                if (x != EOF && dst_buffer_push_u8(b, (uint8_t)x))
                    DST_THROW(args, "buffer overflow");
                if (x == EOF || x == '\n') break;
            }
        } else {
            DST_THROW(args, "expected one of :all, :line");
        }
    } else if (!dst_checktype(args.v[1], DST_INTEGER)) {
        DST_THROW(args, "expected positive integer");
    } else {
        int32_t len = dst_unwrap_integer(args.v[1]);
        if (len < 0) DST_THROW(args, "expected positive integer");
        const char *maybeErr = read_chunk(iof, b, len);
        if (maybeErr) DST_THROW(args, maybeErr);
    }
    DST_RETURN(args, dst_wrap_buffer(b));
}

/* Write bytes to a file */
static int dst_io_fwrite(DstArgs args) {
    int32_t len, i;
    const uint8_t *str;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        DST_THROW(args, "file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        DST_THROW(args, "file is not writeable");
    for (i = 1; i < args.n; i++) {
        DST_CHECKMANY(args, i, DST_TFLAG_BYTES);
    }
    for (i = 1; i < args.n; i++) {
        DST_ARG_BYTES(str, len, args, i);
        if (len) {
            if (!fwrite(str, len, 1, iof->file)) DST_THROW(args, "error writing to file");
        }
    }
    DST_RETURN(args, dst_wrap_abstract(iof));
}

/* Flush the bytes in the file */
static int dst_io_fflush(DstArgs args) {
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        DST_THROW(args, "file is closed");
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        DST_THROW(args, "file is not flushable");
    if (fflush(iof->file)) DST_THROW(args, "could not flush file");
    DST_RETURN(args, dst_wrap_abstract(iof));
}

/* Cleanup a file */
static int dst_io_gc(void *p, size_t len) {
    (void) len;
    IOFile *iof = (IOFile *)p;
    if (!(iof->flags & (IO_NOT_CLOSEABLE | IO_CLOSED))) {
        return fclose(iof->file);
    }
    return 0;
}

/* Close a file */
static int dst_io_fclose(DstArgs args) {
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & (IO_CLOSED))
        DST_THROW(args, "file already closed");
    if (iof->flags & (IO_NOT_CLOSEABLE))
        DST_THROW(args, "file not closable");
    if (iof->flags & IO_PIPED) {
#ifdef DST_WINDOWS
#define pclose _pclose
#endif
        if (pclose(iof->file)) DST_THROW(args, "could not close file");
    } else {
        if (fclose(iof->file)) DST_THROW(args, "could not close file");
    }
    iof->flags |= IO_CLOSED;
    DST_RETURN(args, dst_wrap_abstract(iof));
}

/* Seek a file */
static int dst_io_fseek(DstArgs args) {
    long int offset = 0;
    int whence = SEEK_CUR;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (iof->flags & IO_CLOSED)
        DST_THROW(args, "file is closed");
    if (args.n >= 2) {
        const uint8_t *whence_sym;
        if (!dst_checktype(args.v[1], DST_SYMBOL))
            DST_THROW(args, "expected symbol");
        whence_sym = dst_unwrap_symbol(args.v[1]);
        if (!dst_cstrcmp(whence_sym, ":cur")) {
            whence = SEEK_CUR;
        } else if (!dst_cstrcmp(whence_sym, ":set")) {
            whence = SEEK_SET;
        } else if (!dst_cstrcmp(whence_sym, ":end")) {
            whence = SEEK_END;
        } else {
            DST_THROW(args, "expected one of :cur, :set, :end");
        }
        if (args.n >= 3) {
            if (!dst_checktype(args.v[2], DST_INTEGER))
                DST_THROW(args, "expected integer");
            offset = dst_unwrap_integer(args.v[2]);
        }
    }
    if (fseek(iof->file, offset, whence))
        DST_THROW(args, "error seeking file");
    DST_RETURN(args, args.v[0]);
}

static const DstReg cfuns[] = {
    {"file.open", dst_io_fopen},
    {"file.close", dst_io_fclose},
    {"file.read", dst_io_fread},
    {"file.write", dst_io_fwrite},
    {"file.flush", dst_io_fflush},
    {"file.seek", dst_io_fseek},
    {"file.popen", dst_io_popen},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_io(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);

    /* stdout */
    dst_env_def(env, "stdout",
            makef(stdout, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE));

    /* stderr */
    dst_env_def(env, "stderr",
            makef(stderr, IO_APPEND | IO_NOT_CLOSEABLE | IO_SERIALIZABLE));

    /* stdin */
    dst_env_def(env, "stdin",
            makef(stdin, IO_READ | IO_NOT_CLOSEABLE | IO_SERIALIZABLE));

    return 0;
}

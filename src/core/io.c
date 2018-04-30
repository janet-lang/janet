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

#define IO_WRITE 1
#define IO_READ 2
#define IO_APPEND 4
#define IO_UPDATE 8
#define IO_NOT_CLOSEABLE 16
#define IO_CLOSED 32
#define IO_BINARY 64
#define IO_SERIALIZABLE 128

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

/* Check char array argument */
static int checkchars(DstArgs args, int32_t n, const uint8_t **str, int32_t *len) {
    if (n >= args.n) {
        *args.ret = dst_cstringv("expected string/buffer");
        return 0;
    }
    if (!dst_chararray_view(args.v[n], str, len)) {
        *args.ret = dst_cstringv("expected string/buffer");
        return 0;
    }
    return 1;
}

static Dst makef(FILE *f, int flags) {
    IOFile *iof = (IOFile *) dst_abstract(&dst_io_filetype, sizeof(IOFile));
    iof->file = f;
    iof->flags = flags;
    return dst_wrap_abstract(iof);
}

/* Open a a file and return a userdata wrapper around the C file API. */
static int dst_io_fopen(DstArgs args) {
    const uint8_t *fname, *fmode;
    int32_t modelen;
    FILE *f;
    int flags;
    dst_minarity(args, 1);
    dst_maxarity(args, 2);
    dst_check(args, 0, DST_STRING);
    fname = dst_unwrap_string(args.v[0]);
    if (args.n == 2) {
        if (!dst_checktype(args.v[1], DST_STRING) &&
            !dst_checktype(args.v[1], DST_SYMBOL))
            return dst_throw(args, "expected string mode");
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
    if ((flags = checkflags(fmode, modelen)) < 0) return dst_throw(args, "invalid file mode");
    f = fopen((const char *)fname, (const char *)fmode);
    if (!f) return dst_throw(args, "could not open file");
    return dst_return(args, makef(f, flags));
}

/* Read a certain number of bytes into memory */
static int dst_io_fread(DstArgs args) {
    DstBuffer *b;
    int32_t len;
    size_t ntoread, nread;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    b = checkbuffer(args, 2, 1);
    if (!b) return 1;
    if (dst_checktype(args.v[1], DST_SYMBOL)) {
        const uint8_t *sym = dst_unwrap_symbol(args.v[1]);
        if (!dst_cstrcmp(sym, ":all")) {
            /* Read whole file */
            long fsize;
            fseek(iof->file, 0, SEEK_END);
            fsize = ftell(iof->file);
            fseek(iof->file, 0, SEEK_SET);
            if (fsize > INT32_MAX) return dst_throw(args, "buffer overflow");
            len = fsize;
            /* Fall through to normal code */
        } else if (!dst_cstrcmp(sym, ":line")) {
            for (;;) {
                int x = fgetc(iof->file);
                if (x != EOF && dst_buffer_push_u8(b, (uint8_t)x)) return dst_throw(args, "buffer overflow");
                if (x == EOF || x == '\n') break;
            }
            return dst_return(args, dst_wrap_buffer(b));
        } else {
            return dst_throw(args, "expected one of :all, :line");
        } 
    } else if (!dst_checktype(args.v[1], DST_INTEGER)) {
        return dst_throw(args, "expected positive integer");
    } else {
        len = dst_unwrap_integer(args.v[1]);
        if (len < 0) return dst_throw(args, "expected positive integer");
    }
    if (!(iof->flags & (IO_READ | IO_UPDATE))) return dst_throw(args, "file is not readable");
    /* Ensure buffer size */
    if (dst_buffer_extra(b, len)) return dst_throw(args, "buffer overflow");
    ntoread = len;
    nread = fread((char *)(b->data + b->count), 1, ntoread, iof->file);
    if (nread != ntoread && ferror(iof->file)) return dst_throw(args, "could not read file");
    b->count += nread;
    return dst_return(args, dst_wrap_buffer(b));
}

/* Write bytes to a file */
static int dst_io_fwrite(DstArgs args) {
    int32_t len, i;
    const uint8_t *str;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        return dst_throw(args, "file is not writeable");
    for (i = 1; i < args.n; i++) {
        if (!checkchars(args, i, &str, &len)) return 1;
        if (!fwrite(str, len, 1, iof->file)) return dst_throw(args, "error writing to file");
    }
    return dst_return(args, dst_wrap_abstract(iof));
}

/* Flush the bytes in the file */
static int dst_io_fflush(DstArgs args) {
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (!(iof->flags & (IO_WRITE | IO_APPEND | IO_UPDATE)))
        return dst_throw(args, "file is not flushable");
    if (fflush(iof->file)) return dst_throw(args, "could not flush file");
    return dst_return(args, dst_wrap_abstract(iof));
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
        return dst_throw(args, "file already closed");
    if (iof->flags & (IO_NOT_CLOSEABLE))
        return dst_throw(args, "file not closable");
    if (fclose(iof->file)) return dst_throw(args, "could not close file");
    iof->flags |= IO_CLOSED;
    return dst_return(args, dst_wrap_abstract(iof));
}

/* Seek a file */
static int dst_io_fseek(DstArgs args) {
    long int offset = 0;
    int whence = SEEK_CUR;
    IOFile *iof = checkfile(args, 0);
    if (!iof) return 1;
    if (args.n >= 2) {
        const uint8_t *whence_sym;
        if (!dst_checktype(args.v[1], DST_SYMBOL))
            return dst_throw(args, "expected symbol");
        whence_sym = dst_unwrap_symbol(args.v[1]);
        if (!dst_cstrcmp(whence_sym, ":cur")) {
            whence = SEEK_CUR;
        } else if (!dst_cstrcmp(whence_sym, ":set")) {
            whence = SEEK_SET;
        } else if (!dst_cstrcmp(whence_sym, ":end")) {
            whence = SEEK_END;
        } else {
            return dst_throw(args, "expected one of :cur, :set, :end");
        }
        if (args.n >= 3) {
            if (!dst_checktype(args.v[2], DST_INTEGER))
                return dst_throw(args, "expected integer");
            offset = dst_unwrap_integer(args.v[2]);
        }
    }
    if (fseek(iof->file, offset, whence))
        return dst_throw(args, "error seeking file");
    return dst_return(args, args.v[0]);
}

/* Define the entry point of the library */
#ifdef DST_LIB
#define dst_lib_io _dst_init
#endif

static const DstReg cfuns[] = {
    {"file-open", dst_io_fopen},
    {"file-close", dst_io_fclose},
    {"file-read", dst_io_fread},
    {"file-write", dst_io_fwrite},
    {"file-flush", dst_io_fflush},
    {"file-seek", dst_io_fseek},
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

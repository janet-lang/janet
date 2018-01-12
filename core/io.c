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

DstAbstractType dst_stl_filetype = {
    "stl.file",
    NULL,
    NULL,
    NULL
};

/* Check file argument */
static FILE **checkfile(int32_t argn, Dst *argv, Dst *ret, int32_t n) {
    FILE **fp;
    if (n >= argn) {
        *ret = dst_cstringv("expected stl.file");
        return NULL;
    }
    if (!dst_checktype(argv[n], DST_ABSTRACT)) {
        *ret = dst_cstringv("expected stl.file");
        return NULL;
    }
    fp = (FILE **) dst_unwrap_abstract(argv[n]);
    if (dst_abstract_type(fp) != &dst_stl_filetype) {
        *ret = dst_cstringv("expected stl.file");
        return NULL;
    }
    return fp;
}

/* Check buffer argument */
static DstBuffer *checkbuffer(int32_t argn, Dst *argv, Dst *ret, int32_t n, int optional) {
    if (optional && n == argn) {
        return dst_buffer(0);
    }
    if (n >= argn) {
        *ret = dst_cstringv("expected buffer");
        return NULL;
    }
    if (!dst_checktype(argv[n], DST_BUFFER)) {
        *ret = dst_cstringv("expected buffer");
        return NULL;
    }
    return dst_unwrap_abstract(argv[n]);
}

/* Check char array argument */
static int checkchars(int32_t argn, Dst *argv, Dst *ret, int32_t n, const uint8_t **str, int32_t *len) {
    if (n >= argn) {
        *ret = dst_cstringv("expected string/buffer");
        return 0;
    }
    if (!dst_chararray_view(argv[n], str, len)) {
        *ret = dst_cstringv("expected string/buffer");
        return 0;
    }
    return 1;
}

/* Open a a file and return a userdata wrapper around the C file API. */
int dst_stl_fileopen(int32_t argn, Dst *argv, Dst *ret) {
    if (argn < 2) {
        *ret = dst_cstringv("expected at least 2 arguments");
        return 1;
    }
    const uint8_t *fname = dst_to_string(argv[0]);
    const uint8_t *fmode = dst_to_string(argv[1]);
    FILE *f;
    FILE **fp;
    f = fopen((const char *)fname, (const char *)fmode);
    if (!f) {
        *ret = dst_cstringv("could not open file");
        return 1;
    }
    fp = dst_abstract(&dst_stl_filetype, sizeof(FILE *));
    *fp = f;
    *ret = dst_wrap_abstract(fp);
    return 0;
}

/* Read an entire file into memory */
int dst_stl_slurp(int32_t argn, Dst *argv, Dst *ret) {
    DstBuffer *b;
    size_t fsize;
    FILE *f;
    FILE **fp = checkfile(argn, argv, ret, 0);
    if (!fp) return 1;
    b = checkbuffer(argn, argv, ret, 1, 1);
    if (!b) return 1;
    f = *fp;
    /* Read whole file */
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize > INT32_MAX || dst_buffer_extra(b, fsize)) {
        *ret = dst_cstringv("buffer overflow");
        return 1;
    }
    /* Ensure buffer size */
    if (fsize != fread((char *)(b->data + b->count), fsize, 1, f)) {
        *ret = dst_cstringv("error reading file");
        return 1;
    }
    b->count += fsize;
    /* return */
    *ret = dst_wrap_buffer(b);
    return 0;
}

/* Read a certain number of bytes into memory */
int dst_stl_fileread(int32_t argn, Dst *argv, Dst *ret) {
    DstBuffer *b;
    FILE *f;
    int32_t len;
    FILE **fp = checkfile(argn, argv, ret, 0);
    if (!fp) return 1;
    if (!dst_checktype(argv[1], DST_INTEGER)) {
        *ret = dst_cstringv("expected positive integer");
        return 1;
    }
    len = dst_unwrap_integer(argv[1]);
    if (len < 0) {
        *ret = dst_cstringv("expected positive integer");
        return 1;
    }
    b = checkbuffer(argn, argv, ret, 2, 1);
    if (!b) return 1;

    f = *fp;
    /* Ensure buffer size */
    if (dst_buffer_extra(b, len)) {
        *ret = dst_cstringv("buffer overflow");
        return 1;
    }
    b->count += fread((char *)(b->data + b->count), len, 1, f) * len;
    *ret = dst_wrap_buffer(b);
    return 0;
}

/* Write bytes to a file */
int dst_stl_filewrite(int32_t argn, Dst *argv, Dst *ret) {
    FILE *f;
    int32_t len, i;
    FILE **fp = checkfile(argn, argv, ret, 0);
    const uint8_t *str;
    if (!fp) return 1;
    if (!dst_checktype(argv[1], DST_INTEGER)) {
        *ret = dst_cstringv("expected positive integer");
        return 1;
    }
    len = dst_unwrap_integer(argv[1]);
    if (len < 0) {
        *ret = dst_cstringv("expected positive integer");
        return 1;
    }
    
    for (i = 1; i < argn; i++) {
        if (!checkchars(argn, argv, ret, i, &str, &len)) return 1;
        
        f = *fp;
        if (len != (int32_t) fwrite(str, len, 1, f)) {
            *ret = dst_cstringv("error writing to file");
            return 1;
        }
    }
    return 0;
}

/* Close a file */
int dst_stl_fileclose(int32_t argn, Dst *argv, Dst *ret) {
    FILE *f;
    FILE **fp = checkfile(argn, argv, ret, 0);
    if (!fp) return 1;
    f = *fp;
    if (fclose(f)) {
        *ret = dst_cstringv("could not close file");
        return 1;
    }
    return 0;
}


DstAbstractType dst_stl_filetype = {
    "stl.file",
    NULL,
    NULL,
    NULL
};

/* Open a a file and return a userdata wrapper around the C file API. */
int dst_stl_open(int32_t argn, Dst *argv, Dst *ret) {
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
        dst_c_throwc("could not open file");
        return 1;
    }
    fp = dst_astract(sizeof(FILE *), &dst_stl_filetype);
    *fp = f;
    *ret = dst_wrap_abstract(fp);
    return 0;
}

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

/* Read an entire file into memory */
int dst_stl_slurp(int32_t argn, Dst *argv, Dst *ret) {
    DstBuffer *b;
    long fsize;
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
    if (fsize > INT32_MAX || fsize + b->count > INT32_MAX) {
        *ret = dst_cstringv("buffer overflow");
        return 1;
    }
    /* Ensure buffer size */
    dst_buffer_extra(b, fsize);
    fread((char *)(b->data + b->count), fsize, 1, f);
    b->count += fsize;
    /* return */
    *ret = dst_wrap_buffer(b);
    return 0;
}

/* Read a certain number of bytes into memory */
int dst_stl_read(Dst *vm) {
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
    if (len + bcount
    dst_buffer_extra(b, len);
    b->count += fread((char *)(b->data + b->count), len, 1, f) * len;
    *ret = dst_wrap_buffer(b);
    return 0;
}

/* Write bytes to a file */
int dst_stl_write(Dst *vm) {
    FILE *f;
    const uint8_t *data;
    uint32_t len;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!dst_chararray_view(dst_arg(vm, 1), &data, &len)) dst_c_throwc(vm, "expected string|buffer");
    f = *fp;
    fwrite(data, len, 1, f);
    return DST_RETURN_OK;
}

/* Close a file */
int dst_stl_close(Dst *vm) {
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    fclose(*fp);
    dst_c_return(vm, dst_wrap_nil());
}

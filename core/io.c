
DstAbstractType dst_stl_filetype = {
    "stl.file",
    NULL,
    NULL,
    NULL
};

/* Open a a file and return a userdata wrapper arounf the C file API. */
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
    if (!f)
        dst_c_throwc(vm, "could not open file");
    fp = dst_userdata(vm, sizeof(FILE *), &dst_stl_filetype);
    *fp = f;
    dst_c_return(vm, dst_wrap_userdata(fp));
}

/* Read an entire file into memory */
int dst_stl_slurp(Dst *vm) {
    DstBuffer *b;
    long fsize;
    FILE *f;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!dst_check_buffer(vm, 1, &b)) b = dst_buffer(vm, 10);
    f = *fp;
    /* Read whole file */
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Ensure buffer size */
    dst_buffer_ensure(vm, b, b->count + fsize);
    fread((char *)(b->data + b->count), fsize, 1, f);
    b->count += fsize;
    dst_c_return(vm, dst_wrap_buffer(b));
}

/* Read a certain number of bytes into memory */
int dst_stl_read(Dst *vm) {
    DstBuffer *b;
    FILE *f;
    int64_t len;
    FILE **fp = dst_check_userdata(vm, 0, &dst_stl_filetype);
    if (fp == NULL) dst_c_throwc(vm, "expected file");
    if (!(dst_check_integer(vm, 1, &len))) dst_c_throwc(vm, "expected integer");
    if (!dst_check_buffer(vm, 2, &b)) b = dst_buffer(vm, 10);
    f = *fp;
    /* Ensure buffer size */
    dst_buffer_ensure(vm, b, b->count + len);
    b->count += fread((char *)(b->data + b->count), len, 1, f) * len;
    dst_c_return(vm, dst_wrap_buffer(b));
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

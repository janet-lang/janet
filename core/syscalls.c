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
#include <stdio.h>

int dst_sys_print(DstValue *argv, uint32_t argn) {
    uint32_t i;
    for (i = 0; i < argn; ++i) {
        uint32_t j, len;
        const uint8_t *vstr = dst_to_string(argv[i]);
        len = dst_string_length(vstr);
        for (j = 0; j < len; ++j) {
            putc(vstr[j], stdout);
        }
    }
    putc('\n', stdout);
    return 0;
}

int dst_sys_asm(DstValue *argv, uint32_t argn) {
    DstAssembleOptions opts;
    DstAssembleResult res;
    if (argn < 1) {
        dst_vm_fiber->ret = dst_cstringv("expected assembly source");
        return 1;
    }
    opts.source = argv[0];
    opts.parsemap = argn >= 2 ? argv[1] : dst_wrap_nil();
    opts.flags = 0;
    res = dst_asm(opts);
    if (res.status == DST_ASSEMBLE_OK) {
        dst_vm_fiber->ret = dst_wrap_function(dst_asm_func(res));
        return 0;
    } else {
        dst_vm_fiber->ret = dst_wrap_string(res.result.error);
        return 1;
    }
}

int dst_sys_tuple(DstValue *argv, uint32_t argn) {
    dst_vm_fiber->ret = dst_wrap_tuple(dst_tuple_n(argv, argn));
    return 0;
}

int dst_sys_array(DstValue *argv, uint32_t argn) {
    DstArray *array = dst_array(argn);
    array->count = argn;
    memcpy(array->data, argv, argn * sizeof(DstValue));
    dst_vm_fiber->ret = dst_wrap_array(array);
    return 0;
}

int dst_sys_table(DstValue *argv, uint32_t argn) {
    uint32_t i;
    DstTable *table = dst_table(argn/2);
    if (argn & 1) {
        dst_vm_fiber->ret = dst_cstringv("expected even number of arguments");
        return 1;
    }
    for (i = 0; i < argn; i += 2) {
        dst_table_put(table, argv[i], argv[i + 1]);
    }
    dst_vm_fiber->ret = dst_wrap_table(table);
    return 0;
}

int dst_sys_struct(DstValue *argv, uint32_t argn) {
    uint32_t i;
    DstValue *st = dst_struct_begin(argn/2);
    if (argn & 1) {
        dst_vm_fiber->ret = dst_cstringv("expected even number of arguments");
        return 1;
    }
    for (i = 0; i < argn; i += 2) {
        dst_struct_put(st, argv[i], argv[i + 1]);
    }
    dst_vm_fiber->ret = dst_wrap_struct(dst_struct_end(st));
    return 0;
}

int dst_sys_get(DstValue *argv, uint32_t argn) {
    uint32_t i;
    if (argn < 2) {
        dst_vm_fiber->ret = dst_cstringv("expected at least 1 argument");
        return 1;
    }
    DstValue ds = argv[0];
    for (i = 1; i < argn; i++) {
        const char *err = dst_try_get(ds, argv[i], &ds);
        if (NULL != err) {
            dst_vm_fiber->ret = dst_cstringv(err);
            return 1;
        }
    }
    dst_vm_fiber->ret = ds;
    return 0;
}

DstCFunction dst_vm_syscalls[256] = {
    dst_sys_print,
    dst_sys_asm,
    dst_sys_tuple,
    dst_sys_array,
    dst_sys_struct,
    dst_sys_table,
    NULL
};

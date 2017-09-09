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

#include "internal.h"

static void dst_cmodule_register(Dst *vm, const char *name, const DstModuleItem *mod) {
    uint32_t startLength;
    DstBuffer *buffer = dst_buffer(vm, 10);
    dst_buffer_append_cstring(vm, buffer, name);
    dst_buffer_push(vm, buffer, '.');
    startLength = buffer->count;
    while (mod->name != NULL) {
        DstValue key;
        buffer->count = startLength;
        dst_buffer_append_cstring(vm, buffer, mod->name);
        key = dst_wrap_symbol(dst_buffer_to_string(vm, buffer));
        dst_table_put(vm, vm->registry, key, dst_wrap_cfunction(mod->data));
        dst_table_put(vm, vm->registry, dst_wrap_cfunction(mod->data), key);
        mod++;
    }
}

static DstValue dst_cmodule_table(Dst *vm, const DstModuleItem *mod) {
    DstTable *module = dst_table(vm, 10);
    while (mod->name != NULL) {
        DstValue key = dst_string_cvs(vm, mod->name);
        dst_table_put(vm, module, key, dst_wrap_cfunction(mod->data));
        mod++;
    }
    return dst_wrap_table(module);
}

static DstValue dst_cmodule_struct(Dst *vm, const DstModuleItem *mod) {
    uint32_t count = 0;
    const DstModuleItem *m = mod;
    DstValue *st;
    while (m->name != NULL) {
        ++count;
        ++m;
    }
    st = dst_struct_begin(vm, count);
    m = mod;
    while (m->name != NULL) {
        dst_struct_put(st,
                dst_string_cvs(vm, m->name),
                dst_wrap_cfunction(m->data));
        ++m;
    }
    return dst_wrap_struct(dst_struct_end(vm, st));
}

void dst_module(Dst *vm, const char *packagename, const DstModuleItem *mod) {
    dst_table_put(vm, vm->modules, dst_string_cvs(vm, packagename), dst_cmodule_struct(vm, mod));
    dst_cmodule_register(vm, packagename, mod);
}

void dst_module_mutable(Dst *vm, const char *packagename, const DstModuleItem *mod) {
    dst_table_put(vm, vm->modules, dst_string_cvs(vm, packagename), dst_cmodule_table(vm, mod));
    dst_cmodule_register(vm, packagename, mod);
}

void dst_module_put(Dst *vm, const char *packagename, const char *name, DstValue v) {
    DstValue modtable = dst_table_get(vm->modules, dst_string_cvs(vm, packagename));
    if (modtable.type == DST_TABLE) {
        DstTable *table = modtable.data.table;
        if (v.type == DST_CFUNCTION) {
            DstValue key;
            DstBuffer *buffer = dst_buffer(vm, 10);
            dst_buffer_append_cstring(vm, buffer, packagename);
            dst_buffer_push(vm, buffer, '.');
            dst_buffer_append_cstring(vm, buffer, name);
            key = dst_wrap_string(dst_buffer_to_string(vm, buffer));
            dst_table_put(vm, vm->registry, key, v);
            dst_table_put(vm, vm->registry, v, key);
        }
        dst_table_put(vm, table, dst_string_cvs(vm, name), v);
    }
}

DstValue dst_module_get(Dst *vm, const char *packagename) {
    return dst_table_get(vm->modules, dst_string_cvs(vm, packagename));
}
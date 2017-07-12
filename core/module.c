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

#include <gst/gst.h>

static void gst_cmodule_register(Gst *vm, const char *name, const GstModuleItem *mod) {
    uint32_t startLength;
    GstBuffer *buffer = gst_buffer(vm, 10);
    gst_buffer_append_cstring(vm, buffer, name);
    gst_buffer_push(vm, buffer, '.');
    startLength = buffer->count;
    while (mod->name != NULL) {
        GstValue key;
        buffer->count = startLength;
        gst_buffer_append_cstring(vm, buffer, mod->name);
        key = gst_wrap_symbol(gst_buffer_to_string(vm, buffer));
        gst_table_put(vm, vm->registry, key, gst_wrap_cfunction(mod->data));
        gst_table_put(vm, vm->registry, gst_wrap_cfunction(mod->data), key);
        mod++;
    }
}

static GstValue gst_cmodule_table(Gst *vm, const GstModuleItem *mod) {
    GstTable *module = gst_table(vm, 10);
    while (mod->name != NULL) {
        GstValue key = gst_string_cvs(vm, mod->name);
        gst_table_put(vm, module, key, gst_wrap_cfunction(mod->data));
        mod++;
    }
    return gst_wrap_table(module);
}

static GstValue gst_cmodule_struct(Gst *vm, const GstModuleItem *mod) {
    uint32_t count = 0;
    const GstModuleItem *m = mod;
    GstValue *st;
    while (m->name != NULL) {
        ++count;
        ++m;
    }
    st = gst_struct_begin(vm, count);
    m = mod;
    while (m->name != NULL) {
        gst_struct_put(st,
                gst_string_cvs(vm, m->name),
                gst_wrap_cfunction(m->data));
        ++m;
    }
    return gst_wrap_struct(gst_struct_end(vm, st));
}

void gst_module(Gst *vm, const char *packagename, const GstModuleItem *mod) {
    gst_table_put(vm, vm->modules, gst_string_cvs(vm, packagename), gst_cmodule_struct(vm, mod));
    gst_cmodule_register(vm, packagename, mod);
}

void gst_module_mutable(Gst *vm, const char *packagename, const GstModuleItem *mod) {
    gst_table_put(vm, vm->modules, gst_string_cvs(vm, packagename), gst_cmodule_table(vm, mod));
    gst_cmodule_register(vm, packagename, mod);
}

void gst_module_put(Gst *vm, const char *packagename, const char *name, GstValue v) {
    GstValue modtable = gst_table_get(vm->modules, gst_string_cvs(vm, packagename));
    if (modtable.type == GST_TABLE) {
        GstTable *table = modtable.data.table;
        if (v.type == GST_CFUNCTION) {
            GstValue key;
            GstBuffer *buffer = gst_buffer(vm, 10);
            gst_buffer_append_cstring(vm, buffer, packagename);
            gst_buffer_push(vm, buffer, '.');
            gst_buffer_append_cstring(vm, buffer, name);
            key = gst_wrap_string(gst_buffer_to_string(vm, buffer));
            gst_table_put(vm, vm->registry, key, v);
            gst_table_put(vm, vm->registry, v, key);
        }
        gst_table_put(vm, table, gst_string_cvs(vm, name), v);
    }
}

GstValue gst_module_get(Gst *vm, const char *packagename) {
    return gst_table_get(vm->modules, gst_string_cvs(vm, packagename));
}
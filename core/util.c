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

/* Wrapper functions wrap a data type that is used from C into a
 * gst value, which can then be used in gst. */

GstValue gst_wrap_nil() {
    GstValue y;
    y.type = GST_NIL;
    return y;
}

int gst_check_nil(Gst *vm, uint32_t i) {
    GstValue a = gst_arg(vm, i);
    return a.type == GST_NIL;
}

#define GST_WRAP_DEFINE(NAME, TYPE, GTYPE, UM)\
GstValue gst_wrap_##NAME(TYPE x) {\
    GstValue y;\
    y.type = GTYPE;\
    y.data.UM = x;\
    return y;\
}\
\
int gst_check_##NAME(Gst *vm, uint32_t i, TYPE (*out)) {\
    GstValue a = gst_arg(vm, i);\
    if (a.type != GTYPE) return 0;\
    *out = a.data.UM;\
    return 1;\
}\

GST_WRAP_DEFINE(number, GstNumber, GST_NUMBER, number)
GST_WRAP_DEFINE(boolean, int, GST_BOOLEAN, boolean)
GST_WRAP_DEFINE(string, const uint8_t *, GST_STRING, string)
GST_WRAP_DEFINE(array, GstArray *, GST_ARRAY, array)
GST_WRAP_DEFINE(tuple, const GstValue *, GST_TUPLE, tuple)
GST_WRAP_DEFINE(struct, const GstValue *, GST_STRUCT, st)
GST_WRAP_DEFINE(thread, GstThread *, GST_THREAD, thread)
GST_WRAP_DEFINE(buffer, GstBuffer *, GST_BYTEBUFFER, buffer)
GST_WRAP_DEFINE(function, GstFunction *, GST_FUNCTION, function)
GST_WRAP_DEFINE(cfunction, GstCFunction, GST_CFUNCTION, cfunction)
GST_WRAP_DEFINE(object, GstObject *, GST_OBJECT, object)
GST_WRAP_DEFINE(userdata, void *, GST_USERDATA, pointer)
GST_WRAP_DEFINE(funcenv, GstFuncEnv *, GST_FUNCENV, env)
GST_WRAP_DEFINE(funcdef, GstFuncDef *, GST_FUNCDEF, def)

#undef GST_WRAP_DEFINE

GstValue gst_cmodule_object(Gst *vm, const GstModuleItem *mod) {
    GstObject *module = gst_object(vm, 10);
    while (mod->name != NULL) {
        GstValue key = gst_string_cv(vm, mod->name);
        gst_object_put(vm, module, key, gst_wrap_cfunction(mod->data));
        mod++;
    }
    return gst_wrap_object(module);
}

GstValue gst_cmodule_struct(Gst *vm, const GstModuleItem *mod) {
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
                gst_string_cv(vm, m->name),
                gst_wrap_cfunction(m->data));
        ++m;
    }
    return gst_wrap_struct(gst_struct_end(vm, st));
    
}

void gst_module_put(Gst *vm, const char *packagename, GstValue mod) {
    gst_object_put(vm, vm->modules, gst_string_cv(vm, packagename), mod);
}

GstValue gst_module_get(Gst *vm, const char *packagename) {
    return gst_object_get(vm->modules, gst_string_cv(vm, packagename));
}

void gst_register_put(Gst *vm, const char *name, GstValue c) {
    gst_object_put(vm, vm->registry, gst_string_cv(vm, name), c);
}

GstValue gst_register_get(Gst *vm, const char *name) {
    return gst_object_get(vm->registry, gst_string_cv(vm, name));
}

/****/
/* Misc */
/****/

/* Utilities for manipulating different types with the same semantics */

/* Read both tuples and arrays as c pointers + uint32_t length. Return 1 if the
 * view can be constructed, 0 if an invalid type. */
int gst_seq_view(GstValue seq, const GstValue **data, uint32_t *len) {
    if (seq.type == GST_ARRAY) {
        *data = seq.data.array->data;
        *len = seq.data.array->count;
        return 1;
    } else if (seq.type == GST_TUPLE) {
        *data = seq.data.st;
        *len = gst_tuple_length(seq.data.st);
        return 1;
    } 
    return 0;
}

/* Read both strings and buffer as unsigned character array + uint32_t len.
 * Returns 1 if the view can be constructed and 0 if the type is invalid. */
int gst_chararray_view(GstValue str, const uint8_t **data, uint32_t *len) {
    if (str.type == GST_STRING) {
        *data = str.data.string;
        *len = gst_string_length(str.data.string);
        return 1;
    } else if (str.type == GST_BYTEBUFFER) {
        *data = str.data.buffer->data;
        *len = str.data.buffer->count;
        return 1;
    }
    return 0;
}

/* Read both structs and objects as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int gst_hashtable_view(GstValue tab, const GstValue **data, uint32_t *cap) {
    if (tab.type == GST_OBJECT) {
        *data = tab.data.object->data;
        *cap = tab.data.object->capacity;
        return 1;
    } else if (tab.type == GST_STRUCT) {
        *data = tab.data.st;
        *cap = gst_struct_capacity(tab.data.st);
        return 1;
    }
    return 0;
}

/* Allow negative indexing to get from end of array like structure */
/* This probably isn't very fast - look at Lua conversion function.
 * I would like to keep this standard C for as long as possible, though. */
int32_t gst_to_index(GstNumber raw, int64_t len) {
    int32_t toInt = raw;
    if ((GstNumber) toInt == raw) {
        /* We were able to convert */
        if (toInt < 0 && len > 0) { 
            /* Index from end */
            if (toInt < -len) return -1;    
            return len + toInt;
        } else {    
            /* Normal indexing */
            if (toInt >= len) return -1;
            return toInt;
        }
    } else {
        return -1;
    }
}

int32_t gst_to_endrange(GstNumber raw, int64_t len) {
    int32_t toInt = raw;
    if ((GstNumber) toInt == raw) {
        /* We were able to convert */
        if (toInt < 0 && len > 0) { 
            /* Index from end */
            if (toInt < -len - 1) return -1;    
            return len + toInt + 1;
        } else {    
            /* Normal indexing */
            if (toInt >= len) return -1;
            return toInt;
        }
    } else {
        return -1;
    }
}

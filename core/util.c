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

GST_WRAP_DEFINE(real, GstReal, GST_REAL, real)
GST_WRAP_DEFINE(integer, GstInteger, GST_INTEGER, integer)
GST_WRAP_DEFINE(boolean, int, GST_BOOLEAN, boolean)
GST_WRAP_DEFINE(string, const uint8_t *, GST_STRING, string)
GST_WRAP_DEFINE(symbol, const uint8_t *, GST_SYMBOL, string)
GST_WRAP_DEFINE(array, GstArray *, GST_ARRAY, array)
GST_WRAP_DEFINE(tuple, const GstValue *, GST_TUPLE, tuple)
GST_WRAP_DEFINE(struct, const GstValue *, GST_STRUCT, st)
GST_WRAP_DEFINE(thread, GstThread *, GST_THREAD, thread)
GST_WRAP_DEFINE(buffer, GstBuffer *, GST_BYTEBUFFER, buffer)
GST_WRAP_DEFINE(function, GstFunction *, GST_FUNCTION, function)
GST_WRAP_DEFINE(cfunction, GstCFunction, GST_CFUNCTION, cfunction)
GST_WRAP_DEFINE(table, GstTable *, GST_TABLE, table)
GST_WRAP_DEFINE(funcenv, GstFuncEnv *, GST_FUNCENV, env)
GST_WRAP_DEFINE(funcdef, GstFuncDef *, GST_FUNCDEF, def)

#undef GST_WRAP_DEFINE

GstValue gst_wrap_userdata(void *x) {
    GstValue ret;
    ret.type = GST_USERDATA;
    ret.data.pointer = x;
    return ret;
}

void *gst_check_userdata(Gst *vm, uint32_t i, const GstUserType *type) {
    GstValue x = gst_arg(vm, i);
    GstUserdataHeader *h;
    if (x.type != GST_USERDATA) return NULL;
    h = gst_udata_header(x.data.pointer);
    if (h->type != type) return NULL;
    return x.data.pointer;
}

/****/
/* Parsing utils */
/****/


/* Get an integer power of 10 */
static double exp10(int power) {
    if (power == 0) return 1;
    if (power > 0) {
        double result = 10;
        int currentPower = 1;
        while (currentPower * 2 <= power) {
            result = result * result;
            currentPower *= 2;
        }
        return result * exp10(power - currentPower);
    } else {
        return 1 / exp10(-power);
    }
}

int gst_read_integer(const uint8_t *string, const uint8_t *end, int64_t *ret) {
    int sign = 1, x = 0;
    int64_t accum = 0;
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        x = *string;
        if (x < '0' || x > '9') return 0;
        x -= '0';
        accum = accum * 10 + x;
        ++string;
    }
    *ret = accum * sign;
    return 1;
}

/* Read a real from a string. Returns if successfuly
 * parsed a real from the enitre input string.
 * If returned 1, output is int ret.*/
int gst_read_real(const uint8_t *string, const uint8_t *end, double *ret, int forceInt) {
    int sign = 1, x = 0;
    double accum = 0, exp = 1, place = 1;
    /* Check the sign */
    if (*string == '-') {
        sign = -1;
        ++string;
    } else if (*string == '+') {
        ++string;
    }
    if (string >= end) return 0;
    while (string < end) {
        if (*string == '.' && !forceInt) {
            place = 0.1;
        } else if (!forceInt && (*string == 'e' || *string == 'E')) {
            /* Read the exponent */
            ++string;
            if (string >= end) return 0;
            if (!gst_read_real(string, end, &exp, 1))
                return 0;
            exp = exp10(exp);
            break;
        } else {
            x = *string;
            if (x < '0' || x > '9') return 0;
            x -= '0';
            if (place < 1) {
                accum += x * place;
                place *= 0.1;
            } else {
                accum *= 10;
                accum += x;
            }
        }
        ++string;
    }
    *ret = accum * sign * exp;
    return 1;
}

/****/
/* Module utils */
/****/

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
    if (str.type == GST_STRING || str.type == GST_SYMBOL) {
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

/* Read both structs and tables as the entries of a hashtable with
 * identical structure. Returns 1 if the view can be constructed and
 * 0 if the type is invalid. */
int gst_hashtable_view(GstValue tab, const GstValue **data, uint32_t *cap) {
    if (tab.type == GST_TABLE) {
        *data = tab.data.table->data;
        *cap = tab.data.table->capacity;
        return 1;
    } else if (tab.type == GST_STRUCT) {
        *data = tab.data.st;
        *cap = gst_struct_capacity(tab.data.st);
        return 1;
    }
    return 0;
}

GstReal gst_integer_to_real(GstInteger x) {
    return (GstReal) x;
}

GstInteger gst_real_to_integer(GstReal x) {
    return (GstInteger) x;
}

GstInteger gst_startrange(GstInteger raw, uint32_t len) {
    if (raw >= len)
        return -1;
    if (raw < 0)
        return len + raw;
    return raw;
}

GstInteger gst_endrange(GstInteger raw, uint32_t len) {
    if (raw > len)
        return -1;
    if (raw < 0)
        return len + raw + 1;
    return raw;
}

int gst_callc(Gst *vm, GstCFunction fn, int numargs, ...) {
    int result, i;
    va_list args;
    GstValue *stack;
    va_start(args, numargs);
    stack = gst_thread_beginframe(vm, vm->thread, gst_wrap_cfunction(fn), numargs);
    for (i = 0; i < numargs; ++i) {
        stack[i] = va_arg(args, GstValue);
    }
    va_end(args);
    result = fn(vm);
    gst_thread_popframe(vm, vm->thread);
    return result;
}

static GstTable *gst_env_keytab(Gst *vm, GstTable *env, const char *keyword) {
    GstTable *tab;
    GstValue key = gst_string_cv(vm, keyword);
    GstValue maybeTab = gst_table_get(env, key);
    if (maybeTab.type != GST_TABLE) {
        tab = gst_table(vm, 10);
        gst_table_put(vm, env, key, gst_wrap_table(tab));
    } else {
        tab = maybeTab.data.table;
    }
    return tab;
}

GstTable *gst_env_nils(Gst *vm, GstTable *env) {
    return gst_env_keytab(vm, env, "nils");
}

GstTable *gst_env_meta(Gst *vm, GstTable *env) {
    return gst_env_keytab(vm, env, "meta");
}

/* Add many global variables and bind to nil */
static void mergenils(Gst *vm, GstTable *destEnv, GstTable *nils) {
    const GstValue *data = nils->data;
    uint32_t len = nils->capacity;
    uint32_t i;
    GstTable *destNils = gst_env_nils(vm, destEnv);
    for (i = 0; i < len; i += 2) {
        if (data[i].type == GST_SYMBOL) {
            gst_table_put(vm, destEnv, data[i], gst_wrap_nil());
            gst_table_put(vm, destNils, data[i], gst_wrap_boolean(1));
        }
    }
}

/* Add many global variable metadata */
static void mergemeta(Gst *vm, GstTable *destEnv, GstTable *meta) {
    const GstValue *data = meta->data;
    uint32_t len = meta->capacity;
    uint32_t i;
    GstTable *destMeta = gst_env_meta(vm, destEnv);
    for (i = 0; i < len; i += 2) {
        if (data[i].type == GST_SYMBOL) {
            gst_table_put(vm, destMeta, data[i], data[i + 1]);
        }
    }
}

/* Simple strequal between gst string ans c string, no 0s in b allowed */
static int streq(const char *str, const uint8_t *b) {
    uint32_t len = gst_string_length(b);
    uint32_t i;
    const uint8_t *ustr = (const uint8_t *)str;
    for (i = 0; i < len; ++i) {
        if (ustr[i] != b[i])
            return 0;
    }
    return 1;
}

/* Add many global variables */
void gst_env_merge(Gst *vm, GstTable *destEnv, GstTable *srcEnv) {
    const GstValue *data = srcEnv->data;
    uint32_t len = srcEnv->capacity;
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        if (data[i].type == GST_SYMBOL) {
            gst_table_put(vm, destEnv, data[i], data[i + 1]);
        } else if (data[i].type == GST_STRING) {
            const uint8_t *k = data[i].data.string;
            if (streq("nils", k)) {
                if (data[i + 1].type == GST_TABLE)
                    mergenils(vm, destEnv, data[i + 1].data.table);
            } else if (streq("meta", k)) {
               if (data[i + 1].type == GST_TABLE)
                mergemeta(vm, destEnv, data[i + 1].data.table);
            }
        }
    }
}

void gst_env_put(Gst *vm, GstTable *env, GstValue key, GstValue value) {
    GstTable *meta = gst_env_meta(vm, env);
    gst_table_put(vm, meta, key, gst_wrap_nil());
    gst_table_put(vm, env, key, value);
    if (value.type == GST_NIL) {
        gst_table_put(vm, gst_env_nils(vm, env), key, gst_wrap_boolean(1));
    }
}

void gst_env_putc(Gst *vm, GstTable *env, const char *key, GstValue value) {
    GstValue keyv = gst_string_cvs(vm, key);
    gst_env_put(vm, env, keyv, value);
}

void gst_env_putvar(Gst *vm, GstTable *env, GstValue key, GstValue value) {
    GstTable *meta = gst_env_meta(vm, env);
    GstTable *newmeta = gst_table(vm, 4);
    GstArray *ref = gst_array(vm, 1);
    ref->count = 1;
    ref->data[0] = value;
    gst_table_put(vm, env, key, gst_wrap_array(ref));
    gst_table_put(vm, newmeta, gst_string_cvs(vm, "mutable"), gst_wrap_boolean(1));
    gst_table_put(vm, meta, key, gst_wrap_table(newmeta));
}

void gst_env_putvarc(Gst *vm, GstTable *env, const char *key, GstValue value) {
    GstValue keyv = gst_string_cvs(vm, key);
    gst_env_putvar(vm, env, keyv, value);
}
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
    gst_table_put(vm, newmeta, gst_string_cv(vm, "mutable"), gst_wrap_boolean(1));
    gst_table_put(vm, meta, key, gst_wrap_table(newmeta));
}

void gst_env_putvarc(Gst *vm, GstTable *env, const char *key, GstValue value) {
    GstValue keyv = gst_string_cvs(vm, key);
    gst_env_putvar(vm, env, keyv, value);
}
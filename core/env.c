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

static DstTable *dst_env_keytab(Dst *vm, DstTable *env, const char *keyword) {
    DstTable *tab;
    DstValue key = dst_string_cv(vm, keyword);
    DstValue maybeTab = dst_table_get(env, key);
    if (maybeTab.type != DST_TABLE) {
        tab = dst_table(vm, 10);
        dst_table_put(vm, env, key, dst_wrap_table(tab));
    } else {
        tab = maybeTab.data.table;
    }
    return tab;
}

DstTable *dst_env_nils(Dst *vm, DstTable *env) {
    return dst_env_keytab(vm, env, "nils");
}

DstTable *dst_env_meta(Dst *vm, DstTable *env) {
    return dst_env_keytab(vm, env, "meta");
}

/* Add many global variables and bind to nil */
static void mergenils(Dst *vm, DstTable *destEnv, DstTable *nils) {
    const DstValue *data = nils->data;
    uint32_t len = nils->capacity;
    uint32_t i;
    DstTable *destNils = dst_env_nils(vm, destEnv);
    for (i = 0; i < len; i += 2) {
        if (data[i].type == DST_SYMBOL) {
            dst_table_put(vm, destEnv, data[i], dst_wrap_nil());
            dst_table_put(vm, destNils, data[i], dst_wrap_boolean(1));
        }
    }
}

/* Add many global variable metadata */
static void mergemeta(Dst *vm, DstTable *destEnv, DstTable *meta) {
    const DstValue *data = meta->data;
    uint32_t len = meta->capacity;
    uint32_t i;
    DstTable *destMeta = dst_env_meta(vm, destEnv);
    for (i = 0; i < len; i += 2) {
        if (data[i].type == DST_SYMBOL) {
            dst_table_put(vm, destMeta, data[i], data[i + 1]);
        }
    }
}

/* Simple strequal between dst string ans c string, no 0s in b allowed */
static int streq(const char *str, const uint8_t *b) {
    uint32_t len = dst_string_length(b);
    uint32_t i;
    const uint8_t *ustr = (const uint8_t *)str;
    for (i = 0; i < len; ++i) {
        if (ustr[i] != b[i])
            return 0;
    }
    return 1;
}

/* Add many global variables */
void dst_env_merge(Dst *vm, DstTable *destEnv, DstTable *srcEnv) {
    const DstValue *data = srcEnv->data;
    uint32_t len = srcEnv->capacity;
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        if (data[i].type == DST_SYMBOL) {
            dst_table_put(vm, destEnv, data[i], data[i + 1]);
        } else if (data[i].type == DST_STRING) {
            const uint8_t *k = data[i].data.string;
            if (streq("nils", k)) {
                if (data[i + 1].type == DST_TABLE)
                    mergenils(vm, destEnv, data[i + 1].data.table);
            } else if (streq("meta", k)) {
               if (data[i + 1].type == DST_TABLE)
                mergemeta(vm, destEnv, data[i + 1].data.table);
            }
        }
    }
}

void dst_env_put(Dst *vm, DstTable *env, DstValue key, DstValue value) {
    DstTable *meta = dst_env_meta(vm, env);
    dst_table_put(vm, meta, key, dst_wrap_nil());
    dst_table_put(vm, env, key, value);
    if (value.type == DST_NIL) {
        dst_table_put(vm, dst_env_nils(vm, env), key, dst_wrap_boolean(1));
    }
}

void dst_env_putc(Dst *vm, DstTable *env, const char *key, DstValue value) {
    DstValue keyv = dst_string_cvs(vm, key);
    dst_env_put(vm, env, keyv, value);
}

void dst_env_putvar(Dst *vm, DstTable *env, DstValue key, DstValue value) {
    DstTable *meta = dst_env_meta(vm, env);
    DstTable *newmeta = dst_table(vm, 4);
    DstArray *ref = dst_array(vm, 1);
    ref->count = 1;
    ref->data[0] = value;
    dst_table_put(vm, env, key, dst_wrap_array(ref));
    dst_table_put(vm, newmeta, dst_string_cv(vm, "mutable"), dst_wrap_boolean(1));
    dst_table_put(vm, meta, key, dst_wrap_table(newmeta));
}

void dst_env_putvarc(Dst *vm, DstTable *env, const char *key, DstValue value) {
    DstValue keyv = dst_string_cvs(vm, key);
    dst_env_putvar(vm, env, keyv, value);
}
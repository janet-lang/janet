/*
* Copyright (c) 2018 Calvin Rose
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
#include <setjmp.h>

#include "state.h"
#include "vector.h"
#include "gc.h"

typedef struct {
    jmp_buf err;
    DstBuffer *buf;
    DstTable seen;
    DstFuncEnv **seen_envs;
    DstFuncDef **seen_defs;
    int32_t nextid;
} MarshalState;

enum {
    MR_OK,
    MR_STACKOVERFLOW,
    MR_NYI,
    MR_NRV,
    MR_OVERFLOW
} MarshalResult;

const char *mr_strings[] = {
    "",
    "stack overflow",
    "type NYI",
    "no registry value",
    "buffer overflow"
};

/* Lead bytes in marshaling protocol */
enum {
    LB_NIL = 200,
    LB_FALSE,
    LB_TRUE,
    LB_FIBER,
    LB_INTEGER,
    LB_REAL,
    LB_STRING,
    LB_SYMBOL,
    LB_ARRAY,
    LB_TUPLE,
    LB_TABLE,
    LB_TABLE_PROTO,
    LB_STRUCT,
    LB_BUFFER,
    LB_FUNCTION,
    LB_REGISTRY,
    LB_ABSTRACT,
    LB_REFERENCE,
    LB_FUNCENV_REF,
    LB_FUNCDEF_REF
} LeadBytes;

/* Marshal an integer onto the buffer */
static void pushint(MarshalState *st, int32_t x) {
    if (x >= 0 && x < 200) {
        if (dst_buffer_push_u8(st->buf, x)) longjmp(st->err, MR_OVERFLOW);
    } else {
        uint8_t intbuf[5];
        intbuf[0] = LB_INTEGER;
        intbuf[1] = x & 0xFF;
        intbuf[2] = (x >> 8) & 0xFF;
        intbuf[3] = (x >> 16) & 0xFF;
        intbuf[4] = (x >> 24) & 0xFF;
        if (dst_buffer_push_bytes(st->buf, intbuf, 5)) longjmp(st->err, MR_OVERFLOW);
    }
}

static void pushbyte(MarshalState *st, uint8_t b) {
    if (dst_buffer_push_u8(st->buf, b)) longjmp(st->err, MR_OVERFLOW);
}

static void pushbytes(MarshalState *st, const uint8_t *bytes, int32_t len) {
    if (dst_buffer_push_bytes(st->buf, bytes, len)) longjmp(st->err, MR_OVERFLOW);
}

/* Forward declaration to enable mutual recursion. */
static void marshal_one(MarshalState *st, Dst x, int flags);

/* Marshal a function env */
static void marshal_one_env(MarshalState *st, DstFuncEnv *env, int flags) {
    for (int32_t i = 0; i < dst_v_count(st->seen_envs); i++) {
        if (st->seen_envs[i] == env) {
            pushbyte(st, LB_FUNCENV_REF);
            pushint(st, i);
            return;
        }
    }
    dst_v_push(st->seen_envs, env);
    pushint(st, env->offset);
    pushint(st, env->length);
    if (env->offset >= 0) {
        /* On stack variant */
        marshal_one(st, dst_wrap_fiber(env->as.fiber), flags);
    } else {
        /* Off stack variant */
        for (int32_t i = 0; i < env->length; i++)
            marshal_one(st, env->as.values[i], flags);
    }
}

/* Marshal a function def */
static void marshal_one_def(MarshalState *st, DstFuncDef *def, int flags) {
    for (int32_t i = 0; i < dst_v_count(st->seen_defs); i++) {
        if (st->seen_defs[i] == def) {
            pushbyte(st, LB_FUNCDEF_REF);
            pushint(st, i);
            return;
        }
    }
    dst_v_push(st->seen_defs, def);
    pushint(st, def->flags);
    pushint(st, def->slotcount);
    pushint(st, def->arity);
    pushint(st, def->constants_length);
    pushint(st, def->bytecode_length);
    if (def->flags & DST_FUNCDEF_FLAG_HASENVS)
        pushint(st, def->environments_length);
    if (def->flags & DST_FUNCDEF_FLAG_HASDEFS)
        pushint(st, def->defs_length);
    if (def->flags & DST_FUNCDEF_FLAG_HASNAME)
        marshal_one(st, dst_wrap_string(def->name), flags);
    if (def->flags & DST_FUNCDEF_FLAG_HASSOURCE)
        marshal_one(st, dst_wrap_string(def->source), flags);
        
    /* marshal constants */
    for (int32_t i = 0; i < def->constants_length; i++)
        marshal_one(st, def->constants[i], flags);
        
    /* marshal the bytecode */
    for (int32_t i = 0; i < def->bytecode_length; i++) {
        pushbyte(st, def->bytecode[i] & 0xFF);
        pushbyte(st, (def->bytecode[i] >> 8) & 0xFF);
        pushbyte(st, (def->bytecode[i] >> 16) & 0xFF);
        pushbyte(st, (def->bytecode[i] >> 24) & 0xFF);
    }
    
    /* marshal the environments if needed */
    for (int32_t i = 0; i < def->environments_length; i++)
       pushint(st, def->environments[i]);
       
    /* marshal the sub funcdefs if needed */
    for (int32_t i = 0; i < def->defs_length; i++)
        marshal_one_def(st, def->defs[i], flags);

    /* marshal source maps if needed */
    if (def->flags & DST_FUNCDEF_FLAG_HASSOURCEMAP) {
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            DstSourceMapping map = def->sourcemap[i];
            pushint(st, map.line);
            pushint(st, map.column);
        }
    }
}

/* The main body of the marshaling function. Is the main
 * entry point for the mutually recursive functions. */
static void marshal_one(MarshalState *st, Dst x, int flags) {
    DstType type = dst_type(x);
    if ((flags & 0xFFFF) > DST_RECURSION_GUARD) {
        longjmp(st->err, MR_STACKOVERFLOW);
    }

    /* Check simple primitvies (non reference types, no benefit from memoization) */
    switch (type) {
        default:
            break;
        case DST_NIL:
        case DST_FALSE:
        case DST_TRUE:
            pushbyte(st, 200 + type);
            return;
        case DST_INTEGER:
            pushint(st, dst_unwrap_integer(x));
            return;
    }

    /* Check reference */
    {
        Dst check = dst_table_get(&st->seen, x);
        if (dst_checktype(check, DST_INTEGER)) {
            pushbyte(st, LB_REFERENCE);
            pushint(st, dst_unwrap_integer(check));
            return;
        }
    }

#define MARK_SEEN() \
    dst_table_put(&st->seen, x, dst_wrap_integer(st->nextid++))

    /* Reference types */
    switch (type) {
        case DST_REAL:
            {
                union {
                    double d;
                    uint8_t bytes[8];
                } u;
                u.d = dst_unwrap_real(x);
#ifdef DST_BIG_ENDIAN
                /* Swap byte order */
                uint8_t temp;
                temp = u.bytes[7]; u.bytes[7] = u.bytes[0]; u.bytes[0] = temp;
                temp = u.bytes[6]; u.bytes[6] = u.bytes[1]; u.bytes[1] = temp;
                temp = u.bytes[5]; u.bytes[5] = u.bytes[2]; u.bytes[2] = temp;
                temp = u.bytes[4]; u.bytes[4] = u.bytes[3]; u.bytes[3] = temp;
#endif
                pushbyte(st, LB_REAL);
                pushbytes(st, u.bytes, 8);
                MARK_SEEN();
            }
            return;
        case DST_STRING:
        case DST_SYMBOL:
            {
                const uint8_t *str = dst_unwrap_string(x);
                int32_t length = dst_string_length(str);
                /* Record reference */
                MARK_SEEN();
                uint8_t lb = (type == DST_STRING) ? LB_STRING : LB_SYMBOL;
                pushbyte(st, lb);
                pushint(st, length);
                pushbytes(st, str, length);
            }
            return;
        case DST_BUFFER:
            {
                DstBuffer *buffer = dst_unwrap_buffer(x);
                /* Record reference */
                MARK_SEEN();
                pushbyte(st, LB_BUFFER);
                pushint(st, buffer->count);
                pushbytes(st, buffer->data, buffer->count);
            }
            return;
        case DST_ARRAY:
            {
                int32_t i;
                DstArray *a = dst_unwrap_array(x);
                MARK_SEEN();
                pushbyte(st, LB_ARRAY);
                pushint(st, a->count);
                for (i = 0; i < a->count; i++)
                    marshal_one(st, a->data[i], flags + 1);
            }
            return;
        case DST_TUPLE:
            {
                int32_t i, count;
                const Dst *tup = dst_unwrap_tuple(x);
                count = dst_tuple_length(tup);
                pushbyte(st, LB_TUPLE);
                pushint(st, count);
                for (i = 0; i < count; i++)
                    marshal_one(st, tup[i], flags + 1);
                /* Mark as seen AFTER marshaling */
                MARK_SEEN();
            }
            return;
        case DST_TABLE:
            {
                const DstKV *kv = NULL;
                DstTable *t = dst_unwrap_table(x);
                MARK_SEEN();
                pushbyte(st, t->proto ? LB_TABLE_PROTO : LB_TABLE);
                pushint(st, t->count);
                if (t->proto)
                    marshal_one(st, dst_wrap_table(t->proto), flags + 1);
                while ((kv = dst_table_next(t, kv))) {
                    marshal_one(st, kv->key, flags + 1);
                    marshal_one(st, kv->value, flags + 1);
                }
            }
            return;
        case DST_STRUCT:
            {
                int32_t count;
                const DstKV *kv = NULL;
                const DstKV *struct_ = dst_unwrap_struct(x);
                count = dst_struct_length(struct_);
                pushbyte(st, LB_STRUCT);
                pushint(st, count);
                while ((kv = dst_struct_next(struct_, kv))) {
                    marshal_one(st, kv->key, flags + 1);
                    marshal_one(st, kv->value, flags + 1);
                }
                /* Mark as seen AFTER marshaling */
                MARK_SEEN();
            }
            return;
        case DST_ABSTRACT:
        case DST_CFUNCTION:
            {
                MARK_SEEN();
                Dst regval = dst_table_get(dst_vm_registry, x);
                if (dst_checktype(regval, DST_NIL)) {
                    goto noregval;
                }
                const uint8_t *regname = dst_to_string(regval);
                pushbyte(st, LB_REGISTRY);
                pushint(st, dst_string_length(regname));
                pushbytes(st, regname, dst_string_length(regname));
            }
            return;
        case DST_FUNCTION:
            {
                pushbyte(st, LB_FUNCTION);
                DstFunction *func = dst_unwrap_function(x);
                marshal_one_def(st, func->def, flags);
                /* Mark seen after reading def, but before envs */
                MARK_SEEN();
                for (int32_t i = 0; i < func->def->environments_length; i++)
                    marshal_one_env(st, func->envs[i], flags);
            }
            return;
        case DST_FIBER:
        default:
            goto nyi;
    }

#undef MARK_SEEN

    /* Errors */

nyi:
    longjmp(st->err, MR_NYI);
    
noregval:
    longjmp(st->err, MR_NRV);
}

int dst_marshal(DstBuffer *buf, Dst x, int flags) {
    int status;
    MarshalState st; 
    st.buf = buf;
    st.nextid = 0;
    st.seen_defs = NULL;
    st.seen_envs = NULL;
    dst_table_init(&st.seen, 0);
    if (!(status = setjmp(st.err)))
        marshal_one(&st, x, flags);
    dst_table_deinit(&st.seen);
    return status;
}

typedef struct {
    jmp_buf err;
    DstArray lookup;
    DstFuncEnv **lookup_envs;
    DstFuncDef **lookup_defs;
    const uint8_t *end;
} UnmarshalState;

enum {
    UMR_OK,
    UMR_STACKOVERFLOW,
    UMR_EOS,
    UMR_UNKNOWN,
    UMR_EXPECTED_INTEGER,
    UMR_EXPECTED_TABLE,
    UMR_EXPECTED_FIBER,
    UMR_EXPECTED_STRING,
    UMR_INVALID_REFERENCE,
    UMR_INVALID_BYTECODE
} UnmarshalResult;

const char *umr_strings[] = {
    "",
    "stack overflow",
    "unexpected end of source",
    "unknown byte",
    "expected integer",
    "expected table",
    "expected fiber",
    "expected string",
    "invalid reference",
    "invalid bytecode"
};

/* Helper to read a 32 bit integer from an unmarshal state */
static int32_t readint(UnmarshalState *st, const uint8_t **atdata) {
    const uint8_t *data = *atdata;
    int32_t ret;
    if (data >= st->end) longjmp(st->err, UMR_EOS);
    if (*data < 200) {
        ret = *data++;
    } else if (*data == LB_INTEGER) {
        if (data + 5 > st->end) longjmp(st->err, UMR_EOS);
        ret = (data[1]) |
            (data[2] << 8) |
            (data[3] << 16) |
            (data[4] << 24);
        data += 5;
    } else {
        longjmp(st->err, UMR_EXPECTED_INTEGER);
    }
    *atdata = data;
    return ret;
}

static uint8_t readbyte(UnmarshalState *st, const uint8_t **atdata) {
    const uint8_t *data = *atdata;
    if (data >= st->end) longjmp(st->err, UMR_EOS);
    uint8_t ret = *data++;
    *atdata = data;
    return ret;
}

static void readbytes(UnmarshalState *st, const uint8_t **atdata, uint8_t *into, int32_t n) {
    const uint8_t *data = *atdata;
    if (data + n>= st->end) longjmp(st->err, UMR_EOS);
    memcpy(into, data, n);
    *atdata = data + n;
}

/* Forward declaration */
static const uint8_t *unmarshal_one(
        UnmarshalState *st,
        const uint8_t *data,
        Dst *out,
        int flags);

/* Unmarshal a funcenv */
static const uint8_t *unmarshal_one_env(
        UnmarshalState *st,
        const uint8_t *data,
        DstFuncEnv **out,
        int flags) {
    const uint8_t *end = st->end;
    if (data >= end) longjmp(st->err, UMR_EOS);
    if (*data == LB_FUNCENV_REF) {
        int32_t index = readint(st, &data);
        if (index < 0 || index >= dst_v_count(st->lookup_envs))
            longjmp(st->err, UMR_INVALID_REFERENCE);
        *out = st->lookup_envs[index];
    } else {
        DstFuncEnv *env = dst_gcalloc(DST_MEMORY_FUNCENV, sizeof(DstFuncEnv));
        dst_v_push(st->lookup_envs, env);
        env->offset = readint(st, &data);
        env->length = readint(st, &data);
        if (env->offset >= 0) {
            /* On stack variant */
            Dst fiberv;
            data = unmarshal_one(st, data, &fiberv, flags);
            if (!dst_checktype(fiberv, DST_FIBER)) longjmp(st->err, UMR_EXPECTED_FIBER);
            env->as.fiber = dst_unwrap_fiber(fiberv);
        } else {
            /* Off stack variant */
            env->as.values = malloc(sizeof(Dst) * env->length);
            if (!env->as.values) {
                DST_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < env->length; i++) {
                data = unmarshal_one(st, data, env->as.values + i, flags);
            }
        }
    }
    return data;
}

/* Unmarshal a funcdef */
static const uint8_t *unmarshal_one_def(
        UnmarshalState *st,
        const uint8_t *data,
        DstFuncDef **out,
        int flags) {
    const uint8_t *end = st->end;
    if (data >= end) longjmp(st->err, UMR_EOS);
    if (*data == LB_FUNCDEF_REF) {
        int32_t index = readint(st, &data);
        if (index < 0 || index >= dst_v_count(st->lookup_defs))
            longjmp(st->err, UMR_INVALID_REFERENCE);
        *out = st->lookup_defs[index];
    } else {
        DstFuncDef *def = dst_gcalloc(DST_MEMORY_FUNCDEF, sizeof(DstFuncDef));
        dst_v_push(st->lookup_defs, def);
        
        /* Read flags and other fixed values */
        def->flags = readint(st, &data);
        def->slotcount = readint(st, &data);
        def->arity = readint(st, &data);
        def->constants_length = readint(st, &data);
        def->bytecode_length = readint(st, &data);
        
        def->environments_length = 0;
        def->defs_length = 0;
        def->name = NULL;
        def->source = NULL;
        if (def->flags & DST_FUNCDEF_FLAG_HASENVS)
            def->environments_length = readint(st, &data);
        if (def->flags & DST_FUNCDEF_FLAG_HASDEFS)
            def->defs_length = readint(st, &data);
        if (def->flags & DST_FUNCDEF_FLAG_HASNAME) {
            Dst x;
            data = unmarshal_one(st, data, &x, flags + 1);
            if (!dst_checktype(x, DST_STRING)) longjmp(st->err, UMR_EXPECTED_STRING);
            def->name = dst_unwrap_string(x);
        }
        if (def->flags & DST_FUNCDEF_FLAG_HASSOURCE) {
            Dst x;
            data = unmarshal_one(st, data, &x, flags + 1);
            if (!dst_checktype(x, DST_STRING)) longjmp(st->err, UMR_EXPECTED_STRING);
            def->source = dst_unwrap_string(x);
        }
        
        /* Unmarshal constants */
        if (def->constants_length) {
            def->constants = malloc(sizeof(Dst) * def->constants_length);
            if (!def->constants) {
                DST_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < def->constants_length; i++)
                data = unmarshal_one(st, data, def->constants + i, flags + 1);
        } else {
            def->constants = NULL;
        }
        
        /* Unmarshal bytecode */
        def->bytecode = malloc(sizeof(uint32_t) * def->bytecode_length);
        if (!def->bytecode) {
            DST_OUT_OF_MEMORY;
        }
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            if (data + 4 > st->end) longjmp(st->err, UMR_EOS);
            def->bytecode[i] = 
                (uint32_t)(data[0]) |
                ((uint32_t)(data[1]) << 8) |
                ((uint32_t)(data[2]) << 16) |
                ((uint32_t)(data[3]) << 24);
        }
        
        /* Unmarshal environments */
        if (def->flags & DST_FUNCDEF_FLAG_HASENVS) {
            def->environments = malloc(sizeof(int32_t) * def->environments_length);
            if (!def->environments) {
                DST_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < def->environments_length; i++) {
                def->environments[i] = readint(st, &data);
            }
        } else {
            def->environments = NULL;
        }
        
        /* Unmarshal sub funcdefs */
        if (def->flags & DST_FUNCDEF_FLAG_HASDEFS) {
            def->defs = malloc(sizeof(DstFuncDef *) * def->defs_length);
            if (!def->defs) {
                DST_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < def->defs_length; i++) {
                data = unmarshal_one_def(st, data, def->defs + i, flags + 1); 
            }
        } else {
            def->defs = NULL;
        }
        
        /* Unmarshal source maps if needed */
        if (def->flags & DST_FUNCDEF_FLAG_HASSOURCEMAP) {
            def->sourcemap = malloc(sizeof(DstSourceMapping) * def->bytecode_length);
            if (!def->sourcemap) {
                DST_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < def->bytecode_length; i++) {
                def->sourcemap[i].line = readint(st, &data);
                def->sourcemap[i].column = readint(st, &data);
            }
        } else {
            def->sourcemap = NULL;
        } 
        
        /* Validate */
        if (dst_verify(def)) longjmp(st->err, UMR_INVALID_BYTECODE);
    }
    return data;
}

static const uint8_t *unmarshal_one(
        UnmarshalState *st,
        const uint8_t *data,
        Dst *out,
        int flags) {
    const uint8_t *end = st->end;
    uint8_t lead;
    if ((flags & 0xFFFF) > DST_RECURSION_GUARD) {
        longjmp(st->err, UMR_STACKOVERFLOW);
    }
#define EXTRA(N) if (data + N > end) longjmp(st->err, UMR_EOS)
    EXTRA(1);
    lead = data[0];
    if (lead < 200) {
        *out = dst_wrap_integer(lead);
        return data + 1;
    }
    switch (lead) {
        case LB_NIL:
            *out = dst_wrap_nil();
            return data + 1;
        case LB_FALSE:
            *out = dst_wrap_false();
            return data + 1;
        case LB_TRUE:
            *out = dst_wrap_true();
            return data + 1;
        case LB_INTEGER:
            /* Long integer */
            EXTRA(5);
            *out = dst_wrap_integer(
                    (data[1]) |
                    (data[2] << 8) |
                    (data[3] << 16) |
                    (data[4] << 24));
            return data + 5;
        case LB_REAL:
            /* Real */
            {
                union {
                    double d;
                    uint8_t bytes[8];
                } u;
                EXTRA(9);
#ifdef DST_BIG_ENDIAN
                u.bytes[0] = data[8];
                u.bytes[1] = data[7];
                u.bytes[2] = data[6];
                u.bytes[5] = data[5];
                u.bytes[4] = data[4];
                u.bytes[5] = data[3];
                u.bytes[6] = data[2];
                u.bytes[7] = data[1];
#else
                memcpy(&u.bytes, data + 1, sizeof(double));
#endif
                *out = dst_wrap_real(u.d);
                dst_array_push(&st->lookup, *out);
                return data + 9;
            }
        case LB_STRING:
        case LB_SYMBOL:
        case LB_BUFFER:
        case LB_REGISTRY:
            {
                data++;
                int32_t len = readint(st, &data);
                EXTRA(len);
                if (lead == LB_STRING) {
                    const uint8_t *str = dst_string(data, len);
                    *out = dst_wrap_string(str);
                } else if (lead == LB_SYMBOL) {
                    const uint8_t *str = dst_symbol(data, len);
                    *out = dst_wrap_symbol(str);
                } else if (lead == LB_REGISTRY) {
                    Dst regkey = dst_symbolv(data, len);
                    *out = dst_table_get(dst_vm_registry, regkey);
                } else { /* (lead == LB_BUFFER) */
                    DstBuffer *buffer = dst_buffer(len);
                    buffer->count = len;
                    memcpy(buffer->data, data, len);
                    *out = dst_wrap_buffer(buffer);
                }
                dst_array_push(&st->lookup, *out);
                return data + len;
            }
        case LB_FUNCTION:
            {
                DstFunction *func;
                DstFuncDef *def;
                data = unmarshal_one_def(st, data + 1, &def, flags + 1);
                func = dst_gcalloc(DST_MEMORY_FUNCTION, sizeof(DstFunction) +
                    def->environments_length * sizeof(DstFuncEnv));
                *out = dst_wrap_function(func);
                dst_array_push(&st->lookup, *out);
                for (int32_t i = 0; i < def->environments_length; i++) {
                    data = unmarshal_one_env(st, data, func->envs + i, flags + 1);
                }
                return data;
            }
        case LB_REFERENCE:
        case LB_ARRAY:
        case LB_TUPLE:
        case LB_STRUCT:
        case LB_TABLE:
        case LB_TABLE_PROTO:
            /* Things that open with integers */
            {
                data++;
                int32_t len = readint(st, &data);
                if (lead == LB_ARRAY) {
                    /* Array */
                    DstArray *array = dst_array(len);
                    array->count = len;
                    *out = dst_wrap_array(array);
                    dst_array_push(&st->lookup, *out);
                    for (int32_t i = 0; i < len; i++) {
                        data = unmarshal_one(st, data, array->data + i, flags + 1);
                    }
                } else if (lead == LB_TUPLE) {
                    /* Tuple */
                    Dst *tup = dst_tuple_begin(len);
                    for (int32_t i = 0; i < len; i++) {
                        data = unmarshal_one(st, data, tup + i, flags + 1);
                    }
                    *out = dst_wrap_tuple(dst_tuple_end(tup));
                    dst_array_push(&st->lookup, *out);
                } else if (lead == LB_STRUCT) {
                    /* Struct */
                    DstKV *struct_ = dst_struct_begin(len);
                    for (int32_t i = 0; i < len; i++) {
                        Dst key, value;
                        data = unmarshal_one(st, data, &key, flags + 1);
                        data = unmarshal_one(st, data, &value, flags + 1);
                        dst_struct_put(struct_, key, value);
                    }
                    *out = dst_wrap_struct(dst_struct_end(struct_));
                    dst_array_push(&st->lookup, *out);
                } else if (lead == LB_REFERENCE) {
                    if (len < 0 || len >= st->lookup.count)
                        longjmp(st->err, UMR_INVALID_REFERENCE);
                    *out = st->lookup.data[len];
                } else {
                    /* Table */
                    DstTable *t = dst_table(len);
                    *out = dst_wrap_table(t);
                    dst_array_push(&st->lookup, *out);
                    if (lead == LB_TABLE_PROTO) {
                        Dst proto;
                        data = unmarshal_one(st, data, &proto, flags + 1);
                        if (!dst_checktype(proto, DST_TABLE)) longjmp(st->err, UMR_EXPECTED_TABLE);
                        t->proto = dst_unwrap_table(proto);
                    }
                    for (int32_t i = 0; i < len; i++) {
                        Dst key, value;
                        data = unmarshal_one(st, data, &key, flags + 1);
                        data = unmarshal_one(st, data, &value, flags + 1);
                        dst_table_put(t, key, value);
                    }
                }
                return data;
            }
        default:
            longjmp(st->err, UMR_UNKNOWN);
            return NULL;
    }
#undef EXTRA
}

int dst_unmarshal(
        const uint8_t *bytes,
        size_t len, 
        int flags, 
        Dst *out, 
        const uint8_t **next) {
    int status;
    /* Avoid longjmp clobber warning in GCC */
    UnmarshalState st; 
    st.end = bytes + len;
    st.lookup_defs = NULL;
    st.lookup_envs = NULL;
    dst_array_init(&st.lookup, 0);
    if (!(status = setjmp(st.err))) {
        const uint8_t *nextbytes = unmarshal_one(&st, bytes, out, flags);
        if (next) *next = nextbytes;
    }
    dst_array_deinit(&st.lookup);
    return status;
}

/* C functions */

static int cfun_marshal(DstArgs args) {
    DstBuffer *buffer;
    int status;
    DST_MINARITY(args, 1);
    DST_MAXARITY(args, 2);
    if (args.n == 2) {
        /* Buffer provided */
        DST_ARG_BUFFER(buffer, args, 1);
    } else {
        buffer = dst_buffer(10);
    }
    status = dst_marshal(buffer, args.v[0], 0);
    if (status) {
        DST_THROW(args, mr_strings[status]);
    }
    DST_RETURN_BUFFER(args, buffer);
}

static int cfun_unmarshal(DstArgs args) {
    const uint8_t *bytes;
    int32_t len;
    int status;
    DST_FIXARITY(args, 1);
    DST_ARG_BYTES(bytes, len, args, 0);
    status = dst_unmarshal(bytes, (size_t) len, 0, args.ret, NULL);
    if (status) {
        DST_THROW(args, umr_strings[status]);
    }
    return DST_SIGNAL_OK;
}

static const DstReg cfuns[] = {
    {"marsh.marshal", cfun_marshal},
    {"marsh.unmarshal", cfun_unmarshal},
    {NULL, NULL}
};

/* Module entry point */
int dst_lib_marsh(DstArgs args) {
    DstTable *env = dst_env_arg(args);
    dst_env_cfuns(env, cfuns);
    return 0;
}

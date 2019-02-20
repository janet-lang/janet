/*
* Copyright (c) 2019 Calvin Rose
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

#ifndef JANET_AMALG
#include <janet.h>
#include "state.h"
#include "vector.h"
#include "gc.h"
#include "fiber.h"
#include "util.h"
#endif

typedef struct {
    jmp_buf err;
    Janet current;
    JanetBuffer *buf;
    JanetTable seen;
    JanetTable *rreg;
    JanetFuncEnv **seen_envs;
    JanetFuncDef **seen_defs;
    int32_t nextid;
} MarshalState;

enum {
    MR_OK,
    MR_STACKOVERFLOW,
    MR_NYI,
    MR_NRV,
    MR_C_STACKFRAME,
    MR_OVERFLOW,
    MR_LIVEFIBER
} MarshalResult;

const char *mr_strings[] = {
    "",
    "stack overflow",
    "type NYI",
    "no registry value",
    "fiber has c stack frame",
    "buffer overflow",
    "alive fiber"
};

/* Lead bytes in marshaling protocol */
enum {
    LB_REAL = 200,
    LB_NIL,
    LB_FALSE,
    LB_TRUE,
    LB_FIBER,
    LB_INTEGER,
    LB_STRING,
    LB_SYMBOL,
    LB_KEYWORD,
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

/* Helper to look inside an entry in an environment */
static Janet entry_getval(Janet env_entry) {
    if (janet_checktype(env_entry, JANET_TABLE)) {
        JanetTable *entry = janet_unwrap_table(env_entry);
        Janet checkval = janet_table_get(entry, janet_ckeywordv("value"));
        if (janet_checktype(checkval, JANET_NIL)) {
            checkval = janet_table_get(entry, janet_ckeywordv("ref"));
        }
        return checkval;
    } else if (janet_checktype(env_entry, JANET_STRUCT)) {
        const JanetKV *entry = janet_unwrap_struct(env_entry);
        Janet checkval = janet_struct_get(entry, janet_ckeywordv("value"));
        if (janet_checktype(checkval, JANET_NIL)) {
            checkval = janet_struct_get(entry, janet_ckeywordv("ref"));
        }
        return checkval;
    } else {
        return janet_wrap_nil();
    }
}

/* Make a forward lookup table from an environment (for unmarshaling) */
JanetTable *janet_env_lookup(JanetTable *env) {
    JanetTable *renv = janet_table(env->count);
    while (env) {
        for (int32_t i = 0; i < env->capacity; i++) {
            if (janet_checktype(env->data[i].key, JANET_SYMBOL)) {
                janet_table_put(renv,
                                env->data[i].key,
                                entry_getval(env->data[i].value));
            }
        }
        env = env->proto;
    }
    return renv;
}

/* Marshal an integer onto the buffer */
static void pushint(MarshalState *st, int32_t x) {
    if (x >= 0 && x < 128) {
        janet_buffer_push_u8(st->buf, x);
    } else if (x <= 8191 && x >= -8192) {
        uint8_t intbuf[2];
        intbuf[0] = ((x >> 8) & 0x3F) | 0x80;
        intbuf[1] = x & 0xFF;
        janet_buffer_push_bytes(st->buf, intbuf, 2);
    } else {
        uint8_t intbuf[5];
        intbuf[0] = LB_INTEGER;
        intbuf[1] = (x >> 24) & 0xFF;
        intbuf[2] = (x >> 16) & 0xFF;
        intbuf[3] = (x >> 8) & 0xFF;
        intbuf[4] = x & 0xFF;
        janet_buffer_push_bytes(st->buf, intbuf, 5);
    }
}

static void pushbyte(MarshalState *st, uint8_t b) {
    janet_buffer_push_u8(st->buf, b);
}

static void pushbytes(MarshalState *st, const uint8_t *bytes, int32_t len) {
    janet_buffer_push_bytes(st->buf, bytes, len);
}

/* Forward declaration to enable mutual recursion. */
static void marshal_one(MarshalState *st, Janet x, int flags);
static void marshal_one_fiber(MarshalState *st, JanetFiber *fiber, int flags);
static void marshal_one_def(MarshalState *st, JanetFuncDef *def, int flags);
static void marshal_one_env(MarshalState *st, JanetFuncEnv *env, int flags);

/* Marshal a function env */
static void marshal_one_env(MarshalState *st, JanetFuncEnv *env, int flags) {
    if ((flags & 0xFFFF) > JANET_RECURSION_GUARD)
        longjmp(st->err, MR_STACKOVERFLOW);
    for (int32_t i = 0; i < janet_v_count(st->seen_envs); i++) {
        if (st->seen_envs[i] == env) {
            pushbyte(st, LB_FUNCENV_REF);
            pushint(st, i);
            return;
        }
    }
    janet_v_push(st->seen_envs, env);
    pushint(st, env->offset);
    pushint(st, env->length);
    if (env->offset) {
        /* On stack variant */
        marshal_one(st, janet_wrap_fiber(env->as.fiber), flags + 1);
    } else {
        /* Off stack variant */
        for (int32_t i = 0; i < env->length; i++)
            marshal_one(st, env->as.values[i], flags + 1);
    }
}

/* Add function flags to janet functions */
static void janet_func_addflags(JanetFuncDef *def) {
    if (def->name) def->flags |= JANET_FUNCDEF_FLAG_HASNAME;
    if (def->source) def->flags |= JANET_FUNCDEF_FLAG_HASSOURCE;
    if (def->defs) def->flags |= JANET_FUNCDEF_FLAG_HASDEFS;
    if (def->environments) def->flags |= JANET_FUNCDEF_FLAG_HASENVS;
    if (def->sourcemap) def->flags |= JANET_FUNCDEF_FLAG_HASSOURCEMAP;
}

/* Marshal a function def */
static void marshal_one_def(MarshalState *st, JanetFuncDef *def, int flags) {
    if ((flags & 0xFFFF) > JANET_RECURSION_GUARD)
        longjmp(st->err, MR_STACKOVERFLOW);
    for (int32_t i = 0; i < janet_v_count(st->seen_defs); i++) {
        if (st->seen_defs[i] == def) {
            pushbyte(st, LB_FUNCDEF_REF);
            pushint(st, i);
            return;
        }
    }
    janet_func_addflags(def);
    /* Add to lookup */
    janet_v_push(st->seen_defs, def);
    pushint(st, def->flags);
    pushint(st, def->slotcount);
    pushint(st, def->arity);
    pushint(st, def->constants_length);
    pushint(st, def->bytecode_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASENVS)
        pushint(st, def->environments_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASDEFS)
        pushint(st, def->defs_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASNAME)
        marshal_one(st, janet_wrap_string(def->name), flags);
    if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCE)
        marshal_one(st, janet_wrap_string(def->source), flags);

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
    if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCEMAP) {
        int32_t current = 0;
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            JanetSourceMapping map = def->sourcemap[i];
            pushint(st, map.start - current);
            pushint(st, map.end - map.start);
            current = map.end;
        }
    }
}

#define JANET_FIBER_FLAG_HASCHILD (1 << 29)
#define JANET_STACKFRAME_HASENV (1 << 30)

/* Marshal a fiber */
static void marshal_one_fiber(MarshalState *st, JanetFiber *fiber, int flags) {
    int32_t fflags = fiber->flags;
    if ((flags & 0xFFFF) > JANET_RECURSION_GUARD)
        longjmp(st->err, MR_STACKOVERFLOW);
    if (fiber->child) fflags |= JANET_FIBER_FLAG_HASCHILD;
    if (janet_fiber_status(fiber) == JANET_STATUS_ALIVE)
        longjmp(st->err, MR_LIVEFIBER);
    pushint(st, fflags);
    pushint(st, fiber->frame);
    pushint(st, fiber->stackstart);
    pushint(st, fiber->stacktop);
    pushint(st, fiber->maxstack);
    /* Do frames */
    int32_t i = fiber->frame;
    int32_t j = fiber->stackstart - JANET_FRAME_SIZE;
    while (i > 0) {
        JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
        if (frame->env) frame->flags |= JANET_STACKFRAME_HASENV;
        if (!frame->func) longjmp(st->err, MR_C_STACKFRAME);
        pushint(st, frame->flags);
        pushint(st, frame->prevframe);
        int32_t pcdiff = (int32_t)(frame->pc - frame->func->def->bytecode);
        pushint(st, pcdiff);
        marshal_one(st, janet_wrap_function(frame->func), flags + 1);
        if (frame->env) marshal_one_env(st, frame->env, flags + 1);
        /* Marshal all values in the stack frame */
        for (int32_t k = i; k < j; k++)
            marshal_one(st, fiber->data[k], flags + 1);
        j = i - JANET_FRAME_SIZE;
        i = frame->prevframe;
    }
    if (fiber->child)
        marshal_one(st, janet_wrap_fiber(fiber->child), flags + 1);
}

/* The main body of the marshaling function. Is the main
 * entry point for the mutually recursive functions. */
static void marshal_one(MarshalState *st, Janet x, int flags) {
    Janet parent = st->current;
    JanetType type = janet_type(x);
    st->current = x;
    if ((flags & 0xFFFF) > JANET_RECURSION_GUARD)
        longjmp(st->err, MR_STACKOVERFLOW);

    /* Check simple primitives (non reference types, no benefit from memoization) */
    switch (type) {
        default:
            break;
        case JANET_NIL:
        case JANET_FALSE:
        case JANET_TRUE:
            pushbyte(st, 200 + type);
            goto done;
        case JANET_NUMBER: {
            double xval = janet_unwrap_number(x);
            if (janet_checkintrange(xval)) {
                pushint(st, (int32_t) xval);
                goto done;
            }
            break;
        }
    }

#define MARK_SEEN() \
    janet_table_put(&st->seen, x, janet_wrap_integer(st->nextid++))

    /* Check reference and registry value */
    {
        Janet check = janet_table_get(&st->seen, x);
        if (janet_checkint(check)) {
            pushbyte(st, LB_REFERENCE);
            pushint(st, janet_unwrap_integer(check));
            goto done;
        }
        if (st->rreg) {
            check = janet_table_get(st->rreg, x);
            if (janet_checktype(check, JANET_SYMBOL)) {
                MARK_SEEN();
                const uint8_t *regname = janet_unwrap_symbol(check);
                pushbyte(st, LB_REGISTRY);
                pushint(st, janet_string_length(regname));
                pushbytes(st, regname, janet_string_length(regname));
                goto done;
            }
        }
    }

    /* Reference types */
    switch (type) {
        case JANET_NUMBER: {
            union {
                double d;
                uint8_t bytes[8];
            } u;
            u.d = janet_unwrap_number(x);
#ifdef JANET_BIG_ENDIAN
            /* Swap byte order */
            uint8_t temp;
            temp = u.bytes[7];
            u.bytes[7] = u.bytes[0];
            u.bytes[0] = temp;
            temp = u.bytes[6];
            u.bytes[6] = u.bytes[1];
            u.bytes[1] = temp;
            temp = u.bytes[5];
            u.bytes[5] = u.bytes[2];
            u.bytes[2] = temp;
            temp = u.bytes[4];
            u.bytes[4] = u.bytes[3];
            u.bytes[3] = temp;
#endif
            pushbyte(st, LB_REAL);
            pushbytes(st, u.bytes, 8);
            MARK_SEEN();
        }
        goto done;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            const uint8_t *str = janet_unwrap_string(x);
            int32_t length = janet_string_length(str);
            /* Record reference */
            MARK_SEEN();
            uint8_t lb = (type == JANET_STRING) ? LB_STRING :
                         (type == JANET_SYMBOL) ? LB_SYMBOL :
                         LB_KEYWORD;
            pushbyte(st, lb);
            pushint(st, length);
            pushbytes(st, str, length);
        }
        goto done;
        case JANET_BUFFER: {
            JanetBuffer *buffer = janet_unwrap_buffer(x);
            /* Record reference */
            MARK_SEEN();
            pushbyte(st, LB_BUFFER);
            pushint(st, buffer->count);
            pushbytes(st, buffer->data, buffer->count);
        }
        goto done;
        case JANET_ARRAY: {
            int32_t i;
            JanetArray *a = janet_unwrap_array(x);
            MARK_SEEN();
            pushbyte(st, LB_ARRAY);
            pushint(st, a->count);
            for (i = 0; i < a->count; i++)
                marshal_one(st, a->data[i], flags + 1);
        }
        goto done;
        case JANET_TUPLE: {
            int32_t i, count, flag;
            const Janet *tup = janet_unwrap_tuple(x);
            count = janet_tuple_length(tup);
            flag = janet_tuple_flag(tup);
            pushbyte(st, LB_TUPLE);
            pushint(st, count);
            pushint(st, flag);
            for (i = 0; i < count; i++)
                marshal_one(st, tup[i], flags + 1);
            /* Mark as seen AFTER marshaling */
            MARK_SEEN();
        }
        goto done;
        case JANET_TABLE: {
            JanetTable *t = janet_unwrap_table(x);
            MARK_SEEN();
            pushbyte(st, t->proto ? LB_TABLE_PROTO : LB_TABLE);
            pushint(st, t->count);
            if (t->proto)
                marshal_one(st, janet_wrap_table(t->proto), flags + 1);
            for (int32_t i = 0; i < t->capacity; i++) {
                if (janet_checktype(t->data[i].key, JANET_NIL))
                    continue;
                marshal_one(st, t->data[i].key, flags + 1);
                marshal_one(st, t->data[i].value, flags + 1);
            }
        }
        goto done;
        case JANET_STRUCT: {
            int32_t count;
            const JanetKV *struct_ = janet_unwrap_struct(x);
            count = janet_struct_length(struct_);
            pushbyte(st, LB_STRUCT);
            pushint(st, count);
            for (int32_t i = 0; i < janet_struct_capacity(struct_); i++) {
                if (janet_checktype(struct_[i].key, JANET_NIL))
                    continue;
                marshal_one(st, struct_[i].key, flags + 1);
                marshal_one(st, struct_[i].value, flags + 1);
            }
            /* Mark as seen AFTER marshaling */
            MARK_SEEN();
        }
        goto done;
        case JANET_ABSTRACT:
        case JANET_CFUNCTION:
            goto noregval;
        case JANET_FUNCTION: {
            pushbyte(st, LB_FUNCTION);
            JanetFunction *func = janet_unwrap_function(x);
            marshal_one_def(st, func->def, flags);
            /* Mark seen after reading def, but before envs */
            MARK_SEEN();
            for (int32_t i = 0; i < func->def->environments_length; i++)
                marshal_one_env(st, func->envs[i], flags + 1);
        }
        goto done;
        case JANET_FIBER: {
            MARK_SEEN();
            pushbyte(st, LB_FIBER);
            marshal_one_fiber(st, janet_unwrap_fiber(x), flags + 1);
        }
        goto done;
        default:
            goto nyi;
    }

#undef MARK_SEEN

done:
    st->current = parent;
    return;

    /* Errors */

nyi:
    longjmp(st->err, MR_NYI);

noregval:
    longjmp(st->err, MR_NRV);
}

int janet_marshal(
    JanetBuffer *buf,
    Janet x,
    Janet *errval,
    JanetTable *rreg,
    int flags) {
    int status;
    MarshalState st;
    st.buf = buf;
    st.nextid = 0;
    st.seen_defs = NULL;
    st.seen_envs = NULL;
    st.rreg = rreg;
    st.current = x;
    janet_table_init(&st.seen, 0);
    if (!(status = setjmp(st.err)))
        marshal_one(&st, x, flags);
    if (status && errval)
        *errval = st.current;
    janet_table_deinit(&st.seen);
    janet_v_free(st.seen_envs);
    janet_v_free(st.seen_defs);
    return status;
}

typedef struct {
    jmp_buf err;
    JanetArray lookup;
    JanetTable *reg;
    JanetFuncEnv **lookup_envs;
    JanetFuncDef **lookup_defs;
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
    UMR_INVALID_BYTECODE,
    UMR_INVALID_FIBER
} UnmarshalResult;

const char *umr_strings[] = {
    "",
    "stack overflow",
    "unexpected end of source",
    "unmarshal error",
    "expected integer",
    "expected table",
    "expected fiber",
    "expected string",
    "invalid reference",
    "invalid bytecode",
    "invalid fiber"
};

/* Helper to read a 32 bit integer from an unmarshal state */
static int32_t readint(UnmarshalState *st, const uint8_t **atdata) {
    const uint8_t *data = *atdata;
    int32_t ret;
    if (data >= st->end) longjmp(st->err, UMR_EOS);
    if (*data < 128) {
        ret = *data++;
    } else if (*data < 192) {
        if (data + 2 > st->end) longjmp(st->err, UMR_EOS);
        ret = ((data[0] & 0x3F) << 8) + data[1];
        ret = ((ret << 18) >> 18);
        data += 2;
    } else if (*data == LB_INTEGER) {
        if (data + 5 > st->end) longjmp(st->err, UMR_EOS);
        ret = ((int32_t)(data[1]) << 24) |
              ((int32_t)(data[2]) << 16) |
              ((int32_t)(data[3]) << 8) |
              (int32_t)(data[4]);
        data += 5;
    } else {
        longjmp(st->err, UMR_EXPECTED_INTEGER);
    }
    *atdata = data;
    return ret;
}

/* Forward declarations for mutual recursion */
static const uint8_t *unmarshal_one(
    UnmarshalState *st,
    const uint8_t *data,
    Janet *out,
    int flags);
static const uint8_t *unmarshal_one_env(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFuncEnv **out,
    int flags);
static const uint8_t *unmarshal_one_def(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFuncDef **out,
    int flags);
static const uint8_t *unmarshal_one_fiber(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFiber **out,
    int flags);

/* Unmarshal a funcenv */
static const uint8_t *unmarshal_one_env(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFuncEnv **out,
    int flags) {
    const uint8_t *end = st->end;
    if (data >= end) longjmp(st->err, UMR_EOS);
    if (*data == LB_FUNCENV_REF) {
        data++;
        int32_t index = readint(st, &data);
        if (index < 0 || index >= janet_v_count(st->lookup_envs))
            longjmp(st->err, UMR_INVALID_REFERENCE);
        *out = st->lookup_envs[index];
    } else {
        JanetFuncEnv *env = janet_gcalloc(JANET_MEMORY_FUNCENV, sizeof(JanetFuncEnv));
        env->length = 0;
        env->offset = 0;
        janet_v_push(st->lookup_envs, env);
        int32_t offset = readint(st, &data);
        int32_t length = readint(st, &data);
        if (offset) {
            Janet fiberv;
            /* On stack variant */
            data = unmarshal_one(st, data, &fiberv, flags);
            if (!janet_checktype(fiberv, JANET_FIBER)) longjmp(st->err, UMR_EXPECTED_FIBER);
            env->as.fiber = janet_unwrap_fiber(fiberv);
            /* Unmarshalling fiber may set values */
            if (env->offset != 0 && env->offset != offset) longjmp(st->err, UMR_UNKNOWN);
            if (env->length != 0 && env->length != length) longjmp(st->err, UMR_UNKNOWN);
        } else {
            /* Off stack variant */
            env->as.values = malloc(sizeof(Janet) * length);
            if (!env->as.values) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < length; i++)
                data = unmarshal_one(st, data, env->as.values + i, flags);
        }
        env->offset = offset;
        env->length = length;
        *out = env;
    }
    return data;
}

/* Unmarshal a funcdef */
static const uint8_t *unmarshal_one_def(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFuncDef **out,
    int flags) {
    const uint8_t *end = st->end;
    if (data >= end) longjmp(st->err, UMR_EOS);
    if (*data == LB_FUNCDEF_REF) {
        data++;
        int32_t index = readint(st, &data);
        if (index < 0 || index >= janet_v_count(st->lookup_defs))
            longjmp(st->err, UMR_INVALID_REFERENCE);
        *out = st->lookup_defs[index];
    } else {
        /* Initialize with values that will not break garbage collection
         * if unmarshalling fails. */
        JanetFuncDef *def = janet_gcalloc(JANET_MEMORY_FUNCDEF, sizeof(JanetFuncDef));
        def->environments_length = 0;
        def->defs_length = 0;
        def->constants_length = 0;
        def->bytecode_length = 0;
        def->name = NULL;
        def->source = NULL;
        janet_v_push(st->lookup_defs, def);

        /* Set default lengths to zero */
        int32_t bytecode_length = 0;
        int32_t constants_length = 0;
        int32_t environments_length = 0;
        int32_t defs_length = 0;

        /* Read flags and other fixed values */
        def->flags = readint(st, &data);
        def->slotcount = readint(st, &data);
        def->arity = readint(st, &data);

        /* Read some lengths */
        constants_length = readint(st, &data);
        bytecode_length = readint(st, &data);
        if (def->flags & JANET_FUNCDEF_FLAG_HASENVS)
            environments_length = readint(st, &data);
        if (def->flags & JANET_FUNCDEF_FLAG_HASDEFS)
            defs_length = readint(st, &data);

        /* Check name and source (optional) */
        if (def->flags & JANET_FUNCDEF_FLAG_HASNAME) {
            Janet x;
            data = unmarshal_one(st, data, &x, flags + 1);
            if (!janet_checktype(x, JANET_STRING)) longjmp(st->err, UMR_EXPECTED_STRING);
            def->name = janet_unwrap_string(x);
        }
        if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCE) {
            Janet x;
            data = unmarshal_one(st, data, &x, flags + 1);
            if (!janet_checktype(x, JANET_STRING)) longjmp(st->err, UMR_EXPECTED_STRING);
            def->source = janet_unwrap_string(x);
        }

        /* Unmarshal constants */
        if (constants_length) {
            def->constants = malloc(sizeof(Janet) * constants_length);
            if (!def->constants) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < constants_length; i++)
                data = unmarshal_one(st, data, def->constants + i, flags + 1);
        } else {
            def->constants = NULL;
        }
        def->constants_length = constants_length;

        /* Unmarshal bytecode */
        def->bytecode = malloc(sizeof(uint32_t) * bytecode_length);
        if (!def->bytecode) {
            JANET_OUT_OF_MEMORY;
        }
        for (int32_t i = 0; i < bytecode_length; i++) {
            if (data + 4 > end) longjmp(st->err, UMR_EOS);
            def->bytecode[i] =
                (uint32_t)(data[0]) |
                ((uint32_t)(data[1]) << 8) |
                ((uint32_t)(data[2]) << 16) |
                ((uint32_t)(data[3]) << 24);
            data += 4;
        }
        def->bytecode_length = bytecode_length;

        /* Unmarshal environments */
        if (def->flags & JANET_FUNCDEF_FLAG_HASENVS) {
            def->environments = calloc(1, sizeof(int32_t) * environments_length);
            if (!def->environments) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < environments_length; i++) {
                def->environments[i] = readint(st, &data);
            }
        } else {
            def->environments = NULL;
        }
        def->environments_length = environments_length;

        /* Unmarshal sub funcdefs */
        if (def->flags & JANET_FUNCDEF_FLAG_HASDEFS) {
            def->defs = calloc(1, sizeof(JanetFuncDef *) * defs_length);
            if (!def->defs) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < defs_length; i++) {
                data = unmarshal_one_def(st, data, def->defs + i, flags + 1);
            }
        } else {
            def->defs = NULL;
        }
        def->defs_length = defs_length;

        /* Unmarshal source maps if needed */
        if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCEMAP) {
            int32_t current = 0;
            def->sourcemap = malloc(sizeof(JanetSourceMapping) * bytecode_length);
            if (!def->sourcemap) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < bytecode_length; i++) {
                current += readint(st, &data);
                def->sourcemap[i].start = current;
                current += readint(st, &data);
                def->sourcemap[i].end = current;
            }
        } else {
            def->sourcemap = NULL;
        }

        /* Validate */
        if (janet_verify(def)) longjmp(st->err, UMR_INVALID_BYTECODE);

        /* Set def */
        *out = def;
    }
    return data;
}

/* Unmarshal a fiber */
static const uint8_t *unmarshal_one_fiber(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFiber **out,
    int flags) {

    /* Initialize a new fiber */
    JanetFiber *fiber = janet_gcalloc(JANET_MEMORY_FIBER, sizeof(JanetFiber));
    fiber->flags = 0;
    fiber->frame = 0;
    fiber->stackstart = 0;
    fiber->stacktop = 0;
    fiber->capacity = 0;
    fiber->maxstack = 0;
    fiber->data = NULL;
    fiber->child = NULL;

    /* Set frame later so fiber can be GCed at anytime if unmarshalling fails */
    int32_t frame = 0;
    int32_t stack = 0;
    int32_t stacktop = 0;

    /* Read ints */
    fiber->flags = readint(st, &data);
    frame = readint(st, &data);
    fiber->stackstart = readint(st, &data);
    fiber->stacktop = readint(st, &data);
    fiber->maxstack = readint(st, &data);

    /* Check for bad flags and ints */
    if ((int32_t)(frame + JANET_FRAME_SIZE) > fiber->stackstart ||
            fiber->stackstart > fiber->stacktop ||
            fiber->stacktop > fiber->maxstack) {
        goto error;
    }

    /* Allocate stack memory */
    fiber->capacity = fiber->stacktop + 10;
    fiber->data = malloc(sizeof(Janet) * fiber->capacity);
    if (!fiber->data) {
        JANET_OUT_OF_MEMORY;
    }

    /* get frames */
    stack = frame;
    stacktop = fiber->stackstart - JANET_FRAME_SIZE;
    while (stack > 0) {
        JanetFunction *func = NULL;
        JanetFuncDef *def = NULL;
        JanetFuncEnv *env = NULL;
        int32_t frameflags = readint(st, &data);
        int32_t prevframe = readint(st, &data);
        int32_t pcdiff = readint(st, &data);

        /* Get frame items */
        Janet *framestack = fiber->data + stack;
        JanetStackFrame *framep = janet_stack_frame(framestack);

        /* Get function */
        Janet funcv;
        data = unmarshal_one(st, data, &funcv, flags + 1);
        if (!janet_checktype(funcv, JANET_FUNCTION)) {
            goto error;
        }
        func = janet_unwrap_function(funcv);
        def = func->def;

        /* Check env */
        if (frameflags & JANET_STACKFRAME_HASENV) {
            frameflags &= ~JANET_STACKFRAME_HASENV;
            int32_t offset = stack;
            int32_t length = stacktop - stack;
            data = unmarshal_one_env(st, data, &env, flags + 1);
            if (env->offset != 0 && env->offset != offset) goto error;
            if (env->length != 0 && env->length != offset) goto error;
            env->offset = offset;
            env->length = length;
        }

        /* Error checking */
        int32_t expected_framesize = def->slotcount;
        if (expected_framesize != stacktop - stack) goto error;
        if (pcdiff < 0 || pcdiff >= def->bytecode_length) goto error;
        if ((int32_t)(prevframe + JANET_FRAME_SIZE) > stack) goto error;

        /* Get stack items */
        for (int32_t i = stack; i < stacktop; i++)
            data = unmarshal_one(st, data, fiber->data + i, flags + 1);

        /* Set frame */
        framep->env = env;
        framep->pc = def->bytecode + pcdiff;
        framep->prevframe = prevframe;
        framep->flags = frameflags;
        framep->func = func;

        /* Goto previous frame */
        stacktop = stack - JANET_FRAME_SIZE;
        stack = prevframe;
    }
    if (stack < 0) goto error;

    /* Check for child fiber */
    if (fiber->flags & JANET_FIBER_FLAG_HASCHILD) {
        Janet fiberv;
        fiber->flags &= ~JANET_FIBER_FLAG_HASCHILD;
        data = unmarshal_one(st, data, &fiberv, flags + 1);
        if (!janet_checktype(fiberv, JANET_FIBER)) longjmp(st->err, UMR_EXPECTED_FIBER);
        fiber->child = janet_unwrap_fiber(fiberv);
    }

    /* Return data */
    fiber->frame = frame;
    *out = fiber;
    return data;

error:
    longjmp(st->err, UMR_INVALID_FIBER);
    return NULL;
}

static const uint8_t *unmarshal_one(
    UnmarshalState *st,
    const uint8_t *data,
    Janet *out,
    int flags) {
    const uint8_t *end = st->end;
    uint8_t lead;
    if ((flags & 0xFFFF) > JANET_RECURSION_GUARD) {
        longjmp(st->err, UMR_STACKOVERFLOW);
    }
#define EXTRA(N) if (data + N > end) longjmp(st->err, UMR_EOS)
    EXTRA(1);
    lead = data[0];
    if (lead < 200) {
        *out = janet_wrap_integer(readint(st, &data));
        return data;
    }
    switch (lead) {
        case LB_NIL:
            *out = janet_wrap_nil();
            return data + 1;
        case LB_FALSE:
            *out = janet_wrap_false();
            return data + 1;
        case LB_TRUE:
            *out = janet_wrap_true();
            return data + 1;
        case LB_INTEGER:
            /* Long integer */
            EXTRA(5);
            *out = janet_wrap_integer(
                       (data[4]) |
                       (data[3] << 8) |
                       (data[2] << 16) |
                       (data[1] << 24));
            return data + 5;
        case LB_REAL:
            /* Real */
        {
            union {
                double d;
                uint8_t bytes[8];
            } u;
            EXTRA(9);
#ifdef JANET_BIG_ENDIAN
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
            *out = janet_wrap_number(u.d);
            janet_array_push(&st->lookup, *out);
            return data + 9;
        }
        case LB_STRING:
        case LB_SYMBOL:
        case LB_BUFFER:
        case LB_KEYWORD:
        case LB_REGISTRY: {
            data++;
            int32_t len = readint(st, &data);
            EXTRA(len);
            if (lead == LB_STRING) {
                const uint8_t *str = janet_string(data, len);
                *out = janet_wrap_string(str);
            } else if (lead == LB_SYMBOL) {
                const uint8_t *str = janet_symbol(data, len);
                *out = janet_wrap_symbol(str);
            } else if (lead == LB_KEYWORD) {
                const uint8_t *str = janet_keyword(data, len);
                *out = janet_wrap_keyword(str);
            } else if (lead == LB_REGISTRY) {
                if (st->reg) {
                    Janet regkey = janet_symbolv(data, len);
                    *out = janet_table_get(st->reg, regkey);
                } else {
                    *out = janet_wrap_nil();
                }
            } else { /* (lead == LB_BUFFER) */
                JanetBuffer *buffer = janet_buffer(len);
                buffer->count = len;
                memcpy(buffer->data, data, len);
                *out = janet_wrap_buffer(buffer);
            }
            janet_array_push(&st->lookup, *out);
            return data + len;
        }
        case LB_FIBER: {
            JanetFiber *fiber;
            data = unmarshal_one_fiber(st, data + 1, &fiber, flags);
            *out = janet_wrap_fiber(fiber);
            return data;
        }
        case LB_FUNCTION: {
            JanetFunction *func;
            JanetFuncDef *def;
            data = unmarshal_one_def(st, data + 1, &def, flags + 1);
            func = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction) +
                                 def->environments_length * sizeof(JanetFuncEnv));
            func->def = def;
            *out = janet_wrap_function(func);
            janet_array_push(&st->lookup, *out);
            for (int32_t i = 0; i < def->environments_length; i++) {
                data = unmarshal_one_env(st, data, &(func->envs[i]), flags + 1);
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
                JanetArray *array = janet_array(len);
                array->count = len;
                *out = janet_wrap_array(array);
                janet_array_push(&st->lookup, *out);
                for (int32_t i = 0; i < len; i++) {
                    data = unmarshal_one(st, data, array->data + i, flags + 1);
                }
            } else if (lead == LB_TUPLE) {
                /* Tuple */
                Janet *tup = janet_tuple_begin(len);
                int32_t flag = readint(st, &data);
                janet_tuple_flag(tup) = flag;
                for (int32_t i = 0; i < len; i++) {
                    data = unmarshal_one(st, data, tup + i, flags + 1);
                }
                *out = janet_wrap_tuple(janet_tuple_end(tup));
                janet_array_push(&st->lookup, *out);
            } else if (lead == LB_STRUCT) {
                /* Struct */
                JanetKV *struct_ = janet_struct_begin(len);
                for (int32_t i = 0; i < len; i++) {
                    Janet key, value;
                    data = unmarshal_one(st, data, &key, flags + 1);
                    data = unmarshal_one(st, data, &value, flags + 1);
                    janet_struct_put(struct_, key, value);
                }
                *out = janet_wrap_struct(janet_struct_end(struct_));
                janet_array_push(&st->lookup, *out);
            } else if (lead == LB_REFERENCE) {
                if (len < 0 || len >= st->lookup.count)
                    longjmp(st->err, UMR_INVALID_REFERENCE);
                *out = st->lookup.data[len];
            } else {
                /* Table */
                JanetTable *t = janet_table(len);
                *out = janet_wrap_table(t);
                janet_array_push(&st->lookup, *out);
                if (lead == LB_TABLE_PROTO) {
                    Janet proto;
                    data = unmarshal_one(st, data, &proto, flags + 1);
                    if (!janet_checktype(proto, JANET_TABLE)) longjmp(st->err, UMR_EXPECTED_TABLE);
                    t->proto = janet_unwrap_table(proto);
                }
                for (int32_t i = 0; i < len; i++) {
                    Janet key, value;
                    data = unmarshal_one(st, data, &key, flags + 1);
                    data = unmarshal_one(st, data, &value, flags + 1);
                    janet_table_put(t, key, value);
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

int janet_unmarshal(
    const uint8_t *bytes,
    size_t len,
    int flags,
    Janet *out,
    JanetTable *reg,
    const uint8_t **next) {
    int status;
    /* Avoid longjmp clobber warning in GCC */
    UnmarshalState st;
    st.end = bytes + len;
    st.lookup_defs = NULL;
    st.lookup_envs = NULL;
    st.reg = reg;
    janet_array_init(&st.lookup, 0);
    if (!(status = setjmp(st.err))) {
        const uint8_t *nextbytes = unmarshal_one(&st, bytes, out, flags);
        if (next) *next = nextbytes;
    }
    janet_array_deinit(&st.lookup);
    janet_v_free(st.lookup_defs);
    janet_v_free(st.lookup_envs);
    return status;
}

/* C functions */

static Janet cfun_env_lookup(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetTable *env = janet_gettable(argv, 0);
    return janet_wrap_table(janet_env_lookup(env));
}

static Janet cfun_marshal(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetBuffer *buffer;
    JanetTable *rreg = NULL;
    Janet err_param = janet_wrap_nil();
    int status;
    if (argc > 1) {
        rreg = janet_gettable(argv, 1);
    }
    if (argc > 2) {
        buffer = janet_getbuffer(argv, 2);
    } else {
        buffer = janet_buffer(10);
    }
    status = janet_marshal(buffer, argv[0], &err_param, rreg, 0);
    if (status)
        janet_panicf("%s for %V", mr_strings[status], err_param);
    return janet_wrap_buffer(buffer);
}

static Janet cfun_unmarshal(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetByteView view = janet_getbytes(argv, 0);
    JanetTable *reg = NULL;
    Janet ret;
    int status;
    if (argc > 1) {
        reg = janet_gettable(argv, 1);
    }
    status = janet_unmarshal(view.bytes, (size_t) view.len, 0, &ret, reg, NULL);
    if (status) {
        janet_panic(umr_strings[status]);
    }
    return ret;
}

static const JanetReg marsh_cfuns[] = {
    {
        "marshal", cfun_marshal,
        JDOC("(marshal x [,reverse-lookup [,buffer]])\n\n"
             "Marshal a janet value into a buffer and return the buffer. The buffer "
             "can the later be unmarshalled to reconstruct the initial value. "
             "Optionally, one can pass in a reverse lookup table to not marshal "
             "aliased values that are found in the table. Then a forward"
             "lookup table can be used to recover the original janet value when "
             "unmarshalling.")
    },
    {
        "unmarshal", cfun_unmarshal,
        JDOC("(unmarshal buffer [,lookup])\n\n"
             "Unmarshal a janet value from a buffer. An optional lookup table "
             "can be provided to allow for aliases to be resolved. Returns the value "
             "unmarshalled from the buffer.")
    },
    {
        "env-lookup", cfun_env_lookup,
        JDOC("(env-lookup env)\n\n"
             "Creates a forward lookup table for unmarshalling from an environment. "
             "To create a reverse lookup table, use the invert function to swap keys "
             "and values in the returned table.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_marsh(JanetTable *env) {
    janet_core_cfuns(env, NULL, marsh_cfuns);
}

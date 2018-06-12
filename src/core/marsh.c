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

typedef struct {
    jmp_buf err;
    DstBuffer *buf;
    DstTable seen;
    int32_t nextid;
} MarshalState;

enum {
    MR_OK,
    MR_STACKOVERFLOW,
    MR_NYI,
    MR_OVERFLOW
} MarshalResult;

const char *mr_strings[] = {
    "",
    "stack overflow",
    "type NYI",
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
    LB_CFUNCTION,
    LB_ABSTRACT,
    LB_REFERENCE
} LeadBytes;

/* Marshal an integer onto the buffer */
static int pushint(DstBuffer *b, int32_t x) {
    if (x >= 0 && x < 200) {
        return dst_buffer_push_u8(b, x);
    } else {
        uint8_t intbuf[5];
        intbuf[0] = LB_INTEGER;
        intbuf[1] = x & 0xFF;
        intbuf[2] = (x >> 8) & 0xFF;
        intbuf[3] = (x >> 16) & 0xFF;
        intbuf[4] = (x >> 24) & 0xFF;
        return dst_buffer_push_bytes(b, intbuf, 5);
    }
}

/* Forward declaration to enable mutual recursion. */
static void marshal1(MarshalState *st, Dst x, int flags);

/* Marshal a function environment */
/*static void marshal1_env(MarshalState *st, DstFuncEnv *env, int flags) {*/

/*}*/

/* Marshal a function definition. */
/*static void marshal1_def(MarshalState *st, DstFuncDef *def, int flags) {*/

/*}*/

/* The main body of the marshaling function. Is the main
 * entry point for the mutually recursive functions. */
static void marshal1(MarshalState *st, Dst x, int flags) {
    DstBuffer *b = st->buf;
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
            {
                if (dst_buffer_push_u8(b, 200 + type)) goto overflow;
            }
            return;
        case DST_INTEGER:
            {
                if (pushint(b, dst_unwrap_integer(x))) goto overflow;
            }
            return;
    }

    /* Check reference */
    {
        Dst check = dst_table_get(&st->seen, x);
        if (!dst_checktype(check, DST_NIL)) {
            if (dst_buffer_push_u8(b, LB_REFERENCE)) goto overflow;
            if (pushint(b, dst_unwrap_integer(check))) goto overflow;
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
                if (dst_buffer_push_u8(b, LB_REAL)) goto overflow;
                if (dst_buffer_push_bytes(b, u.bytes, 8)) goto overflow;
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
                if (dst_buffer_push_u8(b, lb)) goto overflow;
                if (pushint(b, length)) goto overflow;
                if (dst_buffer_push_bytes(b, str, length)) goto overflow;
            }
            return;
        case DST_BUFFER:
            {
                DstBuffer *buffer = dst_unwrap_buffer(x);
                /* Record reference */
                MARK_SEEN();
                if (dst_buffer_push_u8(b, LB_BUFFER)) goto overflow;
                if (pushint(b, buffer->count)) goto overflow;
                if (dst_buffer_push_bytes(b, buffer->data, buffer->count)) goto overflow;
            }
            return;
        case DST_ARRAY:
            {
                int32_t i;
                DstArray *a = dst_unwrap_array(x);
                MARK_SEEN();
                if (dst_buffer_push_u8(b, LB_ARRAY)) goto overflow;
                if (pushint(b, a->count)) goto overflow;
                for (i = 0; i < a->count; i++) {
                    marshal1(st, a->data[i], flags + 1);
                }
            }
            return;
        case DST_TUPLE:
            {
                int32_t i, count;
                const Dst *tup = dst_unwrap_tuple(x);
                count = dst_tuple_length(tup);
                if (dst_buffer_push_u8(b, LB_TUPLE)) goto overflow;
                if (pushint(b, count)) goto overflow;
                for (i = 0; i < count; i++) {
                    marshal1(st, tup[i], flags + 1);
                }
                /* Mark as seen AFTER marshaling */
                MARK_SEEN();
            }
            return;
        case DST_TABLE:
            {
                const DstKV *kv = NULL;
                DstTable *t = dst_unwrap_table(x);
                MARK_SEEN();
                if (dst_buffer_push_u8(b, t->proto ? LB_TABLE_PROTO : LB_TABLE)) 
                    goto overflow;
                if (pushint(b, t->count)) goto overflow;
                if (t->proto) {
                    marshal1(st, dst_wrap_table(t->proto), flags + 1);
                }
                while ((kv = dst_table_next(t, kv))) {
                    marshal1(st, kv->key, flags + 1);
                    marshal1(st, kv->value, flags + 1);
                }
            }
            return;
        case DST_STRUCT:
            {
                int32_t count;
                const DstKV *kv = NULL;
                const DstKV *struct_ = dst_unwrap_struct(x);
                count = dst_struct_length(struct_);
                if (dst_buffer_push_u8(b, LB_STRUCT)) goto overflow;
                if (pushint(b, count)) goto overflow;
                while ((kv = dst_struct_next(struct_, kv))) {
                    marshal1(st, kv->key, flags + 1);
                    marshal1(st, kv->value, flags + 1);
                }
                /* Mark as seen AFTER marshaling */
                MARK_SEEN();
            }
            return;
        case DST_FIBER:
        case DST_ABSTRACT:
        case DST_FUNCTION:
        case DST_CFUNCTION:
        default:
            goto nyi;
    }

#undef MARK_SEEN

    /* Errors */

nyi:
    longjmp(st->err, MR_NYI);

overflow:
    longjmp(st->err, MR_OVERFLOW);
}

int dst_marshal(DstBuffer *buf, Dst x, int flags) {
    int status;
    MarshalState st; 
    st.buf = buf;
    st.nextid = 0;
    dst_table_init(&st.seen, 0);
    if (!(status = setjmp(st.err)))
        marshal1(&st, x, flags);
    dst_table_deinit(&st.seen);
    return status;
}

typedef struct {
    jmp_buf err;
    DstArray lookup;
    const uint8_t *end;
} UnmarshalState;

enum {
    UMR_OK,
    UMR_STACKOVERFLOW,
    UMR_EOS,
    UMR_UNKNOWN,
    UMR_EXPECTED_INTEGER,
    UMR_EXPECTED_TABLE,
    UMR_INVALID_REFERENCE
} UnmarshalResult;

const char *umr_strings[] = {
    "",
    "stack overflow",
    "unexpected end of source",
    "unknown byte",
    "expected integer",
    "expected table",
    "invalid reference"
};

static const uint8_t *unmarshal1(
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
            {
                Dst lenv;
                int32_t len;
                data = unmarshal1(st, data + 1, &lenv, flags + 1);
                if (!dst_checktype(lenv, DST_INTEGER)) longjmp(st->err, UMR_EXPECTED_INTEGER);
                len = dst_unwrap_integer(lenv);
                EXTRA(len);
                if (lead == LB_STRING) {
                    const uint8_t *str = dst_string(data, len);
                    *out = dst_wrap_string(str);
                } else if (lead == LB_SYMBOL) {
                    const uint8_t *str = dst_symbol(data, len);
                    *out = dst_wrap_symbol(str);
                } else { /* (lead == LB_BUFFER) */
                    DstBuffer *buffer = dst_buffer(len);
                    buffer->count = len;
                    memcpy(buffer->data, data, len);
                    *out = dst_wrap_buffer(buffer);
                }
                dst_array_push(&st->lookup, *out);
                return data + len;
            }
        case LB_REFERENCE:
        case LB_ARRAY:
        case LB_TUPLE:
        case LB_STRUCT:
        case LB_TABLE:
        case LB_TABLE_PROTO:
            /* Things that open with integers */
            {
                Dst lenv;
                int32_t len, i;
                data = unmarshal1(st, data + 1, &lenv, flags + 1);
                if (!dst_checktype(lenv, DST_INTEGER)) longjmp(st->err, UMR_EXPECTED_INTEGER);
                len = dst_unwrap_integer(lenv);
                if (lead == LB_ARRAY) {
                    /* Array */
                    DstArray *array = dst_array(len);
                    array->count = len;
                    *out = dst_wrap_array(array);
                    dst_array_push(&st->lookup, *out);
                    for (i = 0; i < len; i++) {
                        data = unmarshal1(st, data, array->data + i, flags + 1);
                    }
                } else if (lead == LB_TUPLE) {
                    /* Tuple */
                    Dst *tup = dst_tuple_begin(len);
                    for (i = 0; i < len; i++) {
                        data = unmarshal1(st, data, tup + i, flags + 1);
                    }
                    *out = dst_wrap_tuple(dst_tuple_end(tup));
                    dst_array_push(&st->lookup, *out);
                } else if (lead == LB_STRUCT) {
                    /* Struct */
                    DstKV *struct_ = dst_struct_begin(len);
                    for (i = 0; i < len; i++) {
                        Dst key, value;
                        data = unmarshal1(st, data, &key, flags + 1);
                        data = unmarshal1(st, data, &value, flags + 1);
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
                        data = unmarshal1(st, data, &proto, flags + 1);
                        if (!dst_checktype(proto, DST_TABLE)) longjmp(st->err, UMR_EXPECTED_TABLE);
                        t->proto = dst_unwrap_table(proto);
                    }
                    for (i = 0; i < len; i++) {
                        Dst key, value;
                        data = unmarshal1(st, data, &key, flags + 1);
                        data = unmarshal1(st, data, &value, flags + 1);
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
    dst_array_init(&st.lookup, 0);
    if (!(status = setjmp(st.err))) {
        const uint8_t *nextbytes = unmarshal1(&st, bytes, out, flags);
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

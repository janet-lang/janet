/*
* Copyright (c) 2024 Calvin Rose
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
#include "features.h"
#include <janet.h>
#include "state.h"
#include "vector.h"
#include "gc.h"
#include "fiber.h"
#include "util.h"
#endif

typedef struct {
    JanetBuffer *buf;
    JanetTable seen;
    JanetTable *rreg;
    JanetFuncEnv **seen_envs;
    JanetFuncDef **seen_defs;
    int32_t nextid;
    int maybe_cycles;
} MarshalState;

/* Lead bytes in marshaling protocol */
enum {
    LB_REAL = 200,
    LB_NIL, /* 201 */
    LB_FALSE, /* 202 */
    LB_TRUE,  /* 203 */
    LB_FIBER, /* 204 */
    LB_INTEGER, /* 205 */
    LB_STRING, /* 206 */
    LB_SYMBOL, /* 207 */
    LB_KEYWORD, /* 208 */
    LB_ARRAY, /* 209 */
    LB_TUPLE, /* 210 */
    LB_TABLE, /* 211 */
    LB_TABLE_PROTO, /* 212 */
    LB_STRUCT, /* 213 */
    LB_BUFFER, /* 214 */
    LB_FUNCTION, /* 215 */
    LB_REGISTRY, /* 216 */
    LB_ABSTRACT, /* 217 */
    LB_REFERENCE, /* 218 */
    LB_FUNCENV_REF, /* 219 */
    LB_FUNCDEF_REF, /* 220 */
    LB_UNSAFE_CFUNCTION, /* 221 */
    LB_UNSAFE_POINTER, /* 222 */
    LB_STRUCT_PROTO, /* 223 */
#ifdef JANET_EV
    LB_THREADED_ABSTRACT, /* 224 */
    LB_POINTER_BUFFER, /* 225 */
#endif
    LB_TABLE_WEAKK, /* 226 */
    LB_TABLE_WEAKV, /* 227 */
    LB_TABLE_WEAKKV, /* 228 */
    LB_TABLE_WEAKK_PROTO, /* 229 */
    LB_TABLE_WEAKV_PROTO, /* 230 */
    LB_TABLE_WEAKKV_PROTO, /* 231 */
    LB_ARRAY_WEAK, /* 232 */
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

/* Merge values from an environment into an existing lookup table. */
void janet_env_lookup_into(JanetTable *renv, JanetTable *env, const char *prefix, int recurse) {
    while (env) {
        for (int32_t i = 0; i < env->capacity; i++) {
            if (janet_checktype(env->data[i].key, JANET_SYMBOL)) {
                if (prefix) {
                    int32_t prelen = (int32_t) strlen(prefix);
                    const uint8_t *oldsym = janet_unwrap_symbol(env->data[i].key);
                    int32_t oldlen = janet_string_length(oldsym);
                    uint8_t *symbuf = janet_smalloc(prelen + oldlen);
                    safe_memcpy(symbuf, prefix, prelen);
                    safe_memcpy(symbuf + prelen, oldsym, oldlen);
                    Janet s = janet_symbolv(symbuf, prelen + oldlen);
                    janet_sfree(symbuf);
                    janet_table_put(renv, s, entry_getval(env->data[i].value));
                } else {
                    janet_table_put(renv,
                                    env->data[i].key,
                                    entry_getval(env->data[i].value));
                }
            }
        }
        env = recurse ? env->proto : NULL;
    }
}

/* Make a forward lookup table from an environment (for unmarshaling) */
JanetTable *janet_env_lookup(JanetTable *env) {
    JanetTable *renv = janet_table(env->count);
    janet_env_lookup_into(renv, env, NULL, 1);
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

static void pushpointer(MarshalState *st, const void *ptr) {
    janet_buffer_push_bytes(st->buf, (const uint8_t *) &ptr, sizeof(ptr));
}

/* Marshal a size_t onto the buffer */
static void push64(MarshalState *st, uint64_t x) {
    if (x <= 0xF0) {
        /* Single byte */
        pushbyte(st, (uint8_t) x);
    } else {
        /* Multibyte, little endian */
        uint8_t bytes[9];
        int nbytes = 0;
        while (x) {
            bytes[++nbytes] = x & 0xFF;
            x >>= 8;
        }
        bytes[0] = 0xF0 + nbytes;
        pushbytes(st, bytes, nbytes + 1);
    }
}

/* Forward declaration to enable mutual recursion. */
static void marshal_one(MarshalState *st, Janet x, int flags);
static void marshal_one_fiber(MarshalState *st, JanetFiber *fiber, int flags);
static void marshal_one_def(MarshalState *st, JanetFuncDef *def, int flags);
static void marshal_one_env(MarshalState *st, JanetFuncEnv *env, int flags);

/* Prevent stack overflows */
#define MARSH_STACKCHECK if ((flags & 0xFFFF) > JANET_RECURSION_GUARD) janet_panic("stack overflow")

/* Quick check if a fiber cannot be marshalled. This is will
 * have no false positives, but may have false negatives. */
static int fiber_cannot_be_marshalled(JanetFiber *fiber) {
    if (janet_fiber_status(fiber) == JANET_STATUS_ALIVE) return 1;
    int32_t i = fiber->frame;
    while (i > 0) {
        JanetStackFrame *frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
        if (!frame->func) return 1; /* has cfunction on stack */
        i = frame->prevframe;
    }
    return 0;
}

/* Marshal a function env */
static void marshal_one_env(MarshalState *st, JanetFuncEnv *env, int flags) {
    MARSH_STACKCHECK;
    for (int32_t i = 0; i < janet_v_count(st->seen_envs); i++) {
        if (st->seen_envs[i] == env) {
            pushbyte(st, LB_FUNCENV_REF);
            pushint(st, i);
            return;
        }
    }
    janet_env_valid(env);
    janet_v_push(st->seen_envs, env);

    /* Special case for early detachment */
    if (env->offset > 0 && fiber_cannot_be_marshalled(env->as.fiber)) {
        pushint(st, 0);
        pushint(st, env->length);
        Janet *values = env->as.fiber->data + env->offset;
        uint32_t *bitset = janet_stack_frame(values)->func->def->closure_bitset;
        for (int32_t i = 0; i < env->length; i++) {
            if (1 & (bitset[i >> 5] >> (i & 0x1F))) {
                marshal_one(st, values[i], flags + 1);
            } else {
                pushbyte(st, LB_NIL);
            }
        }
    } else {
        janet_env_maybe_detach(env);
        pushint(st, env->offset);
        pushint(st, env->length);
        if (env->offset > 0) {
            /* On stack variant */
            marshal_one(st, janet_wrap_fiber(env->as.fiber), flags + 1);
        } else {
            /* Off stack variant */
            for (int32_t i = 0; i < env->length; i++)
                marshal_one(st, env->as.values[i], flags + 1);
        }
    }
}

/* Marshal a sequence of u32s */
static void janet_marshal_u32s(MarshalState *st, const uint32_t *u32s, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        pushbyte(st, u32s[i] & 0xFF);
        pushbyte(st, (u32s[i] >> 8) & 0xFF);
        pushbyte(st, (u32s[i] >> 16) & 0xFF);
        pushbyte(st, (u32s[i] >> 24) & 0xFF);
    }
}

/* Marshal a function def */
static void marshal_one_def(MarshalState *st, JanetFuncDef *def, int flags) {
    MARSH_STACKCHECK;
    for (int32_t i = 0; i < janet_v_count(st->seen_defs); i++) {
        if (st->seen_defs[i] == def) {
            pushbyte(st, LB_FUNCDEF_REF);
            pushint(st, i);
            return;
        }
    }
    /* Add to lookup */
    janet_v_push(st->seen_defs, def);

    pushint(st, def->flags);
    pushint(st, def->slotcount);
    pushint(st, def->arity);
    pushint(st, def->min_arity);
    pushint(st, def->max_arity);
    pushint(st, def->constants_length);
    pushint(st, def->bytecode_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASENVS)
        pushint(st, def->environments_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASDEFS)
        pushint(st, def->defs_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASSYMBOLMAP)
        pushint(st, def->symbolmap_length);
    if (def->flags & JANET_FUNCDEF_FLAG_HASNAME)
        marshal_one(st, janet_wrap_string(def->name), flags);
    if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCE)
        marshal_one(st, janet_wrap_string(def->source), flags);

    /* marshal constants */
    for (int32_t i = 0; i < def->constants_length; i++)
        marshal_one(st, def->constants[i], flags + 1);

    /* Marshal symbol map, if needed */
    for (int32_t i = 0; i < def->symbolmap_length; i++) {
        pushint(st, (int32_t) def->symbolmap[i].birth_pc);
        pushint(st, (int32_t) def->symbolmap[i].death_pc);
        pushint(st, (int32_t) def->symbolmap[i].slot_index);
        marshal_one(st, janet_wrap_symbol(def->symbolmap[i].symbol), flags + 1);
    }

    /* marshal the bytecode */
    janet_marshal_u32s(st, def->bytecode, def->bytecode_length);

    /* marshal the environments if needed */
    for (int32_t i = 0; i < def->environments_length; i++)
        pushint(st, def->environments[i]);

    /* marshal the sub funcdefs if needed */
    for (int32_t i = 0; i < def->defs_length; i++)
        marshal_one_def(st, def->defs[i], flags + 1);

    /* marshal source maps if needed */
    if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCEMAP) {
        int32_t current = 0;
        for (int32_t i = 0; i < def->bytecode_length; i++) {
            JanetSourceMapping map = def->sourcemap[i];
            pushint(st, map.line - current);
            pushint(st, map.column);
            current = map.line;
        }
    }

    /* Marshal closure bitset, if needed */
    if (def->flags & JANET_FUNCDEF_FLAG_HASCLOBITSET) {
        janet_marshal_u32s(st, def->closure_bitset, ((def->slotcount + 31) >> 5));
    }
}

#define JANET_FIBER_FLAG_HASCHILD (1 << 29)
#define JANET_FIBER_FLAG_HASENV   (1 << 30)
#define JANET_STACKFRAME_HASENV   (INT32_MIN)

/* Marshal a fiber */
static void marshal_one_fiber(MarshalState *st, JanetFiber *fiber, int flags) {
    MARSH_STACKCHECK;
    int32_t fflags = fiber->flags;
    if (fiber->child) fflags |= JANET_FIBER_FLAG_HASCHILD;
    if (fiber->env) fflags |= JANET_FIBER_FLAG_HASENV;
    if (janet_fiber_status(fiber) == JANET_STATUS_ALIVE)
        janet_panic("cannot marshal alive fiber");
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
        if (!frame->func) janet_panicf("cannot marshal fiber with c stackframe (%v)", janet_wrap_cfunction((JanetCFunction) frame->pc));
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
    if (fiber->env) {
        marshal_one(st, janet_wrap_table(fiber->env), flags + 1);
    }
    if (fiber->child)
        marshal_one(st, janet_wrap_fiber(fiber->child), flags + 1);
    marshal_one(st, fiber->last_value, flags + 1);
}

void janet_marshal_size(JanetMarshalContext *ctx, size_t value) {
    janet_marshal_int64(ctx, (int64_t) value);
}

void janet_marshal_int64(JanetMarshalContext *ctx, int64_t value) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    push64(st, (uint64_t) value);
}

void janet_marshal_int(JanetMarshalContext *ctx, int32_t value) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    pushint(st, value);
}

/* Only use in unsafe - don't marshal pointers otherwise */
void janet_marshal_ptr(JanetMarshalContext *ctx, const void *ptr) {
    if (!(ctx->flags & JANET_MARSHAL_UNSAFE)) {
        janet_panic("can only marshal pointers in unsafe mode");
    }
    MarshalState *st = (MarshalState *)(ctx->m_state);
    pushpointer(st, ptr);
}

void janet_marshal_byte(JanetMarshalContext *ctx, uint8_t value) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    pushbyte(st, value);
}

void janet_marshal_bytes(JanetMarshalContext *ctx, const uint8_t *bytes, size_t len) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    if (len > INT32_MAX) janet_panic("size_t too large to fit in buffer");
    pushbytes(st, bytes, (int32_t) len);
}

void janet_marshal_janet(JanetMarshalContext *ctx, Janet x) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    marshal_one(st, x, ctx->flags + 1);
}

#ifdef JANET_MARSHAL_DEBUG
#define MARK_SEEN() \
    do { if (st->maybe_cycles) { \
        Janet _check = janet_table_get(&st->seen, x); \
        if (!janet_checktype(_check, JANET_NIL)) janet_eprintf("double MARK_SEEN on %v\n", x); \
        janet_eprintf("made reference %d (%t) to %v\n", st->nextid, x, x); \
        janet_table_put(&st->seen, x, janet_wrap_integer(st->nextid++)); \
    } } while (0)
#else
#define MARK_SEEN() \
    do { if (st->maybe_cycles) { \
        janet_table_put(&st->seen, x, janet_wrap_integer(st->nextid++)); \
    } } while (0)
#endif

void janet_marshal_abstract(JanetMarshalContext *ctx, void *abstract) {
    MarshalState *st = (MarshalState *)(ctx->m_state);
    Janet x = janet_wrap_abstract(abstract);
    MARK_SEEN();
}

static void marshal_one_abstract(MarshalState *st, Janet x, int flags) {
    void *abstract = janet_unwrap_abstract(x);
#ifdef JANET_EV
    /* Threaded abstract types get passed through as pointers in the unsafe mode */
    if ((flags & JANET_MARSHAL_UNSAFE) &&
            (JANET_MEMORY_THREADED_ABSTRACT == (janet_abstract_head(abstract)->gc.flags & JANET_MEM_TYPEBITS))) {

        /* Increment refcount before sending message. This prevents a "death in transit" problem
         * where a message is garbage collected while in transit between two threads - i.e., the sending threads
         * loses the reference and runs a garbage collection before the receiving thread gets the message. */
        janet_abstract_incref(abstract);
        pushbyte(st, LB_THREADED_ABSTRACT);
        pushbytes(st, (uint8_t *) &abstract, sizeof(abstract));
        MARK_SEEN();
        return;
    }
#endif
    const JanetAbstractType *at = janet_abstract_type(abstract);
    if (at->marshal) {
        pushbyte(st, LB_ABSTRACT);
        marshal_one(st, janet_csymbolv(at->name), flags + 1);
        JanetMarshalContext context = {st, NULL, flags + 1, NULL, at};
        at->marshal(abstract, &context);
    } else {
        janet_panicf("cannot marshal %p", x);
    }
}

/* The main body of the marshaling function. Is the main
 * entry point for the mutually recursive functions. */
static void marshal_one(MarshalState *st, Janet x, int flags) {
    MARSH_STACKCHECK;
    JanetType type = janet_type(x);

    /* Check simple primitives (non reference types, no benefit from memoization) */
    switch (type) {
        default:
            break;
        case JANET_NIL:
            pushbyte(st, LB_NIL);
            return;
        case JANET_BOOLEAN:
            pushbyte(st, janet_unwrap_boolean(x) ? LB_TRUE : LB_FALSE);
            return;
        case JANET_NUMBER: {
            double xval = janet_unwrap_number(x);
            if (janet_checkintrange(xval)) {
                pushint(st, (int32_t) xval);
                return;
            }
            break;
        }
    }

    /* Check reference and registry value */
    {
        Janet check;
        if (st->maybe_cycles) {
            check = janet_table_get(&st->seen, x);
            if (janet_checkint(check)) {
                pushbyte(st, LB_REFERENCE);
                pushint(st, janet_unwrap_integer(check));
                return;
            }
        }
        if (st->rreg) {
            check = janet_table_get(st->rreg, x);
            if (janet_checktype(check, JANET_SYMBOL)) {
                MARK_SEEN();
                const uint8_t *regname = janet_unwrap_symbol(check);
                pushbyte(st, LB_REGISTRY);
                pushint(st, janet_string_length(regname));
                pushbytes(st, regname, janet_string_length(regname));
                return;
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
            return;
        }
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
            return;
        }
        case JANET_BUFFER: {
            JanetBuffer *buffer = janet_unwrap_buffer(x);
            /* Record reference */
            MARK_SEEN();
#ifdef JANET_EV
            if ((flags & JANET_MARSHAL_UNSAFE) &&
                    (buffer->gc.flags & JANET_BUFFER_FLAG_NO_REALLOC)) {
                pushbyte(st, LB_POINTER_BUFFER);
                pushint(st, buffer->count);
                pushint(st, buffer->capacity);
                pushpointer(st, buffer->data);
                return;
            }
#endif
            pushbyte(st, LB_BUFFER);
            pushint(st, buffer->count);
            pushbytes(st, buffer->data, buffer->count);
            return;
        }
        case JANET_ARRAY: {
            int32_t i;
            JanetArray *a = janet_unwrap_array(x);
            MARK_SEEN();
            enum JanetMemoryType memtype = janet_gc_type(a);
            pushbyte(st, memtype == JANET_MEMORY_ARRAY_WEAK ? LB_ARRAY_WEAK : LB_ARRAY);
            pushint(st, a->count);
            for (i = 0; i < a->count; i++)
                marshal_one(st, a->data[i], flags + 1);
            return;
        }
        case JANET_TUPLE: {
            int32_t i, count, flag;
            const Janet *tup = janet_unwrap_tuple(x);
            count = janet_tuple_length(tup);
            flag = janet_tuple_flag(tup) >> 16;
            pushbyte(st, LB_TUPLE);
            pushint(st, count);
            pushint(st, flag);
            for (i = 0; i < count; i++)
                marshal_one(st, tup[i], flags + 1);
            /* Mark as seen AFTER marshaling */
            MARK_SEEN();
            return;
        }
        case JANET_TABLE: {
            JanetTable *t = janet_unwrap_table(x);
            MARK_SEEN();
            enum JanetMemoryType memtype = janet_gc_type(t);
            if (memtype == JANET_MEMORY_TABLE_WEAKK) {
                pushbyte(st, t->proto ? LB_TABLE_WEAKK_PROTO : LB_TABLE_WEAKK);
            } else if (memtype == JANET_MEMORY_TABLE_WEAKV) {
                pushbyte(st, t->proto ? LB_TABLE_WEAKV_PROTO : LB_TABLE_WEAKV);
            } else if (memtype == JANET_MEMORY_TABLE_WEAKKV) {
                pushbyte(st, t->proto ? LB_TABLE_WEAKKV_PROTO : LB_TABLE_WEAKKV);
            } else {
                pushbyte(st, t->proto ? LB_TABLE_PROTO : LB_TABLE);
            }
            pushint(st, t->count);
            if (t->proto)
                marshal_one(st, janet_wrap_table(t->proto), flags + 1);
            for (int32_t i = 0; i < t->capacity; i++) {
                if (janet_checktype(t->data[i].key, JANET_NIL))
                    continue;
                marshal_one(st, t->data[i].key, flags + 1);
                marshal_one(st, t->data[i].value, flags + 1);
            }
            return;
        }
        case JANET_STRUCT: {
            int32_t count;
            const JanetKV *struct_ = janet_unwrap_struct(x);
            count = janet_struct_length(struct_);
            pushbyte(st, janet_struct_proto(struct_) ? LB_STRUCT_PROTO : LB_STRUCT);
            pushint(st, count);
            if (janet_struct_proto(struct_))
                marshal_one(st, janet_wrap_struct(janet_struct_proto(struct_)), flags + 1);
            for (int32_t i = 0; i < janet_struct_capacity(struct_); i++) {
                if (janet_checktype(struct_[i].key, JANET_NIL))
                    continue;
                marshal_one(st, struct_[i].key, flags + 1);
                marshal_one(st, struct_[i].value, flags + 1);
            }
            /* Mark as seen AFTER marshaling */
            MARK_SEEN();
            return;
        }
        case JANET_ABSTRACT: {
            marshal_one_abstract(st, x, flags);
            return;
        }
        case JANET_FUNCTION: {
            pushbyte(st, LB_FUNCTION);
            JanetFunction *func = janet_unwrap_function(x);
            pushint(st, func->def->environments_length);
            /* Mark seen before reading def */
            MARK_SEEN();
            marshal_one_def(st, func->def, flags);
            for (int32_t i = 0; i < func->def->environments_length; i++)
                marshal_one_env(st, func->envs[i], flags + 1);
            return;
        }
        case JANET_FIBER: {
            MARK_SEEN();
            pushbyte(st, LB_FIBER);
            marshal_one_fiber(st, janet_unwrap_fiber(x), flags + 1);
            return;
        }
        case JANET_CFUNCTION: {
            if (!(flags & JANET_MARSHAL_UNSAFE)) goto no_registry;
            MARK_SEEN();
            pushbyte(st, LB_UNSAFE_CFUNCTION);
            JanetCFunction cfn = janet_unwrap_cfunction(x);
            pushbytes(st, (uint8_t *) &cfn, sizeof(JanetCFunction));
            return;
        }
        case JANET_POINTER: {
            if (!(flags & JANET_MARSHAL_UNSAFE)) goto no_registry;
            MARK_SEEN();
            pushbyte(st, LB_UNSAFE_POINTER);
            pushpointer(st, janet_unwrap_pointer(x));
            return;
        }
    no_registry:
        default: {
            janet_panicf("no registry value and cannot marshal %p", x);
        }
    }
#undef MARK_SEEN
}

void janet_marshal(
    JanetBuffer *buf,
    Janet x,
    JanetTable *rreg,
    int flags) {
    MarshalState st;
    st.buf = buf;
    st.nextid = 0;
    st.seen_defs = NULL;
    st.seen_envs = NULL;
    st.rreg = rreg;
    st.maybe_cycles = !(flags & JANET_MARSHAL_NO_CYCLES);
    janet_table_init(&st.seen, 0);
    marshal_one(&st, x, flags);
    janet_table_deinit(&st.seen);
    janet_v_free(st.seen_envs);
    janet_v_free(st.seen_defs);
}

typedef struct {
    jmp_buf err;
    Janet *lookup;
    JanetTable *reg;
    JanetFuncEnv **lookup_envs;
    JanetFuncDef **lookup_defs;
    const uint8_t *start;
    const uint8_t *end;
} UnmarshalState;

#define MARSH_EOS(st, data) do { \
    if ((data) >= (st)->end) janet_panic("unexpected end of source");\
} while (0)

/* Helper to read a 32 bit integer from an unmarshal state */
static int32_t readint(UnmarshalState *st, const uint8_t **atdata) {
    const uint8_t *data = *atdata;
    int32_t ret;
    MARSH_EOS(st, data);
    if (*data < 128) {
        ret = *data++;
    } else if (*data < 192) {
        MARSH_EOS(st, data + 1);
        uint32_t uret = ((data[0] & 0x3F) << 8) + data[1];
        /* Sign extend 18 MSBs */
        uret |= (uret >> 13) ? 0xFFFFC000 : 0;
        ret = (int32_t)uret;
        data += 2;
    } else if (*data == LB_INTEGER) {
        MARSH_EOS(st, data + 4);
        uint32_t ui = ((uint32_t)(data[1]) << 24) |
                      ((uint32_t)(data[2]) << 16) |
                      ((uint32_t)(data[3]) << 8) |
                      (uint32_t)(data[4]);
        ret = (int32_t)ui;
        data += 5;
    } else {
        janet_panicf("expected integer, got byte %x at index %d",
                     *data,
                     data - st->start);
        ret = 0;
    }
    *atdata = data;
    return ret;
}

/* Helper to read a natural number (int >= 0). */
static int32_t readnat(UnmarshalState *st, const uint8_t **atdata) {
    int32_t ret = readint(st, atdata);
    if (ret < 0) {
        janet_panicf("expected integer >= 0, got %d", ret);
    }
    return ret;
}

/* Helper to read a size_t (up to 8 bytes unsigned). */
static uint64_t read64(UnmarshalState *st, const uint8_t **atdata) {
    uint64_t ret;
    const uint8_t *data = *atdata;
    MARSH_EOS(st, data);
    if (*data <= 0xF0) {
        /* Single byte */
        ret = *data;
        *atdata = data + 1;
    } else {
        /* Multibyte, little endian */
        int nbytes = *data - 0xF0;
        ret = 0;
        if (nbytes > 8) janet_panic("invalid 64 bit integer");
        MARSH_EOS(st, data + nbytes);
        for (int i = nbytes; i > 0; i--)
            ret = (ret << 8) + data[i];
        *atdata = data + nbytes + 1;
    }
    return ret;
}

#ifdef JANET_MARSHAL_DEBUG
static void dump_reference_table(UnmarshalState *st) {
    for (int32_t i = 0; i < janet_v_count(st->lookup); i++) {
        janet_eprintf("  reference %d (%t) = %v\n", i, st->lookup[i], st->lookup[i]);
    }
}
#endif

/* Assert a janet type */
static void janet_asserttype(Janet x, JanetType t, UnmarshalState *st) {
    if (!janet_checktype(x, t)) {
#ifdef JANET_MARSHAL_DEBUG
        dump_reference_table(st);
#else
        (void) st;
#endif
        janet_panicf("expected type %T, got %v", 1 << t, x);
    }
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
    MARSH_EOS(st, data);
    if (*data == LB_FUNCENV_REF) {
        data++;
        int32_t index = readint(st, &data);
        if (index < 0 || index >= janet_v_count(st->lookup_envs))
            janet_panicf("invalid funcenv reference %d", index);
        *out = st->lookup_envs[index];
    } else {
        JanetFuncEnv *env = janet_gcalloc(JANET_MEMORY_FUNCENV, sizeof(JanetFuncEnv));
        env->length = 0;
        env->offset = 0;
        env->as.values = NULL;
        janet_v_push(st->lookup_envs, env);
        int32_t offset = readnat(st, &data);
        int32_t length = readnat(st, &data);
        if (offset > 0) {
            Janet fiberv;
            /* On stack variant */
            data = unmarshal_one(st, data, &fiberv, flags);
            janet_asserttype(fiberv, JANET_FIBER, st);
            env->as.fiber = janet_unwrap_fiber(fiberv);
            /* Negative offset indicates untrusted input */
            env->offset = -offset;
        } else {
            /* Off stack variant */
            if (length == 0) {
                janet_panic("invalid funcenv length");
            }
            env->as.values = janet_malloc(sizeof(Janet) * (size_t) length);
            if (!env->as.values) {
                JANET_OUT_OF_MEMORY;
            }
            env->offset = 0;
            for (int32_t i = 0; i < length; i++)
                data = unmarshal_one(st, data, env->as.values + i, flags);
        }
        env->length = length;
        *out = env;
    }
    return data;
}

/* Unmarshal a series of u32s */
static const uint8_t *janet_unmarshal_u32s(UnmarshalState *st, const uint8_t *data, uint32_t *into, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        MARSH_EOS(st, data + 3);
        into[i] =
            (uint32_t)(data[0]) |
            ((uint32_t)(data[1]) << 8) |
            ((uint32_t)(data[2]) << 16) |
            ((uint32_t)(data[3]) << 24);
        data += 4;
    }
    return data;
}

/* Unmarshal a funcdef */
static const uint8_t *unmarshal_one_def(
    UnmarshalState *st,
    const uint8_t *data,
    JanetFuncDef **out,
    int flags) {
    MARSH_EOS(st, data);
    if (*data == LB_FUNCDEF_REF) {
        data++;
        int32_t index = readint(st, &data);
        if (index < 0 || index >= janet_v_count(st->lookup_defs))
            janet_panicf("invalid funcdef reference %d", index);
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
        def->closure_bitset = NULL;
        def->defs = NULL;
        def->environments = NULL;
        def->constants = NULL;
        def->bytecode = NULL;
        def->sourcemap = NULL;
        def->symbolmap = NULL;
        def->symbolmap_length = 0;
        janet_v_push(st->lookup_defs, def);

        /* Set default lengths to zero */
        int32_t bytecode_length = 0;
        int32_t constants_length = 0;
        int32_t environments_length = 0;
        int32_t defs_length = 0;
        int32_t symbolmap_length = 0;

        /* Read flags and other fixed values */
        def->flags = readint(st, &data);
        def->slotcount = readnat(st, &data);
        def->arity = readnat(st, &data);
        def->min_arity = readnat(st, &data);
        def->max_arity = readnat(st, &data);

        /* Read some lengths */
        constants_length = readnat(st, &data);
        bytecode_length = readnat(st, &data);
        if (def->flags & JANET_FUNCDEF_FLAG_HASENVS)
            environments_length = readnat(st, &data);
        if (def->flags & JANET_FUNCDEF_FLAG_HASDEFS)
            defs_length = readnat(st, &data);
        if (def->flags & JANET_FUNCDEF_FLAG_HASSYMBOLMAP)
            symbolmap_length = readnat(st, &data);

        /* Check name and source (optional) */
        if (def->flags & JANET_FUNCDEF_FLAG_HASNAME) {
            Janet x;
            data = unmarshal_one(st, data, &x, flags + 1);
            janet_asserttype(x, JANET_STRING, st);
            def->name = janet_unwrap_string(x);
        }
        if (def->flags & JANET_FUNCDEF_FLAG_HASSOURCE) {
            Janet x;
            data = unmarshal_one(st, data, &x, flags + 1);
            janet_asserttype(x, JANET_STRING, st);
            def->source = janet_unwrap_string(x);
        }

        /* Unmarshal constants */
        if (constants_length) {
            def->constants = janet_malloc(sizeof(Janet) * constants_length);
            if (!def->constants) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < constants_length; i++)
                data = unmarshal_one(st, data, def->constants + i, flags + 1);
        } else {
            def->constants = NULL;
        }
        def->constants_length = constants_length;

        /* Unmarshal symbol map, if needed */
        if (def->flags & JANET_FUNCDEF_FLAG_HASSYMBOLMAP) {
            size_t size = sizeof(JanetSymbolMap) * symbolmap_length;
            def->symbolmap = janet_malloc(size);
            if (def->symbolmap == NULL) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < symbolmap_length; i++) {
                def->symbolmap[i].birth_pc = (uint32_t) readint(st, &data);
                def->symbolmap[i].death_pc = (uint32_t) readint(st, &data);
                def->symbolmap[i].slot_index = (uint32_t) readint(st, &data);
                Janet value;
                data = unmarshal_one(st, data, &value, flags + 1);
                if (!janet_checktype(value, JANET_SYMBOL)) {
                    janet_panicf("corrupted symbolmap when unmarshalling debug info, got %v", value);
                }
                def->symbolmap[i].symbol = janet_unwrap_symbol(value);
            }
            def->symbolmap_length = (uint32_t) symbolmap_length;
        }

        /* Unmarshal bytecode */
        def->bytecode = janet_malloc(sizeof(uint32_t) * bytecode_length);
        if (!def->bytecode) {
            JANET_OUT_OF_MEMORY;
        }
        data = janet_unmarshal_u32s(st, data, def->bytecode, bytecode_length);
        def->bytecode_length = bytecode_length;

        /* Unmarshal environments */
        if (def->flags & JANET_FUNCDEF_FLAG_HASENVS) {
            def->environments = janet_calloc(1, sizeof(int32_t) * (size_t) environments_length);
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
            def->defs = janet_calloc(1, sizeof(JanetFuncDef *) * (size_t) defs_length);
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
            def->sourcemap = janet_malloc(sizeof(JanetSourceMapping) * (size_t) bytecode_length);
            if (!def->sourcemap) {
                JANET_OUT_OF_MEMORY;
            }
            for (int32_t i = 0; i < bytecode_length; i++) {
                current += readint(st, &data);
                def->sourcemap[i].line = current;
                def->sourcemap[i].column = readint(st, &data);
            }
        } else {
            def->sourcemap = NULL;
        }

        /* Unmarshal closure bitset if needed */
        if (def->flags & JANET_FUNCDEF_FLAG_HASCLOBITSET) {
            int32_t n = (def->slotcount + 31) >> 5;
            def->closure_bitset = janet_malloc(sizeof(uint32_t) * (size_t) n);
            if (NULL == def->closure_bitset) {
                JANET_OUT_OF_MEMORY;
            }
            data = janet_unmarshal_u32s(st, data, def->closure_bitset, n);
        }

        /* Validate */
        if (janet_verify(def))
            janet_panic("funcdef has invalid bytecode");

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

    /* Initialize a new fiber with gc friendly defaults */
    JanetFiber *fiber = janet_gcalloc(JANET_MEMORY_FIBER, sizeof(JanetFiber));
    fiber->flags = 0;
    fiber->frame = 0;
    fiber->stackstart = 0;
    fiber->stacktop = 0;
    fiber->capacity = 0;
    fiber->maxstack = 0;
    fiber->data = NULL;
    fiber->child = NULL;
    fiber->env = NULL;
    fiber->last_value = janet_wrap_nil();
#ifdef JANET_EV
    fiber->sched_id = 0;
    fiber->supervisor_channel = NULL;
    fiber->ev_state = NULL;
    fiber->ev_callback = NULL;
    fiber->ev_stream = NULL;
#endif

    /* Push fiber to seen stack */
    janet_v_push(st->lookup, janet_wrap_fiber(fiber));

    /* Read ints */
    int32_t fiber_flags = readint(st, &data);
    int32_t frame = readnat(st, &data);
    int32_t fiber_stackstart = readnat(st, &data);
    int32_t fiber_stacktop = readnat(st, &data);
    int32_t fiber_maxstack = readnat(st, &data);
    JanetTable *fiber_env = NULL;

    /* Check for bad flags and ints */
    if ((int32_t)(frame + JANET_FRAME_SIZE) > fiber_stackstart ||
            fiber_stackstart > fiber_stacktop ||
            fiber_stacktop > fiber_maxstack) {
        janet_panic("fiber has incorrect stack setup");
    }

    /* Allocate stack memory */
    fiber->capacity = fiber_stacktop + 10;
    fiber->data = janet_malloc(sizeof(Janet) * fiber->capacity);
    if (!fiber->data) {
        JANET_OUT_OF_MEMORY;
    }
    for (int32_t i = 0; i < fiber->capacity; i++) {
        fiber->data[i] = janet_wrap_nil();
    }

    /* get frames */
    int32_t stack = frame;
    int32_t stacktop = fiber_stackstart - JANET_FRAME_SIZE;
    while (stack > 0) {
        JanetFunction *func = NULL;
        JanetFuncDef *def = NULL;
        JanetFuncEnv *env = NULL;
        int32_t frameflags = readint(st, &data);
        int32_t prevframe = readnat(st, &data);
        int32_t pcdiff = readnat(st, &data);

        /* Get frame items */
        Janet *framestack = fiber->data + stack;
        JanetStackFrame *framep = janet_stack_frame(framestack);

        /* Get function */
        Janet funcv;
        data = unmarshal_one(st, data, &funcv, flags + 1);
        janet_asserttype(funcv, JANET_FUNCTION, st);
        func = janet_unwrap_function(funcv);
        def = func->def;

        /* Check env */
        if (frameflags & JANET_STACKFRAME_HASENV) {
            frameflags &= ~JANET_STACKFRAME_HASENV;
            data = unmarshal_one_env(st, data, &env, flags + 1);
        }

        /* Error checking */
        int32_t expected_framesize = def->slotcount;
        if (expected_framesize != stacktop - stack) {
            janet_panic("fiber stackframe size mismatch");
        }
        if (pcdiff >= def->bytecode_length) {
            janet_panic("fiber stackframe has invalid pc");
        }
        if ((int32_t)(prevframe + JANET_FRAME_SIZE) > stack) {
            janet_panic("fiber stackframe does not align with previous frame");
        }

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
    if (stack < 0) {
        janet_panic("fiber has too many stackframes");
    }

    /* Check for fiber env */
    if (fiber_flags & JANET_FIBER_FLAG_HASENV) {
        Janet envv;
        fiber_flags &= ~JANET_FIBER_FLAG_HASENV;
        data = unmarshal_one(st, data, &envv, flags + 1);
        janet_asserttype(envv, JANET_TABLE, st);
        fiber_env = janet_unwrap_table(envv);
    }

    /* Check for child fiber */
    if (fiber_flags & JANET_FIBER_FLAG_HASCHILD) {
        Janet fiberv;
        fiber_flags &= ~JANET_FIBER_FLAG_HASCHILD;
        data = unmarshal_one(st, data, &fiberv, flags + 1);
        janet_asserttype(fiberv, JANET_FIBER, st);
        fiber->child = janet_unwrap_fiber(fiberv);
    }

    /* Get the fiber last value */
    data = unmarshal_one(st, data, &fiber->last_value, flags + 1);

    /* We have valid fiber, finally construct remaining fields. */
    fiber->frame = frame;
    fiber->flags = fiber_flags;
    fiber->stackstart = fiber_stackstart;
    fiber->stacktop = fiber_stacktop;
    fiber->maxstack = fiber_maxstack;
    fiber->env = fiber_env;

    int status = janet_fiber_status(fiber);
    if (status < 0 || status > JANET_STATUS_ALIVE) {
        janet_panic("invalid fiber status");
    }

    /* Return data */
    *out = fiber;
    return data;
}

void janet_unmarshal_ensure(JanetMarshalContext *ctx, size_t size) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    MARSH_EOS(st, ctx->data + size);
}

int32_t janet_unmarshal_int(JanetMarshalContext *ctx) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    return readint(st, &(ctx->data));
}

size_t janet_unmarshal_size(JanetMarshalContext *ctx) {
    return (size_t) janet_unmarshal_int64(ctx);
}

int64_t janet_unmarshal_int64(JanetMarshalContext *ctx) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    return read64(st, &(ctx->data));
}

void *janet_unmarshal_ptr(JanetMarshalContext *ctx) {
    if (!(ctx->flags & JANET_MARSHAL_UNSAFE)) {
        janet_panic("can only unmarshal pointers in unsafe mode");
    }
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    void *ptr;
    MARSH_EOS(st, ctx->data + sizeof(void *) - 1);
    memcpy((char *) &ptr, ctx->data, sizeof(void *));
    ctx->data += sizeof(void *);
    return ptr;
}

uint8_t janet_unmarshal_byte(JanetMarshalContext *ctx) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    MARSH_EOS(st, ctx->data);
    return *(ctx->data++);
}

void janet_unmarshal_bytes(JanetMarshalContext *ctx, uint8_t *dest, size_t len) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    MARSH_EOS(st, ctx->data + len - 1);
    safe_memcpy(dest, ctx->data, len);
    ctx->data += len;
}

Janet janet_unmarshal_janet(JanetMarshalContext *ctx) {
    Janet ret;
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    ctx->data = unmarshal_one(st, ctx->data, &ret, ctx->flags);
    return ret;
}

void janet_unmarshal_abstract_reuse(JanetMarshalContext *ctx, void *p) {
    UnmarshalState *st = (UnmarshalState *)(ctx->u_state);
    if (ctx->at == NULL) {
        janet_panicf("janet_unmarshal_abstract called more than once");
    }
    janet_v_push(st->lookup, janet_wrap_abstract(p));
    ctx->at = NULL;
}

void *janet_unmarshal_abstract(JanetMarshalContext *ctx, size_t size) {
    void *p = janet_abstract(ctx->at, size);
    janet_unmarshal_abstract_reuse(ctx, p);
    return p;
}

void *janet_unmarshal_abstract_threaded(JanetMarshalContext *ctx, size_t size) {
#ifdef JANET_THREADS
    void *p = janet_abstract_threaded(ctx->at, size);
    janet_unmarshal_abstract_reuse(ctx, p);
    return p;
#else
    (void) ctx;
    (void) size;
    janet_panic("threaded abstracts not supported");
#endif
}

static const uint8_t *unmarshal_one_abstract(UnmarshalState *st, const uint8_t *data, Janet *out, int flags) {
    Janet key;
    data = unmarshal_one(st, data, &key, flags + 1);
    const JanetAbstractType *at = janet_get_abstract_type(key);
    if (at == NULL) janet_panic("unknown abstract type");
    if (at->unmarshal) {
        JanetMarshalContext context = {NULL, st, flags, data, at};
        void *abst = at->unmarshal(&context);
        janet_assert(abst != NULL, "null pointer abstract");
        *out = janet_wrap_abstract(abst);
        if (context.at != NULL) {
            janet_panic("janet_unmarshal_abstract not called");
        }
        return context.data;
    }
    janet_panic("invalid abstract type - no unmarshal function pointer");
}

static const uint8_t *unmarshal_one(
    UnmarshalState *st,
    const uint8_t *data,
    Janet *out,
    int flags) {
    uint8_t lead;
    MARSH_STACKCHECK;
    MARSH_EOS(st, data);
    lead = data[0];
    if (lead < LB_REAL) {
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
            MARSH_EOS(st, data + 4);
            uint32_t ui = ((uint32_t)(data[4])) |
                          ((uint32_t)(data[3]) << 8) |
                          ((uint32_t)(data[2]) << 16) |
                          ((uint32_t)(data[1]) << 24);
            int32_t si = (int32_t)ui;
            *out = janet_wrap_integer(si);
            return data + 5;
        case LB_REAL:
            /* Real */
        {
            union {
                double d;
                uint8_t bytes[8];
            } u;
            MARSH_EOS(st, data + 8);
#ifdef JANET_BIG_ENDIAN
            u.bytes[0] = data[8];
            u.bytes[1] = data[7];
            u.bytes[2] = data[6];
            u.bytes[3] = data[5];
            u.bytes[4] = data[4];
            u.bytes[5] = data[3];
            u.bytes[6] = data[2];
            u.bytes[7] = data[1];
#else
            memcpy(&u.bytes, data + 1, sizeof(double));
#endif
            *out = janet_wrap_number_safe(u.d);
            janet_v_push(st->lookup, *out);
            return data + 9;
        }
        case LB_STRING:
        case LB_SYMBOL:
        case LB_BUFFER:
        case LB_KEYWORD:
        case LB_REGISTRY: {
            data++;
            int32_t len = readnat(st, &data);
            MARSH_EOS(st, data - 1 + len);
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
                safe_memcpy(buffer->data, data, len);
                *out = janet_wrap_buffer(buffer);
            }
            janet_v_push(st->lookup, *out);
            return data + len;
        }
        case LB_FIBER: {
            JanetFiber *fiber;
            data = unmarshal_one_fiber(st, data + 1, &fiber, flags + 1);
            *out = janet_wrap_fiber(fiber);
            return data;
        }
        case LB_FUNCTION: {
            JanetFunction *func;
            JanetFuncDef *def;
            data++;
            int32_t len = readnat(st, &data);
            if (len > 255) {
                janet_panicf("invalid function - too many environments (%d)", len);
            }
            func = janet_gcalloc(JANET_MEMORY_FUNCTION, sizeof(JanetFunction) +
                                 len * sizeof(JanetFuncEnv));
            func->def = NULL;
            for (int32_t i = 0; i < len; i++) {
                func->envs[i] = NULL;
            }
            *out = janet_wrap_function(func);
            janet_v_push(st->lookup, *out);
            data = unmarshal_one_def(st, data, &def, flags + 1);
            func->def = def;
            for (int32_t i = 0; i < len; i++) {
                data = unmarshal_one_env(st, data, &(func->envs[i]), flags + 1);
            }
            return data;
        }
        case LB_ABSTRACT: {
            data++;
            return unmarshal_one_abstract(st, data, out, flags);
        }
        case LB_REFERENCE:
        case LB_ARRAY:
        case LB_ARRAY_WEAK:
        case LB_TUPLE:
        case LB_STRUCT:
        case LB_STRUCT_PROTO:
        case LB_TABLE:
        case LB_TABLE_PROTO:
        case LB_TABLE_WEAKK:
        case LB_TABLE_WEAKV:
        case LB_TABLE_WEAKKV:
        case LB_TABLE_WEAKK_PROTO:
        case LB_TABLE_WEAKV_PROTO:
        case LB_TABLE_WEAKKV_PROTO:
            /* Things that open with integers */
        {
            data++;
            int32_t len = readnat(st, &data);
            /* DOS check */
            if (lead != LB_REFERENCE) {
                MARSH_EOS(st, data - 1 + len);
            }
            if (lead == LB_ARRAY || lead == LB_ARRAY_WEAK) {
                /* Array */
                JanetArray *array = (lead == LB_ARRAY_WEAK) ? janet_array_weak(len) : janet_array(len);
                array->count = len;
                *out = janet_wrap_array(array);
                janet_v_push(st->lookup, *out);
                for (int32_t i = 0; i < len; i++) {
                    data = unmarshal_one(st, data, array->data + i, flags + 1);
                }
            } else if (lead == LB_TUPLE) {
                /* Tuple */
                Janet *tup = janet_tuple_begin(len);
                int32_t flag = readint(st, &data);
                janet_tuple_flag(tup) |= flag << 16;
                for (int32_t i = 0; i < len; i++) {
                    data = unmarshal_one(st, data, tup + i, flags + 1);
                }
                *out = janet_wrap_tuple(janet_tuple_end(tup));
                janet_v_push(st->lookup, *out);
            } else if (lead == LB_STRUCT || lead == LB_STRUCT_PROTO) {
                /* Struct */
                JanetKV *struct_ = janet_struct_begin(len);
                if (lead == LB_STRUCT_PROTO) {
                    Janet proto;
                    data = unmarshal_one(st, data, &proto, flags + 1);
                    janet_asserttype(proto, JANET_STRUCT, st);
                    janet_struct_proto(struct_) = janet_unwrap_struct(proto);
                }
                for (int32_t i = 0; i < len; i++) {
                    Janet key, value;
                    data = unmarshal_one(st, data, &key, flags + 1);
                    data = unmarshal_one(st, data, &value, flags + 1);
                    janet_struct_put(struct_, key, value);
                }
                *out = janet_wrap_struct(janet_struct_end(struct_));
                janet_v_push(st->lookup, *out);
            } else if (lead == LB_REFERENCE) {
                if (len >= janet_v_count(st->lookup))
                    janet_panicf("invalid reference %d", len);
                *out = st->lookup[len];
            } else {
                /* Table */
                JanetTable *t;
                if (lead == LB_TABLE_WEAKK_PROTO || lead == LB_TABLE_WEAKK) {
                    t = janet_table_weakk(len);
                } else if (lead == LB_TABLE_WEAKV_PROTO || lead == LB_TABLE_WEAKV) {
                    t = janet_table_weakv(len);
                } else if (lead == LB_TABLE_WEAKKV_PROTO || lead == LB_TABLE_WEAKKV) {
                    t = janet_table_weakkv(len);
                } else {
                    t = janet_table(len);
                }
                *out = janet_wrap_table(t);
                janet_v_push(st->lookup, *out);
                if (lead == LB_TABLE_PROTO || lead == LB_TABLE_WEAKK_PROTO || lead == LB_TABLE_WEAKV_PROTO || lead == LB_TABLE_WEAKKV_PROTO) {
                    Janet proto;
                    data = unmarshal_one(st, data, &proto, flags + 1);
                    janet_asserttype(proto, JANET_TABLE, st);
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
        case LB_UNSAFE_POINTER: {
            MARSH_EOS(st, data + sizeof(void *));
            data++;
            if (!(flags & JANET_MARSHAL_UNSAFE)) {
                janet_panicf("unsafe flag not given, "
                             "will not unmarshal raw pointer at index %d",
                             (int)(data - st->start));
            }
            union {
                void *ptr;
                uint8_t bytes[sizeof(void *)];
            } u;
            memcpy(u.bytes, data, sizeof(void *));
            data += sizeof(void *);
            *out = janet_wrap_pointer(u.ptr);
            janet_v_push(st->lookup, *out);
            return data;
        }
#ifdef JANET_EV
        case LB_POINTER_BUFFER: {
            data++;
            int32_t count = readnat(st, &data);
            int32_t capacity = readnat(st, &data);
            MARSH_EOS(st, data + sizeof(void *));
            union {
                void *ptr;
                uint8_t bytes[sizeof(void *)];
            } u;
            if (!(flags & JANET_MARSHAL_UNSAFE)) {
                janet_panicf("unsafe flag not given, "
                             "will not unmarshal raw pointer at index %d",
                             (int)(data - st->start));
            }
            memcpy(u.bytes, data, sizeof(void *));
            data += sizeof(void *);
            JanetBuffer *buffer = janet_pointer_buffer_unsafe(u.ptr, capacity, count);
            *out = janet_wrap_buffer(buffer);
            janet_v_push(st->lookup, *out);
            return data;
        }
#endif
        case LB_UNSAFE_CFUNCTION: {
            MARSH_EOS(st, data + sizeof(JanetCFunction));
            data++;
            if (!(flags & JANET_MARSHAL_UNSAFE)) {
                janet_panicf("unsafe flag not given, "
                             "will not unmarshal function pointer at index %d",
                             (int)(data - st->start));
            }
            union {
                JanetCFunction ptr;
                uint8_t bytes[sizeof(JanetCFunction)];
            } u;
            memcpy(u.bytes, data, sizeof(JanetCFunction));
            data += sizeof(JanetCFunction);
            *out = janet_wrap_cfunction(u.ptr);
            janet_v_push(st->lookup, *out);
            return data;
        }
#ifdef JANET_EV
        case LB_THREADED_ABSTRACT: {
            MARSH_EOS(st, data + sizeof(void *));
            data++;
            if (!(flags & JANET_MARSHAL_UNSAFE)) {
                janet_panicf("unsafe flag not given, "
                             "will not unmarshal threaded abstract pointer at index %d",
                             (int)(data - st->start));
            }
            union {
                void *ptr;
                uint8_t bytes[sizeof(void *)];
            } u;
            memcpy(u.bytes, data, sizeof(void *));
            data += sizeof(void *);

            if (flags & JANET_MARSHAL_DECREF) {
                /* Decrement immediately and don't bother putting into heap */
                janet_abstract_decref(u.ptr);
                *out = janet_wrap_nil();
            } else {
                *out = janet_wrap_abstract(u.ptr);
                Janet check = janet_table_get(&janet_vm.threaded_abstracts, *out);
                if (janet_checktype(check, JANET_NIL)) {
                    /* Transfers reference from threaded channel buffer to current heap */
                    janet_table_put(&janet_vm.threaded_abstracts, *out, janet_wrap_false());
                } else {
                    /* Heap reference already accounted for, remove threaded channel reference. */
                    janet_abstract_decref(u.ptr);
                }
            }

            janet_v_push(st->lookup, *out);
            return data;
        }
#endif
        default: {
            janet_panicf("unknown byte %x at index %d",
                         *data,
                         (int)(data - st->start));
            return NULL;
        }
    }
}

Janet janet_unmarshal(
    const uint8_t *bytes,
    size_t len,
    int flags,
    JanetTable *reg,
    const uint8_t **next) {
    UnmarshalState st;
    st.start = bytes;
    st.end = bytes + len;
    st.lookup_defs = NULL;
    st.lookup_envs = NULL;
    st.lookup = NULL;
    st.reg = reg;
    Janet out;
    const uint8_t *nextbytes = unmarshal_one(&st, bytes, &out, flags);
    if (next) *next = nextbytes;
    janet_v_free(st.lookup_defs);
    janet_v_free(st.lookup_envs);
    janet_v_free(st.lookup);
    return out;
}

/* C functions */

JANET_CORE_FN(cfun_env_lookup,
              "(env-lookup env)",
              "Creates a forward lookup table for unmarshalling from an environment. "
              "To create a reverse lookup table, use the invert function to swap keys "
              "and values in the returned table.") {
    janet_fixarity(argc, 1);
    JanetTable *env = janet_gettable(argv, 0);
    return janet_wrap_table(janet_env_lookup(env));
}

JANET_CORE_FN(cfun_marshal,
              "(marshal x &opt reverse-lookup buffer no-cycles)",
              "Marshal a value into a buffer and return the buffer. The buffer "
              "can then later be unmarshalled to reconstruct the initial value. "
              "Optionally, one can pass in a reverse lookup table to not marshal "
              "aliased values that are found in the table. Then a forward "
              "lookup table can be used to recover the original value when "
              "unmarshalling.") {
    janet_arity(argc, 1, 4);
    JanetBuffer *buffer;
    JanetTable *rreg = NULL;
    uint32_t flags = 0;
    if (argc > 1) {
        rreg = janet_gettable(argv, 1);
    }
    if (argc > 2) {
        buffer = janet_getbuffer(argv, 2);
    } else {
        buffer = janet_buffer(10);
    }
    if (argc > 3 && janet_truthy(argv[3])) {
        flags |= JANET_MARSHAL_NO_CYCLES;
    }
    janet_marshal(buffer, argv[0], rreg, flags);
    return janet_wrap_buffer(buffer);
}

JANET_CORE_FN(cfun_unmarshal,
              "(unmarshal buffer &opt lookup)",
              "Unmarshal a value from a buffer. An optional lookup table "
              "can be provided to allow for aliases to be resolved. Returns the value "
              "unmarshalled from the buffer.") {
    janet_arity(argc, 1, 2);
    JanetByteView view = janet_getbytes(argv, 0);
    JanetTable *reg = NULL;
    if (argc > 1) {
        reg = janet_gettable(argv, 1);
    }
    return janet_unmarshal(view.bytes, (size_t) view.len, 0, reg, NULL);
}

/* Module entry point */
void janet_lib_marsh(JanetTable *env) {
    JanetRegExt marsh_cfuns[] = {
        JANET_CORE_REG("marshal", cfun_marshal),
        JANET_CORE_REG("unmarshal", cfun_unmarshal),
        JANET_CORE_REG("env-lookup", cfun_env_lookup),
        JANET_REG_END
    };
    janet_core_cfuns_ext(env, NULL, marsh_cfuns);
}

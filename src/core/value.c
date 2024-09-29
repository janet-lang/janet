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
#include "util.h"
#include "state.h"
#include "gc.h"
#include "fiber.h"
#include <janet.h>
#endif

#include <math.h>

static void push_traversal_node(void *lhs, void *rhs, int32_t index2) {
    JanetTraversalNode node;
    node.self = (JanetGCObject *) lhs;
    node.other = (JanetGCObject *) rhs;
    node.index = 0;
    node.index2 = index2;
    int is_new = janet_vm.traversal_base == NULL;
    if (is_new || (janet_vm.traversal + 1 >= janet_vm.traversal_top)) {
        size_t oldsize = is_new ? 0 : (janet_vm.traversal - janet_vm.traversal_base);
        size_t newsize = 2 * oldsize + 1;
        if (newsize < 128) {
            newsize = 128;
        }
        JanetTraversalNode *tn = janet_realloc(janet_vm.traversal_base, newsize * sizeof(JanetTraversalNode));
        if (tn == NULL) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.traversal_base = tn;
        janet_vm.traversal_top = janet_vm.traversal_base + newsize;
        janet_vm.traversal = janet_vm.traversal_base + oldsize;
    }
    *(++janet_vm.traversal) = node;
}

/*
 * Used for travsersing structs and tuples without recursion
 * Returns:
 * 0 - next node found
 * 1 - early stop - lhs < rhs
 * 2 - no next node found
 * 3 - early stop - lhs > rhs
 */
static int traversal_next(Janet *x, Janet *y) {
    JanetTraversalNode *t = janet_vm.traversal;
    while (t && t > janet_vm.traversal_base) {
        JanetGCObject *self = t->self;
        JanetTupleHead *tself = (JanetTupleHead *)self;
        JanetStructHead *sself = (JanetStructHead *)self;
        JanetGCObject *other = t->other;
        JanetTupleHead *tother = (JanetTupleHead *)other;
        JanetStructHead *sother = (JanetStructHead *)other;
        if ((self->flags & JANET_MEM_TYPEBITS) == JANET_MEMORY_TUPLE) {
            /* Node is a tuple at index t->index */
            if (t->index < tself->length && t->index < tother->length) {
                int32_t index = t->index++;
                *x = tself->data[index];
                *y = tother->data[index];
                janet_vm.traversal = t;
                return 0;
            }
            if (t->index2 && tself->length != tother->length) {
                return tself->length > tother->length ? 3 : 1;
            }
        } else {
            /* Node is a struct at index t->index: if t->index2 is true, we should return the values. */
            if (t->index2) {
                t->index2 = 0;
                int32_t index = t->index++;
                *x = sself->data[index].value;
                *y = sother->data[index].value;
                janet_vm.traversal = t;
                return 0;
            }
            for (int32_t i = t->index; i < sself->capacity; i++) {
                t->index2 = 1;
                *x = sself->data[t->index].key;
                *y = sother->data[t->index].key;
                janet_vm.traversal = t;
                return 0;
            }
            /* Traverse prototype */
            JanetStruct sproto = sself->proto;
            JanetStruct oproto = sother->proto;
            if (sproto && !oproto) return 3;
            if (!sproto && oproto) return 1;
            if (oproto && sproto) {
                *x = janet_wrap_struct(sproto);
                *y = janet_wrap_struct(oproto);
                janet_vm.traversal = t - 1;
                return 0;
            }
        }
        t--;
    }
    janet_vm.traversal = t;
    return 2;
}

/*
 * Define a number of functions that can be used internally on ANY Janet.
 */

Janet janet_next(Janet ds, Janet key) {
    return janet_next_impl(ds, key, 0);
}

Janet janet_next_impl(Janet ds, Janet key, int is_interpreter) {
    JanetType t = janet_type(ds);
    switch (t) {
        default:
            janet_panicf("expected iterable type, got %v", ds);
        case JANET_TABLE:
        case JANET_STRUCT: {
            const JanetKV *start;
            int32_t cap;
            if (t == JANET_TABLE) {
                JanetTable *tab = janet_unwrap_table(ds);
                cap = tab->capacity;
                start = tab->data;
            } else {
                JanetStruct st = janet_unwrap_struct(ds);
                cap = janet_struct_capacity(st);
                start = st;
            }
            const JanetKV *end = start + cap;
            const JanetKV *kv = janet_checktype(key, JANET_NIL)
                                ? start
                                : janet_dict_find(start, cap, key) + 1;
            while (kv < end) {
                if (!janet_checktype(kv->key, JANET_NIL)) return kv->key;
                kv++;
            }
            break;
        }
        case JANET_STRING:
        case JANET_KEYWORD:
        case JANET_SYMBOL:
        case JANET_BUFFER:
        case JANET_ARRAY:
        case JANET_TUPLE: {
            int32_t i;
            if (janet_checktype(key, JANET_NIL)) {
                i = 0;
            } else if (janet_checkint(key)) {
                i = janet_unwrap_integer(key) + 1;
            } else {
                break;
            }
            int32_t len;
            if (t == JANET_BUFFER) {
                len = janet_unwrap_buffer(ds)->count;
            } else if (t == JANET_ARRAY) {
                len = janet_unwrap_array(ds)->count;
            } else if (t == JANET_TUPLE) {
                len = janet_tuple_length(janet_unwrap_tuple(ds));
            } else {
                len = janet_string_length(janet_unwrap_string(ds));
            }
            if (i < len && i >= 0) {
                return janet_wrap_integer(i);
            }
            break;
        }
        case JANET_ABSTRACT: {
            JanetAbstract abst = janet_unwrap_abstract(ds);
            const JanetAbstractType *at = janet_abstract_type(abst);
            if (NULL == at->next) break;
            return at->next(abst, key);
        }
        case JANET_FIBER: {
            JanetFiber *child = janet_unwrap_fiber(ds);
            Janet retreg;
            JanetFiberStatus status = janet_fiber_status(child);
            if (status == JANET_STATUS_ALIVE ||
                    status == JANET_STATUS_DEAD ||
                    status == JANET_STATUS_ERROR ||
                    status == JANET_STATUS_USER0 ||
                    status == JANET_STATUS_USER1 ||
                    status == JANET_STATUS_USER2 ||
                    status == JANET_STATUS_USER3 ||
                    status == JANET_STATUS_USER4) {
                return janet_wrap_nil();
            }
            janet_vm.fiber->child = child;
            JanetSignal sig = janet_continue(child, janet_wrap_nil(), &retreg);
            if (sig != JANET_SIGNAL_OK && !(child->flags & (1 << sig))) {
                if (is_interpreter) {
                    janet_signalv(sig, retreg);
                } else {
                    janet_vm.fiber->child = NULL;
                    janet_panicv(retreg);
                }
            }
            janet_vm.fiber->child = NULL;
            if (sig == JANET_SIGNAL_OK ||
                    sig == JANET_SIGNAL_ERROR ||
                    sig == JANET_SIGNAL_USER0 ||
                    sig == JANET_SIGNAL_USER1 ||
                    sig == JANET_SIGNAL_USER2 ||
                    sig == JANET_SIGNAL_USER3 ||
                    sig == JANET_SIGNAL_USER4) {
                /* Fiber cannot be resumed, so discard last value. */
                return janet_wrap_nil();
            } else {
                return janet_wrap_integer(0);
            }
        }
    }
    return janet_wrap_nil();
}

/* Compare two abstract values */
static int janet_compare_abstract(JanetAbstract xx, JanetAbstract yy) {
    if (xx == yy) return 0;
    const JanetAbstractType *xt = janet_abstract_type(xx);
    const JanetAbstractType *yt = janet_abstract_type(yy);
    if (xt != yt) {
        return xt > yt ? 1 : -1;
    }
    if (xt->compare == NULL) {
        return xx > yy ? 1 : -1;
    }
    return xt->compare(xx, yy);
}

int janet_equals(Janet x, Janet y) {
    janet_vm.traversal = janet_vm.traversal_base;
    do {
        if (janet_type(x) != janet_type(y)) return 0;
        switch (janet_type(x)) {
            case JANET_NIL:
                break;
            case JANET_BOOLEAN:
                if (janet_unwrap_boolean(x) != janet_unwrap_boolean(y)) return 0;
                break;
            case JANET_NUMBER:
                if (janet_unwrap_number(x) != janet_unwrap_number(y)) return 0;
                break;
            case JANET_STRING:
                if (!janet_string_equal(janet_unwrap_string(x), janet_unwrap_string(y))) return 0;
                break;
            case JANET_ABSTRACT:
                if (janet_compare_abstract(janet_unwrap_abstract(x), janet_unwrap_abstract(y))) return 0;
                break;
            default:
                if (janet_unwrap_pointer(x) != janet_unwrap_pointer(y)) return 0;
                break;
            case JANET_TUPLE: {
                const Janet *t1 = janet_unwrap_tuple(x);
                const Janet *t2 = janet_unwrap_tuple(y);
                if (t1 == t2) break;
                if (JANET_TUPLE_FLAG_BRACKETCTOR & (janet_tuple_flag(t1) ^ janet_tuple_flag(t2))) return 0;
                if (janet_tuple_hash(t1) != janet_tuple_hash(t2)) return 0;
                if (janet_tuple_length(t1) != janet_tuple_length(t2)) return 0;
                push_traversal_node(janet_tuple_head(t1), janet_tuple_head(t2), 0);
                break;
            }
            break;
            case JANET_STRUCT: {
                const JanetKV *s1 = janet_unwrap_struct(x);
                const JanetKV *s2 = janet_unwrap_struct(y);
                if (s1 == s2) break;
                if (janet_struct_hash(s1) != janet_struct_hash(s2)) return 0;
                if (janet_struct_length(s1) != janet_struct_length(s2)) return 0;
                if (janet_struct_proto(s1) && !janet_struct_proto(s2)) return 0;
                if (!janet_struct_proto(s1) && janet_struct_proto(s2)) return 0;
                push_traversal_node(janet_struct_head(s1), janet_struct_head(s2), 0);
                break;
            }
            break;
        }
    } while (!traversal_next(&x, &y));
    return 1;
}

static uint64_t murmur64(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdUL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53UL;
    h ^= h >> 33;
    return h;
}

/* Computes a hash value for a function */
int32_t janet_hash(Janet x) {
    int32_t hash = 0;
    switch (janet_type(x)) {
        case JANET_NIL:
            hash = 0;
            break;
        case JANET_BOOLEAN:
            hash = janet_unwrap_boolean(x);
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            hash = janet_string_hash(janet_unwrap_string(x));
            break;
        case JANET_TUPLE:
            hash = janet_tuple_hash(janet_unwrap_tuple(x));
            hash += (janet_tuple_flag(janet_unwrap_tuple(x)) & JANET_TUPLE_FLAG_BRACKETCTOR) ? 1 : 0;
            break;
        case JANET_STRUCT:
            hash = janet_struct_hash(janet_unwrap_struct(x));
            break;
        case JANET_NUMBER: {
            union {
                double d;
                uint64_t u;
            } as;
            as.d = janet_unwrap_number(x);
            as.d += 0.0; /* normalize negative 0 */
            uint32_t lo = (uint32_t)(as.u & 0xFFFFFFFF);
            uint32_t hi = (uint32_t)(as.u >> 32);
            uint32_t hilo = (hi ^ lo) * 2654435769u;
            hash = (int32_t)((hilo << 16) | (hilo >> 16));
            break;
        }
        case JANET_ABSTRACT: {
            JanetAbstract xx = janet_unwrap_abstract(x);
            const JanetAbstractType *at = janet_abstract_type(xx);
            if (at->hash != NULL) {
                hash = at->hash(xx, janet_abstract_size(xx));
                break;
            }
        }
        /* fallthrough */
        default:
            if (sizeof(double) == sizeof(void *)) {
                /* Assuming 8 byte pointer (8 byte aligned) */
                uint64_t i = murmur64(janet_u64(x));
                hash = (int32_t)(i >> 32);
            } else {
                /* Assuming 4 byte pointer (or smaller) */
                uintptr_t diff = (uintptr_t) janet_unwrap_pointer(x);
                uint32_t hilo = (uint32_t) diff * 2654435769u;
                hash = (int32_t)((hilo << 16) | (hilo >> 16));
            }
            break;
    }
    return hash;
}

/* Compares x to y. If they are equal returns 0. If x is less, returns -1.
 * If y is less, returns 1. All types are comparable
 * and should have strict ordering, excepts NaNs. */
int janet_compare(Janet x, Janet y) {
    janet_vm.traversal = janet_vm.traversal_base;
    int status;
    do {
        JanetType tx = janet_type(x);
        JanetType ty = janet_type(y);
        if (tx != ty) return tx < ty ? -1 : 1;
        switch (tx) {
            case JANET_NIL:
                break;
            case JANET_BOOLEAN: {
                int diff = janet_unwrap_boolean(x) - janet_unwrap_boolean(y);
                if (diff) return diff;
                break;
            }
            case JANET_NUMBER: {
                double xx = janet_unwrap_number(x);
                double yy = janet_unwrap_number(y);
                if (xx == yy) {
                    break;
                } else {
                    return (xx < yy) ? -1 : 1;
                }
            }
            case JANET_STRING:
            case JANET_SYMBOL:
            case JANET_KEYWORD: {
                int diff = janet_string_compare(janet_unwrap_string(x), janet_unwrap_string(y));
                if (diff) return diff;
                break;
            }
            case JANET_ABSTRACT: {
                int diff = janet_compare_abstract(janet_unwrap_abstract(x), janet_unwrap_abstract(y));
                if (diff) return diff;
                break;
            }
            default: {
                if (janet_unwrap_pointer(x) == janet_unwrap_pointer(y)) {
                    break;
                } else {
                    return janet_unwrap_pointer(x) > janet_unwrap_pointer(y) ? 1 : -1;
                }
            }
            case JANET_TUPLE: {
                const Janet *lhs = janet_unwrap_tuple(x);
                const Janet *rhs = janet_unwrap_tuple(y);
                if (JANET_TUPLE_FLAG_BRACKETCTOR & (janet_tuple_flag(lhs) ^ janet_tuple_flag(rhs))) {
                    return (janet_tuple_flag(lhs) & JANET_TUPLE_FLAG_BRACKETCTOR) ? 1 : -1;
                }
                push_traversal_node(janet_tuple_head(lhs), janet_tuple_head(rhs), 1);
                break;
            }
            case JANET_STRUCT: {
                const JanetKV *lhs = janet_unwrap_struct(x);
                const JanetKV *rhs = janet_unwrap_struct(y);
                int32_t llen = janet_struct_capacity(lhs);
                int32_t rlen = janet_struct_capacity(rhs);
                int32_t lhash = janet_struct_hash(lhs);
                int32_t rhash = janet_struct_hash(rhs);
                if (llen < rlen) return -1;
                if (llen > rlen) return 1;
                if (lhash < rhash) return -1;
                if (lhash > rhash) return 1;
                push_traversal_node(janet_struct_head(lhs), janet_struct_head(rhs), 0);
                break;
            }
        }
    } while (!(status = traversal_next(&x, &y)));
    return status - 2;
}

static int32_t getter_checkint(JanetType type, Janet key, int32_t max) {
    if (!janet_checkint(key)) goto bad;
    int32_t ret = janet_unwrap_integer(key);
    if (ret < 0) goto bad;
    if (ret >= max) goto bad;
    return ret;
bad:
    janet_panicf("expected integer key for %s in range [0, %d), got %v", janet_type_names[type], max, key);
}

/* Gets a value and returns. Can panic. */
Janet janet_in(Janet ds, Janet key) {
    Janet value;
    JanetType type = janet_type(ds);
    switch (type) {
        default:
            janet_panicf("expected %T, got %v", JANET_TFLAG_LENGTHABLE, ds);
            break;
        case JANET_STRUCT:
            value = janet_struct_get(janet_unwrap_struct(ds), key);
            break;
        case JANET_TABLE:
            value = janet_table_get(janet_unwrap_table(ds), key);
            break;
        case JANET_ARRAY: {
            JanetArray *array = janet_unwrap_array(ds);
            int32_t index = getter_checkint(type, key, array->count);
            value = array->data[index];
            break;
        }
        case JANET_TUPLE: {
            const Janet *tuple = janet_unwrap_tuple(ds);
            int32_t len = janet_tuple_length(tuple);
            value = tuple[getter_checkint(type, key, len)];
            break;
        }
        case JANET_BUFFER: {
            JanetBuffer *buffer = janet_unwrap_buffer(ds);
            int32_t index = getter_checkint(type, key, buffer->count);
            value = janet_wrap_integer(buffer->data[index]);
            break;
        }
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            const uint8_t *str = janet_unwrap_string(ds);
            int32_t index = getter_checkint(type, key, janet_string_length(str));
            value = janet_wrap_integer(str[index]);
            break;
        }
        case JANET_ABSTRACT: {
            JanetAbstractType *type = (JanetAbstractType *)janet_abstract_type(janet_unwrap_abstract(ds));
            if (type->get) {
                if (!(type->get)(janet_unwrap_abstract(ds), key, &value))
                    janet_panicf("key %v not found in %v ", key, ds);
            } else {
                janet_panicf("no getter for %v ", ds);
            }
            break;
        }
        case JANET_FIBER: {
            /* Bit of a hack to allow iterating over fibers. */
            if (janet_equals(key, janet_wrap_integer(0))) {
                return janet_unwrap_fiber(ds)->last_value;
            } else {
                janet_panicf("expected key 0, got %v", key);
            }
        }
    }
    return value;
}

Janet janet_get(Janet ds, Janet key) {
    JanetType t = janet_type(ds);
    switch (t) {
        default:
            return janet_wrap_nil();
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD: {
            if (!janet_checkint(key)) return janet_wrap_nil();
            int32_t index = janet_unwrap_integer(key);
            if (index < 0) return janet_wrap_nil();
            const uint8_t *str = janet_unwrap_string(ds);
            if (index >= janet_string_length(str)) return janet_wrap_nil();
            return janet_wrap_integer(str[index]);
        }
        case JANET_ABSTRACT: {
            Janet value;
            void *abst = janet_unwrap_abstract(ds);
            JanetAbstractType *type = (JanetAbstractType *)janet_abstract_type(abst);
            if (!type->get) return janet_wrap_nil();
            if ((type->get)(abst, key, &value))
                return value;
            return janet_wrap_nil();
        }
        case JANET_ARRAY:
        case JANET_TUPLE:
        case JANET_BUFFER: {
            if (!janet_checkint(key)) return janet_wrap_nil();
            int32_t index = janet_unwrap_integer(key);
            if (index < 0) return janet_wrap_nil();
            if (t == JANET_ARRAY) {
                JanetArray *a = janet_unwrap_array(ds);
                if (index >= a->count) return janet_wrap_nil();
                return a->data[index];
            } else if (t == JANET_BUFFER) {
                JanetBuffer *b = janet_unwrap_buffer(ds);
                if (index >= b->count) return janet_wrap_nil();
                return janet_wrap_integer(b->data[index]);
            } else {
                const Janet *t = janet_unwrap_tuple(ds);
                if (index >= janet_tuple_length(t)) return janet_wrap_nil();
                return t[index];
            }
        }
        case JANET_TABLE: {
            return janet_table_get(janet_unwrap_table(ds), key);
        }
        case JANET_STRUCT: {
            const JanetKV *st = janet_unwrap_struct(ds);
            return janet_struct_get(st, key);
        }
        case JANET_FIBER: {
            /* Bit of a hack to allow iterating over fibers. */
            if (janet_equals(key, janet_wrap_integer(0))) {
                return janet_unwrap_fiber(ds)->last_value;
            } else {
                return janet_wrap_nil();
            }
        }
    }
}

Janet janet_getindex(Janet ds, int32_t index) {
    Janet value;
    if (index < 0) janet_panic("expected non-negative index");
    switch (janet_type(ds)) {
        default:
            janet_panicf("expected %T, got %v", JANET_TFLAG_LENGTHABLE, ds);
            break;
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            if (index >= janet_string_length(janet_unwrap_string(ds))) {
                value = janet_wrap_nil();
            } else {
                value = janet_wrap_integer(janet_unwrap_string(ds)[index]);
            }
            break;
        case JANET_ARRAY:
            if (index >= janet_unwrap_array(ds)->count) {
                value = janet_wrap_nil();
            } else {
                value = janet_unwrap_array(ds)->data[index];
            }
            break;
        case JANET_BUFFER:
            if (index >= janet_unwrap_buffer(ds)->count) {
                value = janet_wrap_nil();
            } else {
                value = janet_wrap_integer(janet_unwrap_buffer(ds)->data[index]);
            }
            break;
        case JANET_TUPLE:
            if (index >= janet_tuple_length(janet_unwrap_tuple(ds))) {
                value = janet_wrap_nil();
            } else {
                value = janet_unwrap_tuple(ds)[index];
            }
            break;
        case JANET_TABLE:
            value = janet_table_get(janet_unwrap_table(ds), janet_wrap_integer(index));
            break;
        case JANET_STRUCT:
            value = janet_struct_get(janet_unwrap_struct(ds), janet_wrap_integer(index));
            break;
        case JANET_ABSTRACT: {
            JanetAbstractType *type = (JanetAbstractType *)janet_abstract_type(janet_unwrap_abstract(ds));
            if (type->get) {
                if (!(type->get)(janet_unwrap_abstract(ds), janet_wrap_integer(index), &value))
                    value = janet_wrap_nil();
            } else {
                janet_panicf("no getter for %v ", ds);
            }
            break;
        }
        case JANET_FIBER: {
            if (index == 0) {
                value = janet_unwrap_fiber(ds)->last_value;
            } else {
                value = janet_wrap_nil();
            }
            break;
        }
    }
    return value;
}

int32_t janet_length(Janet x) {
    switch (janet_type(x)) {
        default:
            janet_panicf("expected %T, got %v", JANET_TFLAG_LENGTHABLE, x);
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            return janet_string_length(janet_unwrap_string(x));
        case JANET_ARRAY:
            return janet_unwrap_array(x)->count;
        case JANET_BUFFER:
            return janet_unwrap_buffer(x)->count;
        case JANET_TUPLE:
            return janet_tuple_length(janet_unwrap_tuple(x));
        case JANET_STRUCT:
            return janet_struct_length(janet_unwrap_struct(x));
        case JANET_TABLE:
            return janet_unwrap_table(x)->count;
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            const JanetAbstractType *type = janet_abstract_type(abst);
            if (type->length != NULL) {
                size_t len = type->length(abst, janet_abstract_size(abst));
                if (len > INT32_MAX) {
                    janet_panicf("invalid integer length %u", len);
                }
                return (int32_t)(len);
            }
            Janet argv[1] = { x };
            Janet len = janet_mcall("length", 1, argv);
            if (!janet_checkint(len))
                janet_panicf("invalid integer length %v", len);
            return janet_unwrap_integer(len);
        }
    }
}

Janet janet_lengthv(Janet x) {
    switch (janet_type(x)) {
        default:
            janet_panicf("expected %T, got %v", JANET_TFLAG_LENGTHABLE, x);
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            return janet_wrap_integer(janet_string_length(janet_unwrap_string(x)));
        case JANET_ARRAY:
            return janet_wrap_integer(janet_unwrap_array(x)->count);
        case JANET_BUFFER:
            return janet_wrap_integer(janet_unwrap_buffer(x)->count);
        case JANET_TUPLE:
            return janet_wrap_integer(janet_tuple_length(janet_unwrap_tuple(x)));
        case JANET_STRUCT:
            return janet_wrap_integer(janet_struct_length(janet_unwrap_struct(x)));
        case JANET_TABLE:
            return janet_wrap_integer(janet_unwrap_table(x)->count);
        case JANET_ABSTRACT: {
            void *abst = janet_unwrap_abstract(x);
            const JanetAbstractType *type = janet_abstract_type(abst);
            if (type->length != NULL) {
                size_t len = type->length(abst, janet_abstract_size(abst));
                /* If len is always less then double, we can never overflow */
#ifdef JANET_32
                return janet_wrap_number(len);
#else
                if (len < (size_t) JANET_INTMAX_INT64) {
                    return janet_wrap_number((double) len);
                } else {
                    janet_panicf("integer length %u too large", len);
                }
#endif
            }
            Janet argv[1] = { x };
            return janet_mcall("length", 1, argv);
        }
    }
}

void janet_putindex(Janet ds, int32_t index, Janet value) {
    switch (janet_type(ds)) {
        default:
            janet_panicf("expected %T, got %v",
                         JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER | JANET_TFLAG_TABLE, ds);
        case JANET_ARRAY: {
            JanetArray *array = janet_unwrap_array(ds);
            if (index >= array->count) {
                janet_array_ensure(array, index + 1, 2);
                array->count = index + 1;
            }
            array->data[index] = value;
            break;
        }
        case JANET_BUFFER: {
            JanetBuffer *buffer = janet_unwrap_buffer(ds);
            if (!janet_checkint(value))
                janet_panicf("can only put integers in buffers, got %v", value);
            if (index >= buffer->count) {
                janet_buffer_ensure(buffer, index + 1, 2);
                buffer->count = index + 1;
            }
            buffer->data[index] = (uint8_t)(janet_unwrap_integer(value) & 0xFF);
            break;
        }
        case JANET_TABLE: {
            JanetTable *table = janet_unwrap_table(ds);
            janet_table_put(table, janet_wrap_integer(index), value);
            break;
        }
        case JANET_ABSTRACT: {
            JanetAbstractType *type = (JanetAbstractType *)janet_abstract_type(janet_unwrap_abstract(ds));
            if (type->put) {
                (type->put)(janet_unwrap_abstract(ds), janet_wrap_integer(index), value);
            } else {
                janet_panicf("no setter for %v ", ds);
            }
            break;
        }
    }
}

void janet_put(Janet ds, Janet key, Janet value) {
    JanetType type = janet_type(ds);
    switch (type) {
        default:
            janet_panicf("expected %T, got %v",
                         JANET_TFLAG_ARRAY | JANET_TFLAG_BUFFER | JANET_TFLAG_TABLE, ds);
        case JANET_ARRAY: {
            JanetArray *array = janet_unwrap_array(ds);
            int32_t index = getter_checkint(type, key, INT32_MAX - 1);
            if (index >= array->count) {
                janet_array_setcount(array, index + 1);
            }
            array->data[index] = value;
            break;
        }
        case JANET_BUFFER: {
            JanetBuffer *buffer = janet_unwrap_buffer(ds);
            int32_t index = getter_checkint(type, key, INT32_MAX - 1);
            if (!janet_checkint(value))
                janet_panicf("can only put integers in buffers, got %v", value);
            if (index >= buffer->count) {
                janet_buffer_setcount(buffer, index + 1);
            }
            buffer->data[index] = (uint8_t)(janet_unwrap_integer(value) & 0xFF);
            break;
        }
        case JANET_TABLE:
            janet_table_put(janet_unwrap_table(ds), key, value);
            break;
        case JANET_ABSTRACT: {
            JanetAbstractType *type = (JanetAbstractType *)janet_abstract_type(janet_unwrap_abstract(ds));
            if (type->put) {
                (type->put)(janet_unwrap_abstract(ds), key, value);
            } else {
                janet_panicf("no setter for %v ", ds);
            }
            break;
        }
    }
}

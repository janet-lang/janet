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
#include "symcache.h"
#include "gc.h"
#include "util.h"
#include "fiber.h"
#include "vector.h"
#endif

/* Helpers for marking the various gc types */
static void janet_mark_funcenv(JanetFuncEnv *env);
static void janet_mark_funcdef(JanetFuncDef *def);
static void janet_mark_function(JanetFunction *func);
static void janet_mark_array(JanetArray *array);
static void janet_mark_table(JanetTable *table);
static void janet_mark_struct(const JanetKV *st);
static void janet_mark_tuple(const Janet *tuple);
static void janet_mark_buffer(JanetBuffer *buffer);
static void janet_mark_string(const uint8_t *str);
static void janet_mark_fiber(JanetFiber *fiber);
static void janet_mark_abstract(void *adata);

/* Local state that is only temporary for gc */
static JANET_THREAD_LOCAL uint32_t depth = JANET_RECURSION_GUARD;
static JANET_THREAD_LOCAL size_t orig_rootcount;

/* Hint to the GC that we may need to collect */
void janet_gcpressure(size_t s) {
    janet_vm.next_collection += s;
}

/* Mark a value */
void janet_mark(Janet x) {
    if (depth) {
        depth--;
        switch (janet_type(x)) {
            default:
                break;
            case JANET_STRING:
            case JANET_KEYWORD:
            case JANET_SYMBOL:
                janet_mark_string(janet_unwrap_string(x));
                break;
            case JANET_FUNCTION:
                janet_mark_function(janet_unwrap_function(x));
                break;
            case JANET_ARRAY:
                janet_mark_array(janet_unwrap_array(x));
                break;
            case JANET_TABLE:
                janet_mark_table(janet_unwrap_table(x));
                break;
            case JANET_STRUCT:
                janet_mark_struct(janet_unwrap_struct(x));
                break;
            case JANET_TUPLE:
                janet_mark_tuple(janet_unwrap_tuple(x));
                break;
            case JANET_BUFFER:
                janet_mark_buffer(janet_unwrap_buffer(x));
                break;
            case JANET_FIBER:
                janet_mark_fiber(janet_unwrap_fiber(x));
                break;
            case JANET_ABSTRACT:
                janet_mark_abstract(janet_unwrap_abstract(x));
                break;
        }
        depth++;
    } else {
        janet_gcroot(x);
    }
}

static void janet_mark_string(const uint8_t *str) {
    janet_gc_mark(janet_string_head(str));
}

static void janet_mark_buffer(JanetBuffer *buffer) {
    janet_gc_mark(buffer);
}

static void janet_mark_abstract(void *adata) {
#ifdef JANET_EV
    /* Check if abstract type is a threaded abstract type. If it is, marking means
     * updating the threaded_abstract table. */
    if ((janet_abstract_head(adata)->gc.flags & JANET_MEM_TYPEBITS) == JANET_MEMORY_THREADED_ABSTRACT) {
        janet_table_put(&janet_vm.threaded_abstracts, janet_wrap_abstract(adata), janet_wrap_true());
        return;
    }
#endif
    if (janet_gc_reachable(janet_abstract_head(adata)))
        return;
    janet_gc_mark(janet_abstract_head(adata));
    if (janet_abstract_head(adata)->type->gcmark) {
        janet_abstract_head(adata)->type->gcmark(adata, janet_abstract_size(adata));
    }
}

/* Mark a bunch of items in memory */
static void janet_mark_many(const Janet *values, int32_t n) {
    if (values == NULL)
        return;
    const Janet *end = values + n;
    while (values < end) {
        janet_mark(*values);
        values += 1;
    }
}

/* Mark a bunch of key values items in memory */
static void janet_mark_keys(const JanetKV *kvs, int32_t n) {
    const JanetKV *end = kvs + n;
    while (kvs < end) {
        janet_mark(kvs->key);
        kvs++;
    }
}

/* Mark a bunch of key values items in memory */
static void janet_mark_values(const JanetKV *kvs, int32_t n) {
    const JanetKV *end = kvs + n;
    while (kvs < end) {
        janet_mark(kvs->value);
        kvs++;
    }
}

/* Mark a bunch of key values items in memory */
static void janet_mark_kvs(const JanetKV *kvs, int32_t n) {
    const JanetKV *end = kvs + n;
    while (kvs < end) {
        janet_mark(kvs->key);
        janet_mark(kvs->value);
        kvs++;
    }
}

static void janet_mark_array(JanetArray *array) {
    if (janet_gc_reachable(array))
        return;
    janet_gc_mark(array);
    if (janet_gc_type((JanetGCObject *) array) == JANET_MEMORY_ARRAY) {
        janet_mark_many(array->data, array->count);
    }
}

static void janet_mark_table(JanetTable *table) {
recur: /* Manual tail recursion */
    if (janet_gc_reachable(table))
        return;
    janet_gc_mark(table);
    enum JanetMemoryType memtype = janet_gc_type(table);
    if (memtype == JANET_MEMORY_TABLE_WEAKK) {
        janet_mark_values(table->data, table->capacity);
    } else if (memtype == JANET_MEMORY_TABLE_WEAKV) {
        janet_mark_keys(table->data, table->capacity);
    } else if (memtype == JANET_MEMORY_TABLE) {
        janet_mark_kvs(table->data, table->capacity);
    }
    /* do nothing for JANET_MEMORY_TABLE_WEAKKV */
    if (table->proto) {
        table = table->proto;
        goto recur;
    }
}

static void janet_mark_struct(const JanetKV *st) {
recur:
    if (janet_gc_reachable(janet_struct_head(st)))
        return;
    janet_gc_mark(janet_struct_head(st));
    janet_mark_kvs(st, janet_struct_capacity(st));
    st = janet_struct_proto(st);
    if (st) goto recur;
}

static void janet_mark_tuple(const Janet *tuple) {
    if (janet_gc_reachable(janet_tuple_head(tuple)))
        return;
    janet_gc_mark(janet_tuple_head(tuple));
    janet_mark_many(tuple, janet_tuple_length(tuple));
}

/* Helper to mark function environments */
static void janet_mark_funcenv(JanetFuncEnv *env) {
    if (janet_gc_reachable(env))
        return;
    janet_gc_mark(env);
    /* If closure env references a dead fiber, we can just copy out the stack frame we need so
     * we don't need to keep around the whole dead fiber. */
    janet_env_maybe_detach(env);
    if (env->offset > 0) {
        /* On stack */
        janet_mark_fiber(env->as.fiber);
    } else {
        /* Not on stack */
        janet_mark_many(env->as.values, env->length);
    }
}

/* GC helper to mark a FuncDef */
static void janet_mark_funcdef(JanetFuncDef *def) {
    int32_t i;
    if (janet_gc_reachable(def))
        return;
    janet_gc_mark(def);
    janet_mark_many(def->constants, def->constants_length);
    for (i = 0; i < def->defs_length; ++i) {
        janet_mark_funcdef(def->defs[i]);
    }
    if (def->source)
        janet_mark_string(def->source);
    if (def->name)
        janet_mark_string(def->name);
    if (def->symbolmap) {
        for (int i = 0; i < def->symbolmap_length; i++) {
            janet_mark_string(def->symbolmap[i].symbol);
        }
    }

}

static void janet_mark_function(JanetFunction *func) {
    int32_t i;
    int32_t numenvs;
    if (janet_gc_reachable(func))
        return;
    janet_gc_mark(func);
    if (NULL != func->def) {
        /* this should always be true, except if function is only partially constructed */
        numenvs = func->def->environments_length;
        for (i = 0; i < numenvs; ++i) {
            janet_mark_funcenv(func->envs[i]);
        }
        janet_mark_funcdef(func->def);
    }
}

static void janet_mark_fiber(JanetFiber *fiber) {
    int32_t i, j;
    JanetStackFrame *frame;
recur:
    if (janet_gc_reachable(fiber))
        return;
    janet_gc_mark(fiber);

    janet_mark(fiber->last_value);

    /* Mark values on the argument stack */
    janet_mark_many(fiber->data + fiber->stackstart,
                    fiber->stacktop - fiber->stackstart);

    i = fiber->frame;
    j = fiber->stackstart - JANET_FRAME_SIZE;
    while (i > 0) {
        frame = (JanetStackFrame *)(fiber->data + i - JANET_FRAME_SIZE);
        if (NULL != frame->func)
            janet_mark_function(frame->func);
        if (NULL != frame->env)
            janet_mark_funcenv(frame->env);
        /* Mark all values in the stack frame */
        janet_mark_many(fiber->data + i, j - i);
        j = i - JANET_FRAME_SIZE;
        i = frame->prevframe;
    }

    if (fiber->env)
        janet_mark_table(fiber->env);

#ifdef JANET_EV
    if (fiber->supervisor_channel) {
        janet_mark_abstract(fiber->supervisor_channel);
    }
    if (fiber->ev_stream) {
        janet_mark_abstract(fiber->ev_stream);
    }
    if (fiber->ev_callback) {
        fiber->ev_callback(fiber, JANET_ASYNC_EVENT_MARK);
    }
#endif

    /* Explicit tail recursion */
    if (fiber->child) {
        fiber = fiber->child;
        goto recur;
    }
}

/* Deinitialize a block of memory */
static void janet_deinit_block(JanetGCObject *mem) {
    switch (mem->flags & JANET_MEM_TYPEBITS) {
        default:
        case JANET_MEMORY_FUNCTION:
            break; /* Do nothing for non gc types */
        case JANET_MEMORY_SYMBOL:
            janet_symbol_deinit(((JanetStringHead *) mem)->data);
            break;
        case JANET_MEMORY_ARRAY:
        case JANET_MEMORY_ARRAY_WEAK:
            janet_free(((JanetArray *) mem)->data);
            break;
        case JANET_MEMORY_TABLE:
        case JANET_MEMORY_TABLE_WEAKK:
        case JANET_MEMORY_TABLE_WEAKV:
        case JANET_MEMORY_TABLE_WEAKKV:
            janet_free(((JanetTable *) mem)->data);
            break;
        case JANET_MEMORY_FIBER: {
            JanetFiber *f = (JanetFiber *)mem;
#ifdef JANET_EV
            if (f->ev_state && !(f->flags & JANET_FIBER_EV_FLAG_IN_FLIGHT)) {
                janet_ev_dec_refcount();
                janet_free(f->ev_state);
            }
#endif
            janet_free(f->data);
        }
        break;
        case JANET_MEMORY_BUFFER:
            janet_buffer_deinit((JanetBuffer *) mem);
            break;
        case JANET_MEMORY_ABSTRACT: {
            JanetAbstractHead *head = (JanetAbstractHead *)mem;
            if (head->type->gc) {
                janet_assert(!head->type->gc(head->data, head->size), "finalizer failed");
            }
        }
        break;
        case JANET_MEMORY_FUNCENV: {
            JanetFuncEnv *env = (JanetFuncEnv *)mem;
            if (0 == env->offset)
                janet_free(env->as.values);
        }
        break;
        case JANET_MEMORY_FUNCDEF: {
            JanetFuncDef *def = (JanetFuncDef *)mem;
            /* TODO - get this all with one alloc and one free */
            janet_free(def->defs);
            janet_free(def->environments);
            janet_free(def->constants);
            janet_free(def->bytecode);
            janet_free(def->sourcemap);
            janet_free(def->closure_bitset);
            janet_free(def->symbolmap);
        }
        break;
    }
}

/* Check that a value x has been visited in the mark phase */
static int janet_check_liveref(Janet x) {
    switch (janet_type(x)) {
        default:
            return 1;
        case JANET_ARRAY:
        case JANET_TABLE:
        case JANET_FUNCTION:
        case JANET_BUFFER:
        case JANET_FIBER:
            return janet_gc_reachable(janet_unwrap_pointer(x));
        case JANET_STRING:
        case JANET_SYMBOL:
        case JANET_KEYWORD:
            return janet_gc_reachable(janet_string_head(janet_unwrap_string(x)));
        case JANET_ABSTRACT:
            return janet_gc_reachable(janet_abstract_head(janet_unwrap_abstract(x)));
        case JANET_TUPLE:
            return janet_gc_reachable(janet_tuple_head(janet_unwrap_tuple(x)));
        case JANET_STRUCT:
            return janet_gc_reachable(janet_struct_head(janet_unwrap_struct(x)));
    }
}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void janet_sweep() {
    JanetGCObject *previous = NULL;
    JanetGCObject *current = janet_vm.weak_blocks;
    JanetGCObject *next;

    /* Sweep weak heap to drop weak refs */
    while (NULL != current) {
        next = current->data.next;
        if (current->flags & (JANET_MEM_REACHABLE | JANET_MEM_DISABLED)) {
            /* Check for dead references */
            enum JanetMemoryType type = janet_gc_type(current);
            if (type == JANET_MEMORY_ARRAY_WEAK) {
                JanetArray *array = (JanetArray *) current;
                for (uint32_t i = 0; i < (uint32_t) array->count; i++) {
                    if (!janet_check_liveref(array->data[i])) {
                        array->data[i] = janet_wrap_nil();
                    }
                }
            } else {
                JanetTable *table = (JanetTable *) current;
                int check_values = (type == JANET_MEMORY_TABLE_WEAKV) || (type == JANET_MEMORY_TABLE_WEAKKV);
                int check_keys = (type == JANET_MEMORY_TABLE_WEAKK) || (type == JANET_MEMORY_TABLE_WEAKKV);
                JanetKV *end = table->data + table->capacity;
                JanetKV *kvs = table->data;
                while (kvs < end) {
                    int drop = 0;
                    if (check_keys && !janet_check_liveref(kvs->key)) drop = 1;
                    if (check_values && !janet_check_liveref(kvs->value)) drop = 1;
                    if (drop) {
                        /* Inlined from janet_table_remove without search */
                        table->count--;
                        table->deleted++;
                        kvs->key = janet_wrap_nil();
                        kvs->value = janet_wrap_false();
                    }
                    kvs++;
                }
            }
        }
        current = next;
    }

    /* Sweep weak heap to free blocks */
    previous = NULL;
    current = janet_vm.weak_blocks;
    while (NULL != current) {
        next = current->data.next;
        if (current->flags & (JANET_MEM_REACHABLE | JANET_MEM_DISABLED)) {
            previous = current;
            current->flags &= ~JANET_MEM_REACHABLE;
        } else {
            janet_vm.block_count--;
            janet_deinit_block(current);
            if (NULL != previous) {
                previous->data.next = next;
            } else {
                janet_vm.weak_blocks = next;
            }
            janet_free(current);
        }
        current = next;
    }

    /* Sweep main heap to free blocks */
    previous = NULL;
    current = janet_vm.blocks;
    while (NULL != current) {
        next = current->data.next;
        if (current->flags & (JANET_MEM_REACHABLE | JANET_MEM_DISABLED)) {
            previous = current;
            current->flags &= ~JANET_MEM_REACHABLE;
        } else {
            janet_vm.block_count--;
            janet_deinit_block(current);
            if (NULL != previous) {
                previous->data.next = next;
            } else {
                janet_vm.blocks = next;
            }
            janet_free(current);
        }
        current = next;
    }

#ifdef JANET_EV
    /* Sweep threaded abstract types for references to decrement */
    JanetKV *items = janet_vm.threaded_abstracts.data;
    for (int32_t i = 0; i < janet_vm.threaded_abstracts.capacity; i++) {
        if (janet_checktype(items[i].key, JANET_ABSTRACT)) {

            /* If item was not visited during the mark phase, then this
             * abstract type isn't present in the heap and needs its refcount
             * decremented, and shouuld be removed from table. If the refcount is
             * then 0, the item will be collected. This ensures that only one interpreter
             * will clean up the threaded abstract. */

            /* If not visited... */
            if (!janet_truthy(items[i].value)) {
                void *abst = janet_unwrap_abstract(items[i].key);
                if (0 == janet_abstract_decref(abst)) {
                    /* Run finalizer */
                    JanetAbstractHead *head = janet_abstract_head(abst);
                    if (head->type->gc) {
                        janet_assert(!head->type->gc(head->data, head->size), "finalizer failed");
                    }
                    /* Free memory */
                    janet_free(janet_abstract_head(abst));
                }

                /* Mark as tombstone in place */
                items[i].key = janet_wrap_nil();
                items[i].value = janet_wrap_false();
                janet_vm.threaded_abstracts.deleted++;
                janet_vm.threaded_abstracts.count--;
            }

            /* Reset for next sweep */
            items[i].value = janet_wrap_false();
        }
    }
#endif
}

/* Allocate some memory that is tracked for garbage collection */
void *janet_gcalloc(enum JanetMemoryType type, size_t size) {
    JanetGCObject *mem;

    /* Make sure everything is inited */
    janet_assert(NULL != janet_vm.cache, "please initialize janet before use");
    mem = janet_malloc(size);

    /* Check for bad malloc */
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }

    /* Configure block */
    mem->flags = type;

    /* Prepend block to heap list */
    janet_vm.next_collection += size;
    if (type < JANET_MEMORY_TABLE_WEAKK) {
        /* normal heap */
        mem->data.next = janet_vm.blocks;
        janet_vm.blocks = mem;
    } else {
        /* weak heap */
        mem->data.next = janet_vm.weak_blocks;
        janet_vm.weak_blocks = mem;
    }
    janet_vm.block_count++;

    return (void *)mem;
}

static void free_one_scratch(JanetScratch *s) {
    if (NULL != s->finalize) {
        s->finalize((char *) s->mem);
    }
    janet_free(s);
}

/* Free all allocated scratch memory */
static void janet_free_all_scratch(void) {
    for (size_t i = 0; i < janet_vm.scratch_len; i++) {
        free_one_scratch(janet_vm.scratch_mem[i]);
    }
    janet_vm.scratch_len = 0;
}

static JanetScratch *janet_mem2scratch(void *mem) {
    JanetScratch *s = (JanetScratch *)mem;
    return s - 1;
}

/* Run garbage collection */
void janet_collect(void) {
    uint32_t i;
    if (janet_vm.gc_suspend) return;
    depth = JANET_RECURSION_GUARD;
    janet_vm.gc_mark_phase = 1;
    /* Try to prevent many major collections back to back.
     * A full collection will take O(janet_vm.block_count) time.
     * If we have a large heap, make sure our interval is not too
     * small so we won't make many collections over it. This is just a
     * heuristic for automatically changing the gc interval */
    if (janet_vm.block_count * 8 > janet_vm.gc_interval) {
        janet_vm.gc_interval = janet_vm.block_count * sizeof(JanetGCObject);
    }
    orig_rootcount = janet_vm.root_count;
#ifdef JANET_EV
    janet_ev_mark();
#endif
    janet_mark_fiber(janet_vm.root_fiber);
    for (i = 0; i < orig_rootcount; i++)
        janet_mark(janet_vm.roots[i]);
    while (orig_rootcount < janet_vm.root_count) {
        Janet x = janet_vm.roots[--janet_vm.root_count];
        janet_mark(x);
    }
    janet_vm.gc_mark_phase = 0;
    janet_sweep();
    janet_vm.next_collection = 0;
    janet_free_all_scratch();
}

/* Add a root value to the GC. This prevents the GC from removing a value
 * and all of its children. If gcroot is called on a value n times, unroot
 * must also be called n times to remove it as a gc root. */
void janet_gcroot(Janet root) {
    size_t newcount = janet_vm.root_count + 1;
    if (newcount > janet_vm.root_capacity) {
        size_t newcap = 2 * newcount;
        janet_vm.roots = janet_realloc(janet_vm.roots, sizeof(Janet) * newcap);
        if (NULL == janet_vm.roots) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.root_capacity = newcap;
    }
    janet_vm.roots[janet_vm.root_count] = root;
    janet_vm.root_count = newcount;
}

/* Identity equality for GC purposes */
static int janet_gc_idequals(Janet lhs, Janet rhs) {
    if (janet_type(lhs) != janet_type(rhs))
        return 0;
    switch (janet_type(lhs)) {
        case JANET_BOOLEAN:
        case JANET_NIL:
        case JANET_NUMBER:
            /* These values don't really matter to the gc so returning 1 all the time is fine. */
            return 1;
        default:
            return janet_unwrap_pointer(lhs) == janet_unwrap_pointer(rhs);
    }
}

/* Remove a root value from the GC. This allows the gc to potentially reclaim
 * a value and all its children. */
int janet_gcunroot(Janet root) {
    Janet *vtop = janet_vm.roots + janet_vm.root_count;
    /* Search from top to bottom as access is most likely LIFO */
    for (Janet *v = janet_vm.roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm.roots[--janet_vm.root_count];
            return 1;
        }
    }
    return 0;
}

/* Remove a root value from the GC. This sets the effective reference count to 0. */
int janet_gcunrootall(Janet root) {
    Janet *vtop = janet_vm.roots + janet_vm.root_count;
    int ret = 0;
    /* Search from top to bottom as access is most likely LIFO */
    for (Janet *v = janet_vm.roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm.roots[--janet_vm.root_count];
            vtop--;
            ret = 1;
        }
    }
    return ret;
}

/* Free all allocated memory */
void janet_clear_memory(void) {
#ifdef JANET_EV
    JanetKV *items = janet_vm.threaded_abstracts.data;
    for (int32_t i = 0; i < janet_vm.threaded_abstracts.capacity; i++) {
        if (janet_checktype(items[i].key, JANET_ABSTRACT)) {
            void *abst = janet_unwrap_abstract(items[i].key);
            if (0 == janet_abstract_decref(abst)) {
                JanetAbstractHead *head = janet_abstract_head(abst);
                if (head->type->gc) {
                    janet_assert(!head->type->gc(head->data, head->size), "finalizer failed");
                }
                janet_free(janet_abstract_head(abst));
            }
        }
    }
#endif
    JanetGCObject *current = janet_vm.blocks;
    while (NULL != current) {
        janet_deinit_block(current);
        JanetGCObject *next = current->data.next;
        janet_free(current);
        current = next;
    }
    janet_vm.blocks = NULL;
    janet_free_all_scratch();
    janet_free(janet_vm.scratch_mem);
}

/* Primitives for suspending GC. */
int janet_gclock(void) {
    return janet_vm.gc_suspend++;
}
void janet_gcunlock(int handle) {
    janet_vm.gc_suspend = handle;
}

/* Scratch memory API
 * Scratch memory allocations do not need to be free (but optionally can be), and will be automatically cleaned
 * up in the next call to janet_collect. */

void *janet_smalloc(size_t size) {
    JanetScratch *s = janet_malloc(sizeof(JanetScratch) + size);
    if (NULL == s) {
        JANET_OUT_OF_MEMORY;
    }
    s->finalize = NULL;
    if (janet_vm.scratch_len == janet_vm.scratch_cap) {
        size_t newcap = 2 * janet_vm.scratch_cap + 2;
        JanetScratch **newmem = (JanetScratch **) janet_realloc(janet_vm.scratch_mem, newcap * sizeof(JanetScratch));
        if (NULL == newmem) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.scratch_cap = newcap;
        janet_vm.scratch_mem = newmem;
    }
    janet_vm.scratch_mem[janet_vm.scratch_len++] = s;
    return (char *)(s->mem);
}

void *janet_scalloc(size_t nmemb, size_t size) {
    if (nmemb && size > SIZE_MAX / nmemb) {
        JANET_OUT_OF_MEMORY;
    }
    size_t n = nmemb * size;
    void *p = janet_smalloc(n);
    memset(p, 0, n);
    return p;
}

void *janet_srealloc(void *mem, size_t size) {
    if (NULL == mem) return janet_smalloc(size);
    JanetScratch *s = janet_mem2scratch(mem);
    if (janet_vm.scratch_len) {
        for (size_t i = janet_vm.scratch_len - 1; ; i--) {
            if (janet_vm.scratch_mem[i] == s) {
                JanetScratch *news = janet_realloc(s, size + sizeof(JanetScratch));
                if (NULL == news) {
                    JANET_OUT_OF_MEMORY;
                }
                janet_vm.scratch_mem[i] = news;
                return (char *)(news->mem);
            }
            if (i == 0) break;
        }
    }
    JANET_EXIT("invalid janet_srealloc");
}

void janet_sfinalizer(void *mem, JanetScratchFinalizer finalizer) {
    JanetScratch *s = janet_mem2scratch(mem);
    s->finalize = finalizer;
}

void janet_sfree(void *mem) {
    if (NULL == mem) return;
    JanetScratch *s = janet_mem2scratch(mem);
    if (janet_vm.scratch_len) {
        for (size_t i = janet_vm.scratch_len - 1; ; i--) {
            if (janet_vm.scratch_mem[i] == s) {
                janet_vm.scratch_mem[i] = janet_vm.scratch_mem[--janet_vm.scratch_len];
                free_one_scratch(s);
                return;
            }
            if (i == 0) break;
        }
    }
    JANET_EXIT("invalid janet_sfree");
}

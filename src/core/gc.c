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
#include "symcache.h"
#include "gc.h"
#include "util.h"
#endif

/* GC State */
JANET_THREAD_LOCAL void *janet_vm_blocks;
JANET_THREAD_LOCAL uint32_t janet_vm_gc_interval;
JANET_THREAD_LOCAL uint32_t janet_vm_next_collection;
JANET_THREAD_LOCAL int janet_vm_gc_suspend = 0;

/* Roots */
JANET_THREAD_LOCAL Janet *janet_vm_roots;
JANET_THREAD_LOCAL uint32_t janet_vm_root_count;
JANET_THREAD_LOCAL uint32_t janet_vm_root_capacity;

/* Scratch Memory */
#ifdef JANET_64
#define SCRATCH_HDR_SIZE 16 /* smalloc must guarantee 16 byte alignment. */
#elif JANET_32
#define SCRATCH_HDR_SIZE 8  /* smalloc must guarantee 8 byte alignment. */
#else
#error "unknown scratch alignment"
#endif
JANET_THREAD_LOCAL void **janet_scratch_mem;
JANET_THREAD_LOCAL size_t janet_scratch_cap;
JANET_THREAD_LOCAL size_t janet_scratch_len;

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

/* Local state that is only temporary */
static JANET_THREAD_LOCAL uint32_t depth = JANET_RECURSION_GUARD;
static JANET_THREAD_LOCAL uint32_t orig_rootcount;

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
    if (janet_gc_reachable(janet_abstract_head(adata)))
        return;
    janet_gc_mark(janet_abstract_head(adata));
    if (janet_abstract_head(adata)->type->gcmark) {
        janet_abstract_head(adata)->type->gcmark(adata, janet_abstract_size(adata));
    }
}

/* Mark a bunch of items in memory */
static void janet_mark_many(const Janet *values, int32_t n) {
    const Janet *end = values + n;
    while (values < end) {
        janet_mark(*values);
        values += 1;
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
    janet_mark_many(array->data, array->count);
}

static void janet_mark_table(JanetTable *table) {
recur: /* Manual tail recursion */
    if (janet_gc_reachable(table))
        return;
    janet_gc_mark(table);
    janet_mark_kvs(table->data, table->capacity);
    if (table->proto) {
        table = table->proto;
        goto recur;
    }
}

static void janet_mark_struct(const JanetKV *st) {
    if (janet_gc_reachable(janet_struct_head(st)))
        return;
    janet_gc_mark(janet_struct_head(st));
    janet_mark_kvs(st, janet_struct_capacity(st));
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
    if (env->offset) {
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
}

static void janet_mark_function(JanetFunction *func) {
    int32_t i;
    int32_t numenvs;
    if (janet_gc_reachable(func))
        return;
    janet_gc_mark(func);
    numenvs = func->def->environments_length;
    for (i = 0; i < numenvs; ++i) {
        janet_mark_funcenv(func->envs[i]);
    }
    janet_mark_funcdef(func->def);
}

static void janet_mark_fiber(JanetFiber *fiber) {
    int32_t i, j;
    JanetStackFrame *frame;
recur:
    if (janet_gc_reachable(fiber))
        return;
    janet_gc_mark(fiber);

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
            free(((JanetArray *) mem)->data);
            break;
        case JANET_MEMORY_TABLE:
            free(((JanetTable *) mem)->data);
            break;
        case JANET_MEMORY_FIBER:
            free(((JanetFiber *)mem)->data);
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
                free(env->as.values);
        }
        break;
        case JANET_MEMORY_FUNCDEF: {
            JanetFuncDef *def = (JanetFuncDef *)mem;
            /* TODO - get this all with one alloc and one free */
            free(def->defs);
            free(def->environments);
            free(def->constants);
            free(def->bytecode);
            free(def->sourcemap);
        }
        break;
    }
}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void janet_sweep() {
    JanetGCObject *previous = NULL;
    JanetGCObject *current = janet_vm_blocks;
    JanetGCObject *next;
    while (NULL != current) {
        next = current->next;
        if (current->flags & (JANET_MEM_REACHABLE | JANET_MEM_DISABLED)) {
            previous = current;
            current->flags &= ~JANET_MEM_REACHABLE;
        } else {
            janet_deinit_block(current);
            if (NULL != previous) {
                previous->next = next;
            } else {
                janet_vm_blocks = next;
            }
            free(current);
        }
        current = next;
    }
}

/* Allocate some memory that is tracked for garbage collection */
void *janet_gcalloc(enum JanetMemoryType type, size_t size) {
    JanetGCObject *mem;

    /* Make sure everything is inited */
    janet_assert(NULL != janet_vm_cache, "please initialize janet before use");
    mem = malloc(size);

    /* Check for bad malloc */
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }

    /* Configure block */
    mem->flags = type;

    /* Prepend block to heap list */
    janet_vm_next_collection += (int32_t) size;
    mem->next = janet_vm_blocks;
    janet_vm_blocks = mem;

    return (void *)mem;
}

static void free_one_scratch(void *mem) {
    ScratchFinalizer finalize = *(ScratchFinalizer *)mem;
    if (finalize)
        finalize((char *)mem + SCRATCH_HDR_SIZE);
    free(mem);
}

/* Free all allocated scratch memory */
static void janet_free_all_scratch(void) {
    for (size_t i = 0; i < janet_scratch_len; i++) {
        free_one_scratch(janet_scratch_mem[i]);
    }
    janet_scratch_len = 0;
}

/* Run garbage collection */
void janet_collect(void) {
    uint32_t i;
    if (janet_vm_gc_suspend) return;
    depth = JANET_RECURSION_GUARD;
    orig_rootcount = janet_vm_root_count;
    for (i = 0; i < orig_rootcount; i++)
        janet_mark(janet_vm_roots[i]);
    while (orig_rootcount < janet_vm_root_count) {
        Janet x = janet_vm_roots[--janet_vm_root_count];
        janet_mark(x);
    }
    janet_sweep();
    janet_vm_next_collection = 0;
    janet_free_all_scratch();
}

/* Add a root value to the GC. This prevents the GC from removing a value
 * and all of its children. If gcroot is called on a value n times, unroot
 * must also be called n times to remove it as a gc root. */
void janet_gcroot(Janet root) {
    uint32_t newcount = janet_vm_root_count + 1;
    if (newcount > janet_vm_root_capacity) {
        uint32_t newcap = 2 * newcount;
        janet_vm_roots = realloc(janet_vm_roots, sizeof(Janet) * newcap);
        if (NULL == janet_vm_roots) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_root_capacity = newcap;
    }
    janet_vm_roots[janet_vm_root_count] = root;
    janet_vm_root_count = newcount;
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
    Janet *vtop = janet_vm_roots + janet_vm_root_count;
    /* Search from top to bottom as access is most likely LIFO */
    for (Janet *v = janet_vm_roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm_roots[--janet_vm_root_count];
            return 1;
        }
    }
    return 0;
}

/* Remove a root value from the GC. This sets the effective reference count to 0. */
int janet_gcunrootall(Janet root) {
    Janet *vtop = janet_vm_roots + janet_vm_root_count;
    int ret = 0;
    /* Search from top to bottom as access is most likely LIFO */
    for (Janet *v = janet_vm_roots; v < vtop; v++) {
        if (janet_gc_idequals(root, *v)) {
            *v = janet_vm_roots[--janet_vm_root_count];
            vtop--;
            ret = 1;
        }
    }
    return ret;
}

/* Free all allocated memory */
void janet_clear_memory(void) {
    JanetGCObject *current = janet_vm_blocks;
    while (NULL != current) {
        janet_deinit_block(current);
        JanetGCObject *next = current->next;
        free(current);
        current = next;
    }
    janet_vm_blocks = NULL;
    janet_free_all_scratch();
    free(janet_scratch_mem);
}

/* Primitives for suspending GC. */
int janet_gclock(void) {
    return janet_vm_gc_suspend++;
}
void janet_gcunlock(int handle) {
    janet_vm_gc_suspend = handle;
}

/* Scratch memory API */

void *janet_smalloc(size_t size) {
    void *mem = malloc(SCRATCH_HDR_SIZE + size);
    if (NULL == mem) {
        JANET_OUT_OF_MEMORY;
    }
    *(ScratchFinalizer *)mem = NULL;
    if (janet_scratch_len == janet_scratch_cap) {
        size_t newcap = 2 * janet_scratch_cap + 2;
        void **newmem = (void **) realloc(janet_scratch_mem, newcap * sizeof(void *));
        if (NULL == newmem) {
            JANET_OUT_OF_MEMORY;
        }
        janet_scratch_cap = newcap;
        janet_scratch_mem = newmem;
    }
    janet_scratch_mem[janet_scratch_len++] = mem;
    return (char *)mem + SCRATCH_HDR_SIZE;
}

void *janet_srealloc(void *mem, size_t size) {
    if (NULL == mem) return janet_smalloc(size);
    mem = (char *)mem - SCRATCH_HDR_SIZE;
    if (janet_scratch_len) {
        for (size_t i = janet_scratch_len - 1; ; i--) {
            if (janet_scratch_mem[i] == mem) {
                void *newmem = realloc(mem, size + SCRATCH_HDR_SIZE);
                if (NULL == newmem) {
                    JANET_OUT_OF_MEMORY;
                }
                janet_scratch_mem[i] = newmem;
                return (char *)newmem + SCRATCH_HDR_SIZE;
            }
            if (i == 0) break;
        }
    }
    janet_exit("invalid janet_srealloc");
}

void janet_sfinalizer(void *mem, ScratchFinalizer finalizer) {
    mem = (char *)mem - SCRATCH_HDR_SIZE;
    *(ScratchFinalizer *)mem = finalizer;
}

void janet_sfree(void *mem) {
    if (NULL == mem) return;
    mem = (char *)mem - SCRATCH_HDR_SIZE;
    if (janet_scratch_len) {
        for (size_t i = janet_scratch_len - 1; ; i--) {
            if (janet_scratch_mem[i] == mem) {
                janet_scratch_mem[i] = janet_scratch_mem[--janet_scratch_len];
                free_one_scratch(mem);
                return;
            }
            if (i == 0) break;
        }
    }
    janet_exit("invalid janet_sfree");
}

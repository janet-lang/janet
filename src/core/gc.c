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

#include <dst/dst.h>
#include "state.h"
#include "symcache.h"
#include "gc.h"

/* GC State */
DST_THREAD_LOCAL void *dst_vm_blocks;
DST_THREAD_LOCAL uint32_t dst_vm_gc_interval;
DST_THREAD_LOCAL uint32_t dst_vm_next_collection;
DST_THREAD_LOCAL int dst_vm_gc_suspend = 0;

/* Roots */
DST_THREAD_LOCAL Dst *dst_vm_roots;
DST_THREAD_LOCAL uint32_t dst_vm_root_count;
DST_THREAD_LOCAL uint32_t dst_vm_root_capacity;

/* Helpers for marking the various gc types */
static void dst_mark_funcenv(DstFuncEnv *env);
static void dst_mark_funcdef(DstFuncDef *def);
static void dst_mark_function(DstFunction *func);
static void dst_mark_array(DstArray *array);
static void dst_mark_table(DstTable *table);
static void dst_mark_struct(const DstKV *st);
static void dst_mark_tuple(const Dst *tuple);
static void dst_mark_buffer(DstBuffer *buffer);
static void dst_mark_string(const uint8_t *str);
static void dst_mark_fiber(DstFiber *fiber);
static void dst_mark_abstract(void *adata);

/* Mark a value */
void dst_mark(Dst x) {
    switch (dst_type(x)) {
        default: break;
        case DST_STRING:
        case DST_SYMBOL: dst_mark_string(dst_unwrap_string(x)); break;
        case DST_FUNCTION: dst_mark_function(dst_unwrap_function(x)); break;
        case DST_ARRAY: dst_mark_array(dst_unwrap_array(x)); break;
        case DST_TABLE: dst_mark_table(dst_unwrap_table(x)); break;
        case DST_STRUCT: dst_mark_struct(dst_unwrap_struct(x)); break;
        case DST_TUPLE: dst_mark_tuple(dst_unwrap_tuple(x)); break;
        case DST_BUFFER: dst_mark_buffer(dst_unwrap_buffer(x)); break;
        case DST_FIBER: dst_mark_fiber(dst_unwrap_fiber(x)); break;
        case DST_ABSTRACT: dst_mark_abstract(dst_unwrap_abstract(x)); break;
    }
}

static void dst_mark_string(const uint8_t *str) {
    dst_gc_mark(dst_string_raw(str));
}

static void dst_mark_buffer(DstBuffer *buffer) {
    dst_gc_mark(buffer);
}

static void dst_mark_abstract(void *adata) {
    if (dst_gc_reachable(dst_abstract_header(adata)))
        return;
    dst_gc_mark(dst_abstract_header(adata));
    if (dst_abstract_header(adata)->type->gcmark) {
        dst_abstract_header(adata)->type->gcmark(adata, dst_abstract_size(adata));
    }
}

/* Mark a bunch of items in memory */
static void dst_mark_many(const Dst *values, int32_t n) {
    const Dst *end = values + n;
    while (values < end) {
        dst_mark(*values);
        values += 1;
    }
}

/* Mark a bunch of key values items in memory */
static void dst_mark_kvs(const DstKV *kvs, int32_t n) {
    const DstKV *end = kvs + n;
    while (kvs < end) {
        dst_mark(kvs->key);
        dst_mark(kvs->value);
        kvs++;
    }
}

static void dst_mark_array(DstArray *array) {
    if (dst_gc_reachable(array))
        return;
    dst_gc_mark(array);
    dst_mark_many(array->data, array->count);
}

static void dst_mark_table(DstTable *table) {
    recur: /* Manual tail recursion */
    if (dst_gc_reachable(table))
        return;
    dst_gc_mark(table);
    dst_mark_kvs(table->data, table->capacity);
    if (table->proto) {
        table = table->proto;
        goto recur;
    }
}

static void dst_mark_struct(const DstKV *st) {
    if (dst_gc_reachable(dst_struct_raw(st)))
        return;
    dst_gc_mark(dst_struct_raw(st));
    dst_mark_kvs(st, dst_struct_capacity(st));
}

static void dst_mark_tuple(const Dst *tuple) {
    if (dst_gc_reachable(dst_tuple_raw(tuple)))
        return;
    dst_gc_mark(dst_tuple_raw(tuple));
    dst_mark_many(tuple, dst_tuple_length(tuple));
}

/* Helper to mark function environments */
static void dst_mark_funcenv(DstFuncEnv *env) {
    if (dst_gc_reachable(env))
        return;
    dst_gc_mark(env);
    if (env->offset) {
        /* On stack */
        dst_mark_fiber(env->as.fiber);
    } else {
        /* Not on stack */
        dst_mark_many(env->as.values, env->length);
    }
}

/* GC helper to mark a FuncDef */
static void dst_mark_funcdef(DstFuncDef *def) {
    int32_t i;
    if (dst_gc_reachable(def))
        return;
    dst_gc_mark(def);
    dst_mark_many(def->constants, def->constants_length);
    for (i = 0; i < def->defs_length; ++i) {
        dst_mark_funcdef(def->defs[i]);
    }
    if (def->source)
        dst_mark_string(def->source);
    if (def->sourcepath)
        dst_mark_string(def->sourcepath);
    if (def->name)
        dst_mark_string(def->name);
}

static void dst_mark_function(DstFunction *func) {
    int32_t i;
    int32_t numenvs;
    if (dst_gc_reachable(func))
        return;
    dst_gc_mark(func);
    numenvs = func->def->environments_length;
    for (i = 0; i < numenvs; ++i) {
        dst_mark_funcenv(func->envs[i]);
    }
    dst_mark_funcdef(func->def);
}

static void dst_mark_fiber(DstFiber *fiber) {
    int32_t i, j;
    DstStackFrame *frame;
recur:
    if (dst_gc_reachable(fiber))
        return;
    dst_gc_mark(fiber);

    if (fiber->flags & DST_FIBER_FLAG_NEW) 
        dst_mark_function(fiber->root);
    
    i = fiber->frame;
    j = fiber->stackstart - DST_FRAME_SIZE;
    while (i > 0) {
        frame = (DstStackFrame *)(fiber->data + i - DST_FRAME_SIZE);
        if (NULL != frame->func)
            dst_mark_function(frame->func);
        if (NULL != frame->env)
            dst_mark_funcenv(frame->env);
        /* Mark all values in the stack frame */
        dst_mark_many(fiber->data + i, j - i);
        j = i - DST_FRAME_SIZE;
        i = frame->prevframe;
    }

    if (fiber->child) {
        fiber = fiber->child;
        goto recur;
    }
}

/* Deinitialize a block of memory */
static void dst_deinit_block(DstGCMemoryHeader *block) {
    void *mem = ((char *)(block + 1));
    DstAbstractHeader *h = (DstAbstractHeader *)mem;
    switch (block->flags & DST_MEM_TYPEBITS) {
        default:
        case DST_MEMORY_FUNCTION:
            break; /* Do nothing for non gc types */ 
        case DST_MEMORY_SYMBOL:
            dst_symbol_deinit((const uint8_t *)mem + 2 * sizeof(int32_t));
            break;
        case DST_MEMORY_ARRAY:
            dst_array_deinit((DstArray*) mem);
            break;
        case DST_MEMORY_TABLE:
            dst_table_deinit((DstTable*) mem);
            break;
        case DST_MEMORY_FIBER:
            free(((DstFiber *)mem)->data);
            break;
        case DST_MEMORY_BUFFER:
            dst_buffer_deinit((DstBuffer *) mem);
            break; 
        case DST_MEMORY_ABSTRACT:
            if (h->type->gc) {
                if (h->type->gc((void *)(h + 1), h->size)) {
                    /* finalizer failed. try again later? Panic? For now do nothing. */
                    ;
                }
            }
            break;
        case DST_MEMORY_FUNCENV:
            {
                DstFuncEnv *env = (DstFuncEnv *)mem;
                if (0 == env->offset)
                    free(env->as.values);
            }
            break;
        case DST_MEMORY_FUNCDEF:
            {
                DstFuncDef *def = (DstFuncDef *)mem;
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
void dst_sweep() {
    DstGCMemoryHeader *previous = NULL;
    DstGCMemoryHeader *current = dst_vm_blocks;
    DstGCMemoryHeader *next;
    while (NULL != current) {
        next = current->next;
        if (current->flags & (DST_MEM_REACHABLE | DST_MEM_DISABLED)) {
            previous = current;
            current->flags &= ~DST_MEM_REACHABLE;
        } else {
            dst_deinit_block(current);
            if (NULL != previous) {
                previous->next = next;
            } else {
                dst_vm_blocks = next;
            }
            free(current);
        }
        current = next;
    }
}

/* Allocate some memory that is tracked for garbage collection */
void *dst_gcalloc(enum DstMemoryType type, size_t size) {
    DstGCMemoryHeader *mdata;
    size_t total = size + sizeof(DstGCMemoryHeader);

    /* Make sure everything is inited */
    dst_assert(NULL != dst_vm_cache, "please initialize dst before use");
    void *mem = malloc(total);

    /* Check for bad malloc */
    if (NULL == mem) {
        DST_OUT_OF_MEMORY;
    }

    mdata = (DstGCMemoryHeader *)mem;

    /* Configure block */
    mdata->flags = type;

    /* Prepend block to heap list */
    dst_vm_next_collection += size;
    mdata->next = dst_vm_blocks;
    dst_vm_blocks = mdata;

    return (char *) mem + sizeof(DstGCMemoryHeader);
}

/* Run garbage collection */
void dst_collect(void) {
    uint32_t i;
    if (dst_vm_gc_suspend) return;
    for (i = 0; i < dst_vm_root_count; i++)
        dst_mark(dst_vm_roots[i]);
    dst_sweep();
    dst_vm_next_collection = 0;
}

/* Add a root value to the GC. This prevents the GC from removing a value
 * and all of its children. If gcroot is called on a value n times, unroot
 * must also be called n times to remove it as a gc root. */
void dst_gcroot(Dst root) {
    uint32_t newcount = dst_vm_root_count + 1;
    if (newcount > dst_vm_root_capacity) {
        uint32_t newcap = 2 * newcount;
        dst_vm_roots = realloc(dst_vm_roots, sizeof(Dst) * newcap);
        if (NULL == dst_vm_roots) {
            DST_OUT_OF_MEMORY;
        }
        dst_vm_root_capacity = newcap;
    }
    dst_vm_roots[dst_vm_root_count] = root;
    dst_vm_root_count = newcount;
}

/* Remove a root value from the GC. This allows the gc to potentially reclaim
 * a value and all its children. */
int dst_gcunroot(Dst root) {
    Dst *vtop = dst_vm_roots + dst_vm_root_count;
    Dst *v = dst_vm_roots;
    /* Search from top to bottom as access is most likely LIFO */
    for (v = dst_vm_roots; v < vtop; v++) {
        if (dst_equals(root, *v)) {
            *v = dst_vm_roots[--dst_vm_root_count];
            return 1;
        }
    }
    return 0;
}

/* Remove a root value from the GC. This sets the effective reference count to 0. */
int dst_gcunrootall(Dst root) {
    Dst *vtop = dst_vm_roots + dst_vm_root_count;
    Dst *v = dst_vm_roots;
    int ret = 0;
    /* Search from top to bottom as access is most likely LIFO */
    for (v = dst_vm_roots; v < vtop; v++) {
        if (dst_equals(root, *v)) {
            *v = dst_vm_roots[--dst_vm_root_count];
            vtop--;
            ret = 1;
        }
    }
    return ret;
}

/* Free all allocated memory */
void dst_clear_memory(void) {
    DstGCMemoryHeader *current = dst_vm_blocks;
    while (NULL != current) {
        dst_deinit_block(current);
        DstGCMemoryHeader *next = current->next;
        free(current);
        current = next;
    }
    dst_vm_blocks = NULL;
}

/* Primitives for suspending GC. */
int dst_gclock() { return dst_vm_gc_suspend++; }
void dst_gcunlock(int handle) { dst_vm_gc_suspend = handle; }

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
#include "symcache.h"
#include "gc.h"

/* GC State */
void *dst_vm_blocks;
uint32_t dst_vm_memory_interval;
uint32_t dst_vm_next_collection;

/* Roots */
DstValue *dst_vm_roots;
uint32_t dst_vm_root_count;
uint32_t dst_vm_root_capacity;

/* Helpers for marking the various gc types */
static void dst_mark_funcenv(DstFuncEnv *env);
static void dst_mark_funcdef(DstFuncDef *def);
static void dst_mark_function(DstFunction *func);
static void dst_mark_array(DstArray *array);
static void dst_mark_table(DstTable *table);
static void dst_mark_struct(const DstValue *st);
static void dst_mark_tuple(const DstValue *tuple);
static void dst_mark_buffer(DstBuffer *buffer);
static void dst_mark_string(const uint8_t *str);
static void dst_mark_fiber(DstFiber *fiber);
static void dst_mark_udata(void *udata);

/* Mark a value */
void dst_mark(DstValue x) {
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
        case DST_USERDATA: dst_mark_udata(dst_unwrap_pointer(x)); break;
    }
}

static void dst_mark_string(const uint8_t *str) {
    dst_gc_mark(dst_string_raw(str));
}

static void dst_mark_buffer(DstBuffer *buffer) {
    dst_gc_mark(buffer);
}

static void dst_mark_udata(void *udata) {
    dst_gc_mark(dst_userdata_header(udata));
}

/* Mark a bunch of items in memory */
static void dst_mark_many(const DstValue *values, int32_t n) {
    const DstValue *end = values + n;
    while (values < end) {
        dst_mark(*values);
        values += 1;
    }
}

static void dst_mark_array(DstArray *array) {
    if (dst_gc_reachable(array))
        return;
    dst_gc_mark(array);
    dst_mark_many(array->data, array->count);
}

static void dst_mark_table(DstTable *table) {
    if (dst_gc_reachable(table))
        return;
    dst_gc_mark(table);
    dst_mark_many(table->data, table->capacity);
}

static void dst_mark_struct(const DstValue *st) {
    if (dst_gc_reachable(dst_struct_raw(st)))
        return;
    dst_gc_mark(dst_struct_raw(st));
    dst_mark_many(st, dst_struct_capacity(st));
}

static void dst_mark_tuple(const DstValue *tuple) {
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
    int32_t count, i;
    if (dst_gc_reachable(def))
        return;
    dst_gc_mark(def);
    if (def->constants) {
        count = def->constants_length;
        for (i = 0; i < count; ++i) {
            DstValue v = def->constants[i];
            /* Funcdefs use nil literals to store other funcdefs */
            if (dst_checktype(v, DST_NIL)) {
                dst_mark_funcdef((DstFuncDef *) dst_unwrap_pointer(v));
            } else {
                dst_mark(v);
            }
        }
    }
    if (def->source)
        dst_mark_string(def->source);
    if (def->sourcepath)
        dst_mark_string(def->sourcepath);
}

static void dst_mark_function(DstFunction *func) {
    int32_t i;
    int32_t numenvs;
    if (dst_gc_reachable(func))
        return;
    dst_gc_mark(func);
    numenvs = func->def->environments_length;
    if (NULL != func->envs)
        for (i = 0; i < numenvs; ++i)
            if (NULL != func->envs[i])
                dst_mark_funcenv(func->envs[i]);
    dst_mark_funcdef(func->def);
}

static void dst_mark_fiber(DstFiber *fiber) {
    int32_t i, j;
    DstStackFrame *frame;
    if (dst_gc_reachable(fiber))
        return;
    dst_gc_mark(fiber);
    
    i = fiber->frame;
    j = fiber->frametop;
    while (i > 0) {
        frame = (DstStackFrame *)(fiber->data + i - DST_FRAME_SIZE);
        if (NULL != frame->func)
            dst_mark_function(frame->func);
        /* Mark all values in the stack frame */
        dst_mark_many(fiber->data + i, j - i);
        j = i - DST_FRAME_SIZE;
        i = frame->prevframe;
    }

    if (NULL != fiber->parent)
        dst_mark_fiber(fiber->parent);

    dst_mark(fiber->ret);
}

/* Deinitialize a block of memory */
static void dst_deinit_block(DstGCMemoryHeader *block) {
    void *mem = ((char *)(block + 1));
    DstUserdataHeader *h = (DstUserdataHeader *)mem;
    switch (block->flags & DST_MEM_TYPEBITS) {
        default:
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
            free(((DstFiber *) mem)->data);
            break;
        case DST_MEMORY_BUFFER:
            dst_buffer_deinit((DstBuffer *) mem);
            break; 
        case DST_MEMORY_FUNCTION:
            free(((DstFunction *)mem)->envs);
            break;
        case DST_MEMORY_USERDATA:
            if (h->type->finalize)
                h->type->finalize((void *)(h + 1), h->size);
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
void *dst_gcalloc(DstMemoryType type, size_t size) {
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

    return mem + sizeof(DstGCMemoryHeader);
}

/* Run garbage collection */
void dst_collect() {
    uint32_t i;
    if (dst_vm_fiber)
        dst_mark_fiber(dst_vm_fiber);
    for (i = 0; i < dst_vm_root_count; i++)
        dst_mark(dst_vm_roots[i]);
    dst_sweep();
    dst_vm_next_collection = 0;
}

/* Add a root value to the GC. This prevents the GC from removing a value
 * and all of its children. If gcroot is called on a value n times, unroot
 * must also be called n times to remove it as a gc root. */
void dst_gcroot(DstValue root) {
    uint32_t newcount = dst_vm_root_count + 1;
    if (newcount > dst_vm_root_capacity) {
        uint32_t newcap = 2 * newcount;
        dst_vm_roots = realloc(dst_vm_roots, sizeof(DstValue) * newcap);
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
int dst_gcunroot(DstValue root) {
    DstValue *vtop = dst_vm_roots + dst_vm_root_count;
    DstValue *v = dst_vm_roots;
    /* Search from top to bottom as access is most likely LIFO */
    for (v = dst_vm_roots; v < vtop; v++) {
        if (dst_equals(root, *v)) {
            *v = dst_vm_roots[--dst_vm_root_count];
            return 1;
        }
    }
    return 0;
}

/* Free all allocated memory */
void dst_clear_memory() {
    DstGCMemoryHeader *current = dst_vm_blocks;
    while (current) {
        dst_deinit_block(current);
        DstGCMemoryHeader *next = current->next;
        free(current);
        current = next;
    }
    dst_vm_blocks = NULL;
}

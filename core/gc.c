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
#include "cache.h"
#include "wrap.h"

/* Helpers for marking the various gc types */
static void dst_mark_funcenv(DstFuncEnv *env);
static void dst_mark_funcdef(DstFuncEnv *def);
static void dst_mark_function(DstFunction *func);
static void dst_mark_array(DstArray *array);
static void dst_mark_table(DstTable *table);
static void dst_mark_struct(const DstValue *st);
static void dst_mark_tuple(const DstValue *tuple);
static void dst_mark_buffer(DstBuffer *buffer);
static void dst_mark_string(const uint8_t *str);
static void dst_mark_thread(DstThread *thread);
static void dst_mark_udata(void *udata);

/* Mark a value */
void dst_mark(DstValue x) {
    switch (x.type) {
        default: break;
        case DST_STRING:
        case DST_SYMBOL: dst_mark_string(x.as.string); break;
        case DST_FUNCTION: dst_mark_function(x.as.function); break;
        case DST_ARRAY: dst_mark_array(x.as.array); break;
        case DST_TABLE: dst_mark_table(x.as.table); break;
        case DST_STRUCT: dst_mark_struct(x.as.st); break;
        case DST_TUPLE: dst_mark_tuple(x.as.tuple); break;
        case DST_BUFFER: dst_mark_buffer(x.as.buffer); break;
        case DST_STRING: dst_mark_string(x.as.string); break;
        case DST_THREAD: dst_mark_thread(x.as.thread); break;
        case DST_USERDATA: dst_mark_udata(x.as.pointer); break;
    }
}

/* Unpin a value */
void dst_unpin(DstValue x) {
    switch (x.type) {
        default: break;
        case DST_STRING:
        case DST_SYMBOL: dst_unpin_string(x.as.string); break;
        case DST_FUNCTION: dst_unpin_function(x.as.function); break;
        case DST_ARRAY: dst_unpin_array(x.as.array); break;
        case DST_TABLE: dst_unpin_table(x.as.table); break;
        case DST_STRUCT: dst_unpin_struct(x.as.st); break;
        case DST_TUPLE: dst_unpin_tuple(x.as.tuple); break;
        case DST_BUFFER: dst_unpin_buffer(x.as.buffer); break;
        case DST_STRING: dst_unpin_string(x.as.string); break;
        case DST_THREAD: dst_unpin_thread(x.as.thread); break;
        case DST_USERDATA: dst_unpin_udata(x.as.pointer); break;
    }
}

/* Pin a value */
void dst_pin(DstValue x) {
    switch (x.type) {
        default: break;
        case DST_STRING:
        case DST_SYMBOL: dst_pin_string(x.as.string); break;
        case DST_FUNCTION: dst_pin_function(x.as.function); break;
        case DST_ARRAY: dst_pin_array(x.as.array); break;
        case DST_TABLE: dst_pin_table(x.as.table); break;
        case DST_STRUCT: dst_pin_struct(x.as.st); break;
        case DST_TUPLE: dst_pin_tuple(x.as.tuple); break;
        case DST_BUFFER: dst_pin_buffer(x.as.buffer); break;
        case DST_STRING: dst_pin_string(x.as.string); break;
        case DST_THREAD: dst_pin_thread(x.as.thread); break;
        case DST_USERDATA: dst_pin_udata(x.as.pointer); break;
    }
}

static void dst_mark_string(const uint8_t *str) {
    gc_mark(dst_string_raw(str));
}

static void dst_mark_buffer(DstBuffer *buffer) {
    gc_mark(buffer);
}

static void dst_mark_udata(void *udata) {
    gc_mark(dst_udata_header(udata));
}

/* Mark a bunch of items in memory */
static void dst_mark_many(const DstValue *values, uint32_t n) {
    const DstValue *end = values + n;
    while (values < end) {
        dst_mark(*values)
        ++values;
    }
}

static void dst_mark_array(DstArray *array) {
    if (gc_reachable(array))
        return;
    gc_mark(array);
    dst_mark_many(array->data, array->count);
}

static void dst_mark_table(DstTable *table) {
    if (gc_reachable(table))
        return;
    gc_mark(table);
    dst_mark_many(table->data, table->capacity);
}

static void dst_mark_struct(const DstValue *st) {
    if (gc_reachable(dst_struct_raw(st)))
        return;
    gc_mark(dst_struct_raw(st));
    dst_mark_many(st, dst_struct_capacity(st));
}

static void dst_mark_tuple(const DstValue *tuple) {
    if (gc_reachable(dst_tuple_raw(tuple)))
        return;
    gc_mark(dst_tuple_raw(tuple));
    dst_mark_many(tuple, dst_tuple_count(tuple));
}

/* Helper to mark function environments */
static void dst_mark_funcenv(DstFuncEnv *env) {
    if (gc_reachable(env))
        return;
    gc_mark(env);
    if (env->values) {
        uint32_t count = env->stackOffset;
        uint32_t i;
        for (i = 0; i < count; ++i)
            dst_mark_value(env->values[i]);
    }
    if (env->thread)
        dst_mark_thread(env->thread);
}

/* GC helper to mark a FuncDef */
static void dst_mark_funcdef(DstFuncDef *def) {
    uint32_t count, i;
    if (gc_reachable(def))
        return;
    gc_mark(def);
    if (def->literals) {
        count = def->literalsLen;
        for (i = 0; i < count; ++i) {
            DstValue v = def->literals[i];
            /* Funcdefs use boolean literals to store other funcdefs */
            if (v.type == DST_BOOLEAN) {
                dst_mark_funcdef((DstFuncDef *) v.as.pointer);
            } else {
                dst_mark(v);
            }
        }
    }
}

static void dst_mark_function(DstFunction *func) {
    uint32_t i;
    uint32_t numenvs;
    if (gc_reachable(func))
        return;
    gc_mark(func)
    numenvs = fun->def->envLen;
    for (i = 0; i < numenvs; ++i)
        dst_mark_funcenv(func->envs + i);
    dst_mark_funcdef(func->def);
}

/* Helper to mark a stack frame. Returns the next stackframe. */
static DstValue *dst_mark_stackframe(Dst *vm, DstValue *stack) {
    dst_mark(dst_frame_callee(stack));
    if (dst_frame_env(stack) != NULL)
        dst_mark_funcenv(dst_frame_env(stack));
    /* Mark all values in the stack frame */
    dst_mark_many(stack, dst_frame_size(stack));
    /* Return the nexct frame */
    return stack + dst_frame_size(stack) + DST_FRAME_SIZE;
}

static void dst_mark_thread(DstThread *thread) {
    DstValue *frame = thread->data + DST_FRAME_SIZE;
    DstValue *end = thread->data + thread->count;
    if (gc_reachable(thread))
        return;
    gc_mark(thread);
    while (frame <= end)
        frame = dst_mark_stackframe(vm, frame);
    if (thread->parent)
        dst_mark_thread(thread->parent);
}

/* Deinitialize a block of memory */
static void dst_deinit_block(Dst *vm, GCMemoryHeader *block) {
    void *mem = ((char *)(block + 1));
    DstUserdataHeader *h = (DstUserdataHeader *)mem;
    void *smem = mem + 2 * sizeof(uint32_t);
    switch (current->tags) {
        default:
            break; /* Do nothing for non gc types */ 
        case DST_MEMORY_STRING:
            dst_cache_remove(vm, dst_wrap_string(smem));
            break;
        case DST_MEMORY_ARRAY:
            free(((DstArray*) mem)->data);
            break;
        case DST_MEMORY_TUPLE:
            dst_cache_remove(vm, dst_wrap_tuple(smem));
            break;
        case DST_MEMORY_TABLE:
            free(((DstTable*) mem)->data);
            break;
        case DST_MEMORY_STRUCT:
            dst_cache_remove(vm, dst_wrap_struct(smem));
            break;
        case DST_MEMORY_THREAD:
            free(((DstThread *) mem)->data);
            break;
        case DST_MEMORY_BUFFER:
            free(((DstBuffer *) mem)->data);
            break; 
        case DST_MEMORY_FUNCTION:
            {
                DstFunction *f = (DstFunction *)mem;
                if (NULL != f->envs)
                    free(f->envs);
            }
            break;
        case DST_MEMORY_USERDATA:
            if (h->type->finalize)
                h->type->finalize(vm, (void *)(h + 1), h->size);
            break;
        case DST_MEMORY_FUNCENV:
            {
                DstFuncEnv *env = (DstFuncEnv *)mem;
                if (NULL == env->thread && NULL != env->values)
                    free(env->values);
            }
            break;
        case DST_MEMORY_FUNCDEF:
            {
                DstFunDef *def = (DstFuncDef *)mem;
                /* TODO - get this all with one alloc and one free */
                free(def->envSizes);
                free(def->envCaptures);
                free(def->literals);
                free(def->byteCode);
            }
            break;
    }
}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void dst_sweep(Dst *vm) {
    GCMemoryHeader *previous = NULL;
    GCMemoryHeader *current = vm->blocks;
    GCMemoryHeader *next;
    while (current) {
        next = current->next;
        if (current->flags & (DST_MEM_REACHABLE | DST_MEM_DISABLED)) {
            previous = current;
        } else {
            dst_deinit_block(vm, current);
            if (previous) {
                previous->next = next;
            } else {
                vm->blocks = next;
            }
            free(current);
        }
        current->flags &= ~DST_MEM_REACHABLE;
        current = next;
    }
}

/* Allocate some memory that is tracked for garbage collection */
void *dst_alloc(Dst *vm, DstMemoryType type, size_t size) {
    GCMemoryHeader *mdata;
    size_t totalSize = size + sizeof(GCMemoryHeader);
    void *mem = malloc(totalSize);

    /* Check for bad malloc */
    if (NULL == mem) {
        DST_OUT_OF_MEMORY;
    }

    mdata = (GCMemoryHeader *)rawBlock;

    /* Configure block */
    mdata->flags = type;

    /* Prepend block to heap list */
    vm->nextCollection += size;
    mdata->next = vm->blocks;
    vm->blocks = mdata;

    return mem + sizeof(GCMemoryHeader);
}

/* Run garbage collection */
void dst_collect(Dst *vm) {
    if (vm->thread)
        dst_mark_thread(vm->thread);
    dst_mark_table(vm->modules);
    dst_mark_table(vm->registry);
    dst_mark_table(vm->env);
    dst_mark(vm->ret);
    dst_sweep(vm);
    vm->nextCollection = 0;
}

/* Free all allocated memory */
void dst_clear_memory(Dst *vm) {
    GCMemoryHeader *current = vm->blocks;
    while (current) {
        dst_deinit_block(vm, current);
        GCMemoryHeader *next = current->next;
        free(current);
        current = next;
    }
    vm->blocks = NULL;
}

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

/* The metadata header associated with an allocated block of memory */
#define gc_header(mem) ((GCMemoryHeader *)(mem) - 1)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color : 1;
    uint32_t tags : 31;
};

/* Mark a chunk of memory as reachable for the gc */
void dst_mark_mem(Dst *vm, void *mem) {
	gc_header(mem)->color = vm->black;
}

/* Helper to mark function environments */
static void dst_mark_funcenv(Dst *vm, DstFuncEnv *env) {
    if (gc_header(env)->color != vm->black) {
        gc_header(env)->color = vm->black;
        if (env->thread) {
            DstValueUnion x;
            x.thread = env->thread;
            dst_mark(vm, x, DST_THREAD);
        }
        if (env->values) {
            uint32_t count = env->stackOffset;
            uint32_t i;
            gc_header(env->values)->color = vm->black;
            for (i = 0; i < count; ++i)
                dst_mark_value(vm, env->values[i]);
        }
    }
}

/* GC helper to mark a FuncDef */
static void dst_mark_funcdef(Dst *vm, DstFuncDef *def) {
    if (gc_header(def)->color != vm->black) {
        gc_header(def)->color = vm->black;
        gc_header(def->byteCode)->color = vm->black;
        uint32_t count, i;
        if (def->literals) {
            count = def->literalsLen;
            gc_header(def->literals)->color = vm->black;
            for (i = 0; i < count; ++i)
                dst_mark_value(vm, def->literals[i]);
        }
    }
}

/* Helper to mark a stack frame. Returns the next stackframe. */
static DstValue *dst_mark_stackframe(Dst *vm, DstValue *stack) {
    uint32_t i;
    dst_mark_value(vm, dst_frame_callee(stack));
    if (dst_frame_env(stack) != NULL)
        dst_mark_funcenv(vm, dst_frame_env(stack));
    for (i = 0; i < dst_frame_size(stack); ++i)
        dst_mark_value(vm, stack[i]);
    return stack + dst_frame_size(stack) + DST_FRAME_SIZE;
}

/* Wrapper for marking values */
void dst_mark_value(Dst *vm, DstValue x) {
    dst_mark(vm, x.data, x.type);
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing the garbage collection mark phase. */
void dst_mark(Dst *vm, DstValueUnion x, DstType type) {
    /* Allow for explicit tail recursion */
    begin:
    switch (type) {
        default:
            break;

        case DST_STRING:
        case DST_SYMBOL:
            gc_header(dst_string_raw(x.string))->color = vm->black;
            break;

        case DST_BYTEBUFFER:
            gc_header(x.buffer)->color = vm->black;
            gc_header(x.buffer->data)->color = vm->black;
            break;

        case DST_ARRAY:
            if (gc_header(x.array)->color != vm->black) {
                uint32_t i, count;
                count = x.array->count;
                gc_header(x.array)->color = vm->black;
                gc_header(x.array->data)->color = vm->black;
                for (i = 0; i < count; ++i)
                    dst_mark_value(vm, x.array->data[i]);
            }
            break;

        case DST_TUPLE:
            if (gc_header(dst_tuple_raw(x.tuple))->color != vm->black) {
                uint32_t i, count;
                count = dst_tuple_length(x.tuple);
                gc_header(dst_tuple_raw(x.tuple))->color = vm->black;
                for (i = 0; i < count; ++i)
                    dst_mark_value(vm, x.tuple[i]);
            }
            break;

        case DST_STRUCT:
            if (gc_header(dst_struct_raw(x.st))->color != vm->black) {
                uint32_t i, count;
                count = dst_struct_capacity(x.st);
                gc_header(dst_struct_raw(x.st))->color = vm->black;
                for (i = 0; i < count; ++i)
                    dst_mark_value(vm, x.st[i]);
            }
            break;

        case DST_THREAD:
            if (gc_header(x.thread)->color != vm->black) {
                DstThread *thread = x.thread;
                DstValue *frame = thread->data + DST_FRAME_SIZE;
                DstValue *end = thread->data + thread->count;
                gc_header(thread)->color = vm->black;
                gc_header(thread->data)->color = vm->black;
                while (frame <= end)
                    frame = dst_mark_stackframe(vm, frame);
                if (thread->parent) {
                    x.thread = thread->parent;
                    goto begin;
                }
            }
            break;

        case DST_FUNCTION:
            if (gc_header(x.function)->color != vm->black) {
                DstFunction *f = x.function;
                gc_header(f)->color = vm->black;
                dst_mark_funcdef(vm, f->def);
                if (f->env)
                    dst_mark_funcenv(vm, f->env);
                if (f->parent) {
                    DstValueUnion pval;
                    pval.function = f->parent;
                    dst_mark(vm, pval, DST_FUNCTION);
                }
            }
            break;

        case DST_TABLE:
            if (gc_header(x.table)->color != vm->black) {
                uint32_t i;
                gc_header(x.table)->color = vm->black;
                gc_header(x.table->data)->color = vm->black;
                for (i = 0; i < x.table->capacity; i += 2) {
                    dst_mark_value(vm, x.table->data[i]);
                    dst_mark_value(vm, x.table->data[i + 1]);
                }
            }
            break;

        case DST_USERDATA:
            {
                DstUserdataHeader *h = dst_udata_header(x.pointer);
                gc_header(h)->color = vm->black;
            }
            break;

        case DST_FUNCENV:
            dst_mark_funcenv(vm, x.env);
            break;

        case DST_FUNCDEF:
            dst_mark_funcdef(vm, x.def);
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
        if (current->color != vm->black) {
            if (previous) {
                previous->next = next;
            } else {
                vm->blocks = next;
            }
            if (current->tags) {
                if (current->tags & DST_MEMTAG_STRING)
                    dst_cache_remove_string(vm, (char *)(current + 1));
                if (current->tags & DST_MEMTAG_STRUCT)
                    dst_cache_remove_struct(vm, (char *)(current + 1));
                if (current->tags & DST_MEMTAG_TUPLE)
                    dst_cache_remove_tuple(vm, (char *)(current + 1));
                if (current->tags & DST_MEMTAG_USER) {
                    DstUserdataHeader *h = (DstUserdataHeader *)(current + 1);
                    if (h->type->finalize) {
                        h->type->finalize(vm, h + 1, h->size);
                    }
                }
            }
            dst_raw_free(current);
        } else {
            previous = current;
        }
        current = next;
    }
    /* Rotate flag */
    vm->black = !vm->black;
}

/* Prepare a memory block */
static void *dst_alloc_prepare(Dst *vm, char *rawBlock, uint32_t size) {
    GCMemoryHeader *mdata;
    if (rawBlock == NULL) {
        return NULL;
    }
    vm->nextCollection += size;
    mdata = (GCMemoryHeader *)rawBlock;
    mdata->next = vm->blocks;
    vm->blocks = mdata;
    mdata->color = !vm->black;
    mdata->tags = 0;
    return rawBlock + sizeof(GCMemoryHeader);
}

/* Allocate some memory that is tracked for garbage collection */
void *dst_alloc(Dst *vm, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    void *mem = dst_alloc_prepare(vm, dst_raw_alloc(totalSize), totalSize);
    if (!mem) {
        DST_LOW_MEMORY;
        dst_collect(vm);
        mem = dst_alloc_prepare(vm, dst_raw_alloc(totalSize), totalSize);
        if (!mem) {
            DST_OUT_OF_MEMORY;
        }
    }
    return mem;
}

/* Allocate some zeroed memory that is tracked for garbage collection */
void *dst_zalloc(Dst *vm, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    void *mem = dst_alloc_prepare(vm, dst_raw_calloc(1, totalSize), totalSize);
    if (!mem) {
        DST_LOW_MEMORY;
        dst_collect(vm);
        mem = dst_alloc_prepare(vm, dst_raw_calloc(1, totalSize), totalSize);
        if (!mem) {
            DST_OUT_OF_MEMORY;
        }
    }
    return mem;
}

/* Tag some memory to mark it with special properties */
void dst_mem_tag(void *mem, uint32_t tags) {
    GCMemoryHeader *mh = (GCMemoryHeader *)mem - 1;
    mh->tags |= tags;
}

/* Run garbage collection */
void dst_collect(Dst *vm) {
    DstValue x;
    /* Thread can be null */
    if (vm->thread) {
        x.type = DST_THREAD;
        x.data.thread = vm->thread;
        dst_mark_value(vm, x);
    }
    x.type = DST_TABLE;

    x.data.table = vm->modules;
    dst_mark_value(vm, x);

    x.data.table = vm->registry;
    dst_mark_value(vm, x);

    x.data.table = vm->env;
    dst_mark_value(vm, x);

    dst_mark_value(vm, vm->ret);
    dst_sweep(vm);
    vm->nextCollection = 0;
}

/* Run garbage collection if needed */
void dst_maybe_collect(Dst *vm) {
    if (vm->nextCollection >= vm->memoryInterval)
        dst_collect(vm);
}

/* Free all allocated memory */
void dst_clear_memory(Dst *vm) {
    GCMemoryHeader *current = vm->blocks;
    while (current) {
        GCMemoryHeader *next = current->next;
        dst_raw_free(current);
        current = next;
    }
    vm->blocks = NULL;
}

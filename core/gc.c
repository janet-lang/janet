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

#include <gst/gst.h>
#include "cache.h"

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
void gst_mark_mem(Gst *vm, void *mem) {
	gc_header(mem)->color = vm->black;
}

/* Helper to mark function environments */
static void gst_mark_funcenv(Gst *vm, GstFuncEnv *env) {
    if (gc_header(env)->color != vm->black) {
        gc_header(env)->color = vm->black;
        if (env->thread) {
            GstValueUnion x;
            x.thread = env->thread;
            gst_mark(vm, x, GST_THREAD);
        }
        if (env->values) {
            uint32_t count = env->stackOffset;
            uint32_t i;
            gc_header(env->values)->color = vm->black;
            for (i = 0; i < count; ++i)
                gst_mark_value(vm, env->values[i]);
        }
    }
}

/* GC helper to mark a FuncDef */
static void gst_mark_funcdef(Gst *vm, GstFuncDef *def) {
    if (gc_header(def)->color != vm->black) {
        gc_header(def)->color = vm->black;
        gc_header(def->byteCode)->color = vm->black;
        uint32_t count, i;
        if (def->literals) {
            count = def->literalsLen;
            gc_header(def->literals)->color = vm->black;
            for (i = 0; i < count; ++i)
                gst_mark_value(vm, def->literals[i]);
        }
    }
}

/* Helper to mark a stack frame. Returns the next stackframe. */
static GstValue *gst_mark_stackframe(Gst *vm, GstValue *stack) {
    uint32_t i;
    gst_mark_value(vm, gst_frame_callee(stack));
    if (gst_frame_env(stack) != NULL)
        gst_mark_funcenv(vm, gst_frame_env(stack));
    for (i = 0; i < gst_frame_size(stack); ++i)
        gst_mark_value(vm, stack[i]);
    return stack + gst_frame_size(stack) + GST_FRAME_SIZE;
}

/* Wrapper for marking values */
void gst_mark_value(Gst *vm, GstValue x) {
    gst_mark(vm, x.data, x.type);
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing the garbage collection mark phase. */
void gst_mark(Gst *vm, GstValueUnion x, GstType type) {
    switch (type) {
        default:
            break;

        case GST_STRING:
            gc_header(gst_string_raw(x.string))->color = vm->black;
            break;

        case GST_BYTEBUFFER:
            gc_header(x.buffer)->color = vm->black;
            gc_header(x.buffer->data)->color = vm->black;
            break;

        case GST_ARRAY:
            if (gc_header(x.array)->color != vm->black) {
                uint32_t i, count;
                count = x.array->count;
                gc_header(x.array)->color = vm->black;
                gc_header(x.array->data)->color = vm->black;
                for (i = 0; i < count; ++i)
                    gst_mark_value(vm, x.array->data[i]);
            }
            break;

        case GST_TUPLE:
            if (gc_header(gst_tuple_raw(x.tuple))->color != vm->black) {
                uint32_t i, count;
                count = gst_tuple_length(x.tuple);
                gc_header(gst_tuple_raw(x.tuple))->color = vm->black;
                for (i = 0; i < count; ++i)
                    gst_mark_value(vm, x.tuple[i]);
            }
            break;

        case GST_STRUCT:
            if (gc_header(gst_struct_raw(x.st))->color != vm->black) {
                uint32_t i, count;
                count = gst_struct_capacity(x.st);
                gc_header(gst_struct_raw(x.st))->color = vm->black;
                for (i = 0; i < count; ++i)
                    gst_mark_value(vm, x.st[i]);
            }
            break;

        case GST_THREAD:
            if (gc_header(x.thread)->color != vm->black) {
                GstThread *thread = x.thread;
                GstValue *frame = thread->data + GST_FRAME_SIZE;
                GstValue *end = thread->data + thread->count;
                gc_header(thread)->color = vm->black;
                gc_header(thread->data)->color = vm->black;
                while (frame <= end)
                    frame = gst_mark_stackframe(vm, frame);
                if (thread->parent)
                    gst_mark_value(vm, gst_wrap_thread(thread->parent));
                if (thread->errorParent)
                    gst_mark_value(vm, gst_wrap_thread(thread->errorParent));
            }
            break;

        case GST_FUNCTION:
            if (gc_header(x.function)->color != vm->black) {
                GstFunction *f = x.function;
                gc_header(f)->color = vm->black;
                gst_mark_funcdef(vm, f->def);
                if (f->env)
                    gst_mark_funcenv(vm, f->env);
                if (f->parent) {
                    GstValueUnion pval;
                    pval.function = f->parent;
                    gst_mark(vm, pval, GST_FUNCTION);
                }
            }
            break;

        case GST_TABLE:
            if (gc_header(x.table)->color != vm->black) {
                uint32_t i;
                gc_header(x.table)->color = vm->black;
                gc_header(x.table->data)->color = vm->black;
                for (i = 0; i < x.table->capacity; i += 2) {
                    gst_mark_value(vm, x.table->data[i]);
                    gst_mark_value(vm, x.table->data[i + 1]);
                }
            }
            break;

        case GST_USERDATA:
            if (gc_header(gst_udata_header(x.pointer))->color != vm->black) {
                GstUserdataHeader *h = gst_udata_header(x.pointer);
                gc_header(h)->color = vm->black;
                if (h->type->gcmark)
                    h->type->gcmark(vm, x.pointer, h->size);
            }
            break;

        case GST_FUNCENV:
            gst_mark_funcenv(vm, x.env);
            break;

        case GST_FUNCDEF:
            gst_mark_funcdef(vm, x.def);
            break;
    }
}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void gst_sweep(Gst *vm) {
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
                if (current->tags & GST_MEMTAG_STRING)
                    gst_cache_remove_string(vm, (char *)(current + 1));
                if (current->tags & GST_MEMTAG_STRUCT)
                    gst_cache_remove_struct(vm, (char *)(current + 1));
                if (current->tags & GST_MEMTAG_TUPLE)
                    gst_cache_remove_tuple(vm, (char *)(current + 1));
                if (current->tags & GST_MEMTAG_USER) {
                    GstUserdataHeader *h = (GstUserdataHeader *)(current + 1);
                    if (h->type->finalize) {
                        h->type->finalize(vm, h + 1, h->size);
                    }
                }
            }
            gst_raw_free(current);
        } else {
            previous = current;
        }
        current = next;
    }
    /* Rotate flag */
    vm->black = !vm->black;
}

/* Prepare a memory block */
static void *gst_alloc_prepare(Gst *vm, char *rawBlock, uint32_t size) {
    GCMemoryHeader *mdata;
    if (rawBlock == NULL) {
        GST_OUT_OF_MEMORY;
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
void *gst_alloc(Gst *vm, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return gst_alloc_prepare(vm, gst_raw_alloc(totalSize), totalSize);
}

/* Allocate some zeroed memory that is tracked for garbage collection */
void *gst_zalloc(Gst *vm, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return gst_alloc_prepare(vm, gst_raw_calloc(1, totalSize), totalSize);
}

/* Tag some memory to mark it with special properties */
void gst_mem_tag(void *mem, uint32_t tags) {
    GCMemoryHeader *mh = (GCMemoryHeader *)mem - 1;
    mh->tags |= tags;
}

/* Run garbage collection */
void gst_collect(Gst *vm) {
    /* Thread can be null */
    if (vm->thread)
        gst_mark_value(vm, gst_wrap_thread(vm->thread));
    gst_mark_value(vm, gst_wrap_table(vm->modules));
    gst_mark_value(vm, gst_wrap_table(vm->registry));
    gst_mark_value(vm, gst_wrap_table(vm->env));
    gst_mark_value(vm, vm->ret);
    if (vm->scratch)
        gc_header(vm->scratch)->color = vm->black;
    gst_sweep(vm);
    vm->nextCollection = 0;
}

/* Run garbage collection if needed */
void gst_maybe_collect(Gst *vm) {
    if (vm->nextCollection >= vm->memoryInterval)
        gst_collect(vm);
}

/* Free all allocated memory */
void gst_clear_memory(Gst *vm) {
    GCMemoryHeader *current = vm->blocks;
    while (current) {
        GCMemoryHeader *next = current->next;
        gst_raw_free(current);
        current = next;
    }
    vm->blocks = NULL;
}

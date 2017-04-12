#include <gst/gst.h>
#include "stringcache.h"

/* The metadata header associated with an allocated block of memory */
#define gc_header(mem) ((GCMemoryHeader *)(mem) - 1)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color : 1;
    uint32_t tags : 31;
};

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
        case GST_NIL:
        case GST_BOOLEAN:
        case GST_NUMBER:
        case GST_CFUNCTION:
            break;

        case GST_STRING:
        case GST_SYMBOL:
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

        case GST_THREAD:
            if (gc_header(x.thread)->color != vm->black) {
                GstThread *thread = x.thread;
                GstValue *frame = thread->data + GST_FRAME_SIZE;
                GstValue *end = thread->data + thread->count;
                gc_header(thread)->color = vm->black;
                gc_header(thread->data)->color = vm->black;
                while (frame <= end)
                    frame = gst_mark_stackframe(vm, frame);
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

        case GST_OBJECT:
            if (gc_header(x.object)->color != vm->black) {
                uint32_t i;
                GstBucket *bucket;
                gc_header(x.object)->color = vm->black;
                gc_header(x.object->buckets)->color = vm->black;
                for (i = 0; i < x.object->capacity; ++i) {
                    bucket = x.object->buckets[i];
                    while (bucket) {
                        gc_header(bucket)->color = vm->black;
                        gst_mark_value(vm, bucket->key);
                        gst_mark_value(vm, bucket->value);
                        bucket = bucket->next;
                    }
                }
                if (x.object->parent != NULL) {
                    GstValueUnion temp;
                    temp.object = x.object->parent;
                    gst_mark(vm, temp, GST_OBJECT);
                }
            }
            break;

        case GST_USERDATA:
            if (gc_header(x.string - sizeof(GstUserdataHeader))->color != vm->black) {
                GstUserdataHeader *userHeader = (GstUserdataHeader *)x.string - 1;
                gc_header(userHeader)->color = vm->black;
                GstValueUnion temp;
                temp.object = userHeader->meta;
                gst_mark(vm, temp, GST_OBJECT);
            }

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
            /* Remove from string cache */
            if (current->tags & GST_MEMTAG_STRING) {
                gst_stringcache_remove(vm, (uint8_t *)(current + 1) + 2 * sizeof(uint32_t));
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
    if (vm->thread) {
        GstValueUnion t;
        t.thread = vm->thread;
        gst_mark(vm, t, GST_THREAD);
    }
    gst_mark_value(vm, vm->rootenv);
    gst_mark_value(vm, vm->ret);
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

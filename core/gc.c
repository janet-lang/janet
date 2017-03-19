#include <gst/datatypes.h>
#include <gst/gc.h>
#include <gst/vm.h>
#include <gst/util.h>

/* The metadata header associated with an allocated block of memory */
#define gc_header(mem) ((GCMemoryHeader *)(mem) - 1)

/* Memory header struct. Node of a linked list of memory blocks. */
typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color : 1;
};

/* Helper to mark function environments */
static void gst_mark_funcenv(Gst *vm, GstFuncEnv *env) {
    if (gc_header(env)->color != vm->black) {
        GstValue temp;
        gc_header(env)->color = vm->black;
        if (env->thread) {
            temp.type = GST_THREAD;
            temp.data.thread = env->thread;
            gst_mark(vm, &temp);
        }
        if (env->values) {
            uint32_t count = env->stackOffset;
            uint32_t i;
            gc_header(env->values)->color = vm->black;
            for (i = 0; i < count; ++i)
                gst_mark(vm, env->values + i);
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
                gst_mark(vm, def->literals + i);
        }
    }
}

/* Helper to mark a stack frame. Returns the next stackframe. */
static GstValue *gst_mark_stackframe(Gst *vm, GstValue *stack) {
    uint32_t i;
    gst_mark(vm, &gst_frame_callee(stack));
    if (gst_frame_env(stack) != NULL)
        gst_mark_funcenv(vm, gst_frame_env(stack));
    for (i = 0; i < gst_frame_size(stack); ++i)
        gst_mark(vm, stack + i);
    return stack + gst_frame_size(stack) + GST_FRAME_SIZE;
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing the garbage collection mark phase. */
void gst_mark(Gst *vm, GstValue *x) {
    switch (x->type) {
        case GST_NIL:
        case GST_BOOLEAN:
        case GST_NUMBER:
        case GST_CFUNCTION:
            break;

        case GST_STRING:
            gc_header(gst_string_raw(x->data.string))->color = vm->black;
            break;

        case GST_BYTEBUFFER:
            gc_header(x->data.buffer)->color = vm->black;
            gc_header(x->data.buffer->data)->color = vm->black;
            break;

        case GST_ARRAY:
            if (gc_header(x->data.array)->color != vm->black) {
                uint32_t i, count;
                count = x->data.array->count;
                gc_header(x->data.array)->color = vm->black;
                gc_header(x->data.array->data)->color = vm->black;
                for (i = 0; i < count; ++i)
                    gst_mark(vm, x->data.array->data + i);
            }
            break;

        case GST_TUPLE:
            if (gc_header(gst_tuple_raw(x->data.tuple))->color != vm->black) {
                uint32_t i, count;
                count = gst_tuple_length(x->data.tuple);
                gc_header(gst_tuple_raw(x->data.tuple))->color = vm->black;
                for (i = 0; i < count; ++i)
                    gst_mark(vm, x->data.tuple + i);
            }
            break;

        case GST_THREAD:
            if (gc_header(x->data.thread)->color != vm->black) {
                GstThread *thread = x->data.thread;
                GstValue *frame = thread->data + GST_FRAME_SIZE;
                GstValue *end = thread->data + thread->count;
                gc_header(thread)->color = vm->black;
                gc_header(thread->data)->color = vm->black;
                while (frame <= end)
                    frame = gst_mark_stackframe(vm, frame);
            }
            break;

        case GST_FUNCTION:
            if (gc_header(x->data.function)->color != vm->black) {
                GstFunction *f = x->data.function;
                gc_header(f)->color = vm->black;
                gst_mark_funcdef(vm, f->def);
                if (f->env)
                    gst_mark_funcenv(vm, f->env);
                if (f->parent) {
                    GstValue temp;
                    temp.type = GST_FUNCTION;
                    temp.data.function = f->parent;
                    gst_mark(vm, &temp);
                }
            }
            break;

        case GST_OBJECT:
            if (gc_header(x->data.object)->color != vm->black) {
                uint32_t i;
                GstBucket *bucket;
                gc_header(x->data.object)->color = vm->black;
                gc_header(x->data.object->buckets)->color = vm->black;
                for (i = 0; i < x->data.object->capacity; ++i) {
                    bucket = x->data.object->buckets[i];
                    while (bucket) {
                        gc_header(bucket)->color = vm->black;
                        gst_mark(vm, &bucket->key);
                        gst_mark(vm, &bucket->value);
                        bucket = bucket->next;
                    }
                }
                if (x->data.object->meta != NULL) {
                    GstValue temp;
                    temp.type = GST_OBJECT;
                    temp.data.object = x->data.object->meta;
                    gst_mark(vm, &temp);
                }
            }
            break;

        case GST_USERDATA:
            if (gc_header(x->data.string - sizeof(GstUserdataHeader))->color != vm->black) {
                GstUserdataHeader *userHeader = (GstUserdataHeader *)x->data.string - 1;
                gc_header(userHeader)->color = vm->black;
                GstValue temp;
                temp.type = GST_OBJECT;
                temp.data.object = userHeader->meta;
                gst_mark(vm, &temp);
            }

        case GST_FUNCENV:
            gst_mark_funcenv(vm, x->data.env);
            break;

        case GST_FUNCDEF:
            gst_mark_funcdef(vm, x->data.def);
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

/* Run garbage collection */
void gst_collect(Gst *vm) {
    GstValue temp;
    /* Thread can be null */
    if (vm->thread) {
        temp.type = GST_THREAD;
        temp.data.thread = vm->thread;
        gst_mark(vm, &temp);
    }
    gst_mark(vm, &vm->rootenv);
    gst_mark(vm, &vm->ret);
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

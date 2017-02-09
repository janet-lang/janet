#include <stdlib.h>
#include "gc.h"
#include "dict.h"
#include "vstring.h"
#include "parse.h"

#define GCHeader(mem) ((GCMemoryHeader *)(mem) - 1)

typedef struct GCMemoryHeader GCMemoryHeader;
struct GCMemoryHeader {
    GCMemoryHeader * next;
    uint32_t color;
};

/* Initialize a garbage collector */
void GCInit(GC * gc, uint32_t memoryInterval) {
    gc->black = 0;
    gc->blocks = NULL;
    gc->nextCollection = 0;
    gc->memoryInterval = memoryInterval;
    gc->handleOutOfMemory = NULL;
}

/* Mark some raw memory */
void GCMarkMemory(GC * gc, void * memory) {
    GCHeader(memory)->color = gc->black;
}

/* Helper to mark function environments */
static void GCMarkFuncEnv(GC * gc, FuncEnv * env) {
    if (GCHeader(env)->color != gc->black) {
        Value temp;
        GCHeader(env)->color = gc->black;
        if (env->thread) {
            temp.type = TYPE_THREAD;
            temp.data.array = env->thread;
            GCMark(gc, &temp);
        } else {
            uint32_t count = env->stackOffset;
            uint32_t i;
            GCHeader(env->values)->color = gc->black;
            for (i = 0; i < count; ++i) {
                GCMark(gc, env->values + i);
            }
        }
    }
}

/* Mark allocated memory associated with a value. This is
 * the main function for doing garbage collection. */
void GCMark(GC * gc, Value * x) {
    switch (x->type) {
        case TYPE_NIL:
        case TYPE_BOOLEAN:
        case TYPE_NUMBER:
        case TYPE_CFUNCTION:
            break;

        case TYPE_STRING:
        case TYPE_SYMBOL:
            GCHeader(VStringRaw(x->data.string))->color = gc->black;
            break;

        case TYPE_BYTEBUFFER:
            GCHeader(x->data.buffer)->color = gc->black;
            GCHeader(x->data.buffer->data)->color = gc->black;
            break;

        case TYPE_ARRAY:
        case TYPE_FORM:
            if (GCHeader(x->data.array)->color != gc->black) {
                uint32_t i, count;
                count = x->data.array->count;
                GCHeader(x->data.array)->color = gc->black;
                GCHeader(x->data.array->data)->color = gc->black;
                for (i = 0; i < count; ++i)
                    GCMark(gc, x->data.array->data + i);
            }
            break;

        case TYPE_THREAD:
            if (GCHeader(x->data.array)->color != gc->black) {
                uint32_t i, count;
                count = x->data.array->count;
                GCHeader(x->data.array)->color = gc->black;
                GCHeader(x->data.array->data)->color = gc->black;
                if (count) {
                    count += FrameSize(x->data.array);
                    for (i = 0; i < count; ++i)
                        GCMark(gc, x->data.array->data + i);
                }               
            }
            break;

        case TYPE_FUNCTION:
            if (GCHeader(x->data.func)->color != gc->black) {
                Func * f = x->data.func;
                GCHeader(f)->color = gc->black;
                GCMarkFuncEnv(gc, f->env);
                {
                    Value temp;
                    temp.type = TYPE_FUNCDEF;
                    temp.data.funcdef = x->data.funcdef;
                    GCMark(gc, &temp);
                    if (f->parent) {
                        temp.type = TYPE_FUNCTION;
                        temp.data.func = f->parent;
                        GCMark(gc, &temp);
                    }
                }
            }
            break;

        case TYPE_DICTIONARY:
            if (GCHeader(x->data.dict)->color != gc->black) {
                DictionaryIterator iter;
                DictBucket * bucket;
                GCHeader(x->data.dict)->color = gc->black;
                GCHeader(x->data.dict->buckets)->color = gc->black;
                DictIterate(x->data.dict, &iter);
                while (DictIterateNext(&iter, &bucket)) {
                    GCHeader(bucket)->color = gc->black;
                    GCMark(gc, &bucket->key);
                    GCMark(gc, &bucket->value);
                }
            }
            break;

        case TYPE_FUNCDEF:
            if (GCHeader(x->data.funcdef)->color != gc->black) {
                GCHeader(x->data.funcdef->byteCode)->color = gc->black;
                uint32_t count, i;
                count = x->data.funcdef->literalsLen;
                if (x->data.funcdef->literals) {
                    GCHeader(x->data.funcdef->literals)->color = gc->black;
                    for (i = 0; i < count; ++i)
                        GCMark(gc, x->data.funcdef->literals + i);
                }
            }
            break;

       	case TYPE_FUNCENV:
			GCMarkFuncEnv(gc, x->data.funcenv);
           	break;

    }

}

/* Iterate over all allocated memory, and free memory that is not
 * marked as reachable. Flip the gc color flag for next sweep. */
void GCSweep(GC * gc) {
    GCMemoryHeader * previous = NULL;
    GCMemoryHeader * current = gc->blocks;
    while (current) {
        if (current->color != gc->black) {
            if (previous) {
                previous->next = current->next;
            } else {
                gc->blocks = current->next;
            }
            free(current);
        } else {
            previous = current;
        }
        current = current->next;
    }
    /* Rotate flag */
    gc->black = !gc->black;
}

/* Clean up all memory */
void GCClear(GC * gc) {
    GCMemoryHeader * current = gc->blocks;
    while (current) {
        GCMemoryHeader * next = current->next;
        free(current);
        current = next;
    }
    gc->blocks = NULL;
}

/* Prepare a memory block */
static void * GCPrepare(GC * gc, char * rawBlock, uint32_t size) {
    GCMemoryHeader * mdata;
    if (rawBlock == NULL) {
        if (gc->handleOutOfMemory != NULL)
            gc->handleOutOfMemory(gc);
        return NULL;
    }
    gc->nextCollection += size;
    mdata = (GCMemoryHeader *) rawBlock;
    mdata->next = gc->blocks;
    gc->blocks = mdata;
    mdata->color = !gc->black;
    return rawBlock + sizeof(GCMemoryHeader);
}

/* Allocate some memory that is tracked for garbage collection */
void * GCAlloc(GC * gc, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return GCPrepare(gc, malloc(totalSize), totalSize);
}

/* Allocate some zeroed memory that is tracked for garbage collection */
void * GCZalloc(GC * gc, uint32_t size) {
    uint32_t totalSize = size + sizeof(GCMemoryHeader);
    return GCPrepare(gc, calloc(1, totalSize), totalSize);
}

#include <gst/gst.h>
#include "stringcache.h"

/* Dud pointer to serve as deletion marker */
static uint8_t *deleted = (uint8_t *) "DELETED";

/* Initialize the string cache for a vm */
void gst_stringcache_init(Gst *vm, uint32_t capacity) {
    vm->strings = gst_raw_calloc(1, capacity * sizeof(uint8_t *));
    if (vm->strings == NULL)
        GST_OUT_OF_MEMORY;
    vm->stringsCapacity = capacity;
    vm->stringsCount = 0;
    vm->stringsDeleted = 0;
}

/* Deinitialize the stringcache for a vm */
void gst_stringcache_deinit(Gst *vm) {
    gst_raw_free(vm->strings);
    vm->stringsCapacity = 0;
    vm->stringsCount = 0;
    vm->stringsDeleted = 0;
}

/* Find a string in the hashtable. Returns null if
 * not found. */
static uint8_t **gst_stringcache_find(Gst *vm, uint8_t *str, int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index, hash;
    uint8_t **firstEmpty = NULL;
    hash = gst_string_hash(str);
    if (!hash) {
        hash = gst_string_hash(str) = gst_string_calchash(str);
    }
    index = hash % vm->stringsCapacity;
    bounds[0] = index;
    bounds[1] = vm->stringsCapacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j+1]; ++i) {
            uint8_t *testStr = vm->strings[i];
            /* Check empty spots */
            if (testStr == NULL) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->strings + i;
                goto notfound;
            }
            if (testStr == deleted) {
                if (firstEmpty == NULL)
                    firstEmpty = vm->strings + i;
                continue;
            }
            if (gst_string_equal(testStr, str)) {
                /* Replace first deleted */
                *success = 1;
                if (firstEmpty != NULL) {
                    *firstEmpty = testStr;
                    vm->strings[i] = deleted;
                    return firstEmpty;
                }
                return vm->strings + i;
            }
        }
    notfound:
    *success = 0;
    return firstEmpty;
}

/* Resize the hashtable. */
static void gst_stringcache_resize(Gst *vm, uint32_t newCapacity) {
    uint32_t i, oldCapacity;
    uint8_t **oldCache = vm->strings;
    uint8_t **newCache = gst_raw_calloc(1, newCapacity * sizeof(uint8_t *));
    if (newCache == NULL)
        GST_OUT_OF_MEMORY;
    oldCapacity = vm->stringsCapacity;
    vm->strings = newCache;
    vm->stringsCapacity = newCapacity;
    vm->stringsCount = 0;
    vm->stringsDeleted = 0;
    /* Add all of the old strings back */
    for (i = 0; i < oldCapacity; ++i) {
        uint8_t *str = oldCache[i];
        if (str != NULL && str != deleted)
            gst_stringcache_get(vm, str);
    }
    /* Free the old cache */
    gst_raw_free(oldCache);
}

/* Get a string from the string cache */
uint8_t *gst_stringcache_get(Gst *vm, uint8_t *str) {
    int status = 0;
    uint8_t **bucket = gst_stringcache_find(vm, str, &status);
    if (status) {
        return *bucket;
    } else {
        if ((vm->stringsCount + vm->stringsDeleted) * 2 > vm->stringsCapacity) {
            gst_stringcache_resize(vm, vm->stringsCount * 4); 
            bucket = gst_stringcache_find(vm, str, &status);
        }
        vm->stringsCount++;
        *bucket = str;
        /* Mark the memory as string memory */
        gst_mem_tag(gst_string_raw(str), GST_MEMTAG_STRING);
        return str;
    }
}

/* Remove a string from the cache */
void gst_stringcache_remove(Gst *vm, uint8_t *str) {
    int status = 0;
    uint8_t **bucket = gst_stringcache_find(vm, str, &status);
    if (status) {
        vm->stringsCount--;
        vm->stringsDeleted++;
        *bucket = deleted;
    }
}

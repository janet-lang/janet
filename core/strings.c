#include <gst/gst.h>
#include "stringcache.h"

/* Dud pointer to serve as deletion marker */
static uint8_t *deleted = (uint8_t *) "";

/* Check if string and cstring are equal */
/* To check if strings are equal externally, one can
 * just use == */
static int gst_cstring_equal(const uint8_t *lhs, const uint8_t *rhs, uint32_t rlen, uint32_t rhash) {
    uint32_t lhash, len, i;
    /* Check lengths */
    len = gst_string_length(lhs);
    if (len != rlen) return 0;
    /* Check hashes */
    lhash = gst_string_hash(lhs);
    if (lhash != rhash) return 0;
    for (i = 0; i < len; ++i)
        if (lhs[i] != rhs[i])
            return 0;
    return 1;
}

/* Simple hash function (djb2) */
static uint32_t gst_string_calchash(const uint8_t *str, uint32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* Find a string in the hashtable. Returns null if
 * not found. */
static const uint8_t **gst_stringcache_find(
        Gst *vm, 
        const uint8_t *str,
        uint32_t len,
        uint32_t hash,
        int *success) {
    uint32_t bounds[4];
    uint32_t i, j, index;
    const uint8_t **firstEmpty = NULL;
    index = hash % vm->stringsCapacity;
    bounds[0] = index;
    bounds[1] = vm->stringsCapacity;
    bounds[2] = 0;
    bounds[3] = index;
    for (j = 0; j < 4; j += 2)
        for (i = bounds[j]; i < bounds[j+1]; ++i) {
            const uint8_t *testStr = vm->strings[i];
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
            if (gst_cstring_equal(testStr, str, len, hash)) {
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
    const uint8_t **oldCache = vm->strings;
    const uint8_t **newCache = gst_raw_calloc(1, newCapacity * sizeof(uint8_t *));
    if (newCache == NULL)
        GST_OUT_OF_MEMORY;
    oldCapacity = vm->stringsCapacity;
    vm->strings = newCache;
    vm->stringsCapacity = newCapacity;
    vm->stringsDeleted = 0;
    /* Add all of the old strings back */
    for (i = 0; i < oldCapacity; ++i) {
        int status;
        const uint8_t **bucket;
        const uint8_t *str = oldCache[i];
        if (str != NULL && str != deleted) {
            bucket = gst_stringcache_find(vm, str,
                    gst_string_length(str),
                    gst_string_hash(str), &status);
            if (status || bucket == NULL) {
                /* there was a problem with the algorithm. */
                break;
            }
            *bucket = str;
        }
    }
    /* Free the old cache */
    gst_raw_free(oldCache);
}

/****/
/* Internal API */
/****/

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

/* Remove a string from the cache */
void gst_stringcache_remove(Gst *vm, const uint8_t *str) {
    int status = 0;
    const uint8_t **bucket = gst_stringcache_find(vm, str,
            gst_string_length(str), 
            gst_string_hash(str),
            &status);
    if (status) {
        vm->stringsCount--;
        vm->stringsDeleted++;
        *bucket = deleted;
    }
}

/****/
/* Public C API */
/****/

/* Begin creation of a string */
uint8_t *gst_string_begin(Gst *vm, uint32_t len) {
    uint8_t *data = gst_alloc(vm, len + 1 + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    gst_string_length(data) = len;
    data[len] = 0;
    return data;
}

/* Finish building the string. Calculates the hash and deduplicates it */
const uint8_t *gst_string_end(Gst *vm, uint8_t *str) {
    int status = 0;
    const uint8_t **bucket;
    uint32_t hash, len;
    len = gst_string_length(str);
    hash = gst_string_hash(str) = gst_string_calchash(str, len);
    bucket = gst_stringcache_find(vm, str, len, hash, &status);
    if (status) {
        return *bucket;
    } else {
        if ((vm->stringsCount + vm->stringsDeleted) * 2 > vm->stringsCapacity) {
            gst_stringcache_resize(vm, vm->stringsCount * 4); 
            bucket = gst_stringcache_find(vm, str, len, hash, &status);
        }
        /* Mark the memory as string memory */
        gst_mem_tag(gst_string_raw(str), GST_MEMTAG_STRING);
        vm->stringsCount++;
        *bucket = str;
        return str;
    }
}

/* Loads a constant buffer as a string into a gst vm */
const uint8_t *gst_string_loadbuffer(Gst *vm, const uint8_t *buf, uint32_t len) {
    int status = 0;
    const uint8_t **bucket;
    uint32_t hash;
    hash = gst_string_calchash(buf, len);
    bucket = gst_stringcache_find(vm, buf, len, hash, &status);
    if (status) {
        return *bucket;
    } else {
        uint8_t *str;
        if ((vm->stringsCount + vm->stringsDeleted) * 2 > vm->stringsCapacity) {
            gst_stringcache_resize(vm, vm->stringsCount * 4); 
            bucket = gst_stringcache_find(vm, buf, len, hash, &status);
        }
        vm->stringsCount++;
        str = gst_string_begin(vm, len);
        gst_memcpy(str, buf, len);
        gst_string_hash(str) = hash;
        /* Mark the memory as string memory */
        gst_mem_tag(gst_string_raw(str), GST_MEMTAG_STRING);
        *bucket = str;
        return str;
    }
}

/* Converts a c style string to a gst string */
const uint8_t *gst_cstring_to_string(Gst *vm, const char *cstring) {
    uint32_t len = 0;
    while (cstring[len]) ++len;
    return gst_string_loadbuffer(vm, (const uint8_t *)cstring, len);
}

/* Load a c string into a GST string */
GstValue gst_load_cstring(Gst *vm, const char *string) {
    GstValue ret;
    ret.type = GST_STRING;
    ret.data.string = gst_cstring_to_string(vm, string);
    return ret;
}

/* Compares two strings */
int gst_string_compare(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t xlen = gst_string_length(lhs);
    uint32_t ylen = gst_string_length(rhs);
    uint32_t len = xlen > ylen ? ylen : xlen;
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (lhs[i] == rhs[i]) {
            continue;
        } else if (lhs[i] < rhs[i]) {
            return -1; /* x is less then y */
        } else {
            return 1; /* y is less than x */
        }
    }
    if (xlen == ylen) {
        return 0;
    } else {
        return xlen < ylen ? -1 : 1;
    }
}

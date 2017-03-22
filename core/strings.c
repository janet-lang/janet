#include <gst/gst.h>
#include "stringcache.h"

uint8_t *gst_load_cstring_rawlen(Gst *vm, const char *string, uint32_t len) {
    uint8_t *data = gst_alloc(vm, len + 1 + 2 * sizeof(uint32_t));
    data += 2 * sizeof(uint32_t);
    gst_string_hash(data) = 0;
    gst_string_length(data) = len;
    gst_memcpy(data, string, len);
    data[len] = 0;
    /* Check string cache */
    return gst_stringcache_get(vm, data);
}

/* Load a c string into a GST string */
GstValue gst_load_cstring(Gst *vm, const char *string) {
    GstValue ret;
    ret.type = GST_STRING;
    ret.data.string = gst_load_cstring_rawlen(vm, string, strlen(string));
    return ret;
}

/* Load a c string into a GST symbol */
GstValue gst_load_csymbol(Gst *vm, const char *string) {
    GstValue ret;
    ret.type = GST_SYMBOL;
    ret.data.string = gst_load_cstring_rawlen(vm, string, strlen(string));
    return ret;
}

/* Simple hash function (djb2) */
uint32_t gst_cstring_calchash(const uint8_t *str, uint32_t len) {
    const uint8_t *end = str + len;
    uint32_t hash = 5381;
    while (str < end)
        hash = (hash << 5) + hash + *str++;
    return hash;
}

/* GST string version */
uint32_t gst_string_calchash(const uint8_t *str) {
    return gst_cstring_calchash(str, gst_string_length(str));
}

/* Check if two strings are equal. Does not check the string cache. */
int gst_string_equal(const uint8_t *lhs, const uint8_t *rhs) {
    uint32_t hash_l, hash_r, len, i;
    if (lhs == rhs)
        return 1;
    /* Check lengths */
    len = gst_string_length(lhs);
    if (len != gst_string_length(rhs)) return 0;
    /* Check hashes */
    hash_l = gst_string_hash(lhs); 
    hash_r = gst_string_hash(rhs); 
    if (!hash_l)
        hash_l = gst_string_hash(lhs) = gst_string_calchash(lhs);
    if (!hash_r)
        hash_r = gst_string_hash(rhs) = gst_string_calchash(rhs);
    if (hash_l != hash_r) return 0;
    for (i = 0; i < len; ++i)
        if (lhs[i] != rhs[i])
            return 0;
    return 1;
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

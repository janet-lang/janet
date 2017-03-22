#ifndef GST_STRINGCACHE_defined
#define GST_STRINGCACHE_defined

#include <gst/gst.h>

/****/
/* String Cache (move internal) */
/****/

void gst_stringcache_init(Gst *vm, uint32_t capacity);
void gst_stringcache_deinit(Gst *vm);
uint8_t *gst_stringcache_get(Gst *vm, uint8_t *str);
void gst_stringcache_remove(Gst *vm, uint8_t *str);

#endif

#ifndef CACHE_H_LVYZMBLR
#define CACHE_H_LVYZMBLR

#include <gst/gst.h>

void gst_cache_remove_string(Gst *vm, char *strmem);
void gst_cache_remove_tuple(Gst *vm, char *tuplemem);
void gst_cache_remove_struct(Gst *vm, char *structmem);

#endif /* end of include guard: CACHE_H_LVYZMBLR */

#ifndef util_h_INCLUDED
#define util_h_INCLUDED

/* String utils */
#define gst_string_raw(s) ((uint32_t *)(s) - 2)
#define gst_string_length(v) (gst_string_raw(v)[0])
#define gst_string_hash(v) (gst_string_raw(v)[1])

/* Tuple utils */
#define gst_tuple_raw(s) ((uint32_t *)(s) - 2)
#define gst_tuple_length(v) (gst_tuple_raw(v)[0])
#define gst_tuple_hash(v) (gst_tuple_raw(v)[1])

/* Memcpy for moving memory */
#ifndef gst_memcpy
#include <string.h>
#define gst_memcpy memcpy
#endif

/* Allocation */
#ifndef gst_raw_alloc
#include <stdlib.h>
#define gst_raw_alloc malloc
#endif

/* Zero allocation */
#ifndef gst_raw_calloc
#include <stdlib.h>
#define gst_raw_calloc calloc
#endif

/* Realloc */
#ifndef gst_raw_realloc
#include <stdlib.h>
#define gst_raw_realloc realloc
#endif

/* Free */
#ifndef gst_raw_free
#include <stdlib.h>
#define gst_raw_free free
#endif

/* Null */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* C function helpers */

/* Return in a c function */
#define gst_c_return(vm, x) (do { (vm)->ret = (x); return GST_RETURN_OK; } while (0))

/* Throw error from a c function */
#define gst_c_throw(vm, e) (do { (vm)->ret = (e); return GST_RETURN_ERROR; } while (0))

/* What to do when out of memory */
#ifndef GST_OUT_OF_MEMORY
#include <stdlib.h>
#include <stdio.h>
#define GST_OUT_OF_MEMORY do { printf("out of memory.\n"); exit(1); } while (0)
#endif

#endif // util_h_INCLUDED


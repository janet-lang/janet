#ifndef util_h_INCLUDED
#define util_h_INCLUDED

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

/* Clear allocation */
#ifndef gst_raw_calloc
#include <stdlib.h>
#define gst_raw_calloc calloc
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

#endif // util_h_INCLUDED


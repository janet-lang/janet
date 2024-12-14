/*
* Copyright (c) 2024 Calvin Rose
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#ifndef JANET_STATE_H_defined
#define JANET_STATE_H_defined

#include <janet.h>
#include <stdint.h>

#ifdef JANET_EV
#ifndef JANET_WINDOWS
#include <pthread.h>
#endif
#endif

typedef int64_t JanetTimestamp;

typedef struct JanetScratch {
    JanetScratchFinalizer finalize;
    long long mem[]; /* for proper alignment */
} JanetScratch;

typedef struct {
    JanetGCObject *self;
    JanetGCObject *other;
    int32_t index;
    int32_t index2;
} JanetTraversalNode;

typedef struct {
    int32_t capacity;
    int32_t head;
    int32_t tail;
    void *data;
} JanetQueue;

typedef struct {
    JanetTimestamp when;
    JanetFiber *fiber;
    JanetFiber *curr_fiber;
    uint32_t sched_id;
    int is_error;
} JanetTimeout;

/* Registry table for C functions - contains metadata that can
 * be looked up by cfunction pointer. All strings here are pointing to
 * static memory not managed by Janet. */
typedef struct {
    JanetCFunction cfun;
    const char *name;
    const char *name_prefix;
    const char *source_file;
    int32_t source_line;
    /* int32_t min_arity; */
    /* int32_t max_arity; */
} JanetCFunRegistry;

struct JanetVM {
    /* Place for user data */
    void *user;

    /* Top level dynamic bindings */
    JanetTable *top_dyns;

    /* Cache the core environment */
    JanetTable *core_env;

    /* How many VM stacks have been entered */
    int stackn;

    /* If this flag is true, suspend on function calls and backwards jumps.
     * When this occurs, this flag will be reset to 0. */
    volatile JanetAtomicInt auto_suspend;

    /* The current running fiber on the current thread.
     * Set and unset by functions in vm.c */
    JanetFiber *fiber;
    JanetFiber *root_fiber;

    /* The current pointer to the inner most jmp_buf. The current
     * return point for panics. */
    jmp_buf *signal_buf;
    Janet *return_reg;
    int coerce_error;

    /* The global registry for c functions. Used to store meta-data
     * along with otherwise bare c function pointers. */
    JanetCFunRegistry *registry;
    size_t registry_cap;
    size_t registry_count;
    int registry_dirty;

    /* Registry for abstract types that can be marshalled.
     * We need this to look up the constructors when unmarshalling. */
    JanetTable *abstract_registry;

    /* Immutable value cache */
    const uint8_t **cache;
    uint32_t cache_capacity;
    uint32_t cache_count;
    uint32_t cache_deleted;
    uint8_t gensym_counter[8];

    /* Garbage collection */
    void *blocks;
    void *weak_blocks;
    size_t gc_interval;
    size_t next_collection;
    size_t block_count;
    int gc_suspend;
    int gc_mark_phase;

    /* GC roots */
    Janet *roots;
    size_t root_count;
    size_t root_capacity;

    /* Scratch memory */
    JanetScratch **scratch_mem;
    size_t scratch_cap;
    size_t scratch_len;

    /* Sandbox flags */
    uint32_t sandbox_flags;

    /* Random number generator */
    JanetRNG rng;

    /* Traversal pointers */
    JanetTraversalNode *traversal;
    JanetTraversalNode *traversal_top;
    JanetTraversalNode *traversal_base;

    /* Thread safe strerror error buffer - for janet_strerror */
#ifndef JANET_WINDOWS
    char strerror_buf[256];
#endif

    /* Event loop and scheduler globals */
#ifdef JANET_EV
    size_t tq_count;
    size_t tq_capacity;
    JanetQueue spawn;
    JanetTimeout *tq;
    JanetRNG ev_rng;
    volatile JanetAtomicInt listener_count; /* used in signal handler, must be volatile */
    JanetTable threaded_abstracts; /* All abstract types that can be shared between threads (used in this thread) */
    JanetTable active_tasks; /* All possibly live task fibers - used just for tracking */
    JanetTable signal_handlers;
#ifdef JANET_WINDOWS
    void **iocp;
#elif defined(JANET_EV_EPOLL)
    pthread_attr_t new_thread_attr;
    JanetHandle selfpipe[2];
    int epoll;
    int timerfd;
    int timer_enabled;
#elif defined(JANET_EV_KQUEUE)
    pthread_attr_t new_thread_attr;
    JanetHandle selfpipe[2];
    int kq;
    int timer;
    int timer_enabled;
#else
    JanetStream **streams;
    size_t stream_count;
    size_t stream_capacity;
    pthread_attr_t new_thread_attr;
    JanetHandle selfpipe[2];
    struct pollfd *fds;
#endif
#endif

};

extern JANET_THREAD_LOCAL JanetVM janet_vm;

#ifdef JANET_NET
void janet_net_init(void);
void janet_net_deinit(void);
#endif

#ifdef JANET_EV
void janet_ev_init(void);
void janet_ev_deinit(void);
#endif

#endif /* JANET_STATE_H_defined */

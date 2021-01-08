/*
* Copyright (c) 2021 Calvin Rose
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

#ifndef JANET_AMALG
#include "features.h"
#include <janet.h>
#include "util.h"
#include "gc.h"
#include "state.h"
#include "fiber.h"
#endif

#ifdef JANET_EV

#include <math.h>
#ifdef JANET_WINDOWS
#include <winsock2.h>
#include <windows.h>
#else
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef JANET_EV_EPOLL
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif
#endif

/* Ring buffer for storing a list of fibers */
typedef struct {
    int32_t capacity;
    int32_t head;
    int32_t tail;
    void *data;
} JanetQueue;

typedef struct {
    JanetFiber *fiber;
    uint32_t sched_id;
    enum {
        JANET_CP_MODE_ITEM,
        JANET_CP_MODE_CHOICE_READ,
        JANET_CP_MODE_CHOICE_WRITE
    } mode;
} JanetChannelPending;

typedef struct {
    JanetQueue items;
    JanetQueue read_pending;
    JanetQueue write_pending;
    int32_t limit;
} JanetChannel;

#define JANET_MAX_Q_CAPACITY 0x7FFFFFF

static void janet_q_init(JanetQueue *q) {
    q->data = NULL;
    q->head = 0;
    q->tail = 0;
    q->capacity = 0;
}

static void janet_q_deinit(JanetQueue *q) {
    free(q->data);
}

static int32_t janet_q_count(JanetQueue *q) {
    return (q->head > q->tail)
           ? (q->tail + q->capacity - q->head)
           : (q->tail - q->head);
}

static int janet_q_push(JanetQueue *q, void *item, size_t itemsize) {
    int32_t count = janet_q_count(q);
    /* Resize if needed */
    if (count + 1 >= q->capacity) {
        if (count + 1 >= JANET_MAX_Q_CAPACITY) return 1;
        int32_t newcap = (count + 2) * 2;
        if (newcap > JANET_MAX_Q_CAPACITY) newcap = JANET_MAX_Q_CAPACITY;
        q->data = realloc(q->data, itemsize * newcap);
        if (NULL == q->data) {
            JANET_OUT_OF_MEMORY;
        }
        if (q->head > q->tail) {
            /* Two segments, fix 2nd seg. */
            int32_t newhead = q->head + (newcap - q->capacity);
            size_t seg1 = (size_t)(q->capacity - q->head);
            if (seg1 > 0) {
                memmove((char *) q->data + (newhead * itemsize),
                        (char *) q->data + (q->head * itemsize),
                        seg1 * itemsize);
            }
            q->head = newhead;
        }
        q->capacity = newcap;
    }
    memcpy((char *) q->data + itemsize * q->tail, item, itemsize);
    q->tail = q->tail + 1 < q->capacity ? q->tail + 1 : 0;
    return 0;
}

static int janet_q_pop(JanetQueue *q, void *out, size_t itemsize) {
    if (q->head == q->tail) return 1;
    memcpy(out, (char *) q->data + itemsize * q->head, itemsize);
    q->head = q->head + 1 < q->capacity ? q->head + 1 : 0;
    return 0;
}

/* New fibers to spawn or resume */
typedef struct JanetTask JanetTask;
struct JanetTask {
    JanetFiber *fiber;
    Janet value;
    JanetSignal sig;
};

/* Min priority queue of timestamps for timeouts. */
typedef int64_t JanetTimestamp;
typedef struct JanetTimeout JanetTimeout;
struct JanetTimeout {
    JanetTimestamp when;
    JanetFiber *fiber;
    JanetFiber *curr_fiber;
    uint32_t sched_id;
    int is_error;
};

/* Forward declaration */
static void janet_unlisten(JanetListenerState *state);

/* Global data */
JANET_THREAD_LOCAL size_t janet_vm_tq_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_tq_capacity = 0;
JANET_THREAD_LOCAL JanetQueue janet_vm_spawn;
JANET_THREAD_LOCAL JanetTimeout *janet_vm_tq = NULL;
JANET_THREAD_LOCAL JanetRNG janet_vm_ev_rng;
JANET_THREAD_LOCAL JanetListenerState **janet_vm_listeners = NULL;
JANET_THREAD_LOCAL size_t janet_vm_listener_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_listener_cap = 0;
JANET_THREAD_LOCAL size_t janet_vm_extra_listeners = 0;

/* Get current timestamp (millisecond precision) */
static JanetTimestamp ts_now(void);

/* Get current timestamp + an interval (millisecond precision) */
static JanetTimestamp ts_delta(JanetTimestamp ts, double delta) {
    ts += (int64_t)round(delta * 1000);
    return ts;
}

/* Look at the next timeout value without
 * removing it. */
static int peek_timeout(JanetTimeout *out) {
    if (janet_vm_tq_count == 0) return 0;
    *out = janet_vm_tq[0];
    return 1;
}

/* Remove the next timeout from the priority queue */
static void pop_timeout(size_t index) {
    if (janet_vm_tq_count <= index) return;
    janet_vm_tq[index] = janet_vm_tq[--janet_vm_tq_count];
    for (;;) {
        size_t left = (index << 1) + 1;
        size_t right = left + 1;
        size_t smallest = index;
        if (left < janet_vm_tq_count &&
                (janet_vm_tq[left].when < janet_vm_tq[smallest].when))
            smallest = left;
        if (right < janet_vm_tq_count &&
                (janet_vm_tq[right].when < janet_vm_tq[smallest].when))
            smallest = right;
        if (smallest == index) return;
        JanetTimeout temp = janet_vm_tq[index];
        janet_vm_tq[index] = janet_vm_tq[smallest];
        janet_vm_tq[smallest] = temp;
        index = smallest;
    }
}

/* Add a timeout to the timeout min heap */
static void add_timeout(JanetTimeout to) {
    size_t oldcount = janet_vm_tq_count;
    size_t newcount = oldcount + 1;
    if (newcount > janet_vm_tq_capacity) {
        size_t newcap = 2 * newcount;
        JanetTimeout *tq = realloc(janet_vm_tq, newcap * sizeof(JanetTimeout));
        if (NULL == tq) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_tq = tq;
        janet_vm_tq_capacity = newcap;
    }
    /* Append */
    janet_vm_tq_count = (int32_t) newcount;
    janet_vm_tq[oldcount] = to;
    /* Heapify */
    size_t index = oldcount;
    while (index > 0) {
        size_t parent = (index - 1) >> 1;
        if (janet_vm_tq[parent].when <= janet_vm_tq[index].when) break;
        /* Swap */
        JanetTimeout tmp = janet_vm_tq[index];
        janet_vm_tq[index] = janet_vm_tq[parent];
        janet_vm_tq[parent] = tmp;
        /* Next */
        index = parent;
    }
}

/* Create a new event listener */
static JanetListenerState *janet_listen_impl(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    if (stream->_mask & mask) {
        janet_panic("cannot listen for duplicate event on stream");
    }
    if (janet_vm_root_fiber->waiting != NULL) {
        janet_panic("current fiber is already waiting for event");
    }
    if (size < sizeof(JanetListenerState))
        size = sizeof(JanetListenerState);
    JanetListenerState *state = malloc(size);
    if (NULL == state) {
        JANET_OUT_OF_MEMORY;
    }
    state->machine = behavior;
    state->fiber = janet_vm_root_fiber;
    janet_vm_root_fiber->waiting = state;
    state->stream = stream;
    state->_mask = mask;
    stream->_mask |= mask;
    state->_next = stream->state;
    stream->state = state;

    /* Keep track of a listener for GC purposes */
    int resize = janet_vm_listener_cap == janet_vm_listener_count;
    if (resize) {
        size_t newcap = janet_vm_listener_count ? janet_vm_listener_cap * 2 : 16;
        janet_vm_listeners = realloc(janet_vm_listeners, newcap * sizeof(JanetListenerState *));
        if (NULL == janet_vm_listeners) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_listener_cap = newcap;
    }
    size_t index = janet_vm_listener_count++;
    janet_vm_listeners[index] = state;
    state->_index = index;

    /* Emit INIT event for convenience */
    state->event = user;
    state->machine(state, JANET_ASYNC_EVENT_INIT);
    return state;
}

/* Indicate we are no longer listening for an event. This
 * frees the memory of the state machine as well. */
static void janet_unlisten_impl(JanetListenerState *state) {
    state->machine(state, JANET_ASYNC_EVENT_DEINIT);
    /* Remove state machine from poll list */
    JanetListenerState **iter = &(state->stream->state);
    while (*iter && *iter != state)
        iter = &((*iter)->_next);
    janet_assert(*iter, "failed to remove listener");
    *iter = state->_next;
    /* Remove mask */
    state->stream->_mask &= ~(state->_mask);
    /* Ensure fiber does not reference this state */
    JanetFiber *fiber = state->fiber;
    if (NULL != fiber && fiber->waiting == state) {
        fiber->waiting = NULL;
    }
    /* Untrack a listener for gc purposes */
    size_t index = state->_index;
    janet_vm_listeners[index] = janet_vm_listeners[--janet_vm_listener_count];
    janet_vm_listeners[index]->_index = index;
    free(state);
}

static const JanetMethod ev_default_stream_methods[] = {
    {"close", janet_cfun_stream_close},
    {"read", janet_cfun_stream_read},
    {"chunk", janet_cfun_stream_chunk},
    {"write", janet_cfun_stream_write},
    {NULL, NULL}
};

/* Create a stream*/
JanetStream *janet_stream(JanetHandle handle, uint32_t flags, const JanetMethod *methods) {
    JanetStream *stream = janet_abstract(&janet_stream_type, sizeof(JanetStream));
    stream->handle = handle;
    stream->flags = flags;
    stream->state = NULL;
    stream->_mask = 0;
    if (methods == NULL) methods = ev_default_stream_methods;
    stream->methods = methods;
#ifdef JANET_NET
    if (flags & JANET_STREAM_SOCKET) {
#ifdef JANET_WINDOWS
        u_long iMode = 0;
        ioctlsocket((SOCKET) handle, FIONBIO, &iMode);
#else
#if !defined(SOCK_CLOEXEC) && defined(O_CLOEXEC)
        int extra = O_CLOEXEC;
#else
        int extra = 0;
#endif
        fcntl(handle, F_SETFL, fcntl(handle, F_GETFL, 0) | O_NONBLOCK | extra);
#endif
    }
#endif
    return stream;
}

/* Called to clean up a stream */
static int janet_stream_gc(void *p, size_t s) {
    (void) s;
    JanetStream *stream = (JanetStream *)p;
    janet_stream_close(stream);
    return 0;
}

/* Close a stream */
void janet_stream_close(JanetStream *stream) {
    if (stream->flags & JANET_STREAM_CLOSED) return;
    JanetListenerState *state = stream->state;
    while (NULL != state) {
        state->machine(state, JANET_ASYNC_EVENT_CLOSE);
        JanetListenerState *next_state = state->_next;
        janet_unlisten(state);
        state = next_state;
    }
    stream->state = NULL;
    stream->flags |= JANET_STREAM_CLOSED;
#ifdef JANET_WINDOWS
#ifdef JANET_NET
    if (stream->flags & JANET_STREAM_SOCKET) {
        closesocket((SOCKET) stream->handle);
    } else
#endif
    {
        CloseHandle(stream->handle);
    }
#else
    close(stream->handle);
#endif
}

/* Mark a stream for GC */
static int janet_stream_mark(void *p, size_t s) {
    (void) s;
    JanetStream *stream = (JanetStream *) p;
    JanetListenerState *state = stream->state;
    while (NULL != state) {
        if (NULL != state->fiber) {
            janet_mark(janet_wrap_fiber(state->fiber));
        }
        (state->machine)(state, JANET_ASYNC_EVENT_MARK);
        state = state->_next;
    }
    return 0;
}

static int janet_stream_getter(void *p, Janet key, Janet *out) {
    JanetStream *stream = (JanetStream *)p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    const JanetMethod *stream_methods = stream->methods;
    return janet_getmethod(janet_unwrap_keyword(key), stream_methods, out);
    return 0;
}

static void janet_stream_marshal(void *p, JanetMarshalContext *ctx) {
    JanetStream *s = p;
    if (!(ctx->flags & JANET_MARSHAL_UNSAFE)) {
        janet_panic("can only marshal stream with unsafe flag");
    }
    janet_marshal_abstract(ctx, p);
    janet_marshal_int(ctx, (int32_t) s->flags);
    janet_marshal_int64(ctx, (intptr_t) s->methods);
#ifdef JANET_WINDOWS
    /* TODO - ref counting to avoid situation where a handle is closed or GCed
     * while in transit, and it's value gets reused. DuplicateHandle does not work
     * for network sockets, and in general for winsock it is better to nipt duplicate
     * unless there is a need to. */
    janet_marshal_int64(ctx, (int64_t)(s->handle));
#else
    /* Marshal after dup becuse it is easier than maintaining our own ref counting. */
    int duph = dup(s->handle);
    if (duph < 0) janet_panicf("failed to duplicate stream handle: %V", janet_ev_lasterr());
    janet_marshal_int(ctx, (int32_t)(duph));
#endif
}

static void *janet_stream_unmarshal(JanetMarshalContext *ctx) {
    if (!(ctx->flags & JANET_MARSHAL_UNSAFE)) {
        janet_panic("can only unmarshal stream with unsafe flag");
    }
    JanetStream *p = janet_unmarshal_abstract(ctx, sizeof(JanetStream));
    /* Can't share listening state and such across threads */
    p->_mask = 0;
    p->state = NULL;
    p->flags = (uint32_t) janet_unmarshal_int(ctx);
    p->methods = (void *) janet_unmarshal_int64(ctx);
#ifdef JANET_WINDOWS
    p->handle = (JanetHandle) janet_unmarshal_int64(ctx);
#else
    p->handle = (JanetHandle) janet_unmarshal_int(ctx);
#endif
    return p;
}


const JanetAbstractType janet_stream_type = {
    "core/stream",
    janet_stream_gc,
    janet_stream_mark,
    janet_stream_getter,
    NULL,
    janet_stream_marshal,
    janet_stream_unmarshal,
    JANET_ATEND_UNMARSHAL
};

/* Register a fiber to resume with value */
void janet_schedule_signal(JanetFiber *fiber, Janet value, JanetSignal sig) {
    if (fiber->flags & JANET_FIBER_FLAG_SCHEDULED) return;
    fiber->flags |= JANET_FIBER_FLAG_SCHEDULED;
    fiber->sched_id++;
    JanetTask t = { fiber, value, sig };
    janet_q_push(&janet_vm_spawn, &t, sizeof(t));
}

void janet_cancel(JanetFiber *fiber, Janet value) {
    janet_schedule_signal(fiber, value, JANET_SIGNAL_ERROR);
}

void janet_schedule(JanetFiber *fiber, Janet value) {
    janet_schedule_signal(fiber, value, JANET_SIGNAL_OK);
}

void janet_fiber_did_resume(JanetFiber *fiber) {
    /* Cancel any pending fibers */
    if (fiber->waiting) {
        fiber->waiting->machine(fiber->waiting, JANET_ASYNC_EVENT_CANCEL);
        janet_unlisten(fiber->waiting);
    }
}

/* Mark all pending tasks */
void janet_ev_mark(void) {

    /* Pending tasks */
    JanetTask *tasks = janet_vm_spawn.data;
    if (janet_vm_spawn.head <= janet_vm_spawn.tail) {
        for (int32_t i = janet_vm_spawn.head; i < janet_vm_spawn.tail; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
    } else {
        for (int32_t i = janet_vm_spawn.head; i < janet_vm_spawn.capacity; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
        for (int32_t i = 0; i < janet_vm_spawn.tail; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
    }

    /* Pending timeouts */
    for (size_t i = 0; i < janet_vm_tq_count; i++) {
        janet_mark(janet_wrap_fiber(janet_vm_tq[i].fiber));
        if (janet_vm_tq[i].curr_fiber != NULL) {
            janet_mark(janet_wrap_fiber(janet_vm_tq[i].curr_fiber));
        }
    }

    /* Pending listeners */
    for (size_t i = 0; i < janet_vm_listener_count; i++) {
        JanetListenerState *state = janet_vm_listeners[i];
        if (NULL != state->fiber) {
            janet_mark(janet_wrap_fiber(state->fiber));
        }
        janet_stream_mark(state->stream, sizeof(JanetStream));
        (state->machine)(state, JANET_ASYNC_EVENT_MARK);
    }
}

static int janet_channel_push(JanetChannel *channel, Janet x, int is_choice);

/* Run a top level task */
static void run_one(JanetFiber *fiber, Janet value, JanetSignal sigin) {
    fiber->flags &= ~JANET_FIBER_FLAG_SCHEDULED;
    Janet res;
    JanetSignal sig = janet_continue_signal(fiber, value, &res, sigin);
    if (sig != JANET_SIGNAL_EVENT) {
        if (fiber->supervisor_channel) {
            JanetChannel *chan = (JanetChannel *)(fiber->supervisor_channel);
            janet_channel_push(chan, janet_wrap_fiber(fiber), 0);
            fiber->supervisor_channel = NULL;
        } else if (sig != JANET_SIGNAL_OK) {
            janet_stacktrace(fiber, res);
        }
    }
}

/* Common init code */
void janet_ev_init_common(void) {
    janet_q_init(&janet_vm_spawn);
    janet_vm_listener_count = 0;
    janet_vm_listener_cap = 0;
    janet_vm_listeners = NULL;
    janet_vm_tq = NULL;
    janet_vm_tq_count = 0;
    janet_vm_tq_capacity = 0;
    janet_rng_seed(&janet_vm_ev_rng, 0);
}

/* Common deinit code */
void janet_ev_deinit_common(void) {
    janet_q_deinit(&janet_vm_spawn);
    free(janet_vm_tq);
    free(janet_vm_listeners);
    janet_vm_listeners = NULL;
}

/* Short hand to yield to event loop */
void janet_await(void) {
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

/* Set timeout for the current root fiber */
void janet_addtimeout(double sec) {
    JanetFiber *fiber = janet_vm_root_fiber;
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = fiber;
    to.curr_fiber = NULL;
    to.sched_id = fiber->sched_id;
    to.is_error = 1;
    add_timeout(to);
}

void janet_ev_inc_refcount(void) {
    janet_vm_extra_listeners++;
}

void janet_ev_dec_refcount(void) {
    janet_vm_extra_listeners--;
}

/* Channels */

#define JANET_MAX_CHANNEL_CAPACITY 0xFFFFFF

static void janet_chan_init(JanetChannel *chan, int32_t limit) {
    chan->limit = limit;
    janet_q_init(&chan->items);
    janet_q_init(&chan->read_pending);
    janet_q_init(&chan->write_pending);
}

static void janet_chan_deinit(JanetChannel *chan) {
    janet_q_deinit(&chan->read_pending);
    janet_q_deinit(&chan->write_pending);
    janet_q_deinit(&chan->items);
}

/*
 * Janet Channel abstract type
 */

/*static int janet_chanat_get(void *p, Janet key, Janet *out);*/
static int janet_chanat_mark(void *p, size_t s);
static int janet_chanat_gc(void *p, size_t s);

static const JanetAbstractType ChannelAT = {
    "core/channel",
    janet_chanat_gc,
    janet_chanat_mark,
    NULL, /* janet_chanat_get */
    JANET_ATEND_GET
};

static int janet_chanat_gc(void *p, size_t s) {
    (void) s;
    JanetChannel *channel = p;
    janet_chan_deinit(channel);
    return 0;
}

static void janet_chanat_mark_fq(JanetQueue *fq) {
    JanetChannelPending *pending = fq->data;
    if (fq->head <= fq->tail) {
        for (int32_t i = fq->head; i < fq->tail; i++)
            janet_mark(janet_wrap_fiber(pending[i].fiber));
    } else {
        for (int32_t i = fq->head; i < fq->capacity; i++)
            janet_mark(janet_wrap_fiber(pending[i].fiber));
        for (int32_t i = 0; i < fq->tail; i++)
            janet_mark(janet_wrap_fiber(pending[i].fiber));
    }
}

static int janet_chanat_mark(void *p, size_t s) {
    (void) s;
    JanetChannel *chan = p;
    janet_chanat_mark_fq(&chan->read_pending);
    janet_chanat_mark_fq(&chan->write_pending);
    JanetQueue *items = &chan->items;
    Janet *data = chan->items.data;
    if (items->head <= items->tail) {
        for (int32_t i = items->head; i < items->tail; i++)
            janet_mark(data[i]);
    } else {
        for (int32_t i = items->head; i < items->capacity; i++)
            janet_mark(data[i]);
        for (int32_t i = 0; i < items->tail; i++)
            janet_mark(data[i]);
    }
    return 0;
}

static Janet make_write_result(JanetChannel *channel) {
    Janet *tup = janet_tuple_begin(2);
    tup[0] = janet_ckeywordv("give");
    tup[1] = janet_wrap_abstract(channel);
    return janet_wrap_tuple(janet_tuple_end(tup));
}

static Janet make_read_result(JanetChannel *channel, Janet x) {
    Janet *tup = janet_tuple_begin(3);
    tup[0] = janet_ckeywordv("take");
    tup[1] = janet_wrap_abstract(channel);
    tup[2] = x;
    return janet_wrap_tuple(janet_tuple_end(tup));
}

/* Push a value to a channel, and return 1 if channel should block, zero otherwise.
 * If the push would block, will add to the write_pending queue in the channel. */
static int janet_channel_push(JanetChannel *channel, Janet x, int is_choice) {
    JanetChannelPending reader;
    int is_empty;
    do {
        is_empty = janet_q_pop(&channel->read_pending, &reader, sizeof(reader));
    } while (!is_empty && (reader.sched_id != reader.fiber->sched_id));
    if (is_empty) {
        /* No pending reader */
        if (janet_q_push(&channel->items, &x, sizeof(Janet))) {
            janet_panicf("channel overflow: %v", x);
        } else if (janet_q_count(&channel->items) > channel->limit) {
            /* No root fiber, we are in completion on a root fiber. Don't block. */
            if (NULL == janet_vm_root_fiber) {
                return 0;
            }
            /* Pushed successfully, but should block. */
            JanetChannelPending pending;
            pending.fiber = janet_vm_root_fiber,
            pending.sched_id = janet_vm_root_fiber->sched_id,
            pending.mode = is_choice ? JANET_CP_MODE_CHOICE_WRITE : JANET_CP_MODE_ITEM;
            janet_q_push(&channel->write_pending, &pending, sizeof(pending));
            return 1;
        }
    } else {
        /* Pending reader */
        if (reader.mode == JANET_CP_MODE_CHOICE_READ) {
            janet_schedule(reader.fiber, make_read_result(channel, x));
        } else {
            janet_schedule(reader.fiber, x);
        }
    }
    return 0;
}

/* Pop from a channel - returns 1 if item was obtain, 0 otherwise. The item
 * is returned by reference. If the pop would block, will add to the read_pending
 * queue in the channel. */
static int janet_channel_pop(JanetChannel *channel, Janet *item, int is_choice) {
    JanetChannelPending writer;
    if (janet_q_pop(&channel->items, item, sizeof(Janet))) {
        /* Queue empty */
        JanetChannelPending pending;
        pending.fiber = janet_vm_root_fiber,
        pending.sched_id = janet_vm_root_fiber->sched_id;
        pending.mode = is_choice ? JANET_CP_MODE_CHOICE_READ : JANET_CP_MODE_ITEM;
        janet_q_push(&channel->read_pending, &pending, sizeof(pending));
        return 0;
    }
    if (!janet_q_pop(&channel->write_pending, &writer, sizeof(writer))) {
        /* pending writer */
        if (writer.mode == JANET_CP_MODE_CHOICE_WRITE) {
            janet_schedule(writer.fiber, make_write_result(channel));
        } else {
            janet_schedule(writer.fiber, janet_wrap_abstract(channel));
        }
    }
    return 1;
}

/* Channel Methods */

static Janet cfun_channel_push(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetChannel *channel = janet_getabstract(argv, 0, &ChannelAT);
    if (janet_channel_push(channel, argv[1], 0)) {
        janet_await();
    }
    return argv[0];
}

static Janet cfun_channel_pop(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getabstract(argv, 0, &ChannelAT);
    Janet item;
    if (janet_channel_pop(channel, &item, 0)) {
        janet_schedule(janet_vm_root_fiber, item);
    }
    janet_await();
}

static Janet cfun_channel_choice(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    int32_t len;
    const Janet *data;

    /* Check channels for immediate reads and writes */
    for (int32_t i = 0; i < argc; i++) {
        if (janet_indexed_view(argv[i], &data, &len) && len == 2) {
            /* Write */
            JanetChannel *chan = janet_getabstract(data, 0, &ChannelAT);
            if (janet_q_count(&chan->items) < chan->limit) {
                janet_channel_push(chan, data[1], 1);
                return make_write_result(chan);
            }
        } else {
            /* Read */
            JanetChannel *chan = janet_getabstract(argv, i, &ChannelAT);
            if (chan->items.head != chan->items.tail) {
                Janet item;
                janet_channel_pop(chan, &item, 1);
                return make_read_result(chan, item);
            }
        }
    }

    /* Wait for all readers or writers */
    for (int32_t i = 0; i < argc; i++) {
        if (janet_indexed_view(argv[i], &data, &len) && len == 2) {
            /* Write */
            JanetChannel *chan = janet_getabstract(data, 0, &ChannelAT);
            janet_channel_push(chan, data[1], 1);
        } else {
            /* Read */
            Janet item;
            JanetChannel *chan = janet_getabstract(argv, i, &ChannelAT);
            janet_channel_pop(chan, &item, 1);
        }
    }

    janet_await();
}

static Janet cfun_channel_full(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getabstract(argv, 0, &ChannelAT);
    return janet_wrap_boolean(janet_q_count(&channel->items) >= channel->limit);
}

static Janet cfun_channel_capacity(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getabstract(argv, 0, &ChannelAT);
    return janet_wrap_integer(channel->limit);
}

static Janet cfun_channel_count(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getabstract(argv, 0, &ChannelAT);
    return janet_wrap_integer(janet_q_count(&channel->items));
}

/* Fisher yates shuffle of arguments to get fairness */
static void fisher_yates_args(int32_t argc, Janet *argv) {
    for (int32_t i = argc; i > 1; i--) {
        int32_t swap_index = janet_rng_u32(&janet_vm_ev_rng) % i;
        Janet temp = argv[swap_index];
        argv[swap_index] = argv[i - 1];
        argv[i - 1] = temp;
    }
}

static Janet cfun_channel_rchoice(int32_t argc, Janet *argv) {
    fisher_yates_args(argc, argv);
    return cfun_channel_choice(argc, argv);
}

static Janet cfun_channel_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    int32_t limit = janet_optnat(argv, argc, 0, 0);
    JanetChannel *channel = janet_abstract(&ChannelAT, sizeof(JanetChannel));
    janet_chan_init(channel, limit);
    return janet_wrap_abstract(channel);
}

/* Main event loop */

void janet_loop1_impl(int has_timeout, JanetTimestamp timeout);

void janet_loop1(void) {
    /* Schedule expired timers */
    JanetTimeout to;
    JanetTimestamp now = ts_now();
    while (peek_timeout(&to) && to.when <= now) {
        pop_timeout(0);
        if (to.curr_fiber != NULL) {
            /* This is a deadline (for a fiber, not a function call) */
            JanetFiberStatus s = janet_fiber_status(to.curr_fiber);
            int isFinished = s == (JANET_STATUS_DEAD ||
                                   s == JANET_STATUS_ERROR ||
                                   s == JANET_STATUS_USER0 ||
                                   s == JANET_STATUS_USER1 ||
                                   s == JANET_STATUS_USER2 ||
                                   s == JANET_STATUS_USER3 ||
                                   s == JANET_STATUS_USER4);
            if (!isFinished) {
                janet_cancel(to.fiber, janet_cstringv("deadline expired"));
            }
        } else {
            /* This is a timeout (for a function call, not a whole fiber) */
            if (to.fiber->sched_id == to.sched_id) {
                if (to.is_error) {
                    janet_cancel(to.fiber, janet_cstringv("timeout"));
                } else {
                    janet_schedule(to.fiber, janet_wrap_nil());
                }
            }
        }
    }

    /* Run scheduled fibers */
    while (janet_vm_spawn.head != janet_vm_spawn.tail) {
        JanetTask task = {NULL, janet_wrap_nil(), JANET_SIGNAL_OK};
        janet_q_pop(&janet_vm_spawn, &task, sizeof(task));
        run_one(task.fiber, task.value, task.sig);
    }

    /* Poll for events */
    if (janet_vm_listener_count || janet_vm_tq_count || janet_vm_extra_listeners) {
        JanetTimeout to;
        memset(&to, 0, sizeof(to));
        int has_timeout;
        /* Drop timeouts that are no longer needed */
        while ((has_timeout = peek_timeout(&to)) && (to.curr_fiber == NULL) && to.fiber->sched_id != to.sched_id) {
            pop_timeout(0);
        }
        /* Run polling implementation only if pending timeouts or pending events */
        if (janet_vm_tq_count || janet_vm_listener_count || janet_vm_extra_listeners) {
            janet_loop1_impl(has_timeout, to.when);
        }
    }
}

void janet_loop(void) {
    while (janet_vm_listener_count || (janet_vm_spawn.head != janet_vm_spawn.tail) || janet_vm_tq_count || janet_vm_extra_listeners) {
        janet_loop1();
    }
}

/*
 * Self-pipe handling code.
 */

/* Wrap return value by pairing it with the callback used to handle it
 * in the main thread */
typedef struct {
    JanetEVGenericMessage msg;
    JanetThreadedCallback cb;
} JanetSelfPipeEvent;

/* Structure used to initialize threads in the thread pool
 * (same head structure as self pipe event)*/
typedef struct {
    JanetEVGenericMessage msg;
    JanetThreadedCallback cb;
    JanetThreadedSubroutine subr;
    JanetHandle write_pipe;
} JanetEVThreadInit;

#ifdef JANET_WINDOWS

/* On windows, use PostQueuedCompletionStatus instead for
 * custom events */

#else

static JANET_THREAD_LOCAL JanetHandle janet_vm_selfpipe[2];

static void janet_ev_setup_selfpipe(void) {
    if (janet_make_pipe(janet_vm_selfpipe)) {
        JANET_EXIT("failed to initialize self pipe in event loop");
    }
}

/* Handle events from the self pipe inside the event loop */
static void janet_ev_handle_selfpipe(void) {
    JanetSelfPipeEvent response;
    while (read(janet_vm_selfpipe[0], &response, sizeof(response)) > 0) {
        response.cb(response.msg);
        janet_ev_dec_refcount();
    }
}

static void janet_ev_cleanup_selfpipe(void) {
    close(janet_vm_selfpipe[0]);
    close(janet_vm_selfpipe[1]);
}

#endif

#ifdef JANET_WINDOWS

JANET_THREAD_LOCAL HANDLE janet_vm_iocp = NULL;

static JanetTimestamp ts_now(void) {
    return (JanetTimestamp) GetTickCount64();
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (NULL == janet_vm_iocp) janet_panic("could not create io completion port");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    CloseHandle(janet_vm_iocp);
}

JanetListenerState *janet_listen(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    /* Add the handle to the io completion port if not already added */
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    if (!(stream->flags & JANET_STREAM_IOCP)) {
        if (NULL == CreateIoCompletionPort(stream->handle, janet_vm_iocp, (ULONG_PTR) stream, 0)) {
            janet_panic("failed to listen for events");
        }
        stream->flags |= JANET_STREAM_IOCP;
    }
    return state;
}


static void janet_unlisten(JanetListenerState *state) {
    janet_unlisten_impl(state);
}

void janet_loop1_impl(int has_timeout, JanetTimestamp to) {
    ULONG_PTR completionKey = 0;
    DWORD num_bytes_transfered = 0;
    LPOVERLAPPED overlapped;

    /* Calculate how long to wait before timeout */
    uint64_t waittime;
    if (has_timeout) {
        JanetTimestamp now = ts_now();
        if (now > to) {
            waittime = 0;
        } else {
            waittime = (uint64_t)(to - now);
        }
    } else {
        waittime = INFINITE;
    }
    BOOL result = GetQueuedCompletionStatus(janet_vm_iocp, &num_bytes_transfered, &completionKey, &overlapped, (DWORD) waittime);

    if (!result) {
        if (!has_timeout) {
            /* queue emptied */
        }
    } else if (0 == completionKey) {
        /* Custom event */
        JanetSelfPipeEvent *response = (JanetSelfPipeEvent *)(overlapped);
        response->cb(response->msg);
        free(response);
        janet_ev_dec_refcount();
    } else {
        /* Normal event */
        JanetStream *stream = (JanetStream *) completionKey;
        JanetListenerState *state = stream->state;
        while (state != NULL) {
            if (state->tag == overlapped) {
                state->event = overlapped;
                state->bytes = num_bytes_transfered;
                JanetAsyncStatus status = state->machine(state, JANET_ASYNC_EVENT_COMPLETE);
                if (status == JANET_ASYNC_STATUS_DONE) {
                    janet_unlisten(state);
                }
                break;
            } else {
                state = state->_next;
            }
        }
    }
}

#elif defined(JANET_EV_EPOLL)

JANET_THREAD_LOCAL int janet_vm_epoll = 0;
JANET_THREAD_LOCAL int janet_vm_timerfd = 0;
JANET_THREAD_LOCAL int janet_vm_timer_enabled = 0;

static JanetTimestamp ts_now(void) {
    struct timespec now;
    janet_assert(-1 != clock_gettime(CLOCK_MONOTONIC, &now), "failed to get time");
    uint64_t res = 1000 * now.tv_sec;
    res += now.tv_nsec / 1000000;
    return res;
}

static int make_epoll_events(int mask) {
    int events = EPOLLET;
    if (mask & JANET_ASYNC_LISTEN_READ)
        events |= EPOLLIN;
    if (mask & JANET_ASYNC_LISTEN_WRITE)
        events |= EPOLLOUT;
    return events;
}

/* Wait for the next event */
JanetListenerState *janet_listen(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    int is_first = !(stream->state);
    int op = is_first ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    struct epoll_event ev;
    ev.events = make_epoll_events(state->stream->_mask);
    ev.data.ptr = stream;
    int status;
    do {
        status = epoll_ctl(janet_vm_epoll, op, stream->handle, &ev);
    } while (status == -1 && errno == EINTR);
    if (status == -1) {
        janet_unlisten_impl(state);
        janet_panicv(janet_ev_lasterr());
    }
    return state;
}

/* Tell system we are done listening for a certain event */
static void janet_unlisten(JanetListenerState *state) {
    JanetStream *stream = state->stream;
    if (!(stream->flags & JANET_STREAM_CLOSED)) {
        int is_last = (state->_next == NULL && stream->state == state);
        int op = is_last ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
        struct epoll_event ev;
        ev.events = make_epoll_events(stream->_mask & ~state->_mask);
        ev.data.ptr = stream;
        int status;
        do {
            status = epoll_ctl(janet_vm_epoll, op, stream->handle, &ev);
        } while (status == -1 && errno == EINTR);
        if (status == -1) {
            janet_panicv(janet_ev_lasterr());
        }
    }
    /* Destroy state machine and free memory */
    janet_unlisten_impl(state);
}

#define JANET_EPOLL_MAX_EVENTS 64
void janet_loop1_impl(int has_timeout, JanetTimestamp timeout) {
    struct itimerspec its;
    if (janet_vm_timer_enabled || has_timeout) {
        memset(&its, 0, sizeof(its));
        if (has_timeout) {
            its.it_value.tv_sec = timeout / 1000;
            its.it_value.tv_nsec = (timeout % 1000) * 1000000;
        }
        timerfd_settime(janet_vm_timerfd, TFD_TIMER_ABSTIME, &its, NULL);
    }
    janet_vm_timer_enabled = has_timeout;

    /* Poll for events */
    struct epoll_event events[JANET_EPOLL_MAX_EVENTS];
    int ready;
    do {
        ready = epoll_wait(janet_vm_epoll, events, JANET_EPOLL_MAX_EVENTS, -1);
    } while (ready == -1 && errno == EINTR);
    if (ready == -1) {
        JANET_EXIT("failed to poll events");
    }

    /* Step state machines */
    for (int i = 0; i < ready; i++) {
        void *p = events[i].data.ptr;
        if (&janet_vm_timerfd == p) {
            /* Timer expired, ignore */;
        } else if (janet_vm_selfpipe == p) {
            /* Self-pipe handling */
            janet_ev_handle_selfpipe();
        } else {
            JanetStream *stream = p;
            int mask = events[i].events;
            JanetListenerState *state = stream->state;
            state->event = events + i;
            while (NULL != state) {
                JanetListenerState *next_state = state->_next;
                JanetAsyncStatus status1 = JANET_ASYNC_STATUS_NOT_DONE;
                JanetAsyncStatus status2 = JANET_ASYNC_STATUS_NOT_DONE;
                JanetAsyncStatus status3 = JANET_ASYNC_STATUS_NOT_DONE;
                JanetAsyncStatus status4 = JANET_ASYNC_STATUS_NOT_DONE;
                if (mask & EPOLLOUT)
                    status1 = state->machine(state, JANET_ASYNC_EVENT_WRITE);
                if (mask & EPOLLIN)
                    status2 = state->machine(state, JANET_ASYNC_EVENT_READ);
                if (mask & EPOLLERR)
                    status3 = state->machine(state, JANET_ASYNC_EVENT_ERR);
                if (mask & EPOLLHUP)
                    status4 = state->machine(state, JANET_ASYNC_EVENT_HUP);
                if (status1 == JANET_ASYNC_STATUS_DONE ||
                        status2 == JANET_ASYNC_STATUS_DONE ||
                        status3 == JANET_ASYNC_STATUS_DONE ||
                        status4 == JANET_ASYNC_STATUS_DONE)
                    janet_unlisten(state);
                state = next_state;
            }
        }
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_ev_setup_selfpipe();
    janet_vm_epoll = epoll_create1(EPOLL_CLOEXEC);
    janet_vm_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    janet_vm_timer_enabled = 0;
    if (janet_vm_epoll == -1 || janet_vm_timerfd == -1) goto error;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &janet_vm_timerfd;
    if (-1 == epoll_ctl(janet_vm_epoll, EPOLL_CTL_ADD, janet_vm_timerfd, &ev)) goto error;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = janet_vm_selfpipe;
    if (-1 == epoll_ctl(janet_vm_epoll, EPOLL_CTL_ADD, janet_vm_selfpipe[0], &ev)) goto error;
    return;
error:
    JANET_EXIT("failed to initialize event loop");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    close(janet_vm_epoll);
    close(janet_vm_timerfd);
    janet_ev_cleanup_selfpipe();
    janet_vm_epoll = 0;
}

/*
 * End epoll implementation
 */

#else

#include <poll.h>

JANET_THREAD_LOCAL struct pollfd *janet_vm_fds = NULL;

static JanetTimestamp ts_now(void) {
    struct timespec now;
    janet_assert(-1 != clock_gettime(CLOCK_REALTIME, &now), "failed to get time");
    uint64_t res = 1000 * now.tv_sec;
    res += now.tv_nsec / 1000000;
    return res;
}

static int make_poll_events(int mask) {
    int events = 0;
    if (mask & JANET_ASYNC_LISTEN_READ)
        events |= POLLIN;
    if (mask & JANET_ASYNC_LISTEN_WRITE)
        events |= POLLOUT;
    return events;
}

/* Wait for the next event */
JanetListenerState *janet_listen(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    size_t oldsize = janet_vm_listener_cap;
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    size_t newsize = janet_vm_listener_cap;
    if (newsize > oldsize) {
        janet_vm_fds = realloc(janet_vm_fds, (newsize + 1) * sizeof(struct pollfd));
        if (NULL == janet_vm_fds) {
            JANET_OUT_OF_MEMORY;
        }
    }
    struct pollfd ev;
    ev.fd = stream->handle;
    ev.events = make_poll_events(state->stream->_mask);
    ev.revents = 0;
    janet_vm_fds[state->_index + 1] = ev;
    return state;
}

static void janet_unlisten(JanetListenerState *state) {
    janet_vm_fds[state->_index + 1] = janet_vm_fds[janet_vm_listener_count];
    janet_unlisten_impl(state);
}

void janet_loop1_impl(int has_timeout, JanetTimestamp timeout) {
    /* Poll for events */
    int ready;
    do {
        int to = -1;
        if (has_timeout) {
            JanetTimestamp now = ts_now();
            to = now > timeout ? 0 : (int)(timeout - now);
        }
        ready = poll(janet_vm_fds, janet_vm_listener_count + 1, to);
    } while (ready == -1 && errno == EINTR);
    if (ready == -1) {
        JANET_EXIT("failed to poll events");
    }

    /* Check selfpipe */
    if (janet_vm_fds[0].revents & POLLIN) {
        janet_vm_fds[0].revents = 0;
        janet_ev_handle_selfpipe();
    }

    /* Step state machines */
    for (size_t i = 0; i < janet_vm_listener_count; i++) {
        struct pollfd *pfd = janet_vm_fds + i + 1;
        /* Skip fds where nothing interesting happened */
        JanetListenerState *state = janet_vm_listeners[i];
        /* Normal event */
        int mask = pfd->revents;
        JanetAsyncStatus status1 = JANET_ASYNC_STATUS_NOT_DONE;
        JanetAsyncStatus status2 = JANET_ASYNC_STATUS_NOT_DONE;
        JanetAsyncStatus status3 = JANET_ASYNC_STATUS_NOT_DONE;
        JanetAsyncStatus status4 = JANET_ASYNC_STATUS_NOT_DONE;
        state->event = pfd;
        if (mask & POLLOUT)
            status1 = state->machine(state, JANET_ASYNC_EVENT_WRITE);
        if (mask & POLLIN)
            status2 = state->machine(state, JANET_ASYNC_EVENT_READ);
        if (mask & POLLERR)
            status2 = state->machine(state, JANET_ASYNC_EVENT_ERR);
        if (mask & POLLHUP)
            status2 = state->machine(state, JANET_ASYNC_EVENT_HUP);
        if (status1 == JANET_ASYNC_STATUS_DONE ||
                status2 == JANET_ASYNC_STATUS_DONE ||
                status3 == JANET_ASYNC_STATUS_DONE ||
                status4 == JANET_ASYNC_STATUS_DONE)
            janet_unlisten(state);
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm_fds = NULL;
    janet_ev_setup_selfpipe();
    janet_vm_fds = malloc(sizeof(struct pollfd));
    if (NULL == janet_vm_fds) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm_fds[0].fd = janet_vm_selfpipe[0];
    janet_vm_fds[0].events = POLLIN;
    janet_vm_fds[0].revents = 0;
    return;
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    janet_ev_cleanup_selfpipe();
    free(janet_vm_fds);
    janet_vm_fds = NULL;
}

#endif

/*
 * End poll implementation
 */

/*
 * Threaded calls
 */

#ifdef JANET_WINDOWS
static DWORD WINAPI janet_thread_body(LPVOID ptr) {
    JanetEVThreadInit *init = (JanetEVThreadInit *)ptr;
    JanetEVGenericMessage msg = init->msg;
    JanetThreadedSubroutine subr = init->subr;
    JanetThreadedCallback cb = init->cb;
    JanetHandle iocp = init->write_pipe;
    /* Reuse memory from thread init for returning data */
    init->msg = subr(msg);
    init->cb = cb;
    janet_assert(PostQueuedCompletionStatus(iocp,
                                            sizeof(JanetSelfPipeEvent),
                                            0,
                                            (LPOVERLAPPED) init),
                 "failed to post completion event");
    return 0;
}
#else
static void *janet_thread_body(void *ptr) {
    JanetEVThreadInit *init = (JanetEVThreadInit *)ptr;
    JanetEVGenericMessage msg = init->msg;
    JanetThreadedSubroutine subr = init->subr;
    JanetThreadedCallback cb = init->cb;
    int fd = init->write_pipe;
    free(init);
    JanetSelfPipeEvent response;
    response.msg = subr(msg);
    response.cb = cb;
    /* handle a bit of back pressure before giving up. */
    int tries = 4;
    while (tries > 0) {
        int status;
        do {
            status = write(fd, &response, sizeof(response));
        } while (status == -1 && errno == EINTR);
        if (status > 0) break;
        sleep(1);
        tries--;
    }
    return NULL;
}
#endif

void janet_ev_threaded_call(JanetThreadedSubroutine fp, JanetEVGenericMessage arguments, JanetThreadedCallback cb) {
    JanetEVThreadInit *init = malloc(sizeof(JanetEVThreadInit));
    if (NULL == init) {
        JANET_OUT_OF_MEMORY;
    }
    init->msg = arguments;
    init->subr = fp;
    init->cb = cb;

#ifdef JANET_WINDOWS
    init->write_pipe = janet_vm_iocp;
    HANDLE thread_handle = CreateThread(NULL, 0, janet_thread_body, init, 0, NULL);
    if (NULL == thread_handle) {
        free(init);
        janet_panic("failed to create thread");
    }
    CloseHandle(thread_handle); /* detach from thread */
#else
    init->write_pipe = janet_vm_selfpipe[1];
    pthread_t waiter_thread;
    int err = pthread_create(&waiter_thread, NULL, janet_thread_body, init);
    if (err) {
        free(init);
        janet_panicf("%s", strerror(err));
    }
    pthread_detach(waiter_thread);
#endif

    /* Increment ev refcount so we don't quit while waiting for a subprocess */
    janet_ev_inc_refcount();
}

/* Default callback for janet_ev_threaded_await. */
void janet_ev_default_threaded_callback(JanetEVGenericMessage return_value) {
    switch (return_value.tag) {
        default:
        case JANET_EV_TCTAG_NIL:
            janet_schedule(return_value.fiber, janet_wrap_nil());
            break;
        case JANET_EV_TCTAG_INTEGER:
            janet_schedule(return_value.fiber, janet_wrap_integer(return_value.argi));
            break;
        case JANET_EV_TCTAG_STRING:
        case JANET_EV_TCTAG_STRINGF:
            janet_schedule(return_value.fiber, janet_cstringv((const char *) return_value.argp));
            if (return_value.tag == JANET_EV_TCTAG_STRINGF) free(return_value.argp);
            break;
        case JANET_EV_TCTAG_KEYWORD:
            janet_schedule(return_value.fiber, janet_ckeywordv((const char *) return_value.argp));
            break;
        case JANET_EV_TCTAG_ERR_STRING:
        case JANET_EV_TCTAG_ERR_STRINGF:
            janet_cancel(return_value.fiber, janet_cstringv((const char *) return_value.argp));
            if (return_value.tag == JANET_EV_TCTAG_STRINGF) free(return_value.argp);
            break;
        case JANET_EV_TCTAG_ERR_KEYWORD:
            janet_cancel(return_value.fiber, janet_ckeywordv((const char *) return_value.argp));
            break;
    }
    janet_gcunroot(janet_wrap_fiber(return_value.fiber));
}


/* Convenience method for common case */
void janet_ev_threaded_await(JanetThreadedSubroutine fp, int tag, int argi, void *argp) {
    JanetEVGenericMessage arguments;
    arguments.tag = tag;
    arguments.argi = argi;
    arguments.argp = argp;
    arguments.fiber = janet_root_fiber();
    janet_gcroot(janet_wrap_fiber(arguments.fiber));
    janet_ev_threaded_call(fp, arguments, janet_ev_default_threaded_callback);
    janet_await();
}

/*
 * C API helpers for reading and writing from streams.
 * There is some networking code in here as well as generic
 * reading and writing primitives.
 */

void janet_stream_flags(JanetStream *stream, uint32_t flags) {
    if (stream->flags & JANET_STREAM_CLOSED) {
        janet_panic("stream is closed");
    }
    if ((stream->flags & flags) != flags) {
        const char *rmsg = "", *wmsg = "", *amsg = "", *dmsg = "", *smsg = "stream";
        if (flags & JANET_STREAM_READABLE) rmsg = "readable ";
        if (flags & JANET_STREAM_WRITABLE) wmsg = "writable ";
        if (flags & JANET_STREAM_ACCEPTABLE) amsg = "server ";
        if (flags & JANET_STREAM_UDPSERVER) dmsg = "datagram ";
        if (flags & JANET_STREAM_SOCKET) smsg = "socket";
        janet_panicf("bad stream, expected %s%s%s%s%s", rmsg, wmsg, amsg, dmsg, smsg);
    }
}

/* When there is an IO error, we need to be able to convert it to a Janet
 * string to raise a Janet error. */
#ifdef JANET_WINDOWS
#define JANET_EV_CHUNKSIZE 4096
Janet janet_ev_lasterr(void) {
    int code = GetLastError();
    char msgbuf[256];
    msgbuf[0] = '\0';
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  code,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  msgbuf,
                  sizeof(msgbuf),
                  NULL);
    if (!*msgbuf) sprintf(msgbuf, "%d", code);
    char *c = msgbuf;
    while (*c) {
        if (*c == '\n' || *c == '\r') {
            *c = '\0';
            break;
        }
        c++;
    }
    return janet_cstringv(msgbuf);
}
#else
Janet janet_ev_lasterr(void) {
    return janet_cstringv(strerror(errno));
}
#endif

/* State machine for read/recv/recvfrom */

typedef enum {
    JANET_ASYNC_READMODE_READ,
    JANET_ASYNC_READMODE_RECV,
    JANET_ASYNC_READMODE_RECVFROM
} JanetReadMode;

typedef struct {
    JanetListenerState head;
    int32_t bytes_left;
    int32_t bytes_read;
    JanetBuffer *buf;
    int is_chunk;
    JanetReadMode mode;
#ifdef JANET_WINDOWS
    OVERLAPPED overlapped;
#ifdef JANET_NET
    WSABUF wbuf;
    DWORD flags;
    struct sockaddr from;
    int fromlen;
#endif
    uint8_t chunk_buf[JANET_EV_CHUNKSIZE];
#else
    int flags;
#endif
} StateRead;

JanetAsyncStatus ev_machine_read(JanetListenerState *s, JanetAsyncEvent event) {
    StateRead *state = (StateRead *) s;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(janet_wrap_buffer(state->buf));
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_cancel(s->fiber, janet_cstringv("stream closed"));
            return JANET_ASYNC_STATUS_DONE;
#ifdef JANET_WINDOWS
        case JANET_ASYNC_EVENT_COMPLETE: {
            /* Called when read finished */
            state->bytes_read += s->bytes;
            if (state->bytes_read == 0 && (state->mode != JANET_ASYNC_READMODE_RECVFROM)) {
                janet_schedule(s->fiber, janet_wrap_nil());
                return JANET_ASYNC_STATUS_DONE;
            }

            janet_buffer_push_bytes(state->buf, state->chunk_buf, s->bytes);
            state->bytes_left -= s->bytes;

            if (state->bytes_left == 0 || !state->is_chunk || s->bytes == 0) {
                Janet resume_val;
#ifdef JANET_NET
                if (state->mode == JANET_ASYNC_READMODE_RECVFROM) {
                    void *abst = janet_abstract(&janet_address_type, state->fromlen);
                    memcpy(abst, &state->from, state->fromlen);
                    resume_val = janet_wrap_abstract(abst);
                } else
#endif
                {
                    resume_val = janet_wrap_buffer(state->buf);
                }
                janet_schedule(s->fiber, resume_val);
                return JANET_ASYNC_STATUS_DONE;
            }
        }

        /* fallthrough */
        case JANET_ASYNC_EVENT_USER: {
            int32_t chunk_size = state->bytes_left > JANET_EV_CHUNKSIZE ? JANET_EV_CHUNKSIZE : state->bytes_left;
            s->tag = &state->overlapped;
            memset(&(state->overlapped), 0, sizeof(OVERLAPPED));
            int status;
#ifdef JANET_NET
            if (state->mode != JANET_ASYNC_READMODE_READ) {
                state->wbuf.len = (ULONG) chunk_size;
                state->wbuf.buf = state->chunk_buf;
                if (state->mode == JANET_ASYNC_READMODE_RECVFROM) {
                    status = WSARecvFrom((SOCKET) s->stream->handle, &state->wbuf, 1,
                                         NULL, &state->flags, &state->from, &state->fromlen, &state->overlapped, NULL);
                } else {
                    status = WSARecv((SOCKET) s->stream->handle, &state->wbuf, 1,
                                     NULL, &state->flags, &state->overlapped, NULL);
                }
                if (status && (WSA_IO_PENDING != WSAGetLastError())) {
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }
            } else
#endif
            {
                status = ReadFile(s->stream->handle, state->chunk_buf, chunk_size, NULL, &state->overlapped);
                if (!status && (ERROR_IO_PENDING != WSAGetLastError())) {
                    if (WSAGetLastError() == ERROR_BROKEN_PIPE) {
                        janet_schedule(s->fiber, janet_wrap_nil());
                    } else {
                        janet_cancel(s->fiber, janet_ev_lasterr());
                    }
                    return JANET_ASYNC_STATUS_DONE;
                }
            }
        }
        break;
#else
        case JANET_ASYNC_EVENT_ERR:
        case JANET_ASYNC_EVENT_HUP: {
            if (state->bytes_read) {
                janet_schedule(s->fiber, janet_wrap_buffer(state->buf));
            } else {
                janet_schedule(s->fiber, janet_wrap_nil());
            }
            return JANET_ASYNC_STATUS_DONE;
        }
        case JANET_ASYNC_EVENT_READ: {
            JanetBuffer *buffer = state->buf;
            int32_t bytes_left = state->bytes_left;
            int32_t read_limit = bytes_left < 0 ? 4096 : bytes_left;
            janet_buffer_extra(buffer, read_limit);
            ssize_t nread;
#ifdef JANET_NET
            char saddr[256];
            socklen_t socklen = sizeof(saddr);
#endif
            do {
#ifdef JANET_NET
                if (state->mode == JANET_ASYNC_READMODE_RECVFROM) {
                    nread = recvfrom(s->stream->handle, buffer->data + buffer->count, read_limit, state->flags,
                                     (struct sockaddr *)&saddr, &socklen);
                } else if (state->mode == JANET_ASYNC_READMODE_RECV) {
                    nread = recv(s->stream->handle, buffer->data + buffer->count, read_limit, state->flags);
                } else
#endif
                {
                    nread = read(s->stream->handle, buffer->data + buffer->count, read_limit);
                }
            } while (nread == -1 && errno == EINTR);

            /* Check for errors - special case errors that can just be waited on to fix */
            if (nread == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return JANET_ASYNC_STATUS_NOT_DONE;
                }
                /* In stream protocols, a pipe error is end of stream */
                if (errno == EPIPE && (state->mode != JANET_ASYNC_READMODE_RECVFROM)) {
                    nread = 0;
                } else {
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }
            }

            /* Only allow 0-length packets in recv-from. In stream protocols, a zero length packet is EOS. */
            state->bytes_read += nread;
            if (state->bytes_read == 0 && (state->mode != JANET_ASYNC_READMODE_RECVFROM)) {
                janet_schedule(s->fiber, janet_wrap_nil());
                return JANET_ASYNC_STATUS_DONE;
            }

            /* Increment buffer counts */
            buffer->count += nread;
            bytes_left -= nread;
            state->bytes_left = bytes_left;

            /* Resume if done */
            if (!state->is_chunk || bytes_left == 0 || nread == 0) {
                Janet resume_val;
#ifdef JANET_NET
                if (state->mode == JANET_ASYNC_READMODE_RECVFROM) {
                    void *abst = janet_abstract(&janet_address_type, socklen);
                    memcpy(abst, &saddr, socklen);
                    resume_val = janet_wrap_abstract(abst);
                } else
#endif
                {
                    resume_val = janet_wrap_buffer(buffer);
                }
                janet_schedule(s->fiber, resume_val);
                return JANET_ASYNC_STATUS_DONE;
            }
        }
        break;
#endif
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

static void janet_ev_read_generic(JanetStream *stream, JanetBuffer *buf, int32_t nbytes, int is_chunked, JanetReadMode mode, int flags) {
    StateRead *state = (StateRead *) janet_listen(stream, ev_machine_read,
                       JANET_ASYNC_LISTEN_READ, sizeof(StateRead), NULL);
    state->is_chunk = is_chunked;
    state->buf = buf;
    state->bytes_left = nbytes;
    state->bytes_read = 0;
    state->mode = mode;
#ifdef JANET_WINDOWS
    ev_machine_read((JanetListenerState *) state, JANET_ASYNC_EVENT_USER);
    state->flags = (DWORD) flags;
#else
    state->flags = flags;
#endif
}

void janet_ev_read(JanetStream *stream, JanetBuffer *buf, int32_t nbytes) {
    janet_ev_read_generic(stream, buf, nbytes, 0, JANET_ASYNC_READMODE_READ, 0);
}
void janet_ev_readchunk(JanetStream *stream, JanetBuffer *buf, int32_t nbytes) {
    janet_ev_read_generic(stream, buf, nbytes, 1, JANET_ASYNC_READMODE_READ, 0);
}
#ifdef JANET_NET
void janet_ev_recv(JanetStream *stream, JanetBuffer *buf, int32_t nbytes, int flags) {
    janet_ev_read_generic(stream, buf, nbytes, 0, JANET_ASYNC_READMODE_RECV, flags);
}
void janet_ev_recvchunk(JanetStream *stream, JanetBuffer *buf, int32_t nbytes, int flags) {
    janet_ev_read_generic(stream, buf, nbytes, 1, JANET_ASYNC_READMODE_RECV, flags);
}
void janet_ev_recvfrom(JanetStream *stream, JanetBuffer *buf, int32_t nbytes, int flags) {
    janet_ev_read_generic(stream, buf, nbytes, 0, JANET_ASYNC_READMODE_RECVFROM, flags);
}
#endif

/*
 * State machine for write/send/send-to
 */

typedef enum {
    JANET_ASYNC_WRITEMODE_WRITE,
    JANET_ASYNC_WRITEMODE_SEND,
    JANET_ASYNC_WRITEMODE_SENDTO
} JanetWriteMode;

typedef struct {
    JanetListenerState head;
    union {
        JanetBuffer *buf;
        const uint8_t *str;
    } src;
    int is_buffer;
    JanetWriteMode mode;
    void *dest_abst;
#ifdef JANET_WINDOWS
    OVERLAPPED overlapped;
#ifdef JANET_NET
    WSABUF wbuf;
    DWORD flags;
#endif
#else
    int flags;
    int32_t start;
#endif
} StateWrite;

JanetAsyncStatus ev_machine_write(JanetListenerState *s, JanetAsyncEvent event) {
    StateWrite *state = (StateWrite *) s;
    switch (event) {
        default:
            break;
        case JANET_ASYNC_EVENT_MARK:
            janet_mark(state->is_buffer
                       ? janet_wrap_buffer(state->src.buf)
                       : janet_wrap_string(state->src.str));
            if (state->mode == JANET_ASYNC_WRITEMODE_SENDTO) {
                janet_mark(janet_wrap_abstract(state->dest_abst));
            }
            break;
        case JANET_ASYNC_EVENT_CLOSE:
            janet_cancel(s->fiber, janet_cstringv("stream closed"));
            return JANET_ASYNC_STATUS_DONE;
#ifdef JANET_WINDOWS
        case JANET_ASYNC_EVENT_COMPLETE: {
            /* Called when write finished */
            if (s->bytes == 0 && (state->mode != JANET_ASYNC_WRITEMODE_SENDTO)) {
                janet_cancel(s->fiber, janet_cstringv("disconnect"));
                return JANET_ASYNC_STATUS_DONE;
            }

            janet_schedule(s->fiber, janet_wrap_nil());
            return JANET_ASYNC_STATUS_DONE;
        }
        break;
        case JANET_ASYNC_EVENT_USER: {
            /* Begin write */
            int32_t len;
            const uint8_t *bytes;
            if (state->is_buffer) {
                /* If buffer, convert to string. */
                /* TODO - be more efficient about this */
                JanetBuffer *buffer = state->src.buf;
                JanetString str = janet_string(buffer->data, buffer->count);
                bytes = str;
                len = buffer->count;
                state->is_buffer = 0;
                state->src.str = str;
            } else {
                bytes = state->src.str;
                len = janet_string_length(bytes);
            }
            s->tag = &state->overlapped;
            memset(&(state->overlapped), 0, sizeof(WSAOVERLAPPED));

            int status;
#ifdef JANET_NET
            if (state->mode != JANET_ASYNC_WRITEMODE_WRITE) {
                SOCKET sock = (SOCKET) s->stream->handle;
                state->wbuf.buf = (char *) bytes;
                state->wbuf.len = len;
                if (state->mode == JANET_ASYNC_WRITEMODE_SENDTO) {
                    const struct sockaddr *to = state->dest_abst;
                    int tolen = (int) janet_abstract_size((void *) to);
                    status = WSASendTo(sock, &state->wbuf, 1, NULL, state->flags, to, tolen, &state->overlapped, NULL);
                } else {
                    status = WSASend(sock, &state->wbuf, 1, NULL, state->flags, &state->overlapped, NULL);
                }
                if (status && (WSA_IO_PENDING != WSAGetLastError())) {
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }
            } else
#endif
            {
                status = WriteFile(s->stream->handle, bytes, len, NULL, &state->overlapped);
                if (!status && (ERROR_IO_PENDING != WSAGetLastError())) {
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }
            }
        }
        break;
#else
        case JANET_ASYNC_EVENT_ERR:
            janet_cancel(s->fiber, janet_cstringv("stream err"));
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_HUP:
            janet_cancel(s->fiber, janet_cstringv("stream hup"));
            return JANET_ASYNC_STATUS_DONE;
        case JANET_ASYNC_EVENT_WRITE: {
            int32_t start, len;
            const uint8_t *bytes;
            start = state->start;
            if (state->is_buffer) {
                JanetBuffer *buffer = state->src.buf;
                bytes = buffer->data;
                len = buffer->count;
            } else {
                bytes = state->src.str;
                len = janet_string_length(bytes);
            }
            ssize_t nwrote = 0;
            if (start < len) {
                int32_t nbytes = len - start;
                void *dest_abst = state->dest_abst;
                do {
#ifdef JANET_NET
                    if (state->mode == JANET_ASYNC_WRITEMODE_SENDTO) {
                        nwrote = sendto(s->stream->handle, bytes + start, nbytes, state->flags,
                                        (struct sockaddr *) dest_abst, janet_abstract_size(dest_abst));
                    } else if (state->mode == JANET_ASYNC_WRITEMODE_SEND) {
                        nwrote = send(s->stream->handle, bytes + start, nbytes, state->flags);
                    } else
#endif
                    {
                        nwrote = write(s->stream->handle, bytes + start, nbytes);
                    }
                } while (nwrote == -1 && errno == EINTR);

                /* Handle write errors */
                if (nwrote == -1) {
                    if (errno == EAGAIN || errno  == EWOULDBLOCK) break;
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }

                /* Unless using datagrams, empty message is a disconnect */
                if (nwrote == 0 && !dest_abst) {
                    janet_cancel(s->fiber, janet_cstringv("disconnect"));
                    return JANET_ASYNC_STATUS_DONE;
                }

                if (nwrote > 0) {
                    start += nwrote;
                } else {
                    start = len;
                }
            }
            state->start = start;
            if (start >= len) {
                janet_schedule(s->fiber, janet_wrap_nil());
                return JANET_ASYNC_STATUS_DONE;
            }
            break;
        }
        break;
#endif
    }
    return JANET_ASYNC_STATUS_NOT_DONE;
}

static void janet_ev_write_generic(JanetStream *stream, void *buf, void *dest_abst, JanetWriteMode mode, int is_buffer, int flags) {
    StateWrite *state = (StateWrite *) janet_listen(stream, ev_machine_write,
                        JANET_ASYNC_LISTEN_WRITE, sizeof(StateWrite), NULL);
    state->is_buffer = is_buffer;
    state->src.buf = buf;
    state->dest_abst = dest_abst;
    state->mode = mode;
#ifdef JANET_WINDOWS
    state->flags = (DWORD) flags;
    ev_machine_write((JanetListenerState *) state, JANET_ASYNC_EVENT_USER);
#else
    state->start = 0;
    state->flags = flags;
#endif
}


void janet_ev_write_buffer(JanetStream *stream, JanetBuffer *buf) {
    janet_ev_write_generic(stream, buf, NULL, JANET_ASYNC_WRITEMODE_WRITE, 1, 0);
}

void janet_ev_write_string(JanetStream *stream, JanetString str) {
    janet_ev_write_generic(stream, (void *) str, NULL, JANET_ASYNC_WRITEMODE_WRITE, 0, 0);
}

#ifdef JANET_NET
void janet_ev_send_buffer(JanetStream *stream, JanetBuffer *buf, int flags) {
    janet_ev_write_generic(stream, buf, NULL, JANET_ASYNC_WRITEMODE_SEND, 1, flags);
}

void janet_ev_send_string(JanetStream *stream, JanetString str, int flags) {
    janet_ev_write_generic(stream, (void *) str, NULL, JANET_ASYNC_WRITEMODE_SEND, 0, flags);
}

void janet_ev_sendto_buffer(JanetStream *stream, JanetBuffer *buf, void *dest, int flags) {
    janet_ev_write_generic(stream, buf, dest, JANET_ASYNC_WRITEMODE_SENDTO, 1, flags);
}

void janet_ev_sendto_string(JanetStream *stream, JanetString str, void *dest, int flags) {
    janet_ev_write_generic(stream, (void *) str, dest, JANET_ASYNC_WRITEMODE_SENDTO, 0, flags);
}
#endif

/* For a pipe ID */
#ifdef JANET_WINDOWS
static volatile long PipeSerialNumber;
#endif

int janet_make_pipe(JanetHandle handles[2]) {
#ifdef JANET_WINDOWS
    /*
     * On windows, the built in CreatePipe function doesn't support overlapped IO
     * so we lift from the windows source code and modify for our own version.
     */
    JanetHandle rhandle, whandle;
    UCHAR PipeNameBuffer[MAX_PATH];
    sprintf(PipeNameBuffer,
            "\\\\.\\Pipe\\JanetPipeFile.%08x.%08x",
            GetCurrentProcessId(),
            InterlockedIncrement(&PipeSerialNumber));
    rhandle = CreateNamedPipeA(
                  PipeNameBuffer,
                  PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                  PIPE_TYPE_BYTE | PIPE_NOWAIT,
                  1,             /* Number of pipes */
                  4096,          /* Out buffer size */
                  4096,          /* In buffer size */
                  120 * 1000,    /* Timeout in ms */
                  NULL);
    if (!rhandle) return -1;
    whandle = CreateFileA(
                  PipeNameBuffer,
                  GENERIC_WRITE,
                  0,
                  NULL,
                  OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                  NULL);
    if (whandle == INVALID_HANDLE_VALUE) {
        CloseHandle(rhandle);
        return -1;
    }
    handles[0] = rhandle;
    handles[1] = whandle;
    return 0;
#else
    if (pipe(handles)) return -1;
    if (fcntl(handles[0], F_SETFL, O_NONBLOCK)) goto error;
    if (fcntl(handles[1], F_SETFL, O_NONBLOCK)) goto error;
    return 0;
error:
    close(handles[0]);
    close(handles[1]);
    return -1;
#endif
}

/* C functions */

static Janet cfun_ev_go(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet value = argc == 2 ? argv[1] : janet_wrap_nil();
    JanetChannel *channel = janet_optabstract(argv, argc, 2, &ChannelAT,
            janet_vm_root_fiber->supervisor_channel);
    fiber->supervisor_channel = channel;
    janet_schedule(fiber, value);
    return argv[0];
}

static Janet cfun_ev_call(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    JanetFunction *fn = janet_getfunction(argv, 0);
    JanetFiber *fiber = janet_fiber(fn, 64, argc - 1, argv + 1);
    if (NULL == fiber) janet_panicf("invalid arity to function %v", argv[0]);
    fiber->env = janet_table(0);
    fiber->env->proto = janet_current_fiber()->env;
    fiber->supervisor_channel = janet_vm_root_fiber->supervisor_channel;
    janet_schedule(fiber, janet_wrap_nil());
    return janet_wrap_fiber(fiber);
}

JANET_NO_RETURN void janet_sleep_await(double sec) {
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = janet_vm_root_fiber;
    to.is_error = 0;
    to.sched_id = to.fiber->sched_id;
    to.curr_fiber = NULL;
    add_timeout(to);
    janet_await();
}

static Janet cfun_ev_sleep(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    double sec = janet_getnumber(argv, 0);
    janet_sleep_await(sec);
}

static Janet cfun_ev_deadline(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    double sec = janet_getnumber(argv, 0);
    JanetFiber *tocancel = janet_optfiber(argv, argc, 1, janet_vm_root_fiber);
    JanetFiber *tocheck = janet_optfiber(argv, argc, 2, janet_vm_fiber);
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = tocancel;
    to.curr_fiber = tocheck;
    to.is_error = 0;
    to.sched_id = to.fiber->sched_id;
    add_timeout(to);
    return janet_wrap_fiber(tocancel);
}

static Janet cfun_ev_cancel(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet err = argv[1];
    janet_cancel(fiber, err);
    return argv[0];
}

Janet janet_cfun_stream_close(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_close(stream);
    return argv[0];
}

Janet janet_cfun_stream_read(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_READABLE);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (janet_keyeq(argv[1], "all")) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_readchunk(stream, buffer, -1);
    } else {
        int32_t n = janet_getnat(argv, 1);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_read(stream, buffer, n);
    }
    janet_await();
}

Janet janet_cfun_stream_chunk(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_READABLE);
    int32_t n = janet_getnat(argv, 1);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (to != INFINITY) janet_addtimeout(to);
    janet_ev_readchunk(stream, buffer, n);
    janet_await();
}

Janet janet_cfun_stream_write(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_WRITABLE);
    double to = janet_optnumber(argv, argc, 2, INFINITY);
    if (janet_checktype(argv[1], JANET_BUFFER)) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_write_buffer(stream, janet_getbuffer(argv, 1));
    } else {
        JanetByteView bytes = janet_getbytes(argv, 1);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_write_string(stream, bytes.bytes);
    }
    janet_await();
}

static const JanetReg ev_cfuns[] = {
    {
        "ev/call", cfun_ev_call,
        JDOC("(ev/call fn & args)\n\n"
             "Call a function asynchronously. Returns a fiber that is scheduled to "
             "run the function.")
    },
    {
        "ev/go", cfun_ev_go,
        JDOC("(ev/go fiber &opt value chan)\n\n"
             "Put a fiber on the event loop to be resumed later. Optionally pass "
             "a value to resume with, otherwise resumes with nil. If chan is provided, "
             "the fiber will push itself to the channel upon completion or error. Returns the fiber.")
    },
    {
        "ev/sleep", cfun_ev_sleep,
        JDOC("(ev/sleep sec)\n\n"
             "Suspend the current fiber for sec seconds without blocking the event loop.")
    },
    {
        "ev/deadline", cfun_ev_deadline,
        JDOC("(ev/deadline sec &opt tocancel tocheck)\n\n"
             "Set a deadline for a fiber `tocheck`. If `tocheck` is not finished after `sec` seconds, "
             "`tocancel` will be canceled as with `ev/cancel`. "
             "If `tocancel` and `tocheck` are not given, they default to `(fiber/root)` and "
             "`(fiber/current)` respectively. Returns `tocancel`.")
    },
    {
        "ev/chan", cfun_channel_new,
        JDOC("(ev/chan &opt capacity)\n\n"
             "Create a new channel. capacity is the number of values to queue before "
             "blocking writers, defaults to 0 if not provided. Returns a new channel.")
    },
    {
        "ev/give", cfun_channel_push,
        JDOC("(ev/give channel value)\n\n"
             "Write a value to a channel, suspending the current fiber if the channel is full.")
    },
    {
        "ev/take", cfun_channel_pop,
        JDOC("(ev/take channel)\n\n"
             "Read from a channel, suspending the current fiber if no value is available.")
    },
    {
        "ev/full", cfun_channel_full,
        JDOC("(ev/full channel)\n\n"
             "Check if a channel is full or not.")
    },
    {
        "ev/capacity", cfun_channel_capacity,
        JDOC("(ev/capacity channel)\n\n"
             "Get the number of items a channel will store before blocking writers.")
    },
    {
        "ev/count", cfun_channel_count,
        JDOC("(ev/count channel)\n\n"
             "Get the number of items currently waiting in a channel.")
    },
    {
        "ev/cancel", cfun_ev_cancel,
        JDOC("(ev/cancel fiber err)\n\n"
             "Cancel a suspended fiber in the event loop. Differs from cancel in that it returns the canceled fiber immediately")
    },
    {
        "ev/select", cfun_channel_choice,
        JDOC("(ev/select & clauses)\n\n"
             "Block until the first of several channel operations occur. Returns a tuple of the form [:give chan] or [:take chan x], where "
             "a :give tuple is the result of a write and :take tuple is the result of a write. Each clause must be either a channel (for "
             "a channel take operation) or a tuple [channel x] for a channel give operation. Operations are tried in order, such that the first "
             "clauses will take precedence over later clauses.")
    },
    {
        "ev/rselect", cfun_channel_rchoice,
        JDOC("(ev/rselect & clauses)\n\n"
             "Similar to ev/choice, but will try clauses in a random order for fairness.")
    },
    {
        "ev/close", janet_cfun_stream_close,
        JDOC("(ev/close stream)\n\n"
             "Close a stream. This should be the same as calling (:close stream) for all streams.")
    },
    {
        "ev/read", janet_cfun_stream_read,
        JDOC("(ev/read stream n &opt buffer timeout)\n\n"
             "Read up to n bytes into a buffer asynchronously from a stream. `n` can also be the keyword "
             "`:all` to read into the buffer until end of stream. "
             "Optionally provide a buffer to write into "
             "as well as a timeout in seconds after which to cancel the operation and raise an error. "
             "Returns the buffer if the read was successful or nil if end-of-stream reached. Will raise an "
             "error if there are problems with the IO operation.")
    },
    {
        "ev/chunk", janet_cfun_stream_chunk,
        JDOC("(ev/chunk stream n &opt buffer timeout)\n\n"
             "Same as ev/read, but will not return early if less than n bytes are available. If an end of "
             "stream is reached, will also return early with the collected bytes.")
    },
    {
        "ev/write", janet_cfun_stream_write,
        JDOC("(ev/write stream data &opt timeout)\n\n"
             "Write data to a stream, suspending the current fiber until the write "
             "completes. Takes an optional timeout in seconds, after which will return nil. "
             "Returns nil, or raises an error if the write failed.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_ev(JanetTable *env) {
    janet_core_cfuns(env, NULL, ev_cfuns);
}

#endif

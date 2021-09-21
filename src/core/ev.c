/* The above copyright notice and this permission notice shall be included in
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
#ifdef JANET_EV_KQUEUE
#include <sys/event.h>
#endif
#endif

typedef struct {
    JanetVM *thread;
    JanetFiber *fiber;
    uint32_t sched_id;
    enum {
        JANET_CP_MODE_READ,
        JANET_CP_MODE_WRITE,
        JANET_CP_MODE_CHOICE_READ,
        JANET_CP_MODE_CHOICE_WRITE,
        JANET_CP_MODE_CLOSE
    } mode;
} JanetChannelPending;

typedef struct {
    JanetQueue items;
    JanetQueue read_pending;
    JanetQueue write_pending;
    int32_t limit;
    int closed;
    int is_threaded;
    JanetOSMutex lock;
} JanetChannel;

typedef struct {
    JanetFiber *fiber;
    Janet value;
    JanetSignal sig;
} JanetTask;

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

#define JANET_MAX_Q_CAPACITY 0x7FFFFFF

static void janet_q_init(JanetQueue *q) {
    q->data = NULL;
    q->head = 0;
    q->tail = 0;
    q->capacity = 0;
}

static void janet_q_deinit(JanetQueue *q) {
    janet_free(q->data);
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
        q->data = janet_realloc(q->data, itemsize * newcap);
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

/* Forward declaration */
static void janet_unlisten(JanetListenerState *state, int is_gc);

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
    if (janet_vm.tq_count == 0) return 0;
    *out = janet_vm.tq[0];
    return 1;
}

/* Remove the next timeout from the priority queue */
static void pop_timeout(size_t index) {
    if (janet_vm.tq_count <= index) return;
    janet_vm.tq[index] = janet_vm.tq[--janet_vm.tq_count];
    for (;;) {
        size_t left = (index << 1) + 1;
        size_t right = left + 1;
        size_t smallest = index;
        if (left < janet_vm.tq_count &&
                (janet_vm.tq[left].when < janet_vm.tq[smallest].when))
            smallest = left;
        if (right < janet_vm.tq_count &&
                (janet_vm.tq[right].when < janet_vm.tq[smallest].when))
            smallest = right;
        if (smallest == index) return;
        JanetTimeout temp = janet_vm.tq[index];
        janet_vm.tq[index] = janet_vm.tq[smallest];
        janet_vm.tq[smallest] = temp;
        index = smallest;
    }
}

/* Add a timeout to the timeout min heap */
static void add_timeout(JanetTimeout to) {
    size_t oldcount = janet_vm.tq_count;
    size_t newcount = oldcount + 1;
    if (newcount > janet_vm.tq_capacity) {
        size_t newcap = 2 * newcount;
        JanetTimeout *tq = janet_realloc(janet_vm.tq, newcap * sizeof(JanetTimeout));
        if (NULL == tq) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.tq = tq;
        janet_vm.tq_capacity = newcap;
    }
    /* Append */
    janet_vm.tq_count = (int32_t) newcount;
    janet_vm.tq[oldcount] = to;
    /* Heapify */
    size_t index = oldcount;
    while (index > 0) {
        size_t parent = (index - 1) >> 1;
        if (janet_vm.tq[parent].when <= janet_vm.tq[index].when) break;
        /* Swap */
        JanetTimeout tmp = janet_vm.tq[index];
        janet_vm.tq[index] = janet_vm.tq[parent];
        janet_vm.tq[parent] = tmp;
        /* Next */
        index = parent;
    }
}

/* Create a new event listener */
static JanetListenerState *janet_listen_impl(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    if (stream->_mask & mask) {
        janet_panic("cannot listen for duplicate event on stream");
    }
    if (janet_vm.root_fiber->waiting != NULL) {
        janet_panic("current fiber is already waiting for event");
    }
    if (size < sizeof(JanetListenerState))
        size = sizeof(JanetListenerState);
    JanetListenerState *state = janet_malloc(size);
    if (NULL == state) {
        JANET_OUT_OF_MEMORY;
    }
    state->machine = behavior;
    state->fiber = janet_vm.root_fiber;
    janet_vm.root_fiber->waiting = state;
    state->stream = stream;
    state->_mask = mask;
    stream->_mask |= mask;
    state->_next = stream->state;
    stream->state = state;

    /* Keep track of a listener for GC purposes */
    int resize = janet_vm.listener_cap == janet_vm.listener_count;
    if (resize) {
        size_t newcap = janet_vm.listener_count ? janet_vm.listener_cap * 2 : 16;
        janet_vm.listeners = janet_realloc(janet_vm.listeners, newcap * sizeof(JanetListenerState *));
        if (NULL == janet_vm.listeners) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm.listener_cap = newcap;
    }
    size_t index = janet_vm.listener_count++;
    janet_vm.listeners[index] = state;
    state->_index = index;

    /* Emit INIT event for convenience */
    state->event = user;
    state->machine(state, JANET_ASYNC_EVENT_INIT);
    return state;
}

/* Indicate we are no longer listening for an event. This
 * frees the memory of the state machine as well. */
static void janet_unlisten_impl(JanetListenerState *state, int is_gc) {
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
    if (!is_gc) {
        JanetFiber *fiber = state->fiber;
        if (NULL != fiber && fiber->waiting == state) {
            fiber->waiting = NULL;
        }
    }
    /* Untrack a listener for gc purposes */
    size_t index = state->_index;
    janet_vm.listeners[index] = janet_vm.listeners[--janet_vm.listener_count];
    janet_vm.listeners[index]->_index = index;
    janet_free(state);
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
    return stream;
}

/* Close a stream */
static void janet_stream_close_impl(JanetStream *stream, int is_gc) {
    if (stream->flags & JANET_STREAM_CLOSED) return;
    JanetListenerState *state = stream->state;
    while (NULL != state) {
        if (!is_gc) {
            state->machine(state, JANET_ASYNC_EVENT_CLOSE);
        }
        JanetListenerState *next_state = state->_next;
        janet_unlisten(state, is_gc);
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

void janet_stream_close(JanetStream *stream) {
    janet_stream_close_impl(stream, 0);
}


/* Called to clean up a stream */
static int janet_stream_gc(void *p, size_t s) {
    (void) s;
    JanetStream *stream = (JanetStream *)p;
    janet_stream_close_impl(stream, 1);
    return 0;
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
    HANDLE duph = INVALID_HANDLE_VALUE;
    if (s->flags & JANET_STREAM_SOCKET) {
        duph = s->handle;
    } else {
        DuplicateHandle(
            GetCurrentProcess(),
            s->handle,
            GetCurrentProcess(),
            &duph,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS);
    }
    janet_marshal_int64(ctx, (int64_t)(duph));
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

static Janet janet_stream_next(void *p, Janet key) {
    JanetStream *stream = (JanetStream *)p;
    return janet_nextmethod(stream->methods, key);
}

const JanetAbstractType janet_stream_type = {
    "core/stream",
    janet_stream_gc,
    janet_stream_mark,
    janet_stream_getter,
    NULL,
    janet_stream_marshal,
    janet_stream_unmarshal,
    NULL,
    NULL,
    NULL,
    janet_stream_next,
    JANET_ATEND_NEXT
};

/* Register a fiber to resume with value */
void janet_schedule_signal(JanetFiber *fiber, Janet value, JanetSignal sig) {
    if (fiber->flags & JANET_FIBER_FLAG_SCHEDULED) return;
    fiber->flags |= JANET_FIBER_FLAG_SCHEDULED;
    fiber->sched_id++;
    JanetTask t = { fiber, value, sig };
    janet_q_push(&janet_vm.spawn, &t, sizeof(t));
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
        janet_unlisten(fiber->waiting, 0);
    }
}

/* Mark all pending tasks */
void janet_ev_mark(void) {

    /* Pending tasks */
    JanetTask *tasks = janet_vm.spawn.data;
    if (janet_vm.spawn.head <= janet_vm.spawn.tail) {
        for (int32_t i = janet_vm.spawn.head; i < janet_vm.spawn.tail; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
    } else {
        for (int32_t i = janet_vm.spawn.head; i < janet_vm.spawn.capacity; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
        for (int32_t i = 0; i < janet_vm.spawn.tail; i++) {
            janet_mark(janet_wrap_fiber(tasks[i].fiber));
            janet_mark(tasks[i].value);
        }
    }

    /* Pending timeouts */
    for (size_t i = 0; i < janet_vm.tq_count; i++) {
        janet_mark(janet_wrap_fiber(janet_vm.tq[i].fiber));
        if (janet_vm.tq[i].curr_fiber != NULL) {
            janet_mark(janet_wrap_fiber(janet_vm.tq[i].curr_fiber));
        }
    }

    /* Pending listeners */
    for (size_t i = 0; i < janet_vm.listener_count; i++) {
        JanetListenerState *state = janet_vm.listeners[i];
        if (NULL != state->fiber) {
            janet_mark(janet_wrap_fiber(state->fiber));
        }
        janet_stream_mark(state->stream, sizeof(JanetStream));
        (state->machine)(state, JANET_ASYNC_EVENT_MARK);
    }
}

static int janet_channel_push(JanetChannel *channel, Janet x, int mode);
static int janet_channel_pop(JanetChannel *channel, Janet *item, int is_choice);

static Janet make_supervisor_event(const char *name, JanetFiber *fiber, int threaded) {
    Janet tup[2];
    tup[0] = janet_ckeywordv(name);
    tup[1] = threaded ? fiber->last_value : janet_wrap_fiber(fiber) ;
    return janet_wrap_tuple(janet_tuple_n(tup, 2));
}

/* Common init code */
void janet_ev_init_common(void) {
    janet_q_init(&janet_vm.spawn);
    janet_vm.listener_count = 0;
    janet_vm.listener_cap = 0;
    janet_vm.listeners = NULL;
    janet_vm.tq = NULL;
    janet_vm.tq_count = 0;
    janet_vm.tq_capacity = 0;
    janet_table_init_raw(&janet_vm.threaded_abstracts, 0);
    janet_rng_seed(&janet_vm.ev_rng, 0);
}

/* Common deinit code */
void janet_ev_deinit_common(void) {
    janet_q_deinit(&janet_vm.spawn);
    janet_free(janet_vm.tq);
    janet_free(janet_vm.listeners);
    janet_vm.listeners = NULL;
    janet_table_deinit(&janet_vm.threaded_abstracts);
}

/* Short hand to yield to event loop */
void janet_await(void) {
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

/* Set timeout for the current root fiber */
void janet_addtimeout(double sec) {
    JanetFiber *fiber = janet_vm.root_fiber;
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = fiber;
    to.curr_fiber = NULL;
    to.sched_id = fiber->sched_id;
    to.is_error = 1;
    add_timeout(to);
}

void janet_ev_inc_refcount(void) {
    janet_vm.extra_listeners++;
}

void janet_ev_dec_refcount(void) {
    janet_vm.extra_listeners--;
}

/* Channels */

#define JANET_MAX_CHANNEL_CAPACITY 0xFFFFFF

static inline int janet_chan_is_threaded(JanetChannel *chan) {
    return chan->is_threaded;
}

static int janet_chan_pack(JanetChannel *chan, Janet *x) {
    if (!janet_chan_is_threaded(chan)) return 0;
    switch (janet_type(*x)) {
        default: {
            JanetBuffer *buf = janet_malloc(sizeof(JanetBuffer));
            if (NULL == buf) {
                JANET_OUT_OF_MEMORY;
            }
            janet_buffer_init(buf, 10);
            janet_marshal(buf, *x, NULL, JANET_MARSHAL_UNSAFE);
            *x = janet_wrap_buffer(buf);
            return 0;
        }
        case JANET_NIL:
        case JANET_NUMBER:
        case JANET_POINTER:
        case JANET_BOOLEAN:
        case JANET_CFUNCTION:
            return 0;
    }
}

static int janet_chan_unpack(JanetChannel *chan, Janet *x, int is_cleanup) {
    if (!janet_chan_is_threaded(chan)) return 0;
    switch (janet_type(*x)) {
        default:
            return 1;
        case JANET_BUFFER: {
            JanetBuffer *buf = janet_unwrap_buffer(*x);
            int flags = is_cleanup ? JANET_MARSHAL_UNSAFE : (JANET_MARSHAL_UNSAFE | JANET_MARSHAL_DECREF);
            *x = janet_unmarshal(buf->data, buf->count, flags, NULL, NULL);
            janet_buffer_deinit(buf);
            janet_free(buf);
            return 0;
        }
        case JANET_NIL:
        case JANET_NUMBER:
        case JANET_POINTER:
        case JANET_BOOLEAN:
        case JANET_CFUNCTION:
            return 0;
    }
}

static void janet_chan_init(JanetChannel *chan, int32_t limit, int threaded) {
    chan->limit = limit;
    chan->closed = 0;
    chan->is_threaded = threaded;
    janet_q_init(&chan->items);
    janet_q_init(&chan->read_pending);
    janet_q_init(&chan->write_pending);
    janet_os_mutex_init(&chan->lock);
}

static void janet_chan_deinit(JanetChannel *chan) {
    janet_q_deinit(&chan->read_pending);
    janet_q_deinit(&chan->write_pending);
    if (janet_chan_is_threaded(chan)) {
        Janet item;
        while (!janet_q_pop(&chan->items, &item, sizeof(item))) {
            janet_chan_unpack(chan, &item, 1);
        }
    }
    janet_q_deinit(&chan->items);
    janet_os_mutex_deinit(&chan->lock);
}

static void janet_chan_lock(JanetChannel *chan) {
    if (!janet_chan_is_threaded(chan)) return;
    janet_os_mutex_lock(&chan->lock);
}

static void janet_chan_unlock(JanetChannel *chan) {
    if (!janet_chan_is_threaded(chan)) return;
    janet_os_mutex_unlock(&chan->lock);
}

/*
 * Janet Channel abstract type
 */

static Janet janet_wrap_channel(JanetChannel *channel) {
    return janet_wrap_abstract(channel);
}

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
    tup[1] = janet_wrap_channel(channel);
    return janet_wrap_tuple(janet_tuple_end(tup));
}

static Janet make_read_result(JanetChannel *channel, Janet x) {
    Janet *tup = janet_tuple_begin(3);
    tup[0] = janet_ckeywordv("take");
    tup[1] = janet_wrap_channel(channel);
    tup[2] = x;
    return janet_wrap_tuple(janet_tuple_end(tup));
}

static Janet make_close_result(JanetChannel *channel) {
    Janet *tup = janet_tuple_begin(2);
    tup[0] = janet_ckeywordv("close");
    tup[1] = janet_wrap_channel(channel);
    return janet_wrap_tuple(janet_tuple_end(tup));
}

/* Callback to use for scheduling a fiber from another thread. */
static void janet_thread_chan_cb(JanetEVGenericMessage msg) {
    uint32_t sched_id = (uint32_t) msg.argi;
    JanetFiber *fiber = msg.fiber;
    int mode = msg.tag;
    JanetChannel *channel = (JanetChannel *) msg.argp;
    Janet x = msg.argj;
    janet_ev_dec_refcount();
    if (fiber->sched_id == sched_id) {
        if (mode == JANET_CP_MODE_CHOICE_READ) {
            janet_assert(!janet_chan_unpack(channel, &x, 0), "packing error");
            janet_schedule(fiber, make_read_result(channel, x));
        } else if (mode == JANET_CP_MODE_CHOICE_WRITE) {
            janet_schedule(fiber, make_write_result(channel));
        } else if (mode == JANET_CP_MODE_READ) {
            janet_assert(!janet_chan_unpack(channel, &x, 0), "packing error");
            janet_schedule(fiber, x);
        } else if (mode == JANET_CP_MODE_WRITE) {
            janet_schedule(fiber, janet_wrap_channel(channel));
        } else { /* (mode == JANET_CP_MODE_CLOSE) */
            janet_schedule(fiber, janet_wrap_nil());
        }
    } else if (mode != JANET_CP_MODE_CLOSE) {
        /* Fiber has already been cancelled or resumed. */
        /* Resend event to another waiting thread, depending on mode */
        int is_read = (mode == JANET_CP_MODE_CHOICE_READ) || (mode == JANET_CP_MODE_READ);
        if (is_read) {
            JanetChannelPending reader;
            janet_chan_lock(channel);
            if (!janet_q_pop(&channel->read_pending, &reader, sizeof(reader))) {
                JanetVM *vm = reader.thread;
                JanetEVGenericMessage msg;
                msg.tag = reader.mode;
                msg.fiber = reader.fiber;
                msg.argi = (int32_t) reader.sched_id;
                msg.argp = channel;
                msg.argj = x;
                janet_ev_post_event(vm, janet_thread_chan_cb, msg);
            }
            janet_chan_unlock(channel);
        } else {
            JanetChannelPending writer;
            janet_chan_lock(channel);
            if (!janet_q_pop(&channel->write_pending, &writer, sizeof(writer))) {
                JanetVM *vm = writer.thread;
                JanetEVGenericMessage msg;
                msg.tag = writer.mode;
                msg.fiber = writer.fiber;
                msg.argi = (int32_t) writer.sched_id;
                msg.argp = channel;
                msg.argj = janet_wrap_nil();
                janet_ev_post_event(vm, janet_thread_chan_cb, msg);
            }
            janet_chan_unlock(channel);
        }
    }
}

/* Push a value to a channel, and return 1 if channel should block, zero otherwise.
 * If the push would block, will add to the write_pending queue in the channel.
 * Handles both threaded and unthreaded channels. */
static int janet_channel_push(JanetChannel *channel, Janet x, int mode) {
    JanetChannelPending reader;
    int is_empty;
    if (janet_chan_pack(channel, &x)) {
        janet_panicf("failed to pack value for channel: %v", x);
    }
    janet_chan_lock(channel);
    if (channel->closed) {
        janet_chan_unlock(channel);
        janet_panic("cannot write to closed channel");
    }
    int is_threaded = janet_chan_is_threaded(channel);
    if (is_threaded) {
        /* don't dereference fiber from another thread */
        is_empty = janet_q_pop(&channel->read_pending, &reader, sizeof(reader));
    } else {
        do {
            is_empty = janet_q_pop(&channel->read_pending, &reader, sizeof(reader));
        } while (!is_empty && (reader.sched_id != reader.fiber->sched_id));
    }
    if (is_empty) {
        /* No pending reader */
        if (janet_q_push(&channel->items, &x, sizeof(Janet))) {
            janet_chan_unlock(channel);
            janet_panicf("channel overflow: %v", x);
        } else if (janet_q_count(&channel->items) > channel->limit) {
            /* No root fiber, we are in completion on a root fiber. Don't block. */
            if (mode == 2) {
                janet_chan_unlock(channel);
                return 0;
            }
            /* Pushed successfully, but should block. */
            JanetChannelPending pending;
            pending.thread = &janet_vm;
            pending.fiber = janet_vm.root_fiber,
            pending.sched_id = janet_vm.root_fiber->sched_id,
            pending.mode = mode ? JANET_CP_MODE_CHOICE_WRITE : JANET_CP_MODE_WRITE;
            janet_q_push(&channel->write_pending, &pending, sizeof(pending));
            janet_chan_unlock(channel);
            if (is_threaded) {
                janet_ev_inc_refcount();
                janet_gcroot(janet_wrap_fiber(pending.fiber));
            }
            return 1;
        }
    } else {
        /* Pending reader */
        if (is_threaded) {
            JanetVM *vm = reader.thread;
            JanetEVGenericMessage msg;
            msg.tag = reader.mode;
            msg.fiber = reader.fiber;
            msg.argi = (int32_t) reader.sched_id;
            msg.argp = channel;
            msg.argj = x;
            janet_ev_post_event(vm, janet_thread_chan_cb, msg);
        } else {
            if (reader.mode == JANET_CP_MODE_CHOICE_READ) {
                janet_schedule(reader.fiber, make_read_result(channel, x));
            } else {
                janet_schedule(reader.fiber, x);
            }
        }
    }
    janet_chan_unlock(channel);
    return 0;
}

/* Pop from a channel - returns 1 if item was obtained, 0 otherwise. The item
 * is returned by reference. If the pop would block, will add to the read_pending
 * queue in the channel. */
static int janet_channel_pop(JanetChannel *channel, Janet *item, int is_choice) {
    JanetChannelPending writer;
    janet_chan_lock(channel);
    if (channel->closed) {
        janet_chan_unlock(channel);
        *item = janet_wrap_nil();
        return 1;
    }
    int is_threaded = janet_chan_is_threaded(channel);
    if (janet_q_pop(&channel->items, item, sizeof(Janet))) {
        /* Queue empty */
        JanetChannelPending pending;
        pending.thread = &janet_vm;
        pending.fiber = janet_vm.root_fiber,
        pending.sched_id = janet_vm.root_fiber->sched_id;
        pending.mode = is_choice ? JANET_CP_MODE_CHOICE_READ : JANET_CP_MODE_READ;
        janet_q_push(&channel->read_pending, &pending, sizeof(pending));
        janet_chan_unlock(channel);
        if (is_threaded) {
            janet_ev_inc_refcount();
            janet_gcroot(janet_wrap_fiber(pending.fiber));
        }
        return 0;
    }
    janet_assert(!janet_chan_unpack(channel, item, 0), "bad channel packing");
    if (!janet_q_pop(&channel->write_pending, &writer, sizeof(writer))) {
        /* Pending writer */
        if (is_threaded) {
            JanetVM *vm = writer.thread;
            JanetEVGenericMessage msg;
            msg.tag = writer.mode;
            msg.fiber = writer.fiber;
            msg.argi = (int32_t) writer.sched_id;
            msg.argp = channel;
            msg.argj = janet_wrap_nil();
            janet_ev_post_event(vm, janet_thread_chan_cb, msg);
        } else {
            if (writer.mode == JANET_CP_MODE_CHOICE_WRITE) {
                janet_schedule(writer.fiber, make_write_result(channel));
            } else {
                janet_schedule(writer.fiber, janet_wrap_abstract(channel));
            }
        }
    }
    janet_chan_unlock(channel);
    return 1;
}

JanetChannel *janet_channel_unwrap(void *abstract) {
    return abstract;
}

JanetChannel *janet_getchannel(const Janet *argv, int32_t n) {
    return janet_channel_unwrap(janet_getabstract(argv, n, &janet_channel_type));
}

JanetChannel *janet_optchannel(const Janet *argv, int32_t argc, int32_t n, JanetChannel *dflt) {
    if (argc > n && !janet_checktype(argv[n], JANET_NIL)) {
        return janet_getchannel(argv, n);
    } else {
        return dflt;
    }
}

/* Channel Methods */

JANET_CORE_FN(cfun_channel_push,
              "(ev/give channel value)",
              "Write a value to a channel, suspending the current fiber if the channel is full. "
              "Returns the channel if the write succeeded, nil otherwise.") {
    janet_fixarity(argc, 2);
    JanetChannel *channel = janet_getchannel(argv, 0);
    if (janet_channel_push(channel, argv[1], 0)) {
        janet_await();
    }
    return argv[0];
}

JANET_CORE_FN(cfun_channel_pop,
              "(ev/take channel)",
              "Read from a channel, suspending the current fiber if no value is available.") {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    Janet item;
    if (janet_channel_pop(channel, &item, 0)) {
        janet_schedule(janet_vm.root_fiber, item);
    }
    janet_await();
}

JANET_CORE_FN(cfun_channel_choice,
              "(ev/select & clauses)",
              "Block until the first of several channel operations occur. Returns a tuple of the form [:give chan], [:take chan x], or [:close chan], where "
              "a :give tuple is the result of a write and :take tuple is the result of a read. Each clause must be either a channel (for "
              "a channel take operation) or a tuple [channel x] for a channel give operation. Operations are tried in order, such that the first "
              "clauses will take precedence over later clauses. Both and give and take operations can return a [:close chan] tuple, which indicates that "
              "the specified channel was closed while waiting, or that the channel was already closed.") {
    janet_arity(argc, 1, -1);
    int32_t len;
    const Janet *data;

    /* Check channels for immediate reads and writes */
    for (int32_t i = 0; i < argc; i++) {
        if (janet_indexed_view(argv[i], &data, &len) && len == 2) {
            /* Write */
            JanetChannel *chan = janet_getchannel(data, 0);
            janet_chan_lock(chan);
            if (chan->closed) {
                return make_close_result(chan);
            }
            if (janet_q_count(&chan->items) < chan->limit) {
                janet_channel_push(chan, data[1], 1);
                return make_write_result(chan);
            }
        } else {
            /* Read */
            JanetChannel *chan = janet_getchannel(argv, i);
            if (chan->closed) {
                return make_close_result(chan);
            }
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
            JanetChannel *chan = janet_getchannel(data, 0);
            if (chan->closed) continue;
            janet_channel_push(chan, data[1], 1);
        } else {
            /* Read */
            Janet item;
            JanetChannel *chan = janet_getchannel(argv, i);
            if (chan->closed) continue;
            janet_channel_pop(chan, &item, 1);
        }
    }

    janet_await();
}

JANET_CORE_FN(cfun_channel_full,
              "(ev/full channel)",
              "Check if a channel is full or not.") {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    janet_chan_lock(channel);
    Janet ret = janet_wrap_boolean(janet_q_count(&channel->items) >= channel->limit);
    janet_chan_unlock(channel);
    return ret;
}

JANET_CORE_FN(cfun_channel_capacity,
              "(ev/capacity channel)",
              "Get the number of items a channel will store before blocking writers.") {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    janet_chan_lock(channel);
    Janet ret = janet_wrap_integer(channel->limit);
    janet_chan_unlock(channel);
    return ret;
}

JANET_CORE_FN(cfun_channel_count,
              "(ev/count channel)",
              "Get the number of items currently waiting in a channel.") {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    janet_chan_lock(channel);
    Janet ret = janet_wrap_integer(janet_q_count(&channel->items));
    janet_chan_unlock(channel);
    return ret;
}

/* Fisher yates shuffle of arguments to get fairness */
static void fisher_yates_args(int32_t argc, Janet *argv) {
    for (int32_t i = argc; i > 1; i--) {
        int32_t swap_index = janet_rng_u32(&janet_vm.ev_rng) % i;
        Janet temp = argv[swap_index];
        argv[swap_index] = argv[i - 1];
        argv[i - 1] = temp;
    }
}

JANET_CORE_FN(cfun_channel_rchoice,
              "(ev/rselect & clauses)",
              "Similar to ev/select, but will try clauses in a random order for fairness.") {
    fisher_yates_args(argc, argv);
    return cfun_channel_choice(argc, argv);
}

JANET_CORE_FN(cfun_channel_new,
              "(ev/chan &opt capacity)",
              "Create a new channel. capacity is the number of values to queue before "
              "blocking writers, defaults to 0 if not provided. Returns a new channel.") {
    janet_arity(argc, 0, 1);
    int32_t limit = janet_optnat(argv, argc, 0, 0);
    JanetChannel *channel = janet_abstract(&janet_channel_type, sizeof(JanetChannel));
    janet_chan_init(channel, limit, 0);
    return janet_wrap_abstract(channel);
}

JANET_CORE_FN(cfun_channel_new_threaded,
              "(ev/thread-chan &opt limit)",
              "Create a threaded channel. A threaded channel is a channel that can be shared between threads and "
              "used to communicate between any number of operating system threads.") {
    janet_arity(argc, 0, 1);
    int32_t limit = janet_optnat(argv, argc, 0, 0);
    JanetChannel *tchan = janet_abstract_threaded(&janet_channel_type, sizeof(JanetChannel));
    janet_chan_init(tchan, limit, 1);
    return janet_wrap_abstract(tchan);
}

JANET_CORE_FN(cfun_channel_close,
              "(ev/chan-close chan)",
              "Close a channel. A closed channel will cause all pending reads and writes to return nil. "
              "Returns the channel.") {
    janet_fixarity(argc, 1);
    JanetChannel *channel = janet_getchannel(argv, 0);
    janet_chan_lock(channel);
    if (!channel->closed) {
        channel->closed = 1;
        JanetChannelPending writer;
        while (!janet_q_pop(&channel->write_pending, &writer, sizeof(writer))) {
            if (writer.thread != &janet_vm) {
                JanetVM *vm = writer.thread;
                JanetEVGenericMessage msg;
                msg.fiber = writer.fiber;
                msg.argp = channel;
                msg.tag = JANET_CP_MODE_CLOSE;
                msg.argi = (int32_t) writer.sched_id;
                msg.argj = janet_wrap_nil();
                janet_ev_post_event(vm, janet_thread_chan_cb, msg);
            } else {
                if (writer.mode == JANET_CP_MODE_CHOICE_WRITE) {
                    janet_schedule(writer.fiber, janet_wrap_nil());
                } else {
                    janet_schedule(writer.fiber, make_close_result(channel));
                }
            }
        }
        JanetChannelPending reader;
        while (!janet_q_pop(&channel->read_pending, &reader, sizeof(reader))) {
            if (reader.thread != &janet_vm) {
                JanetVM *vm = reader.thread;
                JanetEVGenericMessage msg;
                msg.fiber = reader.fiber;
                msg.argp = channel;
                msg.tag = JANET_CP_MODE_CLOSE;
                msg.argi = (int32_t) reader.sched_id;
                msg.argj = janet_wrap_nil();
                janet_ev_post_event(vm, janet_thread_chan_cb, msg);
            } else {
                if (reader.mode == JANET_CP_MODE_CHOICE_READ) {
                    janet_schedule(reader.fiber, janet_wrap_nil());
                } else {
                    janet_schedule(reader.fiber, make_close_result(channel));
                }
            }
        }
    }
    janet_chan_unlock(channel);
    return argv[0];
}

static const JanetMethod ev_chanat_methods[] = {
    {"select", cfun_channel_choice},
    {"rselect", cfun_channel_rchoice},
    {"count", cfun_channel_count},
    {"take", cfun_channel_pop},
    {"give", cfun_channel_push},
    {"capacity", cfun_channel_capacity},
    {"full", cfun_channel_full},
    {"close", cfun_channel_close},
    {NULL, NULL}
};

static int janet_chanat_get(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), ev_chanat_methods, out);
}

static Janet janet_chanat_next(void *p, Janet key) {
    (void) p;
    return janet_nextmethod(ev_chanat_methods, key);
}

const JanetAbstractType janet_channel_type = {
    "core/channel",
    janet_chanat_gc,
    janet_chanat_mark,
    janet_chanat_get,
    NULL, /* put */
    NULL, /* marshal */
    NULL, /* unmarshal */
    NULL, /* tostring */
    NULL, /* compare */
    NULL, /* hash */
    janet_chanat_next,
    JANET_ATEND_NEXT
};

/* Main event loop */

void janet_loop1_impl(int has_timeout, JanetTimestamp timeout);

int janet_loop_done(void) {
    return !(janet_vm.listener_count ||
             (janet_vm.spawn.head != janet_vm.spawn.tail) ||
             janet_vm.tq_count ||
             janet_vm.extra_listeners);
}

JanetFiber *janet_loop1(void) {
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
    while (janet_vm.spawn.head != janet_vm.spawn.tail) {
        JanetTask task = {NULL, janet_wrap_nil(), JANET_SIGNAL_OK};
        janet_q_pop(&janet_vm.spawn, &task, sizeof(task));
        task.fiber->flags &= ~JANET_FIBER_FLAG_SCHEDULED;
        Janet res;
        JanetSignal sig = janet_continue_signal(task.fiber, task.value, &res, task.sig);
        void *sv = task.fiber->supervisor_channel;
        int is_suspended = sig == JANET_SIGNAL_EVENT || sig == JANET_SIGNAL_YIELD || sig == JANET_SIGNAL_INTERRUPT;
        if (NULL == sv) {
            if (!is_suspended) {
                janet_stacktrace(task.fiber, res);
            }
        } else if (sig == JANET_SIGNAL_OK || (task.fiber->flags & (1 << sig))) {
            JanetChannel *chan = janet_channel_unwrap(sv);
            janet_channel_push(chan, make_supervisor_event(janet_signal_names[sig],
                               task.fiber, chan->is_threaded), 2);
        } else if (!is_suspended) {
            janet_stacktrace(task.fiber, res);
        }
        if (sig == JANET_SIGNAL_INTERRUPT) {
            /* On interrupts, return the interrupted fiber immediately */
            return task.fiber;
        }
    }

    /* Poll for events */
    if (janet_vm.listener_count || janet_vm.tq_count || janet_vm.extra_listeners) {
        JanetTimeout to;
        memset(&to, 0, sizeof(to));
        int has_timeout;
        /* Drop timeouts that are no longer needed */
        while ((has_timeout = peek_timeout(&to)) && (to.curr_fiber == NULL) && to.fiber->sched_id != to.sched_id) {
            pop_timeout(0);
        }
        /* Run polling implementation only if pending timeouts or pending events */
        if (janet_vm.tq_count || janet_vm.listener_count || janet_vm.extra_listeners) {
            janet_loop1_impl(has_timeout, to.when);
        }
    }

    /* No fiber was interrupted */
    return NULL;
}

/* Same as janet_interpreter_interrupt, but will also
 * break out of the event loop if waiting for an event
 * (say, waiting for ev/sleep to finish). Does this by pushing
 * an empty event to the event loop. */
void janet_loop1_interrupt(JanetVM *vm) {
    janet_interpreter_interrupt(vm);
    JanetEVGenericMessage msg = {0};
    JanetCallback cb = NULL;
    janet_ev_post_event(vm, cb, msg);
}

void janet_loop(void) {
    while (!janet_loop_done()) {
        JanetFiber *interrupted_fiber = janet_loop1();
        if (NULL != interrupted_fiber) {
            janet_schedule(interrupted_fiber, janet_wrap_nil());
        }
    }
}

/*
 * Self-pipe handling code.
 */

#ifdef JANET_WINDOWS

/* On windows, use PostQueuedCompletionStatus instead for
 * custom events */

#else

static void janet_ev_setup_selfpipe(void) {
    if (janet_make_pipe(janet_vm.selfpipe, 0)) {
        JANET_EXIT("failed to initialize self pipe in event loop");
    }
}

/* Handle events from the self pipe inside the event loop */
static void janet_ev_handle_selfpipe(void) {
    JanetSelfPipeEvent response;
    while (read(janet_vm.selfpipe[0], &response, sizeof(response)) > 0) {
        if (NULL != response.cb) {
            response.cb(response.msg);
        }
    }
}

static void janet_ev_cleanup_selfpipe(void) {
    close(janet_vm.selfpipe[0]);
    close(janet_vm.selfpipe[1]);
}

#endif

#ifdef JANET_WINDOWS

static JanetTimestamp ts_now(void) {
    return (JanetTimestamp) GetTickCount64();
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (NULL == janet_vm.iocp) janet_panic("could not create io completion port");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    CloseHandle(janet_vm.iocp);
}

JanetListenerState *janet_listen(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    /* Add the handle to the io completion port if not already added */
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    if (!(stream->flags & JANET_STREAM_IOCP)) {
        if (NULL == CreateIoCompletionPort(stream->handle, janet_vm.iocp, (ULONG_PTR) stream, 0)) {
            janet_panicf("failed to listen for events: %V", janet_ev_lasterr());
        }
        stream->flags |= JANET_STREAM_IOCP;
    }
    return state;
}


static void janet_unlisten(JanetListenerState *state, int is_gc) {
    janet_unlisten_impl(state, is_gc);
}

void janet_loop1_impl(int has_timeout, JanetTimestamp to) {
    ULONG_PTR completionKey = 0;
    DWORD num_bytes_transfered = 0;
    LPOVERLAPPED overlapped = NULL;

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
    BOOL result = GetQueuedCompletionStatus(janet_vm.iocp, &num_bytes_transfered, &completionKey, &overlapped, (DWORD) waittime);

    if (result || overlapped) {
        if (0 == completionKey) {
            /* Custom event */
            JanetSelfPipeEvent *response = (JanetSelfPipeEvent *)(overlapped);
            if (NULL != response->cb) {
                response->cb(response->msg);
            }
            janet_free(response);
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
                        janet_unlisten(state, 0);
                    }
                    break;
                } else {
                    state = state->_next;
                }
            }
        }
    }
}

#elif defined(JANET_EV_EPOLL)

static JanetTimestamp ts_now(void) {
    struct timespec now;
    janet_assert(-1 != clock_gettime(CLOCK_MONOTONIC, &now), "failed to get time");
    uint64_t res = 1000 * now.tv_sec;
    res += now.tv_nsec / 1000000;
    return res;
}

static int make_epoll_events(int mask) {
    int events = 0;
    if (mask & JANET_ASYNC_LISTEN_READ)
        events |= EPOLLIN;
    if (mask & JANET_ASYNC_LISTEN_WRITE)
        events |= EPOLLOUT;
    return events;
}

static void janet_epoll_sync_callback(JanetEVGenericMessage msg) {
    JanetListenerState *state = msg.argp;
    JanetAsyncStatus status1 = JANET_ASYNC_STATUS_NOT_DONE;
    JanetAsyncStatus status2 = JANET_ASYNC_STATUS_NOT_DONE;
    if (state->stream->_mask & JANET_ASYNC_LISTEN_WRITE)
        status1 = state->machine(state, JANET_ASYNC_EVENT_WRITE);
    if (state->stream->_mask & JANET_ASYNC_LISTEN_WRITE)
        status2 = state->machine(state, JANET_ASYNC_EVENT_READ);
    if (status1 == JANET_ASYNC_STATUS_DONE ||
            status2 == JANET_ASYNC_STATUS_DONE) {
        janet_unlisten(state, 0);
    } else {
        /* Repost event */
        janet_ev_post_event(NULL, janet_epoll_sync_callback, msg);
    }
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
        status = epoll_ctl(janet_vm.epoll, op, stream->handle, &ev);
    } while (status == -1 && errno == EINTR);
    if (status == -1) {
        if (errno == EPERM) {
            /* Couldn't add to event loop, so assume that it completes
             * synchronously. In that case, fire the completion
             * event manually, since this should be a read or write
             * event to a file. So we just post a custom event to do the read/write
             * asap. */
            /* Use flag to indicate state is not registered in epoll */
            state->_mask |= (1 << JANET_ASYNC_EVENT_COMPLETE);
            JanetEVGenericMessage msg = {0};
            msg.argp = state;
            janet_ev_post_event(NULL, janet_epoll_sync_callback, msg);
        } else {
            /* Unexpected error */
            janet_unlisten_impl(state, 0);
            janet_panicv(janet_ev_lasterr());
        }
    }
    return state;
}

/* Tell system we are done listening for a certain event */
static void janet_unlisten(JanetListenerState *state, int is_gc) {
    JanetStream *stream = state->stream;
    if (!(stream->flags & JANET_STREAM_CLOSED)) {
        /* Use flag to indicate state is not registered in epoll */
        if (!(state->_mask & (1 << JANET_ASYNC_EVENT_COMPLETE))) {
            int is_last = (state->_next == NULL && stream->state == state);
            int op = is_last ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
            struct epoll_event ev;
            ev.events = make_epoll_events(stream->_mask & ~state->_mask);
            ev.data.ptr = stream;
            int status;
            do {
                status = epoll_ctl(janet_vm.epoll, op, stream->handle, &ev);
            } while (status == -1 && errno == EINTR);
            if (status == -1) {
                janet_panicv(janet_ev_lasterr());
            }
        }
    }
    /* Destroy state machine and free memory */
    janet_unlisten_impl(state, is_gc);
}

#define JANET_EPOLL_MAX_EVENTS 64
void janet_loop1_impl(int has_timeout, JanetTimestamp timeout) {
    struct itimerspec its;
    if (janet_vm.timer_enabled || has_timeout) {
        memset(&its, 0, sizeof(its));
        if (has_timeout) {
            its.it_value.tv_sec = timeout / 1000;
            its.it_value.tv_nsec = (timeout % 1000) * 1000000;
        }
        timerfd_settime(janet_vm.timerfd, TFD_TIMER_ABSTIME, &its, NULL);
    }
    janet_vm.timer_enabled = has_timeout;

    /* Poll for events */
    struct epoll_event events[JANET_EPOLL_MAX_EVENTS];
    int ready;
    do {
        ready = epoll_wait(janet_vm.epoll, events, JANET_EPOLL_MAX_EVENTS, -1);
    } while (ready == -1 && errno == EINTR);
    if (ready == -1) {
        JANET_EXIT("failed to poll events");
    }

    /* Step state machines */
    for (int i = 0; i < ready; i++) {
        void *p = events[i].data.ptr;
        if (&janet_vm.timerfd == p) {
            /* Timer expired, ignore */;
        } else if (janet_vm.selfpipe == p) {
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
                if ((mask & EPOLLHUP) && !(mask & (EPOLLOUT | EPOLLIN)))
                    status4 = state->machine(state, JANET_ASYNC_EVENT_HUP);
                if (status1 == JANET_ASYNC_STATUS_DONE ||
                        status2 == JANET_ASYNC_STATUS_DONE ||
                        status3 == JANET_ASYNC_STATUS_DONE ||
                        status4 == JANET_ASYNC_STATUS_DONE)
                    janet_unlisten(state, 0);
                state = next_state;
            }
        }
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_ev_setup_selfpipe();
    janet_vm.epoll = epoll_create1(EPOLL_CLOEXEC);
    janet_vm.timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    janet_vm.timer_enabled = 0;
    if (janet_vm.epoll == -1 || janet_vm.timerfd == -1) goto error;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &janet_vm.timerfd;
    if (-1 == epoll_ctl(janet_vm.epoll, EPOLL_CTL_ADD, janet_vm.timerfd, &ev)) goto error;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = janet_vm.selfpipe;
    if (-1 == epoll_ctl(janet_vm.epoll, EPOLL_CTL_ADD, janet_vm.selfpipe[0], &ev)) goto error;
    return;
error:
    JANET_EXIT("failed to initialize event loop");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    close(janet_vm.epoll);
    close(janet_vm.timerfd);
    janet_ev_cleanup_selfpipe();
    janet_vm.epoll = 0;
}

/*
 * End epoll implementation
 */

#elif defined(JANET_EV_KQUEUE)
/* Definition from:
 *   https://github.com/wahern/cqueues/blob/master/src/lib/kpoll.c
 * NetBSD uses intptr_t while others use void * for .udata */
#define EV_SETx(ev, a, b, c, d, e, f) EV_SET((ev), (a), (b), (c), (d), (e), ((__typeof__((ev)->udata))(f)))
#define JANET_KQUEUE_TF (EV_ADD | EV_ENABLE | EV_CLEAR | EV_ONESHOT)

/* NOTE:
 * NetBSD doesn't like intervals less than 1 millisecond so lets make that the
 * default anywhere JANET_KQUEUE_TS will be used. */
#ifdef __NetBSD__
#define JANET_KQUEUE_MIN_INTERVAL 1
#else
#define JANET_KQUEUE_MIN_INTERVAL 0
#endif

#ifdef __FreeBSD__
#define JANET_KQUEUE_TS(timestamp) (timestamp)
#else
/* NOTE:
 * NetBSD and OpenBSD expect things are always intervals, so fake that we have
 * abstime capability by changing how a timestamp is used in all kqueue calls
 * and defining absent macros. Additionally NetBSD expects intervals be
 * greater than 1 millisecond, so correct all intervals to be at least 1
 * millisecond under NetBSD. */
JanetTimestamp fix_interval(const JanetTimestamp ts) {
    return ts >= JANET_KQUEUE_MIN_INTERVAL ? ts : JANET_KQUEUE_MIN_INTERVAL;
}
#define JANET_KQUEUE_TS(timestamp) (fix_interval((timestamp - ts_now())))
#define NOTE_MSECONDS 0
#define NOTE_ABSTIME 0
#endif


/* TODO: make this available be we using kqueue or epoll, instead of
 * redefinining it for kqueue and epoll separately? */
static JanetTimestamp ts_now(void) {
    struct timespec now;
    janet_assert(-1 != clock_gettime(CLOCK_MONOTONIC, &now), "failed to get time");
    uint64_t res = 1000 * now.tv_sec;
    res += now.tv_nsec / 1000000;
    return res;
}

void add_kqueue_events(const struct kevent *events, int length) {
    /* NOTE: Status should be equal to the amount of events added, which isn't
     * always known since deletions or modifications occur. Can't use the
     * eventlist argument for it to report to us what failed otherwise we may
     * poll in events to handle! This code assumes atomicity, that kqueue can
     * either succeed or fail, but never partially (which is seemingly how it
     * works in practice). When encountering an "inbetween" state we currently
     * just panic!
     *
     * The FreeBSD man page kqueue(2) shows a check through the change list to
     * check if kqueue had an error with any of the events being pushed to
     * change. Maybe we should do this, even tho the man page also doesn't
     * note that kqueue actually does this. We do not do this at this time.  */
    int status;
    status = kevent(janet_vm.kq, events, length, NULL, 0, NULL);
    if (status == -1 && errno != EINTR)
        janet_panicv(janet_ev_lasterr());
}

JanetListenerState *janet_listen(JanetStream *stream, JanetListener behavior, int mask, size_t size, void *user) {
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    struct kevent kev[2];

    int length = 0;
    if (state->stream->_mask & JANET_ASYNC_LISTEN_READ) {
        EV_SETx(&kev[length], stream->handle, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, stream);
        length++;
    }
    if (state->stream->_mask & JANET_ASYNC_LISTEN_WRITE) {
        EV_SETx(&kev[length], stream->handle, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, stream);
        length++;
    }

    if (length > 0) {
        add_kqueue_events(kev, length);
    }

    return state;
}

static void janet_unlisten(JanetListenerState *state, int is_gc) {
    JanetStream *stream = state->stream;
    if (!(stream->flags & JANET_STREAM_CLOSED)) {
        /* Use flag to indicate state is not registered in kqueue */
        if (!(state->_mask & (1 << JANET_ASYNC_EVENT_COMPLETE))) {
            int is_last = (state->_next == NULL && stream->state == state);
            int op = is_last ? EV_DELETE : EV_DISABLE | EV_ADD;
            struct kevent kev[2];
            EV_SETx(&kev[1], stream->handle, EVFILT_WRITE, op, 0, 0, stream);

            int length = 0;
            if (stream->_mask & JANET_ASYNC_EVENT_WRITE) {
                EV_SETx(&kev[length], stream->handle, EVFILT_WRITE, op, 0, 0, stream);
                length++;
            }
            if (stream->_mask & JANET_ASYNC_EVENT_READ) {
                EV_SETx(&kev[length], stream->handle, EVFILT_READ, op, 0, 0, stream);
                length++;
            }

            add_kqueue_events(kev, length);
        }
    }
    janet_unlisten_impl(state, is_gc);
}

#define JANET_KQUEUE_TIMER_IDENT 1
#define JANET_KQUEUE_MAX_EVENTS 64

void janet_loop1_impl(int has_timeout, JanetTimestamp timeout) {
    /* Construct our timer which is a definite time on the clock, not an
     * interval, in milliseconds as that is `JanetTimestamp`'s precision. */
    struct kevent timer;
    if (janet_vm.timer_enabled || has_timeout) {
        EV_SETx(&timer,
                JANET_KQUEUE_TIMER_IDENT,
                EVFILT_TIMER,
                JANET_KQUEUE_TF,
                NOTE_MSECONDS | NOTE_ABSTIME,
                JANET_KQUEUE_TS(timeout), &janet_vm.timer);
        add_kqueue_events(&timer, 1);
    }
    janet_vm.timer_enabled = has_timeout;

    /* Poll for events */
    int status;
    struct kevent events[JANET_KQUEUE_MAX_EVENTS];
    do {
        status = kevent(janet_vm.kq, NULL, 0, events, JANET_KQUEUE_MAX_EVENTS, NULL);
    } while (status == -1 && errno == EINTR);
    if (status == -1)
        JANET_EXIT("failed to poll events");

    /* Step state machines */
    for (int i = 0; i < status; i++) {
        void *p = (void *) events[i].udata;
        if (&janet_vm.timer == p) {
            /* Timer expired, ignore */;
        } else if (janet_vm.selfpipe == p) {
            /* Self-pipe handling */
            janet_ev_handle_selfpipe();
        } else {
            JanetStream *stream = p;
            JanetListenerState *state = stream->state;
            if (NULL != state) {
                state->event = events + i;
                JanetAsyncStatus statuses[4];
                for (int i = 0; i < 4; i++)
                    statuses[i] = JANET_ASYNC_STATUS_NOT_DONE;

                if (!(events[i].flags & EV_ERROR)) {
                    if (events[i].filter == EVFILT_WRITE)
                        statuses[0] = state->machine(state, JANET_ASYNC_EVENT_WRITE);
                    if (events[i].filter == EVFILT_READ)
                        statuses[1] = state->machine(state, JANET_ASYNC_EVENT_READ);
                    if ((events[i].flags & EV_EOF) && !(events[i].data > 0))
                        statuses[3] = state->machine(state, JANET_ASYNC_EVENT_HUP);
                } else {
                    statuses[2] = state->machine(state, JANET_ASYNC_EVENT_ERR);
                }
                if (statuses[0] == JANET_ASYNC_STATUS_DONE ||
                        statuses[1] == JANET_ASYNC_STATUS_DONE ||
                        statuses[2] == JANET_ASYNC_STATUS_DONE ||
                        statuses[3] == JANET_ASYNC_STATUS_DONE)
                    janet_unlisten(state, 0);
            }
        }
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_ev_setup_selfpipe();
    janet_vm.kq = kqueue();
    janet_vm.timer_enabled = 0;
    if (janet_vm.kq == -1) goto error;
    struct kevent events[2];
    /* Don't use JANET_KQUEUE_TS here, as even under FreeBSD we use intervals
     * here. */
    EV_SETx(&events[0],
            JANET_KQUEUE_TIMER_IDENT,
            EVFILT_TIMER,
            JANET_KQUEUE_TF,
            NOTE_MSECONDS, JANET_KQUEUE_MIN_INTERVAL, &janet_vm.timer);
    EV_SETx(&events[1], janet_vm.selfpipe[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, janet_vm.selfpipe);
    add_kqueue_events(events, 2);
    return;
error:
    JANET_EXIT("failed to initialize event loop");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    close(janet_vm.kq);
    janet_ev_cleanup_selfpipe();
    janet_vm.kq = 0;
}

#else

#include <poll.h>

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
    size_t oldsize = janet_vm.listener_cap;
    JanetListenerState *state = janet_listen_impl(stream, behavior, mask, size, user);
    size_t newsize = janet_vm.listener_cap;
    if (newsize > oldsize) {
        janet_vm.fds = janet_realloc(janet_vm.fds, (newsize + 1) * sizeof(struct pollfd));
        if (NULL == janet_vm.fds) {
            JANET_OUT_OF_MEMORY;
        }
    }
    struct pollfd ev;
    ev.fd = stream->handle;
    ev.events = make_poll_events(state->stream->_mask);
    ev.revents = 0;
    janet_vm.fds[state->_index + 1] = ev;
    return state;
}

static void janet_unlisten(JanetListenerState *state, int is_gc) {
    janet_vm.fds[state->_index + 1] = janet_vm.fds[janet_vm.listener_count];
    janet_unlisten_impl(state, is_gc);
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
        ready = poll(janet_vm.fds, janet_vm.listener_count + 1, to);
    } while (ready == -1 && errno == EINTR);
    if (ready == -1) {
        JANET_EXIT("failed to poll events");
    }

    /* Check selfpipe */
    if (janet_vm.fds[0].revents & POLLIN) {
        janet_vm.fds[0].revents = 0;
        janet_ev_handle_selfpipe();
    }

    /* Step state machines */
    for (size_t i = 0; i < janet_vm.listener_count; i++) {
        struct pollfd *pfd = janet_vm.fds + i + 1;
        /* Skip fds where nothing interesting happened */
        JanetListenerState *state = janet_vm.listeners[i];
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
            status3 = state->machine(state, JANET_ASYNC_EVENT_ERR);
        if ((mask & POLLHUP) && !(mask & (POLLIN | POLLOUT)))
            status4 = state->machine(state, JANET_ASYNC_EVENT_HUP);
        if (status1 == JANET_ASYNC_STATUS_DONE ||
                status2 == JANET_ASYNC_STATUS_DONE ||
                status3 == JANET_ASYNC_STATUS_DONE ||
                status4 == JANET_ASYNC_STATUS_DONE)
            janet_unlisten(state, 0);
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm.fds = NULL;
    janet_ev_setup_selfpipe();
    janet_vm.fds = janet_malloc(sizeof(struct pollfd));
    if (NULL == janet_vm.fds) {
        JANET_OUT_OF_MEMORY;
    }
    janet_vm.fds[0].fd = janet_vm.selfpipe[0];
    janet_vm.fds[0].events = POLLIN;
    janet_vm.fds[0].revents = 0;
    return;
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    janet_ev_cleanup_selfpipe();
    janet_free(janet_vm.fds);
    janet_vm.fds = NULL;
}

#endif

/*
 * End poll implementation
 */

/*
 * Generic Callback system. Post a function pointer + data to the event loop (from another
 * thread or even a signal handler). Allows posting events from another thread or signal handler.
 */
void janet_ev_post_event(JanetVM *vm, JanetCallback cb, JanetEVGenericMessage msg) {
    vm = vm ? vm : &janet_vm;
#ifdef JANET_WINDOWS
    JanetHandle iocp = vm->iocp;
    JanetSelfPipeEvent *event = janet_malloc(sizeof(JanetSelfPipeEvent));
    if (NULL == event) {
        JANET_OUT_OF_MEMORY;
    }
    event->msg = msg;
    event->cb = cb;
    janet_assert(PostQueuedCompletionStatus(iocp,
                                            sizeof(JanetSelfPipeEvent),
                                            0,
                                            (LPOVERLAPPED) event),
                 "failed to post completion event");
#else
    JanetSelfPipeEvent event;
    memset(&event, 0, sizeof(event));
    event.msg = msg;
    event.cb = cb;
    int fd = vm->selfpipe[1];
    /* handle a bit of back pressure before giving up. */
    int tries = 4;
    while (tries > 0) {
        int status;
        do {
            status = write(fd, &event, sizeof(event));
        } while (status == -1 && errno == EINTR);
        if (status > 0) break;
        sleep(0);
        tries--;
    }
    janet_assert(tries > 0, "failed to write event to self-pipe");
#endif
}

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
    janet_free(init);
    JanetSelfPipeEvent response;
    memset(&response, 0, sizeof(response));
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
    JanetEVThreadInit *init = janet_malloc(sizeof(JanetEVThreadInit));
    if (NULL == init) {
        JANET_OUT_OF_MEMORY;
    }
    init->msg = arguments;
    init->subr = fp;
    init->cb = cb;

#ifdef JANET_WINDOWS
    init->write_pipe = janet_vm.iocp;
    HANDLE thread_handle = CreateThread(NULL, 0, janet_thread_body, init, 0, NULL);
    if (NULL == thread_handle) {
        janet_free(init);
        janet_panic("failed to create thread");
    }
    CloseHandle(thread_handle); /* detach from thread */
#else
    init->write_pipe = janet_vm.selfpipe[1];
    pthread_t waiter_thread;
    int err = pthread_create(&waiter_thread, NULL, janet_thread_body, init);
    if (err) {
        janet_free(init);
        janet_panicf("%s", strerror(err));
    }
    pthread_detach(waiter_thread);
#endif

    /* Increment ev refcount so we don't quit while waiting for a subprocess */
    janet_ev_inc_refcount();
}

/* Default callback for janet_ev_threaded_await. */
void janet_ev_default_threaded_callback(JanetEVGenericMessage return_value) {
    janet_ev_dec_refcount();
    if (return_value.fiber == NULL) {
        return;
    }
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
            if (return_value.tag == JANET_EV_TCTAG_STRINGF) janet_free(return_value.argp);
            break;
        case JANET_EV_TCTAG_KEYWORD:
            janet_schedule(return_value.fiber, janet_ckeywordv((const char *) return_value.argp));
            break;
        case JANET_EV_TCTAG_ERR_STRING:
        case JANET_EV_TCTAG_ERR_STRINGF:
            janet_cancel(return_value.fiber, janet_cstringv((const char *) return_value.argp));
            if (return_value.tag == JANET_EV_TCTAG_STRINGF) janet_free(return_value.argp);
            break;
        case JANET_EV_TCTAG_ERR_KEYWORD:
            janet_cancel(return_value.fiber, janet_ckeywordv((const char *) return_value.argp));
            break;
        case JANET_EV_TCTAG_BOOLEAN:
            janet_schedule(return_value.fiber, janet_wrap_boolean(return_value.argi));
            break;
    }
    janet_gcunroot(janet_wrap_fiber(return_value.fiber));
}


/* Convenience method for common case */
JANET_NO_RETURN
void janet_ev_threaded_await(JanetThreadedSubroutine fp, int tag, int argi, void *argp) {
    JanetEVGenericMessage arguments;
    memset(&arguments, 0, sizeof(arguments));
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
            janet_schedule(s->fiber, janet_wrap_nil());
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
            if (state->mode == JANET_ASYNC_READMODE_RECVFROM) {
                state->wbuf.len = (ULONG) chunk_size;
                state->wbuf.buf = state->chunk_buf;
                status = WSARecvFrom((SOCKET) s->stream->handle, &state->wbuf, 1,
                                     NULL, &state->flags, &state->from, &state->fromlen, &state->overlapped, NULL);
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
                        if (state->bytes_read) {
                            janet_schedule(s->fiber, janet_wrap_buffer(state->buf));
                        } else {
                            janet_schedule(s->fiber, janet_wrap_nil());
                        }
                    } else {
                        janet_cancel(s->fiber, janet_ev_lasterr());
                    }
                    return JANET_ASYNC_STATUS_DONE;
                }
            }
        }
        break;
#else
        case JANET_ASYNC_EVENT_ERR: {
            if (state->bytes_read) {
                janet_schedule(s->fiber, janet_wrap_buffer(state->buf));
            } else {
                janet_schedule(s->fiber, janet_wrap_nil());
            }
            return JANET_ASYNC_STATUS_DONE;
        }
        case JANET_ASYNC_EVENT_HUP:
        case JANET_ASYNC_EVENT_READ: {
            JanetBuffer *buffer = state->buf;
            int32_t bytes_left = state->bytes_left;
            int limited = bytes_left > 4096;
            int32_t read_limit = limited ? 4096 : bytes_left;
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
            if ((!state->is_chunk && !limited) || bytes_left == 0 || nread == 0) {
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
            if (state->mode == JANET_ASYNC_WRITEMODE_SENDTO) {
                SOCKET sock = (SOCKET) s->stream->handle;
                state->wbuf.buf = (char *) bytes;
                state->wbuf.len = len;
                const struct sockaddr *to = state->dest_abst;
                int tolen = (int) janet_abstract_size((void *) to);
                status = WSASendTo(sock, &state->wbuf, 1, NULL, state->flags, to, tolen, &state->overlapped, NULL);
                if (status && (WSA_IO_PENDING != WSAGetLastError())) {
                    janet_cancel(s->fiber, janet_ev_lasterr());
                    return JANET_ASYNC_STATUS_DONE;
                }
            } else
#endif
            {
                /*
                 * File handles in IOCP need to specify this if they are writing to the
                 * ends of files, like how this is used here.
                 * If the underlying resource doesn't support seeking
                 * byte offsets, they will be ignored
                 * but this otherwise writes to the end of the file in question
                 * Right now, os/open streams aren't seekable, so this works.
                 * for more details see the lpOverlapped parameter in
                 * https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
                 */
                state->overlapped.Offset = (DWORD) 0xFFFFFFFF;
                state->overlapped.OffsetHigh = (DWORD) 0xFFFFFFFF;
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

int janet_make_pipe(JanetHandle handles[2], int mode) {
#ifdef JANET_WINDOWS
    /*
     * On windows, the built in CreatePipe function doesn't support overlapped IO
     * so we lift from the windows source code and modify for our own version.
     *
     * mode = 0: both sides non-blocking.
     * mode = 1: only read side non-blocking: write side sent to subprocess
     * mode = 2: only write side non-blocking: read side sent to subprocess
     */
    JanetHandle shandle, chandle;
    UCHAR PipeNameBuffer[MAX_PATH];
    SECURITY_ATTRIBUTES saAttr;
    memset(&saAttr, 0, sizeof(saAttr));
    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    sprintf(PipeNameBuffer,
            "\\\\.\\Pipe\\JanetPipeFile.%08x.%08x",
            GetCurrentProcessId(),
            InterlockedIncrement(&PipeSerialNumber));

    /* server handle goes to subprocess */
    shandle = CreateNamedPipeA(
                  PipeNameBuffer,
                  (mode == 2 ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) | FILE_FLAG_OVERLAPPED,
                  PIPE_TYPE_BYTE | PIPE_WAIT,
                  255,           /* Max number of pipes for duplication. */
                  4096,          /* Out buffer size */
                  4096,          /* In buffer size */
                  120 * 1000,    /* Timeout in ms */
                  &saAttr);
    if (shandle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    /* we keep client handle */
    chandle = CreateFileA(
                  PipeNameBuffer,
                  (mode == 2 ? GENERIC_WRITE : GENERIC_READ),
                  0,
                  &saAttr,
                  OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                  NULL);

    if (chandle == INVALID_HANDLE_VALUE) {
        CloseHandle(shandle);
        return -1;
    }
    if (mode == 2) {
        handles[0] = shandle;
        handles[1] = chandle;
    } else {
        handles[0] = chandle;
        handles[1] = shandle;
    }
    return 0;
#else
    (void) mode;
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

JANET_CORE_FN(cfun_ev_go,
              "(ev/go fiber &opt value supervisor)",
              "Put a fiber on the event loop to be resumed later. Optionally pass "
              "a value to resume with, otherwise resumes with nil. Returns the fiber. "
              "An optional `core/channel` can be provided as a supervisor. When various "
              "events occur in the newly scheduled fiber, an event will be pushed to the supervisor. "
              "If not provided, the new fiber will inherit the current supervisor.") {
    janet_arity(argc, 1, 3);
    Janet value = argc >= 2 ? argv[1] : janet_wrap_nil();
    void *supervisor = janet_optabstract(argv, argc, 2, &janet_channel_type, janet_vm.root_fiber->supervisor_channel);
    JanetFiber *fiber;
    if (janet_checktype(argv[0], JANET_FUNCTION)) {
        /* Create a fiber for the user */
        JanetFunction *func = janet_unwrap_function(argv[0]);
        if (func->def->min_arity > 1) {
            janet_panicf("task function must accept 0 or 1 arguments");
        }
        fiber = janet_fiber(func, 64, func->def->min_arity, &value);
        fiber->flags |=
            JANET_FIBER_MASK_ERROR |
            JANET_FIBER_MASK_USER0 |
            JANET_FIBER_MASK_USER1 |
            JANET_FIBER_MASK_USER2 |
            JANET_FIBER_MASK_USER3 |
            JANET_FIBER_MASK_USER4;
        if (!janet_vm.fiber->env) {
            janet_vm.fiber->env = janet_table(0);
        }
        fiber->env = janet_table(0);
        fiber->env->proto = janet_vm.fiber->env;
    } else {
        fiber = janet_getfiber(argv, 0);
    }
    fiber->supervisor_channel = supervisor;
    janet_schedule(fiber, value);
    return janet_wrap_fiber(fiber);
}

/* For ev/thread - Run an interpreter in the new thread. */
static JanetEVGenericMessage janet_go_thread_subr(JanetEVGenericMessage args) {
    JanetBuffer *buffer = (JanetBuffer *) args.argp;
    const uint8_t *nextbytes = buffer->data;
    const uint8_t *endbytes = nextbytes + buffer->count;
    uint32_t flags = args.tag;
    args.tag = 0;
    janet_init();
    JanetTryState tstate;
    JanetSignal signal = janet_try(&tstate);
    if (!signal) {

        /* Set abstract registry */
        if (!(flags & 0x2)) {
            Janet aregv = janet_unmarshal(nextbytes, endbytes - nextbytes,
                                          JANET_MARSHAL_UNSAFE, NULL, &nextbytes);
            if (!janet_checktype(aregv, JANET_TABLE)) janet_panic("expected table for abstract registry");
            janet_vm.abstract_registry = janet_unwrap_table(aregv);
            janet_gcroot(janet_wrap_table(janet_vm.abstract_registry));
        }

        /* Get supervsior */
        if (flags & 0x8) {
            Janet sup =
                janet_unmarshal(nextbytes, endbytes - nextbytes,
                                JANET_MARSHAL_UNSAFE, NULL, &nextbytes);
            /* Hack - use a global variable to avoid longjmp clobber */
            janet_vm.user = janet_unwrap_pointer(sup);
        }

        /* Set cfunction registry */
        if (!(flags & 0x4)) {
            uint32_t count1;
            memcpy(&count1, nextbytes, sizeof(count1));
            size_t count = (size_t) count1;
            if (count > (endbytes - nextbytes) * sizeof(JanetCFunRegistry)) {
                janet_panic("thread message invalid");
            }
            janet_vm.registry_count = count;
            janet_vm.registry_cap = count;
            janet_vm.registry = janet_malloc(count * sizeof(JanetCFunRegistry));
            if (janet_vm.registry == NULL) {
                JANET_OUT_OF_MEMORY;
            }
            janet_vm.registry_dirty = 1;
            nextbytes += sizeof(uint32_t);
            memcpy(janet_vm.registry, nextbytes, count * sizeof(JanetCFunRegistry));
            nextbytes += count * sizeof(JanetCFunRegistry);
        }

        Janet fiberv = janet_unmarshal(nextbytes, endbytes - nextbytes,
                                       JANET_MARSHAL_UNSAFE, NULL, &nextbytes);
        Janet value = janet_unmarshal(nextbytes, endbytes - nextbytes,
                                      JANET_MARSHAL_UNSAFE, NULL, &nextbytes);
        JanetFiber *fiber;
        if (!janet_checktype(fiberv, JANET_FIBER)) {
            if (!janet_checktype(fiberv, JANET_FUNCTION)) {
                janet_panicf("expected function|fiber, got %v", fiberv);
            }
            JanetFunction *func = janet_unwrap_function(fiberv);
            if (func->def->min_arity > 1) {
                janet_panicf("thread function must accept 0 or 1 arguments");
            }
            fiber = janet_fiber(func, 64, func->def->min_arity, &value);
            fiber->flags |=
                JANET_FIBER_MASK_ERROR |
                JANET_FIBER_MASK_USER0 |
                JANET_FIBER_MASK_USER1 |
                JANET_FIBER_MASK_USER2 |
                JANET_FIBER_MASK_USER3 |
                JANET_FIBER_MASK_USER4;
        } else {
            fiber = janet_unwrap_fiber(fiberv);
        }
        fiber->supervisor_channel = janet_vm.user;
        janet_schedule(fiber, value);
        janet_loop();
        args.tag = JANET_EV_TCTAG_NIL;
    } else {
        void *supervisor = janet_vm.user;
        if (NULL != supervisor) {
            /* Got a supervisor, write error there */
            Janet pair[] = {
                janet_ckeywordv("error"),
                tstate.payload
            };
            janet_channel_push((JanetChannel *)supervisor,
                               janet_wrap_tuple(janet_tuple_n(pair, 2)), 2);
        } else if (flags & 0x1) {
            /* No wait, just print to stderr */
            janet_eprintf("thread start failure: %v\n", tstate.payload);
        } else {
            /* Make ev/thread call from parent thread error */
            if (janet_checktype(tstate.payload, JANET_STRING)) {
                args.tag = JANET_EV_TCTAG_ERR_STRINGF;
                args.argp = strdup((const char *) janet_unwrap_string(tstate.payload));
            } else {
                args.tag = JANET_EV_TCTAG_ERR_STRING;
                args.argp = "failed to start thread";
            }
        }
    }
    janet_restore(&tstate);
    janet_buffer_deinit(buffer);
    janet_free(buffer);
    janet_deinit();
    return args;
}

JANET_CORE_FN(cfun_ev_thread,
              "(ev/thread main &opt value flags supervisor)",
              "Run `main` in a new operating system thread, optionally passing `value` "
              "to resume with. The parameter `main` can either be a fiber, or a function that accepts "
              "0 or 1 arguments. "
              "Unlike `ev/go`, this function will suspend the current fiber until the thread is complete. "
              "If you want to run the thread without waiting for a result, pass the `:n` flag to return nil immediately. "
              "Otherwise, returns nil. Available flags:\n\n"
              "* `:n` - return immediately\n"
              "* `:a` - don't copy abstract registry to new thread (performance optimization)\n"
              "* `:c` - don't copy cfunction registry to new thread (performance optimization)") {
    janet_arity(argc, 1, 4);
    Janet value = argc >= 2 ? argv[1] : janet_wrap_nil();
    if (!janet_checktype(argv[0], JANET_FUNCTION)) janet_getfiber(argv, 0);
    uint64_t flags = 0;
    if (argc >= 3) {
        flags = janet_getflags(argv, 2, "nac");
    }
    void *supervisor = janet_optabstract(argv, argc, 3, &janet_channel_type, janet_vm.root_fiber->supervisor_channel);
    if (NULL != supervisor) flags |= 0x8;

    /* Marshal arguments for the new thread. */
    JanetBuffer *buffer = janet_malloc(sizeof(JanetBuffer));
    if (NULL == buffer) {
        JANET_OUT_OF_MEMORY;
    }
    janet_buffer_init(buffer, 0);
    if (!(flags & 0x2)) {
        janet_marshal(buffer, janet_wrap_table(janet_vm.abstract_registry), NULL, JANET_MARSHAL_UNSAFE);
    }
    if (flags & 0x8) {
        janet_marshal(buffer, janet_wrap_abstract(supervisor), NULL, JANET_MARSHAL_UNSAFE);
    }
    if (!(flags & 0x4)) {
        janet_assert(janet_vm.registry_count <= INT32_MAX, "assert failed size check");
        uint32_t temp = (uint32_t) janet_vm.registry_count;
        janet_buffer_push_bytes(buffer, (uint8_t *) &temp, sizeof(temp));
        janet_buffer_push_bytes(buffer, (uint8_t *) janet_vm.registry, (int32_t) janet_vm.registry_count * sizeof(JanetCFunRegistry));
    }
    janet_marshal(buffer, argv[0], NULL, JANET_MARSHAL_UNSAFE);
    janet_marshal(buffer, value, NULL, JANET_MARSHAL_UNSAFE);
    if (flags & 0x1) {
        /* Return immediately */
        JanetEVGenericMessage arguments;
        memset(&arguments, 0, sizeof(arguments));
        arguments.tag = (uint32_t) flags;
        arguments.argi = argc;
        arguments.argp = buffer;
        arguments.fiber = NULL;
        janet_ev_threaded_call(janet_go_thread_subr, arguments, janet_ev_default_threaded_callback);
        return janet_wrap_nil();
    } else {
        janet_ev_threaded_await(janet_go_thread_subr, (uint32_t) flags, argc, buffer);
    }
}

JANET_CORE_FN(cfun_ev_give_supervisor,
              "(ev/give-supervisor tag & payload)",
              "Send a message to the current supervior channel if there is one. The message will be a "
              "tuple of all of the arguments combined into a single message, where the first element is tag. "
              "By convention, tag should be a keyword indicating the type of message. Returns nil.") {
    janet_arity(argc, 1, -1);
    void *chanv = janet_vm.root_fiber->supervisor_channel;
    if (NULL != chanv) {
        JanetChannel *chan = janet_channel_unwrap(chanv);
        if (janet_channel_push(chan, janet_wrap_tuple(janet_tuple_n(argv, argc)), 0)) {
            janet_await();
        }
    }
    return janet_wrap_nil();
}

JANET_NO_RETURN void janet_sleep_await(double sec) {
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = janet_vm.root_fiber;
    to.is_error = 0;
    to.sched_id = to.fiber->sched_id;
    to.curr_fiber = NULL;
    add_timeout(to);
    janet_await();
}

JANET_CORE_FN(cfun_ev_sleep,
              "(ev/sleep sec)",
              "Suspend the current fiber for sec seconds without blocking the event loop.") {
    janet_fixarity(argc, 1);
    double sec = janet_getnumber(argv, 0);
    janet_sleep_await(sec);
}

JANET_CORE_FN(cfun_ev_deadline,
              "(ev/deadline sec &opt tocancel tocheck)",
              "Set a deadline for a fiber `tocheck`. If `tocheck` is not finished after `sec` seconds, "
              "`tocancel` will be canceled as with `ev/cancel`. "
              "If `tocancel` and `tocheck` are not given, they default to `(fiber/root)` and "
              "`(fiber/current)` respectively. Returns `tocancel`.") {
    janet_arity(argc, 1, 3);
    double sec = janet_getnumber(argv, 0);
    JanetFiber *tocancel = janet_optfiber(argv, argc, 1, janet_vm.root_fiber);
    JanetFiber *tocheck = janet_optfiber(argv, argc, 2, janet_vm.fiber);
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = tocancel;
    to.curr_fiber = tocheck;
    to.is_error = 0;
    to.sched_id = to.fiber->sched_id;
    add_timeout(to);
    return janet_wrap_fiber(tocancel);
}

JANET_CORE_FN(cfun_ev_cancel,
              "(ev/cancel fiber err)",
              "Cancel a suspended fiber in the event loop. Differs from cancel in that it returns the canceled fiber immediately") {
    janet_fixarity(argc, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet err = argv[1];
    janet_cancel(fiber, err);
    return argv[0];
}

JANET_CORE_FN(janet_cfun_stream_close,
              "(ev/close stream)",
              "Close a stream. This should be the same as calling (:close stream) for all streams.") {
    janet_fixarity(argc, 1);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_close(stream);
    return argv[0];
}

JANET_CORE_FN(janet_cfun_stream_read,
              "(ev/read stream n &opt buffer timeout)",
              "Read up to n bytes into a buffer asynchronously from a stream. `n` can also be the keyword "
              "`:all` to read into the buffer until end of stream. "
              "Optionally provide a buffer to write into "
              "as well as a timeout in seconds after which to cancel the operation and raise an error. "
              "Returns the buffer if the read was successful or nil if end-of-stream reached. Will raise an "
              "error if there are problems with the IO operation.") {
    janet_arity(argc, 2, 4);
    JanetStream *stream = janet_getabstract(argv, 0, &janet_stream_type);
    janet_stream_flags(stream, JANET_STREAM_READABLE);
    JanetBuffer *buffer = janet_optbuffer(argv, argc, 2, 10);
    double to = janet_optnumber(argv, argc, 3, INFINITY);
    if (janet_keyeq(argv[1], "all")) {
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_readchunk(stream, buffer, INT32_MAX);
    } else {
        int32_t n = janet_getnat(argv, 1);
        if (to != INFINITY) janet_addtimeout(to);
        janet_ev_read(stream, buffer, n);
    }
    janet_await();
}

JANET_CORE_FN(janet_cfun_stream_chunk,
              "(ev/chunk stream n &opt buffer timeout)",
              "Same as ev/read, but will not return early if less than n bytes are available. If an end of "
              "stream is reached, will also return early with the collected bytes.") {
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

JANET_CORE_FN(janet_cfun_stream_write,
              "(ev/write stream data &opt timeout)",
              "Write data to a stream, suspending the current fiber until the write "
              "completes. Takes an optional timeout in seconds, after which will return nil. "
              "Returns nil, or raises an error if the write failed.") {
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

void janet_lib_ev(JanetTable *env) {
    JanetRegExt ev_cfuns_ext[] = {
        JANET_CORE_REG("ev/give", cfun_channel_push),
        JANET_CORE_REG("ev/take", cfun_channel_pop),
        JANET_CORE_REG("ev/full", cfun_channel_full),
        JANET_CORE_REG("ev/capacity", cfun_channel_capacity),
        JANET_CORE_REG("ev/count", cfun_channel_count),
        JANET_CORE_REG("ev/select", cfun_channel_choice),
        JANET_CORE_REG("ev/rselect", cfun_channel_rchoice),
        JANET_CORE_REG("ev/chan", cfun_channel_new),
        JANET_CORE_REG("ev/thread-chan", cfun_channel_new_threaded),
        JANET_CORE_REG("ev/chan-close", cfun_channel_close),
        JANET_CORE_REG("ev/go", cfun_ev_go),
        JANET_CORE_REG("ev/thread", cfun_ev_thread),
        JANET_CORE_REG("ev/give-supervisor", cfun_ev_give_supervisor),
        JANET_CORE_REG("ev/sleep", cfun_ev_sleep),
        JANET_CORE_REG("ev/deadline", cfun_ev_deadline),
        JANET_CORE_REG("ev/cancel", cfun_ev_cancel),
        JANET_CORE_REG("ev/close", janet_cfun_stream_close),
        JANET_CORE_REG("ev/read", janet_cfun_stream_read),
        JANET_CORE_REG("ev/chunk", janet_cfun_stream_chunk),
        JANET_CORE_REG("ev/write", janet_cfun_stream_write),
        JANET_REG_END
    };

    janet_core_cfuns_ext(env, NULL, ev_cfuns_ext);
    janet_register_abstract_type(&janet_stream_type);
    janet_register_abstract_type(&janet_channel_type);
}

#endif

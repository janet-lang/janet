/*
* Copyright (c) 2020 Calvin Rose
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
#include "vector.h"
#include "fiber.h"
#endif

#ifdef JANET_EV

/* Includes */

#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/timerfd.h>

/* New fibers to spawn or resume */
typedef struct JanetTask JanetTask;
struct JanetTask {
    JanetFiber *fiber;
    Janet value;
};

/* Min priority queue of timestamps for timeouts. */
typedef uint64_t JanetTimestamp;
typedef struct JanetTimeout JanetTimeout;
struct JanetTimeout {
    JanetTimestamp when;
    JanetFiber *fiber;
};

/* Forward declaration */
static void janet_unlisten(JanetListenerState *state);

/* Global data */
JANET_THREAD_LOCAL size_t janet_vm_active_listeners = 0;
JANET_THREAD_LOCAL size_t janet_vm_spawn_capacity = 0;
JANET_THREAD_LOCAL size_t janet_vm_spawn_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_tq_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_tq_capacity = 0;
JANET_THREAD_LOCAL JanetTask *janet_vm_spawn = NULL;
JANET_THREAD_LOCAL JanetTimeout *janet_vm_tq = NULL;

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
    janet_vm_tq[index].fiber->timeout_index = -1;
    janet_vm_tq[index] = janet_vm_tq[--janet_vm_tq_count];
    janet_vm_tq[index].fiber->timeout_index = index;
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
        janet_vm_tq[index].fiber->timeout_index = index;
        janet_vm_tq[smallest].fiber->timeout_index = smallest;
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
    janet_vm_tq_count = newcount;
    janet_vm_tq[oldcount] = to;
    /* Heapify */
    size_t index = oldcount;
    if (to.fiber->timeout_index >= 0) {
        pop_timeout(to.fiber->timeout_index);
    }
    to.fiber->timeout_index = index;
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
static JanetListenerState *janet_listen_impl(JanetPollable *pollable, JanetListener behavior, int mask, size_t size) {
    if (pollable->_mask & mask) {
        janet_panic("cannot listen for duplicate event on pollable");
    }
    if (size < sizeof(JanetListenerState))
        size = sizeof(JanetListenerState);
    JanetListenerState *state = malloc(size);
    if (NULL == state) {
        JANET_OUT_OF_MEMORY;
    }
    state->machine = behavior;
    if (mask & JANET_ASYNC_LISTEN_SPAWNER) {
        state->fiber = NULL;
    } else {
        state->fiber = janet_vm_root_fiber;
        janet_v_push(janet_vm_root_fiber->waiting, state);
    }
    mask |= JANET_ASYNC_LISTEN_SPAWNER;
    state->pollable = pollable;
    state->_mask = mask;
    pollable->_mask |= mask;
    janet_vm_active_listeners++;
    /* Prepend to linked list */
    state->_next = pollable->state;
    pollable->state = state;
    /* Emit INIT event for convenience */
    state->machine(state, JANET_ASYNC_EVENT_INIT);
    return state;
}

/* Indicate we are no longer listening for an event. This
 * frees the memory of the state machine as well. */
static void janet_unlisten_impl(JanetListenerState *state) {
    state->machine(state, JANET_ASYNC_EVENT_DEINIT);
    /* Remove state machine from poll list */
    JanetListenerState **iter = &(state->pollable->state);
    while (*iter && *iter != state)
        iter = &((*iter)->_next);
    janet_assert(*iter, "failed to remove listener");
    *iter = state->_next;
    janet_vm_active_listeners--;
    /* Remove mask */
    state->pollable->_mask &= ~(state->_mask);
    /* Ensure fiber does not reference this state */
    JanetFiber *fiber = state->fiber;
    if (NULL != fiber) {
        int32_t count = janet_v_count(fiber->waiting);
        for (int32_t i = 0; i < count; i++) {
            if (fiber->waiting[i] == state) {
                fiber->waiting[i] = janet_v_last(fiber->waiting);
                janet_v_pop(fiber->waiting);
                break;
            }
        }
    }
    free(state);
}

/* Call after creating a pollable */
void janet_pollable_init(JanetPollable *pollable, JanetPollType handle) {
    pollable->handle = handle;
    pollable->flags = 0;
    pollable->state = NULL;
    pollable->_mask = 0;
}

/* Mark a pollable for GC */
void janet_pollable_mark(JanetPollable *pollable) {
    JanetListenerState *state = pollable->state;
    while (NULL != state) {
        if (NULL != state->fiber) {
            janet_mark(janet_wrap_fiber(state->fiber));
        }
        (state->machine)(state, JANET_ASYNC_EVENT_MARK);
        state = state->_next;
    }
}

/* Must be called to close all pollables - does NOT call `close` for you.
 * Also does not free memory of the pollable, so can be used on close. */
void janet_pollable_deinit(JanetPollable *pollable) {
    pollable->flags |= JANET_POLL_FLAG_CLOSED;
    JanetListenerState *state = pollable->state;
    while (NULL != state) {
        state->machine(state, JANET_ASYNC_EVENT_CLOSE);
        JanetListenerState *next_state = state->_next;
        janet_unlisten_impl(state);
        state = next_state;
    }
    pollable->state = NULL;
}

/* Cancel any state machines waiting on this fiber. */
void janet_cancel(JanetFiber *fiber) {
    int32_t lcount = janet_v_count(fiber->waiting);
    janet_v_empty(fiber->waiting);
    for (int32_t index = 0; index < lcount; index++) {
        janet_unlisten(fiber->waiting[index]);
    }
    /* Clear timeout on the current fiber */
    if (fiber->timeout_index >= 0) {
        pop_timeout(fiber->timeout_index);
        fiber->timeout_index = -1;
    }
}

/* Register a fiber to resume with value */
void janet_schedule(JanetFiber *fiber, Janet value) {
    if (fiber->flags & JANET_FIBER_FLAG_SCHEDULED) return;
    fiber->flags |= JANET_FIBER_FLAG_SCHEDULED;
    size_t oldcount = janet_vm_spawn_count;
    size_t newcount = oldcount + 1;
    if (newcount > janet_vm_spawn_capacity) {
        size_t newcap = 2 * newcount;
        JanetTask *tasks = realloc(janet_vm_spawn, newcap * sizeof(JanetTask));
        if (NULL == tasks) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_spawn = tasks;
        janet_vm_spawn_capacity = newcap;
    }
    janet_vm_spawn_count = newcount;
    janet_vm_spawn[oldcount].fiber = fiber;
    janet_vm_spawn[oldcount].value = value;
}

/* Mark all pending tasks */
void janet_ev_mark(void) {
    for (size_t i = 0; i < janet_vm_spawn_count; i++) {
        janet_mark(janet_wrap_fiber(janet_vm_spawn[i].fiber));
        janet_mark(janet_vm_spawn[i].value);
    }
    for (size_t i = 0; i < janet_vm_tq_count; i++) {
        janet_mark(janet_wrap_fiber(janet_vm_tq[i].fiber));
    }
}

/* Run a top level task */
static void run_one(JanetFiber *fiber, Janet value) {
    fiber->flags &= ~JANET_FIBER_FLAG_SCHEDULED;
    Janet res;
    JanetSignal sig = janet_continue(fiber, value, &res);
    if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_EVENT) {
        janet_stacktrace(fiber, res);
    }
}

/* Common init code */
void janet_ev_init_common(void) {
    janet_vm_spawn_capacity = 0;
    janet_vm_spawn_count = 0;
    janet_vm_spawn = NULL;
    janet_vm_active_listeners = 0;
    janet_vm_tq = NULL;
    janet_vm_tq_count = 0;
    janet_vm_tq_capacity = 0;
}

/* Common deinit code */
void janet_ev_deinit_common(void) {
    free(janet_vm_spawn);
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
    add_timeout(to);
}

/* Channels */

typedef struct {
    int32_t capacity;
    int32_t head;
    int32_t tail;
    JanetFiber **fibers;
} JanetFiberQueue;

#define JANET_MAX_FQ_CAPACITY 0xFFFFFF

static void janet_fq_init(JanetFiberQueue *fq) {
    fq->fibers = NULL;
    fq->head = 0;
    fq->tail = 0;
    fq->capacity = 0;
}

static int32_t janet_fq_count(JanetFiberQueue *fq) {
    return (fq->head > fq->tail)
        ? (fq->tail + fq->capacity - fq->head)
        : (fq->tail - fq->head);
}

static int janet_fq_push(JanetFiberQueue *fq, JanetFiber *fiber) {
    int32_t count = janet_fq_count(fq);
    if (count + 1 == JANET_MAX_FQ_CAPACITY) return 1;
    /* Resize if needed */
    if (count + 1 > fq->capacity) {
        int32_t newcap = (count + 1) * 2;
        if (newcap > JANET_MAX_FQ_CAPACITY) newcap = JANET_MAX_FQ_CAPACITY;
        if (fq->head > fq->tail) {
            /* Two segments, we need to allocate and memcpy x2 */
            JanetFiber **newfibers = malloc(sizeof(JanetFiber *) * newcap);
            if (NULL == newfibers) {
                JANET_OUT_OF_MEMORY;
            }
            int32_t seg1 = fq->capacity - fq->head;
            int32_t seq2 = fq->tail;
            memcpy(newfibers, fq->fibers + fq->head, seg1 * sizeof(JanetFiber *));
            memcpy(newfibers + seg1, fq->fibers, seq2 * sizeof(JanetFiber *));
            free(fq->fibers);
            fq->fibers = newfibers;
            fq->head = 0;
            fq->tail = count;
        } else {
            /* One segment, we can just realloc */
            fq->fibers = realloc(fq->fibers, sizeof(JanetFiber *) * newcap);
            if (NULL == fq->fibers) {
                JANET_OUT_OF_MEMORY;
            }
        }
        fq->capacity = newcap;
    }
    fq->fibers[fq->tail++] = fiber;
    if (fq->tail >= fq->capacity) fq->tail = 0;
    return 0;
}

static int janet_fq_pop(JanetFiberQueue *fq, JanetFiber **out) {
    if (fq->head == fq->tail) return 1;
    *out = fq->fibers[fq->head++];
    if (fq->head >= fq->capacity) fq->head = 0;
    return 0;
}

/* Main event loop */

void janet_loop1_impl(void);

void janet_loop(void) {
    while (janet_vm_active_listeners || janet_vm_spawn_count || janet_vm_tq_count) {
        /* Run expired timers */
        JanetTimeout to;
        while (peek_timeout(&to) && to.when <= ts_now()) {
            pop_timeout(0);
            janet_schedule(to.fiber, janet_wrap_nil());
        }
        /* Run scheduled fibers */
        size_t index = 0;
        while (index < janet_vm_spawn_count) {
            JanetTask task = janet_vm_spawn[index];
            run_one(task.fiber, task.value);
            index++;
        }
        janet_vm_spawn_count = 0;
        /* Poll for events */
        if (janet_vm_active_listeners || janet_vm_tq_count) {
            janet_loop1_impl();
        }
    }
}

#ifdef JANET_LINUX

/*
 * Start linux/epoll implementation
 */

static JanetTimestamp ts_now(void) {
    struct timespec now;
    janet_assert(-1 != clock_gettime(CLOCK_MONOTONIC, &now), "failed to get time");
    uint64_t res = 1000 * now.tv_sec;
    res += now.tv_nsec / 1000000;
    return res;
}

/* Epoll global data */
JANET_THREAD_LOCAL int janet_vm_epoll = 0;
JANET_THREAD_LOCAL int janet_vm_timerfd = 0;
JANET_THREAD_LOCAL int janet_vm_timer_enabled = 0;

static int make_epoll_events(int mask) {
    int events = EPOLLET;
    if (mask & JANET_ASYNC_LISTEN_READ)
        events |= EPOLLIN;
    if (mask & JANET_ASYNC_LISTEN_WRITE)
        events |= EPOLLOUT;
    return events;
}

/* Wait for the next event */
JanetListenerState *janet_listen(JanetPollable *pollable, JanetListener behavior, int mask, size_t size) {
    int is_first = !(pollable->state);
    int op = is_first ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    JanetListenerState *state = janet_listen_impl(pollable, behavior, mask, size);
    struct epoll_event ev;
    ev.events = make_epoll_events(state->pollable->_mask);
    ev.data.ptr = pollable;
    int status;
    do {
        status = epoll_ctl(janet_vm_epoll, op, pollable->handle, &ev);
    } while (status == -1 && errno == EINTR);
    if (status == -1) {
        janet_unlisten_impl(state);
        janet_panicf("failed to schedule event: %s", strerror(errno));
    }
    return state;
}

/* Tell system we are done listening for a certain event */
static void janet_unlisten(JanetListenerState *state) {
    JanetPollable *pollable = state->pollable;
    int is_last = (state->_next == NULL && pollable->state == state);
    int op = is_last ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    struct epoll_event ev;
    ev.events = make_epoll_events(pollable->_mask & ~state->_mask);
    ev.data.ptr = pollable;
    int status;
    do {
        status = epoll_ctl(janet_vm_epoll, op, pollable->handle, &ev);
    } while (status == -1 && errno == EINTR);
    if (status == -1) {
        janet_panicf("failed to unschedule event: %s", strerror(errno));
    }
    /* Destroy state machine and free memory */
    janet_unlisten_impl(state);
}

/* Replace janet_loop with this */
#define JANET_EPOLL_MAX_EVENTS 64
void janet_loop1_impl(void) {
    /* Set timer */
    JanetTimeout to;
    struct itimerspec its;
    memset(&to, 0, sizeof(to));
    int has_timeout = peek_timeout(&to);
    if (janet_vm_timer_enabled || has_timeout) {
        memset(&its, 0, sizeof(its));
        if (has_timeout) {
            its.it_value.tv_sec = to.when / 1000;
            its.it_value.tv_nsec = (to.when % 1000) * 1000000;
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
        JanetPollable *pollable = events[i].data.ptr;
        if (NULL == pollable) {
            /* Timer event */
            pop_timeout(0);
            /* Cancel waiters for this fiber */
            janet_schedule(to.fiber, janet_wrap_nil());
        } else {
            /* Normal event */
            int mask = events[i].events;
            JanetListenerState *state = pollable->state;
            while (NULL != state) {
                JanetListenerState *next_state = state->_next;
                JanetAsyncStatus status1 = JANET_ASYNC_STATUS_NOT_DONE;
                JanetAsyncStatus status2 = JANET_ASYNC_STATUS_NOT_DONE;
                if (mask & EPOLLOUT)
                    status1 = state->machine(state, JANET_ASYNC_EVENT_WRITE);
                if (mask & EPOLLIN)
                    status2 = state->machine(state, JANET_ASYNC_EVENT_READ);
                if (status1 == JANET_ASYNC_STATUS_DONE || status2 == JANET_ASYNC_STATUS_DONE)
                    janet_unlisten(state);
                state = next_state;
            }
        }
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm_epoll = epoll_create1(EPOLL_CLOEXEC);
    janet_vm_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    janet_vm_timer_enabled = 0;
    if (janet_vm_epoll == -1 || janet_vm_timerfd == -1) goto error;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = NULL;
    if (-1 == epoll_ctl(janet_vm_epoll, EPOLL_CTL_ADD, janet_vm_timerfd, &ev)) goto error;
    return;
error:
    JANET_EXIT("failed to initialize event loop");
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    close(janet_vm_epoll);
    close(janet_vm_timerfd);
    janet_vm_epoll = 0;
}

/*
 * End epoll implementation
 */

#else


#endif


/* C functions */

static Janet cfun_ev_go(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet value = argc == 2 ? argv[1] : janet_wrap_nil();
    janet_schedule(fiber, value);
    return argv[0];
}

static Janet cfun_ev_call(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, -1);
    JanetFunction *fn = janet_getfunction(argv, 0);
    JanetFiber *fiber = janet_fiber(fn, 64, argc - 1, argv + 1);
    janet_schedule(fiber, janet_wrap_nil());
    return janet_wrap_fiber(fiber);
}

static Janet cfun_ev_sleep(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    double sec = janet_getnumber(argv, 0);
    JanetTimeout to;
    to.when = ts_delta(ts_now(), sec);
    to.fiber = janet_vm_root_fiber;
    add_timeout(to);
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
        JDOC("(ev/go fiber &opt value)\n\n"
             "Put a fiber on the event loop to be resumed later. Optionally pass "
             "a value to resume with, otherwise resumes with nil.")
    },
    {
        "ev/sleep", cfun_ev_sleep,
        JDOC("(ev/sleep sec)\n\n"
             "Suspend the current fiber for sec seconds without blocking the event loop.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_ev(JanetTable *env) {
    janet_core_cfuns(env, NULL, ev_cfuns);
}

#endif

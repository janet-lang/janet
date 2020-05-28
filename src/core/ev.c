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
#endif

#ifdef JANET_EV

/* Includes */

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
typedef struct JanetListenerTimeout JanetListenerTimeout;
struct JanetListenerTimeout JanetListenerTimeout {
    JanetListenerState *state;
    struct timespec when;
};

/* Global data */
JANET_THREAD_LOCAL size_t janet_vm_active_listeners = 0;
JANET_THREAD_LOCAL size_t janet_vm_spawn_capacity = 0;
JANET_THREAD_LOCAL size_t janet_vm_spawn_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_tq_count = 0;
JANET_THREAD_LOCAL size_t janet_vm_tq_capacity = 0;
JANET_THREAD_LOCAL JanetTask *janet_vm_spawn = NULL;
JANET_THREAD_LOCAL JanetListenerTimeout *janet_vm_tq = NULL;

/* Compare two timespecs - 1 if t1 > t2 */
static int timespec_cmp(struct timespec t1, struct timespec t2) {
    if (t1.tv_sec < t2.tv_sec) return -1;
    if (t1.tv_sec > t2.tv_sec) return 1;
    if (t1.tv_nsec < t2.tv_nsec) return -1;
    if (t1.tv_nsec > t2.tv_nsec) return 1;
    return 0;
}

/* Add a timeout to the timeout min heap */
static void add_timeout(JanetListenerState *state, struct timespec when) {
    size_t oldcount = janet_vm_tq_count;
    size_t newcount = oldcount + 1;
    if (oldcount == janet_vm_tq_capacity) {
        size_t newcap = 2 * newcount;
        JanetListenerTimeout *tq = realloc(janet_vm_tq, newcap * sizeof(JanetListenerTimeout));
        if (NULL == tq) {
            JANET_OUT_OF_MEMORY;
        }
        janet_vm_tq_capacity = newcap;
    }
    /* Append */
    janet_vm_tq_count = newcount;
    janet_vm_tq[oldcount] = { state, when };
    /* Heapify */
    size_t index = oldcount;
    while (index > 0) {
        size_t parent = (index - 1) >> 1;
        int cmp = timespec_cmp(janet_vm_tq[parent].when, when);
        if (cmp <= 0) break;
        /* Swap */
        JanetListenerState tmp = janet_vm_tq[index];
        janet_vm_tq[index] = janet_vm_tq[parent];
        janet_vm_tq[parent] = tmp;
        /* Next */
        index = parent;
    }
}

/* Extract the next timeout from the priority queue */
static JanetListenerTimeout next_timeout(void) {

}

/* Create a new event listener */
static JanetListenerState *janet_listen_impl(JanetPollable *pollable, JanetListener behavior, int mask, size_t size) {
    if (size < sizeof(JanetListenerState))
        size = sizeof(JanetListenerState);
    JanetListenerState *state = malloc(size);
    if (NULL == state) {
        JANET_OUT_OF_MEMORY;
    }
    state->machine = behavior;
    state->fiber = janet_vm_root_fiber;
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

/* Register a fiber to resume with value */
void janet_schedule(JanetFiber *fiber, Janet value) {
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
}

/* Run scheduled tasks */
static void run_scheduled(void) {
    size_t index = 0;
    while (index < janet_vm_spawn_count) {
        JanetTask task = janet_vm_spawn[index];
        Janet res;
        JanetSignal sig = janet_continue(task.fiber, task.value, &res);
        if (sig != JANET_SIGNAL_OK && sig != JANET_SIGNAL_EVENT) {
            janet_stacktrace(task.fiber, res);
        }
        index++;
    }
    janet_vm_spawn_count = 0;
}

/* Main event loop */

void janet_loop1_impl(void);

void janet_loop1(void) {
    if (janet_vm_active_listeners) {
        janet_loop1_impl();
    }
    /* Run scheduled fibers */
    run_scheduled();
}

void janet_loop(void) {
    while (janet_vm_active_listeners || janet_vm_spawn_count) janet_loop1();
}

/* Common init code */
void janet_ev_init_common(void) {
    janet_vm_spawn_capacity = 0;
    janet_vm_spawn_count = 0;
    janet_vm_spawn = NULL;
    janet_vm_active_listeners = 0;
}

/* Common deinit code */
void janet_ev_deinit_common(void) {
    free(janet_vm_spawn);
}

/* Short hand to yield to event loop */
void janet_await(void) {
    janet_signalv(JANET_SIGNAL_EVENT, janet_wrap_nil());
}

/*
 * Start epoll implementation
 */

/* Epoll global data */
JANET_THREAD_LOCAL int janet_vm_epoll = 0;

static int make_epoll_events(int mask) {
    int events = 0;
    if (mask & JANET_ASYNC_EVENT_READ)
        events |= EPOLLIN;
    if (mask & JANET_ASYNC_EVENT_WRITE)
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
void janet_unlisten(JanetListenerState *state) {
    JanetPollable *pollable = state->pollable;
    int is_last = (state->_next == NULL && pollable->state == state);
    int op = is_last ? EPOLL_CTL_DEL : EPOLL_CTL_MOD;
    struct epoll_event ev;
    ev.events = make_epoll_events(pollable->_mask);
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
        int mask = events[i].events;
        JanetListenerState *state = pollable->state;
        while (NULL != state) {
            if (mask & EPOLLOUT)
                state->machine(state, JANET_ASYNC_EVENT_WRITE);
            if (mask & EPOLLIN)
                state->machine(state, JANET_ASYNC_EVENT_READ);
            state = state->_next;
        }
    }
}

void janet_ev_init(void) {
    janet_ev_init_common();
    janet_vm_epoll = epoll_create1(EPOLL_CLOEXEC);
}

void janet_ev_deinit(void) {
    janet_ev_deinit_common();
    close(janet_vm_epoll);
    janet_vm_epoll = 0;
}

/*
 * End epoll implementation
 */

/* C functions */

static Janet cfun_ev_spawn(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 2);
    JanetFiber *fiber = janet_getfiber(argv, 0);
    Janet value = argc == 2 ? argv[1] : janet_wrap_nil();
    janet_schedule(fiber, value);
    return argv[0];
}

static const JanetReg ev_cfuns[] = {
    {
        "ev/go", cfun_ev_spawn,
        JDOC("(ev/go fiber &opt value)\n\n"
            "Put a fiber on the event loop to be resumed later. Optionally pass "
            "a value to resume with, otherwise resumes with nil.")
    },
    {NULL, NULL, NULL}
};

void janet_lib_ev(JanetTable *env) {
    janet_core_cfuns(env, NULL, ev_cfuns);
}

#endif

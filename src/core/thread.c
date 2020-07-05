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
#include "gc.h"
#include "util.h"
#include "state.h"
#endif

#ifdef JANET_THREADS

#include <math.h>
#ifdef JANET_WINDOWS
#include <windows.h>
#else
#include <setjmp.h>
#include <time.h>
#include <pthread.h>
#endif

/* typedefed in janet.h */
struct JanetMailbox {

    /* Synchronization */
#ifdef JANET_WINDOWS
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t lock;
    pthread_cond_t cond;
#endif

    /* Memory management - reference counting */
    int refCount;
    int closed;

    /* Store messages */
    uint16_t messageCapacity;
    uint16_t messageCount;
    uint16_t messageFirst;
    uint16_t messageNext;

    /* Buffers to store messages. These buffers are manually allocated, so
     * are not owned by any thread's GC. */
    JanetBuffer messages[];
};

#define JANET_THREAD_HEAVYWEIGHT 0x1
#define JANET_THREAD_ABSTRACTS 0x2
#define JANET_THREAD_CFUNCTIONS 0x4
static const char janet_thread_flags[] = "hac";

typedef struct {
    JanetMailbox *original;
    JanetMailbox *newbox;
    uint64_t flags;
} JanetMailboxPair;

static JANET_THREAD_LOCAL JanetMailbox *janet_vm_mailbox = NULL;
static JANET_THREAD_LOCAL JanetThread *janet_vm_thread_current = NULL;
static JANET_THREAD_LOCAL JanetTable *janet_vm_thread_decode = NULL;

static JanetTable *janet_thread_get_decode(void) {
    if (janet_vm_thread_decode == NULL) {
        janet_vm_thread_decode = janet_get_core_table("load-image-dict");
        janet_gcroot(janet_wrap_table(janet_vm_thread_decode));
    }
    return janet_vm_thread_decode;
}

static JanetMailbox *janet_mailbox_create(int refCount, uint16_t capacity) {
    JanetMailbox *mailbox = malloc(sizeof(JanetMailbox) + sizeof(JanetBuffer) * (size_t) capacity);
    if (NULL == mailbox) {
        JANET_OUT_OF_MEMORY;
    }
#ifdef JANET_WINDOWS
    InitializeCriticalSection(&mailbox->lock);
    InitializeConditionVariable(&mailbox->cond);
#else
    pthread_mutex_init(&mailbox->lock, NULL);
    pthread_cond_init(&mailbox->cond, NULL);
#endif
    mailbox->refCount = refCount;
    mailbox->closed = 0;
    mailbox->messageCount = 0;
    mailbox->messageCapacity = capacity;
    mailbox->messageFirst = 0;
    mailbox->messageNext = 0;
    for (uint16_t i = 0; i < capacity; i++) {
        janet_buffer_init(mailbox->messages + i, 0);
    }
    return mailbox;
}

static void janet_mailbox_destroy(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    DeleteCriticalSection(&mailbox->lock);
#else
    pthread_mutex_destroy(&mailbox->lock);
    pthread_cond_destroy(&mailbox->cond);
#endif
    for (uint16_t i = 0; i < mailbox->messageCapacity; i++) {
        janet_buffer_deinit(mailbox->messages + i);
    }
    free(mailbox);
}

static void janet_mailbox_lock(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    EnterCriticalSection(&mailbox->lock);
#else
    pthread_mutex_lock(&mailbox->lock);
#endif
}

static void janet_mailbox_unlock(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    LeaveCriticalSection(&mailbox->lock);
#else
    pthread_mutex_unlock(&mailbox->lock);
#endif
}

/* Assumes you have the mailbox lock already */
static void janet_mailbox_ref_with_lock(JanetMailbox *mailbox, int delta) {
    mailbox->refCount += delta;
    if (mailbox->refCount <= 0) {
        janet_mailbox_unlock(mailbox);
        janet_mailbox_destroy(mailbox);
    } else {
        janet_mailbox_unlock(mailbox);
    }
}

static void janet_mailbox_ref(JanetMailbox *mailbox, int delta) {
    janet_mailbox_lock(mailbox);
    janet_mailbox_ref_with_lock(mailbox, delta);
}

static void janet_close_thread(JanetThread *thread) {
    if (thread->mailbox) {
        janet_mailbox_ref(thread->mailbox, -1);
        thread->mailbox = NULL;
    }
}

static int thread_gc(void *p, size_t size) {
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    janet_close_thread(thread);
    return 0;
}

static int thread_mark(void *p, size_t size) {
    (void) size;
    JanetThread *thread = (JanetThread *)p;
    if (thread->encode) {
        janet_mark(janet_wrap_table(thread->encode));
    }
    return 0;
}

static JanetMailboxPair *make_mailbox_pair(JanetMailbox *original, uint64_t flags) {
    JanetMailboxPair *pair = malloc(sizeof(JanetMailboxPair));
    if (NULL == pair) {
        JANET_OUT_OF_MEMORY;
    }
    pair->original = original;
    janet_mailbox_ref(original, 1);
    pair->newbox = janet_mailbox_create(1, 16);
    pair->flags = flags;
    return pair;
}

static void destroy_mailbox_pair(JanetMailboxPair *pair) {
    janet_mailbox_ref(pair->original, -1);
    janet_mailbox_ref(pair->newbox, -1);
    free(pair);
}

/* Abstract waiting for timeout across windows/posix */
typedef struct {
    int timedwait;
    int nowait;
#ifdef JANET_WINDOWS
    DWORD interval;
    DWORD ticksLeft;
#else
    struct timespec ts;
#endif
} JanetWaiter;

static void janet_waiter_init(JanetWaiter *waiter, double sec) {
    waiter->timedwait = 0;
    waiter->nowait = 0;

    if (sec <= 0.0 || isnan(sec)) {
        waiter->nowait = 1;
        return;
    }
    waiter->timedwait = sec > 0.0 && !isinf(sec);

    /* Set maximum wait time to 30 days */
    if (sec > (60.0 * 60.0 * 24.0 * 30.0)) {
        sec = 60.0 * 60.0 * 24.0 * 30.0;
    }

#ifdef JANET_WINDOWS
    if (waiter->timedwait) {
        waiter->ticksLeft = waiter->interval = (DWORD) floor(1000.0 * sec);
    }
#else
    if (waiter->timedwait) {
        /* N seconds -> timespec of (now + sec) */
        struct timespec now;
        janet_gettime(&now);
        time_t tvsec = (time_t) floor(sec);
        long tvnsec = (long) floor(1000000000.0 * (sec - ((double) tvsec)));
        tvsec += now.tv_sec;
        tvnsec += now.tv_nsec;
        if (tvnsec >= 1000000000L) {
            tvnsec -= 1000000000L;
            tvsec += 1;
        }
        waiter->ts.tv_sec = tvsec;
        waiter->ts.tv_nsec = tvnsec;
    }
#endif
}

static int janet_waiter_wait(JanetWaiter *wait, JanetMailbox *mailbox) {
    if (wait->nowait) return 1;
#ifdef JANET_WINDOWS
    if (wait->timedwait) {
        if (wait->ticksLeft == 0) return 1;
        DWORD startTime = GetTickCount();
        int status = !SleepConditionVariableCS(&mailbox->cond, &mailbox->lock, wait->ticksLeft);
        DWORD dTick = GetTickCount() - startTime;
        /* Be careful about underflow */
        wait->ticksLeft = dTick > wait->ticksLeft ? 0 : dTick;
        return status;
    } else {
        SleepConditionVariableCS(&mailbox->cond, &mailbox->lock, INFINITE);
        return 0;
    }
#else
    if (wait->timedwait) {
        return pthread_cond_timedwait(&mailbox->cond, &mailbox->lock, &wait->ts);
    } else {
        pthread_cond_wait(&mailbox->cond, &mailbox->lock);
        return 0;
    }
#endif
}

static void janet_mailbox_wakeup(JanetMailbox *mailbox) {
#ifdef JANET_WINDOWS
    WakeConditionVariable(&mailbox->cond);
#else
    pthread_cond_signal(&mailbox->cond);
#endif
}

static int mailbox_at_capacity(JanetMailbox *mailbox) {
    return mailbox->messageCount >= mailbox->messageCapacity;
}

/* Returns 1 if could not send (encode error or timeout), 2 for mailbox closed, and
 * 0 otherwise. Will not panic.  */
int janet_thread_send(JanetThread *thread, Janet msg, double timeout) {

    /* Ensure mailbox is not closed. */
    JanetMailbox *mailbox = thread->mailbox;
    if (NULL == mailbox) return 2;
    janet_mailbox_lock(mailbox);
    if (mailbox->closed) {
        janet_mailbox_ref_with_lock(mailbox, -1);
        thread->mailbox = NULL;
        return 2;
    }

    /* Back pressure */
    if (mailbox_at_capacity(mailbox)) {
        JanetWaiter wait;
        janet_waiter_init(&wait, timeout);

        if (wait.nowait) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }

        /* Retry loop, as there can be multiple writers */
        while (mailbox_at_capacity(mailbox)) {
            if (janet_waiter_wait(&wait, mailbox)) {
                janet_mailbox_unlock(mailbox);
                janet_mailbox_wakeup(mailbox);
                return 1;
            }
        }
    }

    /* Hack to capture all panics from marshalling. This works because
     * we know janet_marshal won't mess with other essential global state. */
    jmp_buf buf;
    jmp_buf *old_buf = janet_vm_jmp_buf;
    janet_vm_jmp_buf = &buf;
    int32_t oldmcount = mailbox->messageCount;

    int ret = 0;
    if (setjmp(buf)) {
        ret = 1;
        mailbox->messageCount = oldmcount;
    } else {
        JanetBuffer *msgbuf = mailbox->messages + mailbox->messageNext;
        msgbuf->count = 0;

        /* Start panic zone */
        janet_marshal(msgbuf, msg, thread->encode, JANET_MARSHAL_UNSAFE);
        /* End panic zone */

        mailbox->messageNext = (mailbox->messageNext + 1) % mailbox->messageCapacity;
        mailbox->messageCount++;
    }

    /* Cleanup */
    janet_vm_jmp_buf = old_buf;
    janet_mailbox_unlock(mailbox);

    /* Potentially wake up a blocked thread */
    janet_mailbox_wakeup(mailbox);

    return ret;
}

/* Returns 0 on successful message. Returns 1 if timedout */
int janet_thread_receive(Janet *msg_out, double timeout) {
    JanetMailbox *mailbox = janet_vm_mailbox;
    janet_mailbox_lock(mailbox);

    /* For timeouts */
    JanetWaiter wait;
    janet_waiter_init(&wait, timeout);

    for (;;) {

        /* Check for messages waiting for us */
        if (mailbox->messageCount > 0) {

            /* Hack to capture all panics from marshalling. This works because
             * we know janet_marshal won't mess with other essential global state. */
            jmp_buf buf;
            jmp_buf *old_buf = janet_vm_jmp_buf;
            janet_vm_jmp_buf = &buf;

            /* Handle errors */
            if (setjmp(buf)) {
                /* Cleanup jmp_buf, return error.
                 * Do not ignore bad messages as before. */
                janet_vm_jmp_buf = old_buf;
                *msg_out = *janet_vm_return_reg;
                janet_mailbox_unlock(mailbox);
                return 2;
            } else {
                JanetBuffer *msgbuf = mailbox->messages + mailbox->messageFirst;
                mailbox->messageCount--;
                mailbox->messageFirst = (mailbox->messageFirst + 1) % mailbox->messageCapacity;

                /* Read from beginning of channel */
                const uint8_t *nextItem = NULL;
                Janet item = janet_unmarshal(
                                 msgbuf->data, msgbuf->count,
                                 JANET_MARSHAL_UNSAFE, janet_thread_get_decode(), &nextItem);
                *msg_out = item;

                /* Cleanup */
                janet_vm_jmp_buf = old_buf;
                janet_mailbox_unlock(mailbox);

                /* Potentially wake up pending threads */
                janet_mailbox_wakeup(mailbox);

                return 0;
            }
        }

        if (wait.nowait) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }

        /* Wait for next message */
        if (janet_waiter_wait(&wait, mailbox)) {
            janet_mailbox_unlock(mailbox);
            return 1;
        }
    }
}

static int janet_thread_getter(void *p, Janet key, Janet *out);

const JanetAbstractType janet_thread_type = {
    "core/thread",
    thread_gc,
    thread_mark,
    janet_thread_getter,
    JANET_ATEND_GET
};

static JanetThread *janet_make_thread(JanetMailbox *mailbox, JanetTable *encode) {
    JanetThread *thread = janet_abstract(&janet_thread_type, sizeof(JanetThread));
    janet_mailbox_ref(mailbox, 1);
    thread->mailbox = mailbox;
    thread->encode = encode;
    return thread;
}

JanetThread *janet_getthread(const Janet *argv, int32_t n) {
    return (JanetThread *) janet_getabstract(argv, n, &janet_thread_type);
}

/* Runs in new thread */
static int thread_worker(JanetMailboxPair *pair) {
    JanetFiber *fiber = NULL;
    Janet out;

    /* Use the mailbox we were given */
    janet_vm_mailbox = pair->newbox;
    janet_mailbox_ref(pair->newbox, 1);

    /* Init VM */
    janet_init();

    /* Get dictionaries for default encode/decode */
    JanetTable *encode;
    if (pair->flags & JANET_THREAD_HEAVYWEIGHT) {
        encode = janet_get_core_table("make-image-dict");
    } else {
        encode = NULL;
        janet_vm_thread_decode = janet_table(0);
        janet_gcroot(janet_wrap_table(janet_vm_thread_decode));
    }

    /* Create parent thread */
    JanetThread *parent = janet_make_thread(pair->original, encode);
    Janet parentv = janet_wrap_abstract(parent);

    /* Unmarshal the abstract registry */
    if (pair->flags & JANET_THREAD_ABSTRACTS) {
        Janet reg;
        int status = janet_thread_receive(&reg, INFINITY);
        if (status) goto error;
        if (!janet_checktype(reg, JANET_TABLE)) goto error;
        janet_gcunroot(janet_wrap_table(janet_vm_abstract_registry));
        janet_vm_abstract_registry = janet_unwrap_table(reg);
        janet_gcroot(janet_wrap_table(janet_vm_abstract_registry));
    }

    /* Unmarshal the normal registry */
    if (pair->flags & JANET_THREAD_CFUNCTIONS) {
        Janet reg;
        int status = janet_thread_receive(&reg, INFINITY);
        if (status) goto error;
        if (!janet_checktype(reg, JANET_TABLE)) goto error;
        janet_gcunroot(janet_wrap_table(janet_vm_registry));
        janet_vm_registry = janet_unwrap_table(reg);
        janet_gcroot(janet_wrap_table(janet_vm_registry));
    }

    /* Unmarshal the function */
    Janet funcv;
    int status = janet_thread_receive(&funcv, INFINITY);
    if (status) goto error;
    if (!janet_checktype(funcv, JANET_FUNCTION)) goto error;
    JanetFunction *func = janet_unwrap_function(funcv);

    /* Arity check */
    if (func->def->min_arity > 1 || func->def->max_arity < 1) {
        goto error;
    }

    /* Call function */
    Janet argv[1] = { parentv };
    fiber = janet_fiber(func, 64, 1, argv);
    if (pair->flags & JANET_THREAD_HEAVYWEIGHT) {
        fiber->env = janet_table(0);
        fiber->env->proto = janet_core_env(NULL);
    }
    JanetSignal sig = janet_continue(fiber, janet_wrap_nil(), &out);
    if (sig != JANET_SIGNAL_OK && sig < JANET_SIGNAL_USER0) {
        janet_eprintf("in thread %v: ", janet_wrap_abstract(janet_make_thread(pair->newbox, encode)));
        janet_stacktrace(fiber, out);
    }

#ifdef JANET_NET
    janet_loop();
#endif

    /* Normal exit */
    destroy_mailbox_pair(pair);
    janet_deinit();
    return 0;

    /* Fail to set something up */
error:
    destroy_mailbox_pair(pair);
    janet_eprintf("\nthread failed to start\n");
    janet_deinit();
    return 1;
}

#ifdef JANET_WINDOWS

static DWORD WINAPI janet_create_thread_wrapper(LPVOID param) {
    thread_worker((JanetMailboxPair *)param);
    return 0;
}

static int janet_thread_start_child(JanetMailboxPair *pair) {
    HANDLE handle = CreateThread(NULL, 0, janet_create_thread_wrapper, pair, 0, NULL);
    int ret = NULL == handle;
    /* Does not kill thread, simply detatches */
    if (!ret) CloseHandle(handle);
    return ret;
}

#else

static void *janet_pthread_wrapper(void *param) {
    thread_worker((JanetMailboxPair *)param);
    return NULL;
}

static int janet_thread_start_child(JanetMailboxPair *pair) {
    pthread_t handle;
    int error = pthread_create(&handle, NULL, janet_pthread_wrapper, pair);
    if (error) {
        return 1;
    } else {
        pthread_detach(handle);
        return 0;
    }
}

#endif

/*
 * Setup/Teardown
 */

void janet_threads_init(void) {
    if (NULL == janet_vm_mailbox) {
        janet_vm_mailbox = janet_mailbox_create(1, 10);
    }
    janet_vm_thread_decode = NULL;
    janet_vm_thread_current = NULL;
}

void janet_threads_deinit(void) {
    janet_mailbox_lock(janet_vm_mailbox);
    janet_vm_mailbox->closed = 1;
    janet_mailbox_ref_with_lock(janet_vm_mailbox, -1);
    janet_vm_mailbox = NULL;
    janet_vm_thread_current = NULL;
    janet_vm_thread_decode = NULL;
}

/*
 * Cfuns
 */

static Janet cfun_thread_current(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    if (NULL == janet_vm_thread_current) {
        janet_vm_thread_current = janet_make_thread(janet_vm_mailbox, janet_get_core_table("make-image-dict"));
        janet_gcroot(janet_wrap_abstract(janet_vm_thread_current));
    }
    return janet_wrap_abstract(janet_vm_thread_current);
}

static Janet cfun_thread_new(int32_t argc, Janet *argv) {
    janet_arity(argc, 1, 3);
    /* Just type checking */
    janet_getfunction(argv, 0);
    int32_t cap = janet_optinteger(argv, argc, 1, 10);
    if (cap < 1 || cap > UINT16_MAX) {
        janet_panicf("bad slot #1, expected integer in range [1, 65535], got %d", cap);
    }
    uint64_t flags = argc >= 3 ? janet_getflags(argv, 2, janet_thread_flags) : JANET_THREAD_ABSTRACTS;
    JanetTable *encode;
    if (flags & JANET_THREAD_HEAVYWEIGHT) {
        encode = janet_get_core_table("make-image-dict");
    } else {
        encode = NULL;
    }

    JanetMailboxPair *pair = make_mailbox_pair(janet_vm_mailbox, flags);
    JanetThread *thread = janet_make_thread(pair->newbox, encode);
    if (janet_thread_start_child(pair)) {
        destroy_mailbox_pair(pair);
        janet_panic("could not start thread");
    }

    if (flags & JANET_THREAD_ABSTRACTS) {
        if (janet_thread_send(thread, janet_wrap_table(janet_vm_abstract_registry), INFINITY)) {
            janet_panic("could not send abstract registry to thread");
        }
    }

    if (flags & JANET_THREAD_CFUNCTIONS) {
        if (janet_thread_send(thread, janet_wrap_table(janet_vm_registry), INFINITY)) {
            janet_panic("could not send registry to thread");
        }
    }

    /* If thread started, send the worker function. */
    if (janet_thread_send(thread, argv[0], INFINITY)) {
        janet_panicf("could not send worker function %v to thread", argv[0]);
    }

    return janet_wrap_abstract(thread);
}

static Janet cfun_thread_send(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);
    JanetThread *thread = janet_getthread(argv, 0);
    int status = janet_thread_send(thread, argv[1], janet_optnumber(argv, argc, 2, 1.0));
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("failed to send message %v", argv[1]);
        case 2:
            janet_panic("thread mailbox is closed");
    }
    return argv[0];
}

static Janet cfun_thread_receive(int32_t argc, Janet *argv) {
    janet_arity(argc, 0, 1);
    double wait = janet_optnumber(argv, argc, 0, 1.0);
    Janet out;
    int status = janet_thread_receive(&out, wait);
    switch (status) {
        default:
            break;
        case 1:
            janet_panicf("timeout after %f seconds", wait);
        case 2:
            janet_panicf("failed to receive message: %v", out);
    }
    return out;
}

static Janet cfun_thread_close(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetThread *thread = janet_getthread(argv, 0);
    janet_close_thread(thread);
    return janet_wrap_nil();
}

static Janet cfun_thread_exit(int32_t argc, Janet *argv) {
    (void) argv;
    janet_arity(argc, 0, 1);
#if defined(JANET_WINDOWS)
    int32_t flag = janet_optinteger(argv, argc, 0, 0);
    ExitThread(flag);
#else
    pthread_exit(NULL);
#endif
    return janet_wrap_nil();
}

static const JanetMethod janet_thread_methods[] = {
    {"send", cfun_thread_send},
    {"close", cfun_thread_close},
    {NULL, NULL}
};

static int janet_thread_getter(void *p, Janet key, Janet *out) {
    (void) p;
    if (!janet_checktype(key, JANET_KEYWORD)) return 0;
    return janet_getmethod(janet_unwrap_keyword(key), janet_thread_methods, out);
}

static const JanetReg threadlib_cfuns[] = {
    {
        "thread/current", cfun_thread_current,
        JDOC("(thread/current)\n\n"
             "Get the current running thread.")
    },
    {
        "thread/new", cfun_thread_new,
        JDOC("(thread/new func &opt capacity flags)\n\n"
             "Start a new thread that will start immediately. "
             "If capacity is provided, that is how many messages can be stored in the thread's mailbox before blocking senders. "
             "The capacity must be between 1 and 65535 inclusive, and defaults to 10. "
             "Can optionally provide flags to the new thread - supported flags are:\n"
             "\t:h - Start a heavyweight thread. This loads the core environment by default, so may use more memory initially. Messages may compress better, though.\n"
             "\t:a - Allow sending over registered abstract types to the new thread\n"
             "\t:c - Send over cfunction information to the new thread.\n"
             "Returns a handle to the new thread.")
    },
    {
        "thread/send", cfun_thread_send,
        JDOC("(thread/send thread msg)\n\n"
             "Send a message to the thread. This will never block and returns thread immediately. "
             "Will throw an error if there is a problem sending the message.")
    },
    {
        "thread/receive", cfun_thread_receive,
        JDOC("(thread/receive &opt timeout)\n\n"
             "Get a message sent to this thread. If timeout is provided, an error will be thrown after the timeout has elapsed but "
             "no messages are received.")
    },
    {
        "thread/close", cfun_thread_close,
        JDOC("(thread/close thread)\n\n"
             "Close a thread, unblocking it and ending communication with it. Note that closing "
             "a thread is idempotent and does not cancel the thread's operation. Returns nil.")
    },
    {
        "thread/exit", cfun_thread_exit,
        JDOC("(thread/exit &opt code)\n\n"
             "Exit from the current thread. If no more threads are running, ends the process, but otherwise does "
             "not end the current process.")
    },
    {NULL, NULL, NULL}
};

/* Module entry point */
void janet_lib_thread(JanetTable *env) {
    janet_core_cfuns(env, NULL, threadlib_cfuns);
    janet_register_abstract_type(&janet_thread_type);
}

#endif
